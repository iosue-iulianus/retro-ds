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
libretro bsnes core and GBA DS's GBA-only features are gone. M2, on
branch `m2-super-metroid`: Super Metroid's map on the second screen (the
first profile), the game on the screen closer to 4:3 (the Duo's bottom
one), overlays (the bezel) off by default. Much of `src/` is
GBA DS's (the game list, menus, settings, Android glue); upstream's session
notes, with the detail on every inherited subsystem, are
`git show 3b452dc:CLAUDE.md` (GBA DS v1.5.6, the commit this fork starts
from).

## Philosophy

`wiki/Philosophy.md` is the player-facing statement of what SNES DS is for
and what goes on either screen: (1) an SNES with a second screen, neither a
touchscreen, (2) function over form (nothing on the second screen that makes
the player look twice: no prompts, cursor or controlled character, nothing
split across the screens or on both), (3) keep it simple, (4) only the
game's own graphics, (5) no in-game touch, (6) exceptions allowed when they
earn their place, custom asset mode being the standing one. Read it before
changing what a game shows. Keep it written for players (no rule IDs, file
paths or session notes); how sessions apply it lives here:

- **Check requests against it, and say so.** If a request, or the obvious
  way to do it, conflicts with a principle, say which one and why, and
  suggest something that fits. If the owner still wants it, it's an
  exception: do it, write it as a rule in `docs/games/<game>.md`, and add it
  to the page's list of exceptions. Don't quietly bend a principle, and
  don't quietly refuse because of one.
- **Rule first, then code.** A change to what a game shows starts with its
  rules in `docs/games/<game>.md`, in the player's terms; then the profile,
  then the tests.
- **Try simple approaches first.** For a game's second screen, in order: a
  screen the game already draws, opened on the hidden copy and captured; a
  snapshot of a screen the live game shows; custom asset mode, where the
  profile allows it and a capture can't work; the logo. Rebuilding a screen
  from the game's memory, or drawing anything new (SNES DS's font, panels,
  icons), is out.
