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

**Status:** milestone M0 (host only). Most of `src/` is still GBA DS's
code, built around libmgba; this file describes where it is going, and says
"(GBA DS)" where it describes code that hasn't been ported yet. Upstream's
own session notes, with the detail on every inherited subsystem, are
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
FF Advance profiles do). Upstream's GBA profiles and `docs/games/` pages for
ALttP and FF IV/V/VI are good references for rules.

None of the priority games use SA-1, SuperFX or DSP chips. Super Mario Kart
(DSP-1: bsnes needs the firmware or HLE) is not a first game.

## Conventions

- **Branches:** `snes` is the fork's main line. Work happens on topic
  branches cut from it (`m0-libretro`, `game/super-metroid`), merged back
  when built and tried. `main` and `dev` mirror upstream and are never
  committed to.
- **Remotes:** `origin` is upstream (kyannon/gba-ds). Never push there.
  Push only to the owner's own fork, and only when asked.
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

- Cores (host): `../cores/bsnes` (libretro/bsnes, `make target=libretro
  binary=library`) and `../cores/snes9x` (`make` in `libretro/`).
- (GBA DS, until the core swap lands) desktop: `scripts/setup-host.sh` once,
  then `make` and `make tools`; device and Android build scripts are
  upstream's and still build the GBA app.

## Testing without the device

- The harness (`build/host/harness ROM OUTDIR SCRIPT`) drives a core frame
  by frame: `load/save NAME`, `wait N`, `hold BUTTONS N`, `shot NAME`,
  `peek ADDR LEN`, `poke ADDR LEN VAL`, `dumpram NAME`, `map NAME`. SNES
  addresses are written as the CPU sees them (`7E0020`); WRAM ($7E0000 to
  $7FFFFF) comes from `RETRO_MEMORY_SYSTEM_RAM`.
- `tools/ramdiff.py` (RAM dumps with a screen open vs closed) and
  `tools/scout/bench.c` carry over from GBA DS.
- The app runs headless (GBA DS): `SDL_VIDEODRIVER=offscreen
  SDL_AUDIODRIVER=dummy build/host/gba-ds --input-script=SCRIPT ROM`.
- Verify visual changes by rendering screenshots and looking at them before
  sending a build.
- Results that depend on where they were measured (fps, timings) are
  always tagged **host** or **device**.

## Architecture (src/)

- `emu.c` / `emu.h`: the core wrapper. Becomes a libretro loader: `dlopen`
  of a core path, the `retro_*` symbols, video / audio / input callbacks,
  states via `retro_serialize`, WRAM via `RETRO_MEMORY_SYSTEM_RAM`. Each
  `Emu` owns one loaded core, so two `Emu`s need two core paths.
- `mapcap.c`: map capture on the hidden copy in a background thread. Uses
  only the `emu_*` API, so it survives the core swap nearly unchanged.
- `profiles.c/.h`: everything game-specific (`MapProfile`). Today GBA DS's
  64 games; SNES profiles replace them.
- `game.c`: a play session (input, the second screen, pause menu). About
  20 GBA-specific references to port.
- `logo.c`: (GBA DS) logo from the title screen's BG layers. For v1, replaced
  by a title-screen snapshot.
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
- After review: Android (two sonames), profiles, custom asset mode.
