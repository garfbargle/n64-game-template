#ifndef ENGINE_DIAG_H
#define ENGINE_DIAG_H

#include <nusys.h>

/*
 * Freeze forensics.
 *
 * A real N64 that locks up shows you a still picture and nothing else: no
 * console, no debugger, no core dump.  This is the machinery that makes that
 * still picture say something.
 *
 * A high-priority watchdog thread, woken by a hardware timer, keeps running
 * when the graphics thread is stuck in a loop (the N64 scheduler never
 * preempts by time slice, but timer interrupts still fire) and when that
 * thread has been stopped by a CPU exception.  Once the frame heartbeat goes
 * stale for two seconds it paints the last recorded phase colour into both
 * framebuffers, every second, forever -- so the colour left on a frozen screen
 * names the subsystem that died -- and writes a text post-mortem to the
 * flashcart SD if one is attached.
 *
 * None of this costs anything while the game is healthy: a phase paint is a
 * handful of CPU stores into a scanline the picture covers anyway.
 *
 * See docs/hardware.md for how to read the result.
 */

/* Start the watchdog thread.  The engine calls this during boot; a game never
   needs to. */
void initDiagnostics(void);
/* Call at the top of every graphics callback, before anything else. */
void diagFrameStart(int pendingGfx);
/* Call at the foot of it. */
void diagFrameEnd(void);
/* Once per submitted graphics task, so the FPS row counts displayed frames
   rather than callbacks.  gfxEndFrame does this. */
void diagNoteFrameSubmitted(void);

/*
 * Record which subsystem is running.  The frozen screen shows the last one
 * reached, so a game should paint its own phases around anything that can take
 * more than a frame.  Any GPACK_RGBA5551 colour works; these are the engine's,
 * chosen to be told apart on a composite CRT.
 */
void diagPaintPhase(u16 color);

#define DIAG_PHASE_DRAW GPACK_RGBA5551(0, 255, 255, 1)       /* cyan */
#define DIAG_PHASE_UPDATE GPACK_RGBA5551(255, 0, 255, 1)     /* magenta */
#define DIAG_PHASE_LOAD GPACK_RGBA5551(255, 0, 128, 1)       /* rose */
#define DIAG_PHASE_SAVE GPACK_RGBA5551(255, 200, 0, 1)       /* amber */
#define DIAG_PHASE_AUDIO GPACK_RGBA5551(128, 255, 0, 1)      /* lime */
#define DIAG_PHASE_DONE GPACK_RGBA5551(0, 0, 255, 1)         /* blue */
/* Free for the game to use, so its phases cannot be confused with the
   engine's. */
#define DIAG_PHASE_GAME_1 GPACK_RGBA5551(0, 255, 0, 1)       /* green */
#define DIAG_PHASE_GAME_2 GPACK_RGBA5551(255, 255, 0, 1)     /* yellow */
#define DIAG_PHASE_GAME_3 GPACK_RGBA5551(255, 140, 0, 1)     /* orange */
#define DIAG_PHASE_GAME_4 GPACK_RGBA5551(255, 255, 255, 1)   /* white */

/*
 * Frame pacing evidence.  `W` is the worst wall-clock gap between graphics
 * callbacks and `B` the worst CPU time spent inside the gated work block, both
 * over a rolling ~2s window; FPS counts frames actually submitted, which is
 * the rate the player sees -- a callback that finds a task still in flight
 * returns without building a frame.
 *
 * Read them together: a low FPS with W and B both near 166 is the RSP/RDP
 * being the bottleneck rather than your game logic.
 */
void diagNoteGatedWork(u32 usec);
u32 diagWorstFrameInterval(void);
u32 diagWorstGatedWork(void);
u32 diagFramesPerSecond(void);
extern volatile u32 diag_heartbeat;

/* TRUE once a CPU exception has been seen.  The frozen square's bottom band
   says whether the dead thread crashed (white) or is spinning (black) --
   entirely different hunts. */
extern volatile u8 diag_cpu_faulted;

/*
 * The overlay.  Off by default; the engine switches it on automatically when
 * anything anomalous happens, because a fault the player can reproduce is
 * worth more than a clean screen.  Draw it from the textures phase.
 */
extern u8 diagnostics_visible;
void drawDiagnosticsOverlay(void);
/* One labelled row in the overlay's style; returns the next row's y, so a game
   can append its own counters underneath. */
u32 drawDiagnosticRow(const char *label, u32 value, u32 y);

/*
 * Extra lines for the SD post-mortem, written after the console is already
 * dead.  A game supplies whatever would tell it where it was -- a level index,
 * a player position, a state machine's state.  Called from the watchdog
 * thread, so it must touch nothing that takes a lock and nothing that can
 * itself fault; reading a global is exactly right, calling into gameplay is
 * not.
 *
 * `out` has room for `limit` bytes; return how many were written.  Default
 * writes nothing.
 */
typedef u32 (*DiagReportHook)(char *out, u32 limit);
void diagSetReportHook(DiagReportHook hook);
/* The helper the engine's own lines use: appends "LABEL 0000ABCD\n". */
char *diagReportHex(char *out, const char *label, u32 value);

#endif /* ENGINE_DIAG_H */
