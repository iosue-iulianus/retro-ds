# The Wind Waker (GameCube): a feasibility study

Could RetroDS run The Wind Waker's GameCube disc in libretro's Dolphin, with
a Wii U GamePad style second screen made from the game's own art? Studied
Nov 25 to 26, 2026, with the owner, on the PC and the Retroid Pocket Duo.
The study's handoff (the questions asked) lived beside the repo as
`handoff-tww.md`; this is its findings, kept in the repo.

**Outcome: shelved for this game.** Wind Waker HD's own GamePad screen
already runs on Android dual-screen handhelds (ZeldaWWHDRecomp, below).
What was learned is general to GameCube games, and is what this branch
keeps:

- `tools/gamecube/`: a GameCube disc reader (`gcdisc.py`), Yaz0, RARC and
  BTI (`gcformats.py`, `gcdump.py`), a J2D screen layout renderer
  (`gcblo.py`), Dolphin state files unpacked for the libretro core
  (`dolphin_state.py`), and a test frontend for libretro's Dolphin
  (`lrtest.c`; `lrvk.c` is the Vulkan path and its hand-over to GLES) with
  `build.sh`, `run_host.sh` and `run_device.sh`. Pictures they make are the
  player's game: never commit them.
- Not in the repo (beside it, in `tww-spike/`, as `build-cores.sh` keeps
  cores in `../cores/`): libretro Dolphin's clone and builds (Android with
  NDK r29; the PC with GCC 12, X11, ALSA, evdev, udev and HWDB off), the
  decomp (zeldaret/tww, CC0), the owner's test files in
  `test-data/wind-waker/`.

## Findings

