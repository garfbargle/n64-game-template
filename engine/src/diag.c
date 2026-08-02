#include "diag.h"
#include "gfx.h"
#include "text.h"
#include "storage.h"
#include "audio.h"

/*
 * Freeze forensics.  A small square painted straight into the displayed
 * framebuffer by the CPU -- no RSP, no RDP, no display list -- so it keeps
 * reporting after the graphics pipeline is dead.  While the game runs the RDP
 * repaints over it every frame and it reads as a flicker; the moment
 * everything stops, whatever was painted last persists on screen.
 *
 *   RED     a graphics task has been in flight for ~2s -- the RSP or RDP hung
 *           executing it (the watchdog latches; nothing else paints after)
 *   other   the last phase colour reached; see the DIAG_PHASE_* list
 *   BLUE    the callback completed normally (painted last on the way out)
 *
 * A frozen screen showing DONE and no red means callbacks stopped arriving at
 * all, which points at the scheduler rather than at any of this.
 *
 * The square must outlive the graphics thread.  Painting the fatal phase into
 * the displayed buffer is not enough on its own: an already-submitted RSP/RDP
 * task can finish afterwards, repaint the other buffer completely and swap to
 * it, taking the evidence with it.  So the phase is *recorded* here and
 * painted only lightly for the alive-flicker, while the high-priority watchdog
 * below repaints the recorded colour into BOTH framebuffers once the heartbeat
 * goes stale.  The N64 scheduler is strictly priority-based and timer
 * interrupts keep firing while a lower-priority thread spins, so the watchdog
 * reports through an infinite loop or a dead graphics thread alike.
 */
#define DIAG_SQUARE_X 12
#define DIAG_SQUARE_Y 200
#define DIAG_SQUARE_SIZE 16

volatile u16 diag_current_phase;
volatile u32 diag_heartbeat;
volatile u8 diag_cpu_faulted;
u8 diagnostics_visible = FALSE;

static u8 diag_task_hung = FALSE;
static u32 diag_pending_streak = 0;

static void diagPaintRows(u16 *frame_buffer, int y0, int rows, u16 color) {
  int x, y;

  if (frame_buffer == NULL) {
    return;
  }
  for (y = y0; y < y0 + rows; y++) {
    u16 *row = frame_buffer + (DIAG_SQUARE_Y + y) * SCREEN_WD + DIAG_SQUARE_X;

    for (x = 0; x < DIAG_SQUARE_SIZE; x++) {
      row[x] = color;
    }
    /* The VI scans RDRAM; flush each painted row out of the data cache. */
    osWritebackDCache(row, DIAG_SQUARE_SIZE * sizeof (u16));
  }
}

void diagPaintPhase(u16 color) {
  /* Always record -- the watchdog reports the phase whether or not the overlay
     is showing.  Only the alive-flicker paint is cosmetic. */
  diag_current_phase = color;
  if (diag_task_hung || !diagnostics_visible) {
    return;
  }
  diagPaintRows((u16 *) osViGetCurrentFramebuffer(), 0, DIAG_SQUARE_SIZE,
    color);
}

/*
 * Called by the watchdog after the heartbeat stops: keep the fatal evidence on
 * screen no matter which buffer the VI ends up scanning.  The bottom band is
 * white if the CPU took a fault (so the thread crashed) and black if not (so
 * it is spinning in a loop) -- entirely different hunts.
 */
static void diagPaintStalePhase(void) {
  u16 phase = diag_task_hung ? GPACK_RGBA5551(255, 0, 0, 1)
    : diag_current_phase;
  u16 fault = diag_cpu_faulted ? GPACK_RGBA5551(255, 255, 255, 1)
    : GPACK_RGBA5551(0, 0, 0, 1);
  u16 *buffers[2];
  int i;

  buffers[0] = (u16 *) osViGetCurrentFramebuffer();
  buffers[1] = (u16 *) osViGetNextFramebuffer();
  for (i = 0; i < 2; i++) {
    diagPaintRows(buffers[i], 0, 11, phase);
    diagPaintRows(buffers[i], 11, 5, fault);
  }
}

/*
 * Worst-of-window frame pacing.  W is the wall clock between consecutive
 * graphics callbacks -- the player's actual frame time, whatever is causing
 * it.  B is the CPU cost of the work the engine does inside the callback.  B
 * near W says CPU work in the callback is the bottleneck; W high with B low
 * says the RSP/RDP is.  The window resets every ~2s so a single historical
 * spike does not pin the numbers forever.
 */
static u32 diag_worst_frame_usec;
static u32 diag_worst_gated_usec;
static u16 diag_perf_window;

