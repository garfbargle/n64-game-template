#include "cart.h"
#include "ff/ff.h"

#include "storage.h"

/*
 * Saving, to a flashcart's SD card.
 *
 * Every write is transactional, because the machine this runs on has a power
 * switch and no shutdown sequence.  A save goes to a temporary file, is
 * synced, then the existing file is rotated to a backup and the temporary is
 * renamed into place.  Power lost at any point in that sequence leaves either
 * the old file or the new one, never half of either -- and initStorage picks
 * up whichever generation survived.
 *
 * The payload carries a header with a magic number, a version and a checksum,
 * so a file that is present but corrupt is refused rather than loaded into the
 * game as garbage.
 */

static FATFS fs;

u8 saving_available;
u8 storage_status;
u8 files_present[STORAGE_MAX_SLOTS];
u8 deleted_files_present[STORAGE_MAX_SLOTS];
char slot_names[STORAGE_MAX_SLOTS][STORAGE_NAME_LENGTH + 1];

/* "N64G" */
#define STORAGE_MAGIC 0x4E363447
#define STORAGE_VERSION 1
#define STORAGE_PAGE 512

/*
 * The header page.
 *
 * Fixed at one 512-byte page whatever it actually uses, so that adding a field
 * later never moves the payload: a new field goes in the zero-filled tail and
 * old files still read correctly, gated on `version`.  That is the whole
 * reason for the padding -- it is cheap now and impossible to add later.
 */
typedef struct {
  u32 magic;
  u32 version;
  u32 length;
  u32 checksum;
  char name[STORAGE_NAME_LENGTH + 1];
  u8 padding[STORAGE_PAGE - 4 * 4 - (STORAGE_NAME_LENGTH + 1)];
} StorageHeader;

static StorageHeader header __attribute__((aligned(8)));
static u8 page_buffer[STORAGE_PAGE] __attribute__((aligned(8)));

/*
 * Paths.  Built into one scratch buffer per call rather than kept in a table,
 * because STORAGE_DIR is a compile-time define and a game that renames itself
 * should not have to remember to update four parallel arrays.
 */
static char path_buffer[64];

static char *slotPath(u8 slot, const char *suffix) {
  char *out = path_buffer;
  const char *dir = STORAGE_DIR;

  while (*dir) {
    *out++ = *dir++;
  }
  *out++ = '/';
  *out++ = 's';
  *out++ = 'a';
  *out++ = 'v';
  *out++ = 'e';
  *out++ = '0' + slot;
  while (*suffix) {
    *out++ = *suffix++;
  }
  *out = '\0';
  return path_buffer;
}

/* Four generations of the same slot.  ".sav" is the live one; the other three
   only ever exist mid-transaction or after a delete. */
#define SUFFIX_LIVE ".sav"
#define SUFFIX_TEMP ".tmp"
#define SUFFIX_BACKUP ".bak"
#define SUFFIX_DELETED ".del"

/* slotPath reuses one buffer, so two paths cannot be live at once.  A rename
   needs both, hence this second copy. */
static char rename_buffer[64];

static char *slotPathCopy(u8 slot, const char *suffix) {
  char *src = slotPath(slot, suffix);
  char *out = rename_buffer;

  while (*src) {
    *out++ = *src++;
  }
  *out = '\0';
  return rename_buffer;
}

/*
 * A checksum, not a hash: this is guarding against a truncated write and a bad
 * sector, not against tampering.  Cheap enough to run over the whole payload
 * on a 93 MHz CPU without the player noticing.
 */
static u32 storageChecksum(const u8 *data, u32 length) {
  u32 checksum = 0x811C9DC5;
  u32 i;

  for (i = 0; i < length; i++) {
    checksum = (checksum ^ data[i]) * 16777619;
  }
  return checksum;
}

const char *storageStatusText(void) {
  switch (storage_status) {
    case STORAGE_NO_CART:
      return "NO CART SAVE DEVICE";
    case STORAGE_CARD_NOT_READY:
      return "SD CARD NOT READY";
    case STORAGE_BAD_FILESYSTEM:
      return "SD MUST BE FAT32";
    case STORAGE_MOUNT_FAILED:
      return "SD MOUNT FAILED";
    case STORAGE_NO_DIRECTORY:
      return "CANNOT WRITE SD CARD";
    default:
      return "SAVING UNAVAILABLE";
  }
}

static void setDefaultSlotName(u8 slot) {
  slot_names[slot][0] = 'S';
  slot_names[slot][1] = 'L';
  slot_names[slot][2] = 'O';
  slot_names[slot][3] = 'T';
  slot_names[slot][4] = ' ';
  slot_names[slot][5] = '1' + slot;
  slot_names[slot][6] = '\0';
}