> **Superseded for this game (Nov 26, 2026, with the owner).** The Wind
> Waker HD already runs natively on Android with its own GamePad screen
> on a dual-screen handheld's second display:
> [ZeldaWWHDRecomp](https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp)
> (MPL-2.0, a static recompilation of the Wii U game from the player's
> own dump; started Oct 1, 2026, v0.5.1 Nov 6, very active). Its Android
> version puts the GamePad picture (map, items, menus, with the Wii U's
> touch) on a second Presentation display since Oct 10 (#98), defaults
> suited to AYN Thor-style handhelds since Nov 12 (#531), and fills the
> bars round the 16:9 GamePad picture on other screen shapes (#161).
> Not yet tried on the Duo: its speed on the Adreno 722 and how it lays
> out the Duo's 16:9 top and 4:3 bottom screens are unknown; it needs a
> Wii U dump (USA or EU, version 0) and keys, not the GameCube ISO.
>
> So RetroDS's own Wind Waker second screen is shelved: the game did have
> a second screen (HD's), it's already on Android handhelds, and ours
> would be a 2 to 3 month rebuild of it from GameCube art (Philosophy 1:
> "the second screen they never had"). What stays useful is general to
> GameCube games: libretro Dolphin on the Duo, the Vulkan to GLES
> hand-over (option 1), reading RAM, the disc, archive, texture and
> layout readers, pressing a game's buttons for its menus. GameCube
> support goes back to being a system-level question, for games without
> an HD dual-screen version (Four Swords Adventures, which used the GBA
> as each player's screen, is one in the owner's library, not looked
> into). Next for this game: the owner tries the recomp on the Duo.
>
> The findings below stand as written, as a record.

*Nov 26, 2026. Every number says host (the PC: WSL, RTX 4070 Ti through
Mesa's D3D12 GLES 3.1) or device (the Duo).*

**Recommendation: go, if** (1) the owner accepts option 1 below (Dolphin
on Vulkan, its picture handed to RetroDS's GLES drawing) and (2) the
second screen is built in steps that each stand alone: items first, then
the sea chart, then the island and dungeon maps. The emulator, the
speed, reading the game and pressing its buttons are all shown working
on the Duo or the PC; the big cost is drawing the game's screens.

### The game

The ISO: GZLE01, disc 0, revision 0, 1,459,978,240 bytes, MD5
`d8e4d45af2032a081a0f446384e9261b`, SHA-1
`6b5f06c10d50ebb4099cded88217eb71e5bfbb4a` (not checked against Redump:
no network check made). Its `main.dol` (SHA-1 `8d28bab6...7a67`) is
exactly the decomp's GZLE01 build, so the decomp's addresses are this
disc's. The decomp (zeldaret/tww) is **CC0** (public domain), freer than
sotn-decomp's AGPL; still used as a reference only, as decided.

The owner's save and a Dolphin state on Outset are in
`test-data/wind-waker/` (read-only): the memory card file
`duo-dolphin-2026-11-26-01-GZLE-gczelda.gci` and the state
`duo-dolphin-2609b-2026-11-26-GZLE01.s00`. The Dolphin app's state
unpacks (`tools/gamecube/dolphin_state.py`, LZ4) into the buffer the libretro core's
`retro_unserialize` takes, and loads there, so the app's states are test
states for the core.

### 1. The emulator

libretro's Dolphin (`libretro/dolphin`) is the one to use. It is
**maintained** (last commit Nov 25, 2026; several a week), **GPLv2 or
later** (compatible with RetroDS's GPLv3), and based on upstream's
release **2609** (Sep 23, 2026): 323 commits behind upstream master, 467
of its own. The Duo's Dolphin app is 2609b, the same release.

It **builds unchanged for Android arm64** with NDK r29 (CMake, as
libretro's own CI does; `build-cores.sh` would add it as it does
SwanStation): 15.1 MB stripped (6.1 MB gzipped), plus Dolphin's `Sys`
folder (15 MB, 8 MB of it per-game settings), which the core needs at
run time. On the host it needs GCC 12 or newer (the owner installed
`g++-12`) and builds with X11, ALSA, evdev and udev switched off.
Embedding upstream Dolphin instead isn't needed.

Found on the way, things RetroDS's frontend must do: call
`retro_set_controller_port_device` (without it the pad is ignored);
answer `SET_HW_SHARED_CONTEXT` (an experimental call: its flag must not
be masked away); and use the hardware renderer, as its **Software and
Null renderers crash at boot in this libretro build** (only the hardware
path sets the video backend up).

### 2. Rendering

RetroDS draws both screens with SDL2's GLES renderer on one context (the
second screen is `android.c`'s EGL surface on the other display). The
core can render with GLES 3.x (sharing a context with the frontend) or
Vulkan. Both were run on the Duo with the test frontend:

- **GLES** works but is the weak path on the Adreno driver: startup grew
  to **50 to 70 s** (device) as Dolphin's shader cache filled, since it
  rebuilds every cached shader at boot.
- **Vulkan** (Qualcomm's driver 512.800.73, Vulkan 1.3) starts in **4 s**
  (device). This is what the Duo's Dolphin app uses.

**Option 1, tested: Vulkan for Dolphin, GLES for RetroDS.** RetroDS sets
up the Vulkan device through libretro's negotiation interface (adding
the extensions it needs), gives the core a surface from an offscreen
`AImageReader` (the core asks the real driver about its surface), copies
each frame on the GPU into an Android shared buffer (AHardwareBuffer),
hands GLES the copy's completion as a sync fd (`EGL_ANDROID_native_fence_sync`,
`eglWaitSyncKHR`: the GPU waits, not the CPU) and draws the buffer as a
texture (`EGL_ANDROID_image_native_buffer`). Measured on the device:
GLES drew **exactly the frame Vulkan had just made** (0 of 1,351,680
bytes different, three checks); GLES's drawing done **1.7 ms after
`retro_run` returns**, so **no frame of latency** added; about 0.6 ms of
CPU per frame. SNES and PlayStation games are untouched.

Options 2 and 3 (discussed with the owner, not tested): RetroDS on SDL3
drawing with Vulkan (3 to 5 weeks with regression testing), and
RetroArch's slang shaders through librashader (MPL-2.0, Rust: a new
dependency; 1 to 2 weeks on top of option 2). Not needed for this game.

**Custom drivers.** Dolphin loads Turnip (Mesa) through libadrenotools
(BSD-2). The owner uses Turnip Duo v5.1 (sherft2607/turnip-adreno722-duo,
no licence stated, so never shipped; the player could supply it as in
Dolphin). With libretro, the frontend creates the Vulkan instance but the
core loads its own Vulkan library on Android, so using Turnip needs a few
lines changed in the core (use the frontend's loader, as it already does
for its `__SWITCH2__` build). Not tested yet; not needed for speed (below).

### 3. Speed on the Duo

The Duo: Moorechip CQ7790S, Adreno 722 (GLES 3.2, Vulkan 1.3), 3 x
Cortex-A520 at 1.84 GHz, 4 x A720 at 2.4 GHz, 1 x A720 at 2.8 GHz, 7.2 GB.
**All device numbers below were taken in low performance mode** (CPU
caps 1.11 / 1.71 / 1.77 GHz, about two thirds), so they are pessimistic.

- Dolphin app 2609b (Vulkan, Turnip, 2x resolution, hybrid ubershaders),
  the owner playing the opening: **29.8 to 30.0 fps** throughout (the
  game runs at 30), a 50 ms frame now and then (device, read from
  SurfaceFlinger).
- libretro core, Vulkan, option 1's bridge, paced at 59.94 Hz as RetroDS
  would: **9.2 to 9.5 ms per frame** walking and swimming on Outset,
  of the 33 ms a game frame has; 900 frames with none over 50 ms, the
  worst 25 ms, 59.9 fps (device).
- Unpaced (as fast as possible) the test showed ~850 ms stalls: Dolphin's
  speed limiter sleeping after the game ran ahead. An artefact of the
  test: paced, there were none.
- Left for later (the owner agreed, Nov 26): heavier places (Dragon
  Roost, the Forsaken Fortress, storms), first-time shader builds on
  Vulkan, battery and heat over a long session, Turnip.

### 4. Reading the game

The core exposes MEM1 (24 MB at `0x80000000`) through
`RETRO_MEMORY_SYSTEM_RAM` and a memory map, **no core patch needed**
(SwanStation needed one for video RAM; nothing here reads video RAM).
Reading between `retro_run` calls worked on the device and the host.
Dolphin emulates the GameCube CPU on its own thread (dual core); between
frames it is stopped at the frame's end.

From the decomp (`g_dComIfG_gameInfo` at `0x803C4C08`), checked live on
the owner's Outset state (host and device) unless marked:

| What | Where | Checked |
|---|---|---|
| Health, max and now (quarters) | `0x803C4C08`, `0x803C4C0A` (16-bit) | 12 / 12: three hearts, as shown (also in Dolphin's own cheat file) |
| X, Y, Z: inventory slot on each | `0x803C4C11`, 3 bytes | FF (empty), then set by the game's menu as tested (question 6) |
| The HUD's copy: item on X, Y, Z | `0x803CA7DB`, 3 bytes | follows the menu, as above |
| Inventory, 21 slots (item numbers) | `0x803C4C44` | empty on this save, as the items screen showed |
| Sea chart: charted squares | `0x803C4D0C`, 49 bytes, bit 0 | set for squares 1, 11, 44 (Outset is 44) |
| Charts owned, opened, completed, Triforce | `0x803C4CCC`, four 128-bit sets | not checked (none owned yet) |
| Current stage name | `0x803C9D3C`, 8 chars | `sea_T` at the title, `sea` on Outset |
| Room (on the sea: the square) | `0x803C9D46`, and `0x803F6A78` | 0x2C = 44, Outset |
| Link's position | `*(0x803CA754)` + `0x1F8`, 3 floats | sea square from it: 43 (0-based), Outset; moves as he walks |
| The ship's position | `*(0x803CA75C)` + `0x1F8` | null on this save (no boat yet): not checked |
| Sea square from a position | (x + 350000) / 100000, (z + 350000) / 100000; 7 x 7 | from the decomp; matches Link |
| Current dungeon: map, compass, boss key | `0x803C53A1` bits 0, 1, 2 | not checked (no dungeon reached) |
| Current dungeon: rooms visited | `0x803C5398`, 64 bits | not checked |
| Menu open (paused) | `0x803F7097` | 1 exactly while Start's menu is up |
| Menu page | `0x803F7095` | 1 items, 2 quest status, 3 map; keeps the last page after closing |
| Game frame counter | `0x803E8140` | advances once per game frame |

### 5. The game's art

All of it is 2D on the disc, readable from the player's ISO at run time
(`tools/gamecube/gcdisc.py` reads a GameCube disc's file table,
`tools/gamecube/gcformats.py` Yaz0, RARC archives and BTI textures, `gcdump.py`
writes an archive's textures as PNGs; RetroDS would need the same in C
beside `src/disc.c`):

- **Item icons**: `res/Msg/itemicon.arc`, 113 RGBA textures, all decoded.
- **The sea chart**: `res/Fmap/Fmap.arc` (49 squares: an island picture
  each, `fm/rNN`, plus four detail pieces) and `res/Msg/fmapres.arc` (the
  chart's parchment, frame, compass, ships, cursor and its layout
  `f_map.blo`). A first renderer of the layout format (`tools/blo.py`,
  pictures only) already draws the chart's frame from the disc alone. The layout has 423
  pictures and 9 text boxes; which of them show, where the islands and
  fog go and the colours come from the game's code (`d_menu_fmap.cpp`
  and `d_menu_fmap2.cpp`, about 6,800 lines), to be rewritten from the
  decomp as rules, as Super Metroid's map was.
- **Dungeon maps**: in each dungeon's `Stage.arc` a floor plan per floor
  (`dmapNNN.bti`, rooms as palette colours) and in each `RoomN.arc` the
  room's map (`sNNN.bti`, `mNNN.bti` with `mNNN.amp`).
  The full map's frame is `res/Msg/dmapres.arc`.
- **The items and quest status screens**: `itemres.arc`, `clctres.arc`,
  `menures.arc`, made mostly of window panes (the renderer doesn't draw
  those yet). **The quest status screen's waving cloth is a 3D cloth
  simulation** (`d_menu_cloth.cpp`): the only piece that can't be a flat
  copy; drawn still, or left out.
- Text (names on the chart, item names) needs the game's font
  (`fontres.arc`, `res/Menu/*.bfn`) and message files (`bmgres.arc`).

### 6. Touch to equip

The game's own way (`dMenu_Item_c::itemDecide`): with Start's items
screen open and the cursor on an item, X, Y or Z puts it on that button;
if it was already on another button, the two swap. It writes the save's
slot byte for the button and the HUD's item byte.

**Pressing the game's buttons works** (host, the owner's state with three
items given for the test): Start, the control stick to the item (the
D-pad doesn't move this cursor; it starts on SAVE), then X / Y; the RAM
and the HUD followed exactly, including the swap. The cost: the menu opens
and closes on the game screen, about two seconds a tap, with its sound.
The alternative is the write exception, as in Super Metroid and A Link to
the Past: two bytes per button (`0x803C4C11 + n`, `0x803CA7DB + n`),
swapping as the game does, only while the menu flag is clear and Link
isn't using an item. **Owner's choice** (below).

### 7. The screens

The game is 4:3 (640 x 528, shown 4:3) and the GameCube game has **no
widescreen of its own** (its camera is 4:3; HD added 16:9). So, as
RetroDS already decides by shape: **the game on the Duo's bottom 4:3
screen, the GamePad screen on the top 16:9 one**, the reverse of HD's
TV and GamePad but each screen getting the shape that fits it. Dolphin's
widescreen hack would stretch the 3D view and needs care with the HUD;
it changes the game's picture, which Philosophy 1 keeps as the console
drew it: **for the owner to decide** (below).

### 8. Cost

In steps, each built and tried on the Duo (rough, from reading the code;
not measured):

1. **The core**: libretro Dolphin in `build-cores.sh` and CI (pinned
   commit, NDK r29, `Sys` in the APK), 1 copy. 2 to 3 days.
2. **The frontend's Vulkan path** (option 1): device negotiation, the
   bridge, pacing, Dolphin's options (hybrid ubershaders, resolution),
   the pad's port. 1 to 2 weeks.
3. **A GameCube system layer**: `.iso` / `.rvz` (Dolphin reads RVZ), the
   game code from the disc header (GZLE01), memory cards as GCI folders
   in RetroDS's folder, saves and states (states are large: 90 MB raw,
   so no rewind), the analog stick and triggers (RetroDS is digital
   today), L/R/Z mapping. 1 to 2 weeks.
4. **Disc reading**: a GameCube disc reader (header, file table, Yaz0,
   RARC, BTI) in C beside `src/disc.c`; the logo from `opening.bnr`.
   3 to 5 days.
5. **Device test**: The Wind Waker playable on the Duo with sound,
   speed, saves; heavier places; battery.
6. **The second screen, in steps**: the rules first
   (`docs/games/the-wind-waker.md`); then a BLO renderer for the game's
   2D screens (pictures, windows, text, the game's font) about 1 to 2
   weeks; then **the items screen** with touch to equip (1 week); **the
   sea chart** with Link and the ship, charted squares, zoom (2 to 3
   weeks: the biggest piece, the chart's logic rewritten from the
   decomp); **island and dungeon maps** (1 to 2 weeks); quest status
   without the cloth (1 week).

In all roughly **2 to 3 months** of sessions to the full GamePad screen,
with a playable game and its logo after steps 1 to 5 (about a month).

**Biggest risks**: the sea chart's logic (much of it is code, not data);
first-time shader hitches in places not yet measured; Vulkan on other
handhelds' drivers; the core's libretro layer has rough edges (the
Software renderer, the controller port) and may have more; APK size
(+21 MB).

### Questions that were for the owner (before the recomp was found)

1. **Option 1** (Vulkan for Dolphin only, SDL2/GLES kept for RetroDS):
   agreed? Options 2 and 3 (SDL3 + Vulkan, slang shaders) as a later
   project of their own?
2. **Touch to equip**: press the game's buttons (the menu flashes on the
   game screen for about two seconds, no writes) or the write exception
   (instant, two bytes per button, as SM and ALttP)?
3. **Widescreen**: the game as the GameCube drew it (4:3, the default),
   or Dolphin's widescreen hack as an option?
4. **Turnip**: worth supporting a player-supplied driver (a small core
   change), or Qualcomm's driver only, given the speed measured?
5. **Which second screen first**: the items screen (smallest), or the
   sea chart (the one HD is remembered for)?
6. **Internal resolution**: native (640 x 528 on a 1280 x 960 screen,
   scaled 2x by whole pixels) or Dolphin's 2x (sharper 3D, Philosophy's
   "pixels at the game's own size" doesn't quite apply to 3D)?