static void diagNoteFrameInterval(u32 usec) {
  if (usec > diag_worst_frame_usec) {
    diag_worst_frame_usec = usec;
  }
  if (++diag_perf_window >= 120) {
    diag_perf_window = 0;
    diag_worst_frame_usec = usec;
    diag_worst_gated_usec = 0;
  }
}

void diagNoteGatedWork(u32 usec) {
  if (usec > diag_worst_gated_usec) {
    diag_worst_gated_usec = usec;
  }
}

u32 diagWorstFrameInterval(void) {
  return diag_worst_frame_usec;
}

u32 diagWorstGatedWork(void) {
  return diag_worst_gated_usec;
}

/*
 * Frames the player actually saw in the last whole second.
 *
 * This counts submitted graphics tasks, not graphics callbacks, and the
 * difference is the whole point.  The callback runs once per retrace message
 * but only builds a frame when no task is in flight, so when the RSP/RDP is
 * the bottleneck the callback keeps arriving at the field rate and returns
 * without drawing -- three 60 Hz callbacks for one picture.  Counting
 * callbacks would report 60 while the screen updates at 20.
 *
 * That also makes it independent evidence from W: W only stretches once the
 * CPU work in the callback is what is late, because a callback that finds a
 * task still pending returns immediately and keeps its interval short.
 */
static u32 diag_fps;
static u32 diag_fps_frames;
static OSTime diag_fps_window_start;

void diagNoteFrameSubmitted(void) {
  OSTime now = osGetTime();
  OSTime second = OS_USEC_TO_CYCLES(1000000);

  diag_fps_frames++;
  if (diag_fps_window_start == 0) {
    diag_fps_window_start = now;
    return;
  }
  if (now - diag_fps_window_start < second) {
    return;
  }
  diag_fps = diag_fps_frames;
  diag_fps_frames = 0;
  /* Advance by a whole second rather than restarting from now, so the window
     does not drift by however late this frame noticed the boundary. */
  diag_fps_window_start += second;
  /* A long load can stall the pipeline for many seconds at a stretch.
     Resynchronise instead of walking the backlog one second per frame, which
     would report a stale count for as long as the stall lasted. */
  if (now - diag_fps_window_start >= second) {
    diag_fps_window_start = now;
  }
}

u32 diagFramesPerSecond(void) {
  return diag_fps;
}

void diagFrameStart(int pendingGfx) {
  static OSTime last_callback_time;
  OSTime callback_time = osGetTime();

  diag_heartbeat++;

  /* Latch red if a graphics task has been in flight for ~2 seconds: the RSP or
     RDP is hung executing it, which no amount of CPU-side evidence would
     otherwise distinguish from the game simply being slow. */
  if (pendingGfx == 0) {
    diag_pending_streak = 0;
  } else if (++diag_pending_streak > 120 && !diag_task_hung) {
    diag_task_hung = TRUE;
    diagnostics_visible = TRUE;
    diagPaintStalePhase();
  }

  if (last_callback_time != 0) {
    diagNoteFrameInterval(
      (u32) OS_CYCLES_TO_USEC(callback_time - last_callback_time));
  }
  last_callback_time = callback_time;
}

void diagFrameEnd(void) {
  diagPaintPhase(DIAG_PHASE_DONE);
}

/*
 * The overlay.  A vertical stack on the left edge, deliberately narrow so a
 * game's own HUD keeps the rest of the screen.
 */
u32 drawDiagnosticRow(const char *label, u32 value, u32 y) {
  drawString(label, 6, y);
  drawUnsigned(value, 16, y);
  return y + 11;
}

void drawDiagnosticsOverlay(void) {
  u32 y = 30;

  if (!diagnostics_visible) {
    return;
  }
  setTextColor(255, 255, 255);
  /* Frames actually displayed in the last whole second. */
  y = drawDiagnosticRow("F", diagFramesPerSecond(), y);
  /* Worst callback gap and worst CPU block, in tenths of a millisecond. */
  y = drawDiagnosticRow("W", diagWorstFrameInterval() / 100, y);
  y = drawDiagnosticRow("B", diagWorstGatedWork() / 100, y);
  /* Display list commands used by the last frame, against the budget. */
  y = drawDiagnosticRow("D", gfxCommandsUsed(), y);
  y = drawDiagnosticRow("O", frame_overflows, y);
  drawDiagnosticRow("U", audioHeapPeakKiB(), y);
}

/*
 * The post-mortem the frozen screen cannot show.  With the .out symbol map,
 * PC/RA turn a fault into a source line; see tools/resolve_freeze.sh.  Written
 * once, after the heartbeat has been stale for two seconds, from the watchdog
 * thread -- the console is already dead, so a failed write loses nothing.
 */
