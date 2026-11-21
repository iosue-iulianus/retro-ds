# SNES DS

A Super Nintendo emulator for dual-screen handhelds, built for the Retroid Pocket Duo on Android. The game plays on one screen, and the other shows the game's own map or status screen, kept current as you play, the way the DS Castlevanias use their second screen.

SNES DS is a fork of [GBA DS](https://github.com/kyannon/gba-ds) by kyannon, which does the same for the Game Boy Advance. The idea, the design and most of the code around the emulator (the game list, the menus, settings, the Android app) come from GBA DS. SNES DS swaps the GBA emulator for an SNES one and writes its own game profiles. If you play GBA games on a two-screen handheld, use GBA DS.

**Status: early development, not playable yet.** The first milestone (M0, a host-only proof of concept) is done: an SNES core runs twice side by side, and the hidden copy opens Super Metroid's and A Link to the Past's maps from the live game's state without touching it. See [docs/M0-results.md](docs/M0-results.md). The app itself is still GBA DS's until the port is done.

## How it works

SNES DS runs a second, silent copy of the emulator next to the one you play. About every second, and after each room change, it copies the live game's state into the silent copy, presses the map button there, waits for the map screen to settle and checks it (a memory value that holds only while the map is open, and fixed pixels of its frame) before showing it. Title screens, cutscenes and menus are rejected, and the last good map stays up. The game you're playing is never paused, sent input or changed.

The emulator is a [libretro](https://www.libretro.com) core loaded as a library. Releases will ship [bsnes](https://github.com/bsnes-emu/bsnes) (GPLv3), with a small patch that lets SNES DS read the game's work RAM (`scripts/cores/`).

Some games will also get **custom asset mode**: for a screen that can't be captured well (A Link to the Past's whole-world map), you supply your own map picture, and SNES DS shows it with a marker at your position, read from the game's memory. SNES DS ships no game art; see [docs/custom-assets.md](docs/custom-assets.md).

What goes on the second screen, and what doesn't, is set out in [Philosophy](wiki/Philosophy.md).

## Games

First in line: Super Metroid, Chrono Trigger, then A Link to the Past and Final Fantasy IV, V and VI. Any other SNES game will still run, on one screen.

## Building (development)

On a Linux host with git, make, gcc and libpng:

```sh
scripts/build-cores.sh snes9x   # bsnes (patched) and snes9x into ../cores
make m0                         # build/host/snes-harness
build/host/snes-harness ../cores/bsnes/bsnes_libretro.so ROM OUTDIR SCRIPT
```

The harness runs two copies of the core from a script (button presses, save states, memory reads, map captures, benchmarks); its commands are listed at the top of `tools/snes-harness.c`. snes9x is for local experiments only: its licence doesn't allow it to be distributed with GPL software, so SNES DS never ships it.

The rest of the build (the app, the Android build) is still GBA DS's; see `CLAUDE.md`.

## AI disclosure

SNES DS is written almost entirely by AI: the code, documentation and commit messages are written by Claude, Anthropic's AI model, in Claude Code sessions. Its owner decides what gets built and how it should behave, and tests it on the device. GBA DS, which SNES DS is built from, was made the same way (see its README). Emulation itself is bsnes's, a long-established, human-written emulator.

## Not affiliated

SNES DS is an independent project. It is not affiliated with, endorsed by or sponsored by Nintendo, Retroid, or the GBA DS project. Super Nintendo and the names of the games are trademarks of their owners, used here only to say what SNES DS runs. No ROMs or game art are included.

## Licence

GPLv3, as GBA DS (see [LICENSE](LICENSE)). bsnes is GPLv3. Third-party code in `third_party/` and `src/retro/libretro.h` keeps its own licence.
