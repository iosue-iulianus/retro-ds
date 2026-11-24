# SNES DS: notes for Claude sessions

SNES DS is a fork of kyannon/gba-ds (GBA DS) into a Super Nintendo emulator
for dual-screen handhelds, built for the **Retroid Pocket Duo** on Android
16: bottom screen 4.5" 1280x960 (4:3), top screen 5.5" 1920x1080 (16:9),
both 120 Hz OLED, Qualcomm Q-7790, 8 GB RAM. The game runs on the bottom
screen; the top screen shows the game's own map or status screen (captured
from a hidden second copy of the game), a user-supplied map with a live
marker (custom asset mode), or the game's logo.

The owner (Joshua) decides what gets built and tests on the device.
Nothing counts as working until it has run on the Duo.

**Status:** M1 (SNES games on one screen) is merged: the emulator is the
libretro bsnes core and GBA DS's GBA-only features are gone. M2 and M3
are merged too (PRs #3 and #4, Nov 24, 2026, after the owner tried them
on the Duo). M2: Super Metroid's second screen,
the game on the screen closer to 4:3 (the Duo's bottom one), overlays
(the bezel) off by default. Its map and SAMUS page are drawn by SNES DS
itself from the ROM and RAM (`src/smmap.c`, the profile's `native`; no
hidden copy runs for Super Metroid): pixel-exact against the game's
pause screen (`tools/sm_map.py`, the harness's `smmap` and `smtoggle`),
scrolled smoothly on the GPU (drag, fling, glide), and tapping an item
on the SAMUS page switches it (the write exception, SM-10); the reserve
mode switch isn't done. M3: A Link to the Past's second screen (`docs/games/a-link-to-the-past.md`,
ALTTP-1 to 14), all drawn by SNES DS: the items screen (tap to select, a
write exception) with the zoom pane; X for the map: outdoors the world
assembled from the game's own ground layer as Link sees it (video RAM, a
core patch; the strips by area boundaries taken during the scroll),
the game's own clouds over the rest with a puffy edge, Link's sprite
walking, the world map's goal markers, pinch and tap zoom over the
whole top screen; in dungeons the dungeon map as the game draws it
(tiles decompressed from the ROM, its background black); the logo in
special areas, at game over and the menus; at the title the sword falls
from the top screen into the game's across the gap between the screens
(`screen_gap_mm`). `game.c`'s two drawn second screens are in
`game_sm.c` and `game_alttp.c` (PR #5). Next: M4 (Chrono Trigger).
Parked: the 120 Hz map judder (the bottom screen's present
blocks 6 to 8 ms at 120 Hz). Both games' checks run with
`scripts/check-games.sh`. Much of `src/` is
GBA DS's (the game list, menus, settings, Android glue); upstream's session
notes, with the detail on every inherited subsystem, are
`git show 3b452dc:CLAUDE.md` (GBA DS v1.5.6, the commit this fork starts
from).

## Philosophy

`wiki/Philosophy.md` is the player-facing statement (rewritten with the
owner, Nov 22, 2026; GBA DS's version is in git history): a portable
Super Nintendo with a touch second screen. (1) The game screen is the
game; small changes to it only where the second screen takes something
over, listed and switchable. (2) The second screen is in the game's own
style: the game's art (screens, cartridge graphics, what it draws), new
layouts allowed, no SNES DS font/icons/panels; small touch hints (an
arrow on a plate) listed. (3) Useful at a glance and smooth: live,
display-rate scrolling, no capture delays or jumps; the logo or the last
good picture when unsure. (4) Touch the second screen (look, choose,
switch items); play with the buttons; touch on the game screen opens
the pause menu. (5) Read the game freely, write rarely: writes only for
a deliberate player action, done as the game's own menu would, listed;
avoid code patches; off in RetroAchievements hardcore. (6) The best tool
per game: drawn from the game's data (Super Metroid), assembled from
what the player has seen (A Link to the Past's overworld), opened on a
hidden copy (status screens), the logo; each checked against the game
itself. (7) Keep it simple and testable. (8) Exceptions deliberate,
written down, switchable. Custom asset mode (the player's own map
pictures) remains an option. How sessions apply it:

- **Check requests against it, and say so.** If a request conflicts with
  a principle, say which and suggest something that fits; if the owner
  still wants it, write it as an exception in `docs/games/<game>.md`.
- **Rule first, then code.** A change to what a game shows starts with
  its rules in `docs/games/<game>.md`, in the player's terms; then the
  code, then the tests.
- **Writes to the live game** are listed per game, made between frames,
  and disabled in RetroAchievements hardcore.
- **When unsure, show less,** and say what you couldn't do (changelog,
  release notes).
- **Keep changes small enough to test.** Every change ends with a test
  build and a short note of what to check on the device.

### Custom asset mode

Opt-in per game: only a profile with an `.asset` section uses it, and only
when the player has put the files in place. SNES DS ships no asset. They
live in `assets/user/<game>/` (gitignored): never commit, generate or
download one. Lossless only (PNG or lossless WebP, 1 image pixel = 1 game
pixel); anything missing or invalid is logged and falls back to the
game's usual second screen. Position from the live game's RAM. The full
design is in `docs/custom-assets.md`.

## Decisions already made (don't re-open without the owner)

1. **Core:** `emu.c` is written against the libretro API, not against a
   specific core; the core is a swappable `.so`. Releases ship **bsnes**
   (GPLv3, compatible with this project's GPLv3-only licence). **Snes9x is
   for local prototyping only**: its licence is non-commercial and not
   GPL-compatible, so it is never distributed, committed or bundled.
2. **Two instances:** bsnes and snes9x keep their state in globals, so the
   live game and the hidden copy are two loaded copies of the core. Android:
   the core built twice under two sonames (`libsnes_a.so`, `libsnes_b.so`),
   both in the APK, each `dlopen`ed. Linux host: the same `.so` copied to
   two paths. **Never `fork()` per capture on Android** (forking a
   multithreaded ART/SDL process keeps only the calling thread and can
   deadlock); fine for host experiments only.
3. **Game identity:** CRC32 of the ROM with any 512-byte copier header
   stripped (file size % 1024 == 512). Not the internal header checksum.
4. **Second screens (updated Nov 22, 2026):** drawn natively from the
   game's ROM and RAM where that's best (Super Metroid's map and SAMUS
   page), assembled from the live game's own pictures (A Link to the
   Past's overworld, planned), captured on the hidden copy (status
   screens), custom asset mode, the logo. No renderer patches (GBA DS's
   `emu_hud_*`, `emu_hide_*`, `SpriteRule`); covering a piece of the game
   screen (Super Metroid's mini-map) is a frontend draw, listed per game.
5. **Framebuffer size is a runtime value:** 256x224 normally, 512 wide in
   hi-res, 239 lines with overscan. Nothing new may assume a fixed size
   (`GBA_W` / `GBA_H` are being retired).

## Games

Priority (owner, Nov 22, 2026): **1. Super Metroid** (its map and SAMUS
page drawn natively from ROM and RAM, touch: drag the map, tap items),
**2. A Link to the Past** (like Super Metroid where its data allows; the
overworld map assembled from the screens the player has seen, unexplored
parts dark; dungeon maps drawn or captured), **3. Chrono Trigger**
(status screen), then **Final Fantasy IV, V, VI** (party / status
screens, as the FF Advance profiles do). GBA DS's pages for the GBA versions of ALttP and FF
IV/V/VI, and its general rules (GEN-1 and on, the rule format), are kept in
`docs/reference/gba/` as references; other GBA games' pages are in git
history (`git show 3b452dc:docs/games/<game>.md`). SNES games get their own
`docs/games/<game>.md`.

None of the priority games use SA-1, SuperFX or DSP chips. Super Mario Kart
(DSP-1: bsnes needs the firmware or HLE) is not a first game.

## Conventions

- **Branches:** `snes` is the fork's main line. Work happens on topic
  branches cut from it (`m0-libretro`, `game/super-metroid`), merged back
  when built and tried. `main` and `dev` mirror upstream and are never
  committed to.
- **Remotes:** `origin` is SNES DS's own repo (iosue-iulianus/retro-ds,
  private, a standalone copy with GBA DS's history, not a GitHub fork);
  `snes` is its default branch. `upstream` is GBA DS (kyannon/gba-ds):
  fetch from it to pick up fixes to shared code, never push there.
- **Names:** the app is SNES DS everywhere: binary `retrods`, `retrods.cfg`,
  `RETRODS_*` variables and macros, Android package and application ID
  `io.github.iosue_iulianus.retrods` (JNI names write its `_` as `_1`:
  `Java_io_github_iosue_1iulianus_retrods_...`), classes `RetroDsActivity` and
  `RetroDsHereActivity`, the "SNES DS" folder on Android. Still to do: the
  logo artwork (`src/brand_data.c`, from `tools/make_brand.py`), whose font
  isn't settled. GBA DS is named on purpose in the README's credits, this
  file, Philosophy, the changelog's opening, `docs/reference/gba/` and the
  APK's licence notice; `src/profiles.c`, the dump and mask tables and the
  GBA tooling docs (`docs/TESTING.md`, `docs/patches.md`) keep "GBA DS" in
  their prose because they describe GBA DS's games, until the port
  replaces them. Some development notes inherited from GBA DS (dated ones
  in `docs/ANDROID.md`, version mentions in comments) now say SNES DS where
  they meant GBA DS.
- **Commits:** small, one idea each, with the session's attribution
  trailers. Write long messages to a file and `git commit -F`. Never
  force-push, never rewrite pushed history.
- **Style:** readable over clever. **No em dashes** in docs, comments or
  commit messages (use a colon, a comma or parentheses). Player-facing text
  in plain sentence case.
- **ROMs, save states and cores:** never committed. The owner puts ROMs and
  states in their home directory (`/home/joshua`) and says when they're
  there; don't search other drives for them. Cores are built in
  `../cores/` (beside the repo).
- **No new dependencies.** Images load with stb_image and libwebp, already
  in the tree.

## Building

- Cores: `scripts/build-cores.sh` (bsnes at a pinned commit with
  `scripts/cores/bsnes-memory.patch`, into `../cores`; `snes9x` also builds
  snes9x for local experiments; `android` builds bsnes with the NDK).
- Desktop: `scripts/setup-host.sh` once (SDL2 into `deps/`, the cores, the
  app, the harness, and `build/host/bsnes_libretro.so` linked to the core),
  then `make` (`build/host/retrods`) and `make tools`
  (`build/host/retrods-harness`). Host builds run with the SDL in `deps/`
  (rpath). The app finds its core in `RETRODS_CORE`, else
  `bsnes_libretro.so` beside the program, and copies it into
  `<state_dir>/cache/core-{a,b,c}.so`.
- Android: `scripts/build-android.sh` (SDL2, the core with the NDK, packed
  three times as `libsnes_{a,b,c}.so` with their own sonames by patchelf,
  extracted to `nativeLibraryDir`, passed as `RETRODS_CORE_DIR`). It needs
  the SDK and NDK, so it runs on GitHub Actions
  (`.github/workflows/android.yml`: pushes to `snes`, pull requests into
  it, "Run workflow"); release-signed with the repo's secrets (the key is
  in the owner's `~/.config/snes-ds-signing/`). Getting it onto the Duo:
  "The device loop" below.
- Platforms: Android and the Linux desktop only.

## The device loop (build on CI, install over wireless adb)

How a change gets onto the owner's Retroid Pocket Duo. The workspace
can't build Android itself (no SDK), so:

1. **Push the branch.** An open pull request into `snes` (or a push to
   `snes`) runs `.github/workflows/android.yml` (the release-signed APK)
   and the i18n check. If i18n fails, it's almost always shifted line
   numbers: run `python3 tools/i18n.py all`, commit the `.pot`, push.
2. **Wait for the Android run of that exact commit.** Each push starts
   two runs (push and pull_request events), so pick the run by commit
   and name, then wait on its id in the background:
   `gh run list -R iosue-iulianus/retro-ds --branch BRANCH --json databaseId,name,headSha`
   (`name == "Android"`, `headSha` the commit), then loop on
   `gh run view ID --json status` until `completed`.
3. **Download it** into the scratchpad: `gh run download ID -R
   iosue-iulianus/retro-ds` gives `retrods-vX-android/*.apk`.
4. **Check SNES DS isn't in use.** `adb.exe -s 192.168.0.217:PORT shell
   'dumpsys activity activities | grep -E "ResumedActivity:"'`: if
   `RetroDsActivity` is resumed the owner is playing; wait (or ask). A
   cached process in the background is fine (it auto-saves on leaving).
5. **Back up the owner's states first** (never lose them): pull the
   newest `SNES DS/states/*.state` (and the `.srm` in `ROMs/snes`) into
   `/home/joshua/projects/snes-ds/test-data/<game>/`, read-only.
6. **Install over the old one:** `adb.exe -s IP:PORT install -r
   "$(wslpath -w APK)"`. Same signing key, so data and settings stay.
   Never suggest uninstalling: without All files access the app's own
   folder (and its states) goes with it.

Device details:

- adb is Windows' (`/mnt/c/ProgramData/chocolatey/bin/adb.exe`, WSL
  with mirrored networking). The Duo is at 192.168.0.217; the port
  changes whenever wireless debugging restarts (`adb.exe mdns services`
  or `adb.exe devices`).
- Paths on the device: `/storage/emulated/0/SNES DS/` (log
  `retrods.log`, `states/`, `launch.log`), ROMs in
  `/storage/emulated/0/ROMs/snes/`.
- Screenshots of each display: `adb.exe exec-out screencap -d DISPLAY_ID
  -p > file.png` with the ids from `dumpsys SurfaceFlinger --display-id`
  (plain `screencap` came back black). The top display (game activity) is
  logical display 0, the bottom (second screen) display 2.
- Test taps without the owner: `adb.exe shell input -d 0 tap X Y` (top
  display coordinates); the log's `touch down on screen ...` lines show
  where SNES DS put it. Only tap where it can't upset the game.
- Debug logging (`RETRODS_DEBUG`) can't be switched on at runtime on
  Android; the log has the 5-second frame lines and touch lines.

## Testing without the device

- The harness (`build/host/retrods-harness CORE ROM OUTDIR SCRIPT`, commands
  at the top of `tools/retrods-harness.c`) runs two copies of the core frame
  by frame: `load/save`, `wait`, `hold`, `tap`, `shot`, `peek`, `poke`,
  `dumpram`, `copy`, `mapcap` (a capture on copy B, timed), `bench`. SNES
  addresses are written as the CPU sees them (`7E0998`). `RETRODS_SAVE`
  gives copy A a battery save (copied into OUTDIR first).
- Test states and saves live outside the repo, in
  `/home/joshua/projects/snes-ds/test-data/<game>/` (Super Metroid:
  `crateria-ship.state`, playing at the ship, and
  `crateria-pause-map.state`; A Link to the Past: `harness-*`, made in the
  harness). Never delete them; copy new ones from the
  Duo there. The app loads one from a script line `TICK LOAD:path`, and
  `RETRODS_SCREENS=1920x1080,1280x960` (with `RETRODS_WINDOW=1920x2040`)
  stands in for the Duo's two screens.
- **The second screens' regression check:** `scripts/check-games.sh
  --alttp ROM --sm ROM` (`tools/check_games.py`, about 15 s on the host)
  builds the harness and compares both games' second screens with the
  games' own screens and the Python references: ALttP's items screen,
  dungeon map, Link's marker, the GPU map pieces (with the owner's seen
  record, `test-data/a-link-to-the-past/duo-seen-2026-11-23.alttp1`),
  the writes; Super Metroid's map, SAMUS page and item switches. Run it
  before merging anything that touches a second screen; `--only NAME`
  for one check, `--out DIR` to keep the pictures.
- `tools/ramdiff.py` (RAM dumps with a screen open vs closed) carries over.
- The app runs headless: `SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy
  SDL_RENDER_DRIVER=software RETRODS_WINDOW=1024x1536 build/host/retrods
  --rom-dir=DIR --state-dir=DIR --save_dir=DIR --quick_bar_hint=0
  --input-script=SCRIPT [ROM]` (script lines `TICK BUTTONS`, buttons `A B X
  Y START SELECT HOME L R L2 R2 L3`, `HOME+R` save state, `HOME+L` load,
  `L2` held rewinds; `TICK SHOT:file.bmp`, `TICK TAP:x,y` as fractions of
  the window, `TICK QUIT`). **Always pass `--save_dir` in a scratch folder:**
  by default battery saves go beside the ROM, and a test once created a
  save file in the owner's ROM folder.
- Verify visual changes by rendering screenshots and looking at them before
  sending a build.
- Results that depend on where they were measured (fps, timings) are
  always tagged **host** or **device**.

## Architecture (src/)

- `emu.c` / `emu.h`: the libretro loader. A pool of core copies
  (`EMU_CORES` = 3: the game, the map's hidden copy, the logo's); each
  `emu_open` takes a free one. The ROM as played (copier header stripped,
  patches applied, from a `.zip` too) is cached by CRC32 and loaded from
  there. Game codes are that ROM file's CRC32 as 8 hex digits. Battery saves
  through `RETRO_MEMORY_SAVE_RAM`, written in the background as they
  change. States in RetroArch's RASTATE format with SNES DS's blocks (SDSC:
  ROM CRC, SDSX: the second screen). Rewind keeps XOR deltas of
  same-instance states (taken without disturbing the game). Snapshots of
  the live game for other copies must use `emu_state_save_live` with the
  next frame's keys (the loop takes map and bug-report snapshots just
  before a frame for that).
- `smmap.c`: Super Metroid's second screen drawn from the cartridge and
  the game's RAM (map, equipment page, item switching); `game_sm.c`'s
  `native_*` show it in a play session (chrome as a picture, the map's layer and marker as
  textures at screen-pixel offsets).
- `alttp.c`, `alttp_dungeon.c`: A Link to the Past's second screen (the
  items screen, the world map from the game's screens and its seen record,
  the dungeon map, the title sword's place); `game_alttp.c`'s `alt_*`
  show it in a play session:
  the map as 512 x 512 pieces on the GPU (two textures each, written by
  turns: a texture the GPU is drawing from is never written, which made
  drivers wait), made on a thread of their own, the seen record's pieces
  compressed on another ahead of its saves, the minute's
  save copied on the game's thread and written on another.
- `writes.h`, `emu_apply` (`emu.c`): the only way the second screen writes
  to a game (SM-10, ALTTP-10): full SNES addresses, each checked against
  the game's list (`smmap_write_ranges`, `alttp_write_ranges`), all of a
  batch or none, each byte logged old>new. The Python references are
  `tools/alttp_items.py`, `alttp_world.py`, `alttp_gfx.py`,
  `alttp_dungeon.py`; `tools/alttp_save.py` makes test saves.
- `mapcap.c`: map capture on the hidden copy in a background thread
  (no game uses it now: both draw their own; kept for games that won't,
  the owner's choice, Nov 23, 2026), on a
  normalized 256x224 picture (hi-res and interlaced ones halved): profiles
  give coordinates on the SNES screen.
- `profiles.c/.h`: everything game-specific (`MapProfile`), keyed by game
  code (CRC32). Super Metroid so far (`docs/games/super-metroid.md`).
- `game.c`: a play session (input, the second screen, pause menu; the
  two drawn second screens in `game_sm.c` and `game_alttp.c`, what they
  share with it in `game_screens.h`). The
  game's picture is `texW` x `texH` as the core draws it, shown as `picW` x
  `picH` (256 x 224 or 239). The second screen is an SNES-sized picture
  (`SNES_W` x `SNES_H`).
- `logo.c`: the logo as a title-screen snapshot (the profile's `logo`
  steps and box), made on the third core copy.
- `android.c`: Android glue; already knows the Duo (`top_screen` picks the
  larger display; `screen_ppi` knows both panels).
- `library.c`, `ui.c`, `config.c`, `paths.c`, `ra.c`, `patch.c`: the game
  list, drawing, settings, folders, RetroAchievements and patches, inherited
  from GBA DS and mostly system-agnostic.

## Milestones

- **M0 (host only, no Android, no UI):** this file and Philosophy.md
  rewritten; the libretro `emu.c` with two instances; a harness command that
  copies Super Metroid's state from instance A to B, opens the map on B and
  writes both framebuffers to PNG; bench numbers (fps, serialize cost and
  size, settle time, whether blind frames skip rendering) for bsnes and
  snes9x in `docs/M0-results.md`. Stop after M0 for review.
- **M1 (agreed Nov 21):** SNES games on one screen, desktop and Android:
  GBA-only features removed first, then the core swap, then Android (the
  core three times), with logos from a third core copy. Stop for review.
- **M2:** Super Metroid's second screen (native map, SAMUS page, touch);
  the game on the Duo's bottom screen by default.
- **M3:** A Link to the Past (overworld map assembled from what the
  player has seen, dungeon maps).
- **M4:** Chrono Trigger, then the FF status screens.
- Which screen has the game: `swap`, until the player sets it, follows
  the screens' shapes (the one closer to 4:3 gets the game: the Duo's
  bottom screen). `top_screen` only says which display is physically on
  top.
- Overlays default to Off: the owner doesn't want the bezel (on OLED
  screens the black round the picture is the frame).
- For the port, left by the rename: ES-DE's integration (`esde.c`) still
  adds SNES DS to ES-DE's `gba` system; links in `android.c` point at
  wiki pages SNES DS doesn't have yet (Playing, Overlays); the Display
  theme's hint mentions the web player (a translated string: change it with
  its translations); the .po files keep translations of removed strings
  (obsolete, warnings only); three display hints are fuzzy (shown in
  English) until retranslated; Linux features with ROCKNIX roots (CPU
  clocks, the volume file); region flags in the game list (SNES game codes
  carry no region: perhaps from the ROM header's destination code).