static DiagReportHook diag_report_hook;

void diagSetReportHook(DiagReportHook hook) {
  diag_report_hook = hook;
}

char *diagReportHex(char *out, const char *label, u32 value) {
  int shift;

  while (*label) {
    *out++ = *label++;
  }
  *out++ = ' ';
  for (shift = 28; shift >= 0; shift -= 4) {
    *out++ = "0123456789ABCDEF"[(value >> shift) & 15];
  }
  *out++ = '\n';
  return out;
}

/* From libultra's fault handler (os_internal_error.h); present in the release
   library.  NULL when no thread has faulted. */
extern OSThread *__osGetCurrFaultedThread(void);

#define DIAG_REPORT_SIZE 768
#define DIAG_REPORT_GAME_LIMIT 256

static void diagWriteFreezeReport(void) {
  static char report[DIAG_REPORT_SIZE];
  char *out = report;
  OSThread *faulted = __osGetCurrFaultedThread();

  out = diagReportHex(out, "PHASE", diag_current_phase);
  out = diagReportHex(out, "HEART", diag_heartbeat);
  out = diagReportHex(out, "HUNG", diag_task_hung);
  if (faulted != NULL) {
    out = diagReportHex(out, "THREAD", (u32) faulted->id);
    out = diagReportHex(out, "PC", faulted->context.pc);
    out = diagReportHex(out, "CAUSE", faulted->context.cause);
    out = diagReportHex(out, "BADV", faulted->context.badvaddr);
    out = diagReportHex(out, "SR", faulted->context.sr);
    out = diagReportHex(out, "RA", (u32) faulted->context.ra);
    out = diagReportHex(out, "SP", (u32) faulted->context.sp);
  }
  if (diag_report_hook != NULL) {
    out += diag_report_hook(out, DIAG_REPORT_GAME_LIMIT);
  }
  storageWriteFreezeReport(report, (u32) (out - report));
}

/*
 * The watchdog thread.  Runs above every game and NuSystem application thread,
 * woken by a hardware timer, so it keeps executing when the graphics thread is
 * stuck in a loop (the N64 scheduler never preempts by time slice, but timer
 * interrupts still fire) and when that thread has been stopped by a CPU
 * exception.
 */
#define DIAG_WATCHDOG_THREAD_ID 200
#define DIAG_WATCHDOG_PRIORITY 126

static OSThread diag_watchdog_thread;
static u64 diag_watchdog_stack[0x800 / sizeof (u64)];
static OSMesgQueue diag_watchdog_queue;
static OSMesg diag_watchdog_messages[1];
static OSTimer diag_watchdog_timer;
/* A CPU exception posts OS_EVENT_FAULT.  Polling it is what lets the frozen
   square's bottom band say whether the dead thread crashed or is spinning. */
static OSMesgQueue diag_fault_queue;
static OSMesg diag_fault_messages[1];

static void diagWatchdogThread(void *arg) {
  u32 last_heartbeat = 0;
  u32 stale_seconds = 0;
  u8 report_written = FALSE;

  (void) arg;
  osCreateMesgQueue(&diag_watchdog_queue, diag_watchdog_messages, 1);
  osCreateMesgQueue(&diag_fault_queue, diag_fault_messages, 1);
  osSetEventMesg(OS_EVENT_FAULT, &diag_fault_queue, NULL);
  osSetTimer(&diag_watchdog_timer, 0, OS_USEC_TO_CYCLES(1000000),
    &diag_watchdog_queue, NULL);
  while (1) {
    (void) osRecvMesg(&diag_watchdog_queue, NULL, OS_MESG_BLOCK);
    if (!diag_cpu_faulted &&
        osRecvMesg(&diag_fault_queue, NULL, OS_MESG_NOBLOCK) == 0) {
      diag_cpu_faulted = TRUE;
    }
    if (diag_heartbeat != last_heartbeat) {
      last_heartbeat = diag_heartbeat;
      stale_seconds = 0;
      continue;
    }
    if (++stale_seconds >= 2) {
      diagPaintStalePhase();
      if (!report_written) {
        report_written = TRUE;
        diagWriteFreezeReport();
      }
    }
  }
}

void initDiagnostics(void) {
  osCreateThread(&diag_watchdog_thread, DIAG_WATCHDOG_THREAD_ID,
    diagWatchdogThread, NULL,
    diag_watchdog_stack + sizeof (diag_watchdog_stack) / sizeof (u64),
    DIAG_WATCHDOG_PRIORITY);
  osStartThread(&diag_watchdog_thread);
}