void storageSetSlotName(u8 slot, const char *name) {
  u8 i;

  if (slot >= STORAGE_MAX_SLOTS) {
    return;
  }
  for (i = 0; i < STORAGE_NAME_LENGTH && name[i]; i++) {
    slot_names[slot][i] = name[i];
  }
  slot_names[slot][i] = '\0';
}

/* Read just the header page, to learn a slot's name without loading it. */
static u8 readSlotHeader(u8 slot) {
  FIL file;
  UINT n_read;
  u8 ok;

  if (f_open(&file, slotPath(slot, SUFFIX_LIVE), FA_READ) != FR_OK) {
    return FALSE;
  }
  osInvalDCache(&header, sizeof (header));
  ok = f_read(&file, &header, sizeof (header), &n_read) == FR_OK &&
    n_read == sizeof (header) && header.magic == STORAGE_MAGIC;
  f_close(&file);
  return ok;
}

void initStorage(void) {
  FRESULT res;
  FILINFO info;
  int i;

  saving_available = FALSE;
  for (i = 0; i < STORAGE_MAX_SLOTS; i++) {
    files_present[i] = FALSE;
    deleted_files_present[i] = FALSE;
    setDefaultSlotName(i);
  }

  if (cart_init() != 0) {
    storage_status = STORAGE_NO_CART;
    return;
  }
  /*
   * f_mount with the mount-now flag runs disk_initialize, so it reports the
   * card and the filesystem separately: FR_NOT_READY is the SD card itself
   * failing to come up, FR_NO_FILESYSTEM is a card that reads fine but is not
   * FAT16/FAT32 -- exFAT, the default format for cards over 32 GB, lands here
   * because ffconf.h leaves FF_FS_EXFAT at 0.
   */
  res = f_mount(&fs, "", 1);
  if (res != FR_OK) {
    storage_status = res == FR_NOT_READY ? STORAGE_CARD_NOT_READY :
      res == FR_NO_FILESYSTEM ? STORAGE_BAD_FILESYSTEM :
      STORAGE_MOUNT_FAILED;
    return;
  }

  res = f_stat(STORAGE_DIR, &info);
  if (res == FR_NO_FILE) {
    if (f_mkdir(STORAGE_DIR) != FR_OK) {
      storage_status = STORAGE_NO_DIRECTORY;
      return;
    }
  } else if (res != FR_OK || !(info.fattrib & AM_DIR)) {
    storage_status = STORAGE_NO_DIRECTORY;
    return;
  }

  storage_status = STORAGE_OK;
  saving_available = TRUE;

  for (i = 0; i < STORAGE_MAX_SLOTS; i++) {
    res = f_stat(slotPath(i, SUFFIX_LIVE), &info);
    if (res != FR_OK &&
        f_stat(slotPath(i, SUFFIX_BACKUP), &info) == FR_OK &&
        !(info.fattrib & AM_DIR)) {
      /* Recover the previous complete file if power was lost between the
         backup and the final rename of a transactional save. */
      f_rename(slotPathCopy(i, SUFFIX_BACKUP), slotPath(i, SUFFIX_LIVE));
      res = f_stat(slotPath(i, SUFFIX_LIVE), &info);
    }
    if (res != FR_OK &&
        f_stat(slotPath(i, SUFFIX_TEMP), &info) == FR_OK &&
        !(info.fattrib & AM_DIR)) {
      /* The first-save power-loss case: there was no older file to rotate,
         but the synced temporary may already be complete. */
      f_rename(slotPathCopy(i, SUFFIX_TEMP), slotPath(i, SUFFIX_LIVE));
      res = f_stat(slotPath(i, SUFFIX_LIVE), &info);
    }
    files_present[i] = res == FR_OK && !(info.fattrib & AM_DIR);
    if (files_present[i]) {
      if (readSlotHeader(i)) {
        storageSetSlotName(i, header.name);
      }
    } else {
      /* An empty slot may be empty because somebody deleted what was in it.
         The offer to put it back outlives the session that made it, because
         what is being offered is a file rather than a memory of one. */
      deleted_files_present[i] =
        f_stat(slotPath(i, SUFFIX_DELETED), &info) == FR_OK &&
        !(info.fattrib & AM_DIR);
    }
    f_unlink(slotPath(i, SUFFIX_TEMP));
  }
}

u8 storageDeleteSlot(u8 slot) {
  if (!saving_available || slot >= STORAGE_MAX_SLOTS || !files_present[slot]) {
    return FALSE;
  }
  f_unlink(slotPath(slot, SUFFIX_DELETED));
  if (f_rename(slotPathCopy(slot, SUFFIX_LIVE),
      slotPath(slot, SUFFIX_DELETED)) != FR_OK) {
    return FALSE;
  }
  files_present[slot] = FALSE;
  deleted_files_present[slot] = TRUE;
  setDefaultSlotName(slot);
  return TRUE;
}

