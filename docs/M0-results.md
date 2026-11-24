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
| Harness | `make m0`, then `build/host/retrods-harness CORE ROM OUTDIR SCRIPT` (`-O2`) |
| Super Metroid | (JU) [!], CRC32 `D63ED5F8` (no copier header) |
| A Link to the Past | (USA), CRC32 `777AAC2F` (no copier header) |

States used: Super Metroid on Ceres Station just after the intro (made in
the harness), Super Metroid at the ship in Crateria (from the owner's
`.srm`, file A, loaded in the harness), and A Link to the Past just outside
Link's house in the rain. States are per core (a bsnes state doesn't load
in snes9x).

## Answers

### Unthrottled fps of one instance

**host**, `bench 1200`, drawn frames:

| | Super Metroid (Ceres) | Super Metroid (Crateria, rain) | ALttP (outdoors, rain) |
|---|---|---|---|
| bsnes | 1.60 ms (623 fps) | 2.65 ms (378 fps) | 2.54 ms (394 fps) |
| snes9x | 0.53 ms (1904 fps) | 0.43 ms (2342 fps) | 0.44 ms (2291 fps) |

Both instances at once on two threads (the live game plus the hidden copy
working), per frame pair: bsnes 1.87 / 3.04 / 2.86 ms, snes9x 0.66 /
0.58 / 0.56 ms (**host**). bsnes is 3 to 6 times slower than snes9x, as expected
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
live game sees it a frame late. Measured (**host**), serializing the live
game every 60 frames while the input changes on those frames, compared
with an untouched copy after 600 frames: ALttP 18 work RAM bytes apart
(Link 6 pixels off), Super Metroid in Crateria 222 bytes apart. With no
input, a held key, or key changes 15 frames away from the serializes, ALttP
stayed byte-identical. Super Metroid on Ceres happened to stay identical
(the read falls elsewhere in its frame there).

The fix is `emu_state_save_live(e, nextKeys, ...)`: give the core the
coming frame's keys before serializing. With it, all three tests (ALttP,
Super Metroid on Ceres and in Crateria) stay byte-identical, work RAM and
picture, after 600 frames. **The app must snapshot the live game only this
way.**

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

A bsnes patch to skip PPU rendering on blind frames is worth measuring.
Only the frames before the screen is up can be blind, and that depends on
the game: on ALttP the map is up 4 frames after the button (45 more are
drawn to see it settle), but Super Metroid's pause fades out and loads for
67 to 68 of its 76 to 77 frames. On snes9x, running those blind cut the
Super Metroid capture from 28 to 17 ms. How much of bsnes's frame time is
rendering hasn't been measured.

### Time from state copy to a settled map

`mapcap`: copy A into B, hold the button 4 frames on B, run B until the
work RAM conditions hold, then until the picture holds still (at most 64
pixels changing) for 8 frames, not black. A is never touched.

| | frames | ms (**host**) |
|---|---|---|
| ALttP overworld map (X; module `$10 = 0E`, submodule `$11 = 07`), bsnes | 49 | 90 (5.7 of it the state copy) |
| ALttP, snes9x | 48 | 21 |
| Super Metroid pause map (Start; `$0998 = 0F`), bsnes | 76 to 77 | 183 to 193 (6 of it the state copy) |
| Super Metroid, snes9x | 76 | 28 (17 with the frames before `0F` blind) |

Super Metroid's pause screen never holds perfectly still: the map marker's
brackets and the L / R button prompts blink, changing 428 to 538 pixels
every third frame. It counts as settled with `maxdiff=600` (GBA DS's
profiles have the same `maxDiff` for blinking markers; the prompts are the
kind of thing a profile crops or covers). The pause is reached in 67 to 68
frames (`0C` fade out, `0D` / `0E` loading the pause screen, `0F` paused),
then 9 more to settle.

Super Metroid can't be paused on Ceres Station (area index `$7E079F` = 6),
nor for a few seconds after a save is loaded (Samus faces the screen and
ignores input). A capture tried then fails, as it should; the profile's
`live` check needs to cover that.

**device:** at the host's 1.6 to 2.7 ms per bsnes frame, a Super Metroid
capture is about 190 ms of one core's time, ALttP's 90 ms. If the Duo is 3
to 5 times slower per core (a guess until measured), a Super Metroid
capture takes 0.6 to 1 s: close to the once-a-second interval GBA DS
uses, so the map would trail the game by up to a second or two. It runs on
its own core and never slows the live game, but it's the first number to
measure on the device, and the reason the bsnes blind-frame patch is worth
trying.

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
  DATA), `02` OPTION MODE (start game, language), `1E` intro text (Start
  turns the pages), `1F` then `07` on the way in, `05` / `06` / `07`
  loading a save (the planet map, then the room), `08` gameplay, `0C` /
  `0D` / `0E` pausing, `0F` paused. `$0A1C` is Samus's pose (`00` facing
  the screen, as after loading).
- Super Metroid's controller: `$8B` held, `$8F` newly pressed (Start is
  `0x1000`).
- ALttP's module `$7E0010`: `00` title, `01` file select, `04` name entry,
  `05` loading, `07` indoors, `09` overworld, `0E` text and menus
  (submodule `$11` = `02` text box, `07` the overworld map).

## How to reproduce

```sh
scripts/build-cores.sh snes9x
make m0
# OUT: a scratch directory; the scripts are in the M0 notes below
build/host/retrods-harness ../cores/bsnes/bsnes_libretro.so ROM OUT SCRIPT
```

Super Metroid to Ceres (states are saved in OUT as `ceres.state`): wait
600 frames, tap Start four times 120 frames apart (title, file A, start
game), wait 300, then tap Start 58 times 120 frames apart (the intro's
pages), wait 600, `save ceres`. ALttP outdoors: wait 400, tap Start six
times 90 frames apart, `save ztitle`; Start, Start, A (a letter), Down x3,
Right x2, A (END), A; tap A 60 times 48 frames apart (the opening text);
hold Right 20, hold Down 150, wait 120, hold Down 30, wait 30, `save zout`.

Super Metroid in Crateria: copy the `.srm` to `OUT/a/<ROM name>.srm`;
wait 600, tap Start twice 120 frames apart (`save filesel`), tap Start five
times 120 frames apart (file A, the planet map, into the room), wait 300,
`save zebes`; then wait 200, hold Left 30, wait 30, `save zebes2` (Samus
controllable).

Then `load ceres`, `load zebes2` or `load zout`, and `bench 1200` or the
`mapcap` lines above (Super Metroid: `mapcap smmap press=START hold=4
until=7E0998:0F stable=8 maxdiff=600`). `pattern N` and `keysfirst 0|1` before `bench` reproduce the
determinism checks.
