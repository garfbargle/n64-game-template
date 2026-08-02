# RAM budget

Where the console's 4 MiB goes, what the engine has already spent, and how to
find out what your game is spending.

A retail N64 has 4 MiB of RDRAM and no Expansion Pak. Targeting the stock
machine is a choice worth making deliberately: it is what most people own, it
is what emulators default to, and a game that fits in 4 MiB also fits in 8.

## Measuring

`tools/check_ram.py` runs at the end of every build and is the one that matters
day to day. It prints the headroom line you have already seen:

```
n64game.out: image ends at 0x8008D100, framebuffers begin at 0x8038F800 (3081 KiB free)
```

That number is the honest one: the gap between the end of your linked image and
the fixed addresses NuSystem pins its framebuffers at. When it reaches zero the
image has overrun the framebuffers, and the failure on hardware is corruption
rather than a linker error — which is why the check runs on every build rather
than on request.

For a breakdown, `tools/ram_report.py` parses the linked ELF directly — section
headers and symbol tables, no toolchain needed — and prints every section and
symbol above a size threshold, largest first:

```sh
python3 tools/ram_report.py
```

## What is already spent

The starter game leaves roughly 3 MiB free. Almost all of what is gone is fixed
cost that any N64 game pays:

| Object | Size | What it is |
| --- | --- | --- |
| Framebuffers | 2 × 150 KiB | 320×240, 16-bit, double buffered. Pinned by NuSystem at the top of RDRAM. |
| Z-buffer | 150 KiB | Same dimensions, 16-bit depth. |
| RDP command FIFO | 64 KiB | `engine/src/rdp_fifo.c` |
| Display lists | 2 × 26 KiB | `FRAME_DISPLAY_LIST_SIZE` in `gfx.h`, double buffered. |
| Audio heap | 70 KiB | Audio builds only; sits directly beneath the framebuffers. |
| NuSystem, libultra, libcart, FatFS | ~300 KiB | Code and its own buffers. |

### The RDP command FIFO

The ring the RSP writes RDP commands into and the RDP drains. NuSystem sizes it
at 128 KiB by default; the engine halves that in `engine/src/rdp_fifo.c`, which
works because `nuRDPOutputBuf` is the only symbol in its archive member — so
defining it locally means the linker never pulls NuSystem's copy in at all.

The size has to be handed over explicitly. `nuGfxInit` passes the SDK constant
rather than `sizeof`, so a smaller array on its own would leave the RSP
believing it still has 128 KiB; `engineSetRDPFifo` re-registers the real size
and must be called immediately after `nuGfxInit`, which `initGraphics` does.

With the fifo microcode a short FIFO costs throughput, not correctness: the RSP
stalls until the RDP drains rather than overrunning anything. If you are drawing
scenes far heavier than the starter game and the RSP looks starved, this is a
knob worth turning back up.

### The audio heap

Audio builds only. `alHeap` never frees, so its high-water mark is reached
before the first note plays: `nuAuMgrInit` takes the voices, the DMA buffers and
the command list, and `nuAuSndPlayerInit` takes the sound state.

`ENGINE_AU_HEAP_SIZE` in `engine/src/audio.c` is 70 KiB against a measured peak
of 64. Anything that changes `maxVVoices`, `maxPVoices`, `maxUpdates`,
`nuAuDmaBufNum`/`Size`, `nuAuAcmdLen` or `maxSounds` moves that ceiling.

Re-read it on hardware after touching any of them — `audioHeapPeakKiB()` rides
the diagnostics overlay. An undersized audio heap fails **only** on hardware,
and only as silence or corruption; no emulator reproduces it.

## Where a game's memory usually goes

The engine's cost is fixed. Yours will not be, and on this machine the
expensive things are predictable:

- **Anything sized by world extent.** One byte per cell over a 256×64×256
  volume is 4 MiB on its own. A game that needs a large world streams it rather
  than holding it, which means deciding early — retrofitting streaming is a
  rewrite, not a change.
- **Vertex data held in RAM.** Models that never change belong in ROM: the
  cartridge is 8–64 MiB and RDRAM is 4. Mark them `static const` and let the
  linker put them in `.rodata`.
- **Double-buffered per-entity state.** Anything the RSP reads has to be
  double-buffered (see `NUM_DISPLAY_LISTS`), so a matrix per part per entity
  costs twice what it looks like.
- **Textures.** At 160 bytes each these are nearly free — a thousand of them is
  156 KiB. This is rarely the problem, which tends to surprise people arriving
  from other platforms.

## If you run out

Roughly in order of what they buy against what they cost:

1. Move constant data to ROM (`static const`).
2. Shrink whatever is sized by world extent. This is nearly always the answer.
3. Drop `FRAME_DISPLAY_LIST_SIZE`, if `gfxCommandsUsed()` shows real headroom.
4. Drop the RDP FIFO further, and accept the RSP stalling more often.
5. Require the Expansion Pak, and accept that many players do not have one.