u8 storageRestoreSlot(u8 slot) {
  if (!saving_available || slot >= STORAGE_MAX_SLOTS ||
      files_present[slot] || !deleted_files_present[slot]) {
    return FALSE;
  }
  if (f_rename(slotPathCopy(slot, SUFFIX_DELETED),
      slotPath(slot, SUFFIX_LIVE)) != FR_OK) {
    return FALSE;
  }
  files_present[slot] = TRUE;
  deleted_files_present[slot] = FALSE;
  if (readSlotHeader(slot)) {
    storageSetSlotName(slot, header.name);
  }
  return TRUE;
}

/*
 * The sliced save and load.
 *
 * A payload of any size pushed through a 512-byte window from a graphics
 * callback, with no frames and no controller polling in between, is long
 * enough on a slow card to outlast the freeze watchdog's two-second patience
 * -- which makes a successful save look exactly like a crash.  So the work is
 * bounded per call and the caller keeps drawing.
 */
static FIL job_file;
static u8 job_active;
static u8 job_writing;
static u8 job_slot;
static u8 *job_data;
static u32 job_length;
static u32 job_done;
static u32 job_loaded_length;

u8 storageProgress(void) {
  if (job_length == 0) {
    return 100;
  }
  return (u8) (job_done * 100 / job_length);
}

u32 storageLoadedLength(void) {
  return job_loaded_length;
}

void storageCancel(void) {
  if (!job_active) {
    return;
  }
  f_close(&job_file);
  job_active = FALSE;
  if (job_writing) {
    f_unlink(slotPath(job_slot, SUFFIX_TEMP));
  }
}