- **The live game is read-only.** Reading its RAM (gameplay checks, room
  numbers, custom asset mode's position) is fine; writing to it, sending it
  input it didn't get from the player, or changing how its picture is
  drawn is not.
- **When unsure, show less.** If a situation can't be detected reliably,
  show the logo or keep the last good picture and list it as a known issue.
  Don't guess.
- **Say what you couldn't do.** Known limitations go in the changelog and
  release notes in plain words.
- **Keep changes small enough to test.** Every change ends with a test build
  and a short note of what to check on the device; if that note would be
  long, the change is probably too big for one step.

### Custom asset mode (the standing exception)

Opt-in per game: only a profile with an `.asset` section uses it, and only
when the player has put the files in place. Rules for sessions:

- SNES DS ships no asset. They live in `assets/user/<game>/` (gitignored):
  never commit, generate or download one.
- Lossless only: PNG (indexed allowed) or lossless WebP, 1 image pixel = 1
  game pixel. Reject JPEG and lossy WebP with a log line.
- Missing, unreadable or invalid (a degenerate transform, an image that
  doesn't load): log it and fall back to capture, then the logo. Never an
  error on screen.
- Position comes from the live game's RAM every frame; no hidden-copy
  captures run while the asset is up. The marker hides whenever the
  profile's "in normal gameplay" check fails.
- Drawn at the top screen's native resolution, nearest neighbour only.
  Images bigger than the renderer's max texture size are split into 512x512
  tiles at load.

The full design (manifest format, transforms, overlays, the ALttP
addresses still to verify) is in `docs/custom-assets.md`. It's built after
M0.

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
4. **v1 scope:** map capture, screen snapshot and custom asset mode. No HUD
   split, no sprite hiding, no renderer patches (GBA DS's `emu_hud_*`,
   `emu_hide_*` and `SpriteRule` have no SNES equivalent yet).
5. **Framebuffer size is a runtime value:** 256x224 normally, 512 wide in
   hi-res, 239 lines with overscan. Nothing new may assume a fixed size
   (`GBA_W` / `GBA_H` are being retired).

## Games

Priority: **Super Metroid** (map capture: Start opens the pause map after a
fade, the GBA Metroid profiles' pattern), **Chrono Trigger** (status
screen; later a custom asset candidate), then **A Link to the Past**
(dungeon maps by capture; the overworld map is a Mode 7 zoom, so custom
asset mode) and **Final Fantasy IV, V, VI** (party / status screens, as the
FF Advance profiles do). GBA DS's pages for the GBA versions of ALttP and FF
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
- **Remotes:** `origin` is SNES DS's own repo (iosue-iulianus/snes-ds,
  private, a standalone copy with GBA DS's history, not a GitHub fork);
  `snes` is its default branch. `upstream` is GBA DS (kyannon/gba-ds):
  fetch from it to pick up fixes to shared code, never push there.
- **Names:** the app is SNES DS everywhere: binary `snes-ds`, `snes-ds.cfg`,
  `SNESDS_*` variables and macros, Android package and application ID
  `io.github.iosue_iulianus.snesds` (JNI names write its `_` as `_1`:
  `Java_io_github_iosue_1iulianus_snesds_...`), classes `SnesDsActivity` and
  `SnesDsHereActivity`, the "SNES DS" folder on Android. Still to do: the
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
  then `make` (`build/host/snes-ds`) and `make tools`
  (`build/host/snes-harness`). Host builds run with the SDL in `deps/`
  (rpath). The app finds its core in `SNESDS_CORE`, else
  `bsnes_libretro.so` beside the program, and copies it into
  `<state_dir>/cache/core-{a,b,c}.so`.
- Android: `scripts/build-android.sh` (SDL2, the core with the NDK, packed
  three times as `libsnes_{a,b,c}.so` with their own sonames by patchelf,
  extracted to `nativeLibraryDir`, passed as `SNESDS_CORE_DIR`). It needs
  the SDK and NDK, so it runs on GitHub Actions
  (`.github/workflows/android.yml`: pushes to `snes`, pull requests into
  it, "Run workflow"); release-signed with the repo's secrets (the key is
  in the owner's `~/.config/snes-ds-signing/`). Install on the Duo over
  wireless adb: `adb.exe -s IP:PORT install "$(wslpath -w APK)"` (Windows'
  adb; the port from `adb.exe mdns services`).
- Platforms: Android and the Linux desktop only.

## Testing without the device

- The harness (`build/host/snes-harness CORE ROM OUTDIR SCRIPT`, commands
  at the top of `tools/snes-harness.c`) runs two copies of the core frame
  by frame: `load/save`, `wait`, `hold`, `tap`, `shot`, `peek`, `poke`,
  `dumpram`, `copy`, `mapcap` (a capture on copy B, timed), `bench`. SNES
  addresses are written as the CPU sees them (`7E0998`). `SNESDS_SAVE`
  gives copy A a battery save (copied into OUTDIR first).
- Test states and saves live outside the repo, in
  `/home/joshua/projects/snes-ds/test-data/<game>/` (Super Metroid:
  `crateria-ship.state`, playing at the ship, and
  `crateria-pause-map.state`). Never delete them; copy new ones from the
  Duo there. The app loads one from a script line `TICK LOAD:path`, and
  `SNESDS_SCREENS=1920x1080,1280x960` (with `SNESDS_WINDOW=1920x2040`)
  stands in for the Duo's two screens.
- `tools/ramdiff.py` (RAM dumps with a screen open vs closed) carries over.
- The app runs headless: `SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy
  SDL_RENDER_DRIVER=software SNESDS_WINDOW=1024x1536 build/host/snes-ds
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
- `mapcap.c`: map capture on the hidden copy in a background thread, on a
  normalized 256x224 picture (hi-res and interlaced ones halved): profiles
  give coordinates on the SNES screen.
- `profiles.c/.h`: everything game-specific (`MapProfile`), keyed by game
  code (CRC32). Super Metroid so far (`docs/games/super-metroid.md`).
- `game.c`: a play session (input, the second screen, pause menu). The
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
- **M2:** Super Metroid's map on the second screen; the game on the
  Duo's bottom screen by default.
- **M3:** Chrono Trigger and the FF status screens, logos, custom asset
  mode (ALttP's overworld).
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
