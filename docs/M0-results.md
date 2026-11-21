# M0 results (host only)

Milestone M0: the libretro loader (`src/retro/emu.c`), two instances from
two copies of the core, a harness map capture, and the numbers that decide
whether the design holds. Every number here is tagged **host** or
**device**. No device numbers yet: the Duo needs the Android build (after
review).

## Setup

| | |
|---|---|
| Host | AMD Ryzen 5 5600X (6 cores), WSL2 on Windows, Ubuntu 22.04, gcc 11.4 |
| bsnes | libretro/bsnes `63ee22f`, patched to expose work RAM (`scripts/cores/bsnes-workram.patch`), built by `scripts/build-cores.sh` with its default flags |
| snes9x | libretro/snes9x `204ea1c`, default flags; prototyping only, never shipped |
| Harness | `make m0`, then `build/host/snes-harness CORE ROM OUTDIR SCRIPT` (`-O2`) |
| Super Metroid | (JU) [!], CRC32 `D63ED5F8` (no copier header) |
| A Link to the Past | (USA), CRC32 `777AAC2F` (no copier header) |

States used: Super Metroid on Ceres Station just after the intro (made in
the harness, see below), and A Link to the Past just outside Link's house
in the rain. States are per core (a bsnes state doesn't load in snes9x).

## Answers

### Unthrottled fps of one instance

**host**, `bench 1200`, drawn frames:

| | Super Metroid (Ceres) | ALttP (outdoors, rain) |
|---|---|---|
| bsnes | 1.60 ms (623 fps) | 2.54 ms (394 fps) |
| snes9x | 0.53 ms (1904 fps) | 0.44 ms (2291 fps) |

Both instances at once on two threads (the live game plus the hidden copy
working), per frame pair: bsnes 1.87 ms / 2.86 ms, snes9x 0.66 ms /
0.56 ms (**host**). bsnes is 3 to 6 times slower than snes9x, as expected
for its accuracy.

**device:** not measured yet.

### serialize / unserialize cost and state size

**host**, mean of 200 calls:

| | state size | serialize_size | serialize | unserialize (same) | unserialize (into B) |
|---|---|---|---|---|---|
| bsnes | 290,421 bytes | 0.03 ms | 0.03 ms | 5.7 ms | 5.7 ms |
| snes9x | 823,407 bytes | 0.01 ms | 0.03 ms | 0.08 to 0.23 ms | 0.04 to 0.22 ms |

- bsnes's `retro_serialize_size` builds a whole state just to measure it
  (cheap here, but call it once and keep the size).
- bsnes's unserialize costs 5.7 ms because a synchronized state is loaded
  with `power(reset = false)` first. It runs on the hidden copy's thread,
  so it adds to a capture's latency, not to the live game's frame.

### Does bsnes's retro_serialize force a thread synchronize?

**Yes.** `retro_serialize` calls `System::serialize()` with its default
`synchronize = true`, which runs `runToSave()`: the emulation runs forward
until every thread (CPU, SMP, PPU, coprocessors) reaches a sync point.
Run-ahead (`run_with_runahead`) uses `serialize(0)`, the non-synchronizing
path, but `retro_serialize` never does, whatever
`RETRO_ENVIRONMENT_GET_SAVESTATE_CONTEXT` says (bsnes doesn't ask).

Measured (**host**): two serializes in a row with no frame between them
differ in 27 or 28 bytes: the core ran.

The non-synchronizing path couldn't be used anyway: its state holds the
libco thread stacks, return addresses into the .so that saved it. Our two
instances are two copies of the .so at different addresses, so such a
state can't move from A to B. The synchronizing path is the right one for
SNES DS.

**What it does to the live game**, and the fix. While `runToSave` runs, the
game may read the controller, and it gets the keys the core was last given
(the previous frame's). With a key change on exactly the next frame, the
live game sees it a frame late. Measured on ALttP (**host**): serializing
the live game every 60 frames while the input changes on those frames left
it 18 work RAM bytes apart from an untouched copy after 600 frames (Link 6
pixels off). With no input, a held key, or key changes 15 frames away from
the serializes, it stayed byte-identical. Super Metroid on Ceres stayed
identical in every case.

The fix is `emu_state_save_live(e, nextKeys, ...)`: give the core the
coming frame's keys before serializing. With it, the same ALttP test and
the Super Metroid test stay byte-identical (work RAM and picture) after
600 frames. **The app must snapshot the live game only this way.**

snes9x's serialize doesn't run the core (two serializes are identical) and
the live game stays identical either way.

### Is a state copied from A into B faithful?

Yes, for both cores (**host**): A's state loaded into B, then both run 240
frames with the same input: work RAM and picture byte-identical, on both
games, with savestate context 0 (normal) and 2 (run-ahead, same binary;
snes9x's fast path). Two copies of the core don't share anything.

### Does the core honour RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE?

- **bsnes: no.** It never asks. "Blind" frames (`emu_frame_blind`) are 3 to
  4% faster, only because the loader skips converting the picture (1.54 vs
  1.60 ms on Super Metroid, 2.47 vs 2.54 ms on ALttP). The
  hidden copy pays full rendering cost on every frame.
- **snes9x: yes.** It sets `IPPU.RenderThisFrame` from bit 0, and mutes
  audio work with bit 3 (hard disable), which the loader sets for an
  instance without audio. Blind frames are 2 to 2.7 times faster (0.19 vs
  0.53 ms on Super Metroid, 0.21 vs 0.44 ms on ALttP).

A bsnes patch to skip PPU rendering on blind frames is possible (its fast
PPU renders per line), but for map capture only the frames before the map
is up could be blind, so it saves little: on ALttP the map is up 4 frames
after the button and the remaining 45 frames need drawing anyway to see
the picture settle.

### Time from state copy to a settled map

`mapcap`: copy A into B, hold the button 4 frames on B, run B until the
work RAM conditions hold, then until the picture holds still (at most 64
pixels changing) for 8 frames, not black. A is never touched.

| | frames | ms (**host**) |
|---|---|---|
| ALttP overworld map (X; module `$10 = 0E`, submodule `$11 = 07`), bsnes | 49 | 90 (5.7 of it the state copy) |
| ALttP, snes9x | 48 | 21 |
| Super Metroid pause map, bsnes | **pending** | **pending** |
| Super Metroid, snes9x | **pending** | **pending** |

**Super Metroid is pending a Zebes save.** The game doesn't pause on Ceres
Station (area index `$7E079F` = 6): Start does nothing there, so the map
can't be opened from the only state the harness can reach on its own
(Ceres ends in a timed escape). With a Zebes `.srm` from any emulator, or a
state from these core versions, the same command answers it:
`mapcap smmap press=START hold=4 until=7E0998:0F` (game state `0x0F` is
the pause screen; to be confirmed with `peek` when the save arrives).

**device:** at the host's 1.6 to 2.5 ms per bsnes frame, a 49-frame
capture is 80 to 125 ms of one core's time. If the Duo is 3 to 5 times
slower per core (a guess until measured), that's well under the
once-a-second capture interval GBA DS uses.

## Observations along the way

- **libretro's bsnes exposes no memory** (`retro_get_memory_data` returns
  NULL for everything: "no safe/sensible way to use the memory
  interface"). The shipped core carries a 17-line patch for work RAM. Save
  RAM stays with bsnes (it loads and saves `<save dir>/<ROM name>.srm`
  itself), so the hidden copy gets a data directory of its own.
- **bsnes reports a 512x448 base geometry** but draws 256x224 frames in
  normal play. The loader takes the size of each frame as drawn.
- **bsnes uses libretro's default 0RGB1555 pixels** (its own call to set the
  format is commented out); snes9x asks for RGB565. The loader converts both
  to GBA DS's byte order.
- **ALttP's overworld map captures cleanly.** The zoomed map (the default
  when X is pressed) settles in 48 to 49 frames and is a sharp, stable picture. Its
  limits for the second screen are different from "captures badly": it shows
  only the area around Link, and his head marker blinks (the capture above
  caught it off; GBA DS's recorded animation loop handles that). Custom
  asset mode is still the better fit for a whole-world view, for those
  reasons.
- Super Metroid's game state `$7E0998`: `01` title, `04` file select (SAMUS
  DATA), `02` OPTION MODE (start game, language), `1E` intro text (Start turns the pages), `1F` then `07` on the way
  in, `08` gameplay.
- ALttP's module `$7E0010`: `00` title, `01` file select, `04` name entry,
  `05` loading, `07` indoors, `09` overworld, `0E` text and menus
  (submodule `$11` = `02` text box, `07` the overworld map).

## How to reproduce

```sh
scripts/build-cores.sh snes9x
make m0
# OUT: a scratch directory; the scripts are in the M0 notes below
build/host/snes-harness ../cores/bsnes/bsnes_libretro.so ROM OUT SCRIPT
```

Super Metroid to Ceres (states are saved in OUT as `ceres.state`): wait
600 frames, tap Start four times 120 frames apart (title, file A, start
game), wait 300, then tap Start 58 times 120 frames apart (the intro's
pages), wait 600, `save ceres`. ALttP outdoors: wait 400, tap Start six
times 90 frames apart, `save ztitle`; Start, Start, A (a letter), Down x3,
Right x2, A (END), A; tap A 60 times 48 frames apart (the opening text);
hold Right 20, hold Down 150, wait 120, hold Down 30, wait 30, `save zout`.

Then `load ceres` or `load zout`, and `bench 1200` or the `mapcap` lines
above. `pattern N` and `keysfirst 0|1` before `bench` reproduce the
determinism checks.