u8 storageBeginSave(u8 slot, const void *data, u32 length) {
  UINT written;
  u32 i;

  if (!saving_available || slot >= STORAGE_MAX_SLOTS) {
    return STORAGE_FAILED;
  }
  storageCancel();

  header.magic = STORAGE_MAGIC;
  header.version = STORAGE_VERSION;
  header.length = length;
  header.checksum = storageChecksum((const u8 *) data, length);
  for (i = 0; i <= STORAGE_NAME_LENGTH; i++) {
    header.name[i] = slot_names[slot][i];
    if (slot_names[slot][i] == '\0') {
      break;
    }
  }
  for (i = 0; i < sizeof (header.padding); i++) {
    header.padding[i] = 0;
  }

  if (f_open(&job_file, slotPath(slot, SUFFIX_TEMP),
      FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
    return STORAGE_FAILED;
  }
  osWritebackDCache(&header, sizeof (header));
  if (f_write(&job_file, &header, sizeof (header), &written) != FR_OK ||
      written != sizeof (header)) {
    f_close(&job_file);
    f_unlink(slotPath(slot, SUFFIX_TEMP));
    return STORAGE_FAILED;
  }

  job_active = TRUE;
  job_writing = TRUE;
  job_slot = slot;
  job_data = (u8 *) data;
  job_length = length;
  job_done = 0;
  return length == 0 ? storageStepSave(1) : STORAGE_BUSY;
}

/*
 * Publish the file: rotate the live copy out of the way, then rename the
 * temporary in.  The order matters -- a crash between the two leaves the
 * backup, which initStorage recovers.
 */
static u8 finishSave(void) {
  u8 ok;

  f_sync(&job_file);
  f_close(&job_file);
  job_active = FALSE;

  f_unlink(slotPath(job_slot, SUFFIX_BACKUP));
  if (files_present[job_slot]) {
    f_rename(slotPathCopy(job_slot, SUFFIX_LIVE),
      slotPath(job_slot, SUFFIX_BACKUP));
  }
  ok = f_rename(slotPathCopy(job_slot, SUFFIX_TEMP),
    slotPath(job_slot, SUFFIX_LIVE)) == FR_OK;
  if (ok) {
    files_present[job_slot] = TRUE;
    deleted_files_present[job_slot] = FALSE;
    f_unlink(slotPath(job_slot, SUFFIX_BACKUP));
  } else if (files_present[job_slot]) {
    /* Put the old file back rather than leaving the slot with neither. */
    f_rename(slotPathCopy(job_slot, SUFFIX_BACKUP),
      slotPath(job_slot, SUFFIX_LIVE));
  }
  return ok ? STORAGE_DONE : STORAGE_FAILED;
}

u8 storageStepSave(u16 pages) {
  u16 page;

  if (!job_active || !job_writing) {
    return STORAGE_FAILED;
  }
  for (page = 0; page < pages && job_done < job_length; page++) {
    UINT written;
    u32 remaining = job_length - job_done;
    u32 chunk = remaining > STORAGE_PAGE ? STORAGE_PAGE : remaining;
    u32 i;

    for (i = 0; i < chunk; i++) {
      page_buffer[i] = job_data[job_done + i];
    }
    /* Always write a whole page: partial writes at the tail cost an extra
       seek on some cards and buy nothing, since the header records the real
       length. */
    for (i = chunk; i < STORAGE_PAGE; i++) {
      page_buffer[i] = 0;
    }
    osWritebackDCache(page_buffer, STORAGE_PAGE);
    if (f_write(&job_file, page_buffer, STORAGE_PAGE, &written) != FR_OK ||
        written != STORAGE_PAGE) {
      storageCancel();
      return STORAGE_FAILED;
    }
    job_done += chunk;
  }
  return job_done >= job_length ? finishSave() : STORAGE_BUSY;
}

u8 storageBeginLoad(u8 slot, void *data, u32 limit) {
  UINT n_read;

  job_loaded_length = 0;
  if (!saving_available || slot >= STORAGE_MAX_SLOTS ||
      !files_present[slot]) {
    return STORAGE_FAILED;
  }
  storageCancel();

  if (f_open(&job_file, slotPath(slot, SUFFIX_LIVE), FA_READ) != FR_OK) {
    return STORAGE_FAILED;
  }
  osInvalDCache(&header, sizeof (header));
  if (f_read(&job_file, &header, sizeof (header), &n_read) != FR_OK ||
      n_read != sizeof (header) || header.magic != STORAGE_MAGIC ||
      header.version > STORAGE_VERSION || header.length > limit) {
    f_close(&job_file);
    return STORAGE_FAILED;
  }

  job_active = TRUE;
  job_writing = FALSE;
  job_slot = slot;
  job_data = (u8 *) data;
  job_length = header.length;
  job_done = 0;
  return job_length == 0 ? storageStepLoad(1) : STORAGE_BUSY;
}

u8 storageStepLoad(u16 pages) {
  u16 page;

  if (!job_active || job_writing) {
    return STORAGE_FAILED;
  }
  for (page = 0; page < pages && job_done < job_length; page++) {
    UINT n_read;
    u32 remaining = job_length - job_done;
    u32 chunk = remaining > STORAGE_PAGE ? STORAGE_PAGE : remaining;
    u32 i;

    osInvalDCache(page_buffer, STORAGE_PAGE);
    if (f_read(&job_file, page_buffer, STORAGE_PAGE, &n_read) != FR_OK ||
        n_read < chunk) {
      storageCancel();
      return STORAGE_FAILED;
    }
    for (i = 0; i < chunk; i++) {
      job_data[job_done + i] = page_buffer[i];
    }
    job_done += chunk;
  }
  if (job_done < job_length) {
    return STORAGE_BUSY;
  }

  f_close(&job_file);
  job_active = FALSE;
  /*
   * Verify only now that the whole payload is in memory.  A file that fails
   * here is refused outright rather than handed to the game half-trusted: the
   * backup generation is still on the card, and initStorage will find it on
   * the next boot.
   */
  if (storageChecksum(job_data, job_length) != header.checksum) {
    return STORAGE_FAILED;
  }
  job_loaded_length = job_length;
  return STORAGE_DONE;
}

/*
 * The blocking pair, for payloads small enough that stopping the console for
 * the duration is honest.  Identical outcome to driving the sliced pair to
 * completion.
 */
u8 storageSave(u8 slot, const void *data, u32 length) {
  u8 status = storageBeginSave(slot, data, length);

  while (status == STORAGE_BUSY) {
    status = storageStepSave(64);
  }
  return status == STORAGE_DONE;
}

u32 storageLoad(u8 slot, void *data, u32 limit) {
  u8 status = storageBeginLoad(slot, data, limit);

  while (status == STORAGE_BUSY) {
    status = storageStepLoad(64);
  }
  return status == STORAGE_DONE ? job_loaded_length : 0;
}

u8 storageWriteFreezeReport(const char *text, u32 length) {
  FIL file;
  UINT written;
  u8 ok;
  char *path = path_buffer;
  const char *dir = STORAGE_DIR;
  const char *name = "/freeze.txt";

  if (!saving_available) {
    return FALSE;
  }
  while (*dir) {
    *path++ = *dir++;
  }
  while (*name) {
    *path++ = *name++;
  }
  *path = '\0';

  if (f_open(&file, path_buffer, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
    return FALSE;
  }
  ok = f_write(&file, text, length, &written) == FR_OK && written == length;
  f_sync(&file);
  f_close(&file);
  return ok;
}
