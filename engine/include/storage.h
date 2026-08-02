#ifndef ENGINE_STORAGE_H
#define ENGINE_STORAGE_H

#include <nusys.h>

/*
 * Saving, to a flashcart's SD card.
 *
 * This is not a Controller Pak and not cartridge SRAM: it is libcart talking to
 * a SummerCart64 / EverDrive / 64drive, and FatFS reading the FAT filesystem on
 * the card.  That buys real files of real sizes instead of 32 KB of note
 * blocks, at the cost of only working on a flashcart -- so every call here
 * answers honestly when there is no card, and a game must stay playable when
 * `saving_available` is FALSE.  Emulators generally have no cart, which is
 * exactly the case that must not crash.
 *
 * Files live in a directory named for the game (STORAGE_DIR), so a card can
 * hold saves for several without them colliding.
 */

#ifndef STORAGE_DIR
#define STORAGE_DIR "n64game"
#endif

/* Slots, as a game's save menu presents them. */
#define STORAGE_MAX_SLOTS 3

extern u8 saving_available;
extern u8 files_present[STORAGE_MAX_SLOTS];

/*
 * A slot whose file was deleted and is still on the card, waiting to be asked
 * back.  Deleting renames rather than erases: freeing the name is the
 * requirement, destroying the bytes never was, and surviving a power cycle
 * costs nothing extra because the flag is only ever the answer to "is the
 * set-aside file there", which initStorage asks at boot.
 *
 * Undo is worth more here than anywhere else in a game.  There is no
 * confirmation dialog a player reads.
 */
extern u8 deleted_files_present[STORAGE_MAX_SLOTS];

/*
 * Why storage came up unavailable.  There are four distinct ways to fail and a
 * single blanket "NO CART" cannot tell a cart that was never detected from an
 * SD card that is simply formatted exFAT.  On hardware -- the only place any of
 * this can be observed -- that distinction is the whole diagnosis.
 */
#define STORAGE_OK 0
#define STORAGE_NO_CART 1        /* cart_init found no supported flashcart */
#define STORAGE_CARD_NOT_READY 2 /* flashcart found, SD card did not init */
#define STORAGE_BAD_FILESYSTEM 3 /* SD readable but not FAT16/FAT32 */
#define STORAGE_MOUNT_FAILED 4   /* f_mount failed some other way */
#define STORAGE_NO_DIRECTORY 5   /* mounted, but the game directory is unusable */

extern u8 storage_status;
/* Short, uppercase, and sized for a centred line on a title screen. */
const char *storageStatusText(void);

void initStorage(void);

/*
 * Whole-file save and load.
 *
 * Both are checksummed and both write via a temporary file that is renamed
 * into place only once the bytes are down, so a console switched off mid-save
 * loses the new save rather than the old one.  A file that fails its checksum
 * falls back to the previous generation automatically.
 *
 * These block for as long as the cart takes -- hundreds of milliseconds for
 * anything large -- which stops the picture, the controller and the freeze
 * watchdog's heartbeat together.  That is fine for a few KB from a menu.  For
 * anything bigger, use the sliced pair below instead, or the player cannot
 * tell a save from a crash.
 */
u8 storageSave(u8 slot, const void *data, u32 length);
/* Returns bytes read, or 0 if the slot is empty or unreadable. */
u32 storageLoad(u8 slot, void *data, u32 limit);

u8 storageDeleteSlot(u8 slot);
u8 storageRestoreSlot(u8 slot);

/*
 * Sliced save and load, for payloads too big to write inside one graphics
 * callback.  begin* opens the file and writes the header; step* moves a
 * bounded number of 512-byte pages per call and publishes the file when it
 * runs out.  Both answer with a status, so the caller keeps drawing frames in
 * between -- which is what lets a progress bar exist at all.
 */
#define STORAGE_BUSY 0
#define STORAGE_DONE 1
#define STORAGE_FAILED 2

u8 storageBeginSave(u8 slot, const void *data, u32 length);
u8 storageStepSave(u16 pages);
u8 storageBeginLoad(u8 slot, void *data, u32 limit);
u8 storageStepLoad(u16 pages);
void storageCancel(void);
/* 0..100 across the payload, for the bar. */
u8 storageProgress(void);
/* Bytes the finished load actually read. */
u32 storageLoadedLength(void);

/*
 * Slot names, shown by a save menu.  Stored beside the payload so the menu can
 * list slots without loading them.
 */
#define STORAGE_NAME_LENGTH 12
extern char slot_names[STORAGE_MAX_SLOTS][STORAGE_NAME_LENGTH + 1];
void storageSetSlotName(u8 slot, const char *name);

/*
 * Write a plain-text post-mortem to <STORAGE_DIR>/freeze.txt.  Called by the
 * freeze watchdog after the console is already dead, so the usual
 * single-threaded storage assumptions are moot; a failed write costs nothing
 * that was not already lost.
 */
u8 storageWriteFreezeReport(const char *text, u32 length);

#endif /* ENGINE_STORAGE_H */
