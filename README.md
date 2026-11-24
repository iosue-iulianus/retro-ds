# RetroDS

**Your favourite classics, with the second screen they never had. Not a Nintendo DS emulator.**

RetroDS plays games from older consoles on dual-screen Android handhelds, built for the Retroid Pocket Duo. The game plays on one screen; the other shows what the game would have put on a DS's second screen: its map, its items, its status, drawn live in the game's own style and kept current as you play. Touch it to look around and to pick items; play with the buttons.

RetroDS doesn't play Nintendo DS games.

RetroDS was called SNES DS until November 2026, and is a fork of [GBA DS](https://github.com/kyannon/gba-ds) by kyannon, which does the same for the Game Boy Advance. The idea, the design and most of the code around the emulator (the game list, the menus, settings, the Android app) come from GBA DS. If you play GBA games on a two-screen handheld, use GBA DS.

**Status: early development.** Super Nintendo games run, and two have their second screen:

- **Super Metroid**: the area map and the SAMUS equipment page, drawn from the game's own data, scrolled by touch; tap an item to switch it.
- **A Link to the Past**: the items screen (tap to select), the world map built from what you've seen, and the dungeon maps.

Next: the PlayStation, starting with Castlevania: Symphony of the Night. Any other SNES game runs on one screen, with its title logo on the other.

## How it works

The emulator is a [libretro](https://www.libretro.com) core loaded as a library: [bsnes](https://github.com/bsnes-emu/bsnes) (GPLv3) for the Super Nintendo, with a small patch that lets RetroDS read the game's memory (`scripts/cores/`). Each frame, RetroDS reads the live game's memory and draws the second screen from the game's own graphics and data. Touching the second screen only changes the game where its own menu would (switching an item), each such change listed per game in `docs/games/`.

What goes on the second screen, and what doesn't, is set out in [Philosophy](wiki/Philosophy.md).

## Building (development)

On a Linux host with git, make, gcc and libpng:

```sh
scripts/setup-host.sh           # SDL2, the cores, the app and the harness
make && make tools              # build/host/retrods, build/host/retrods-harness
```

The harness runs copies of the core from a script (button presses, save states, memory reads, benchmarks); its commands are listed at the top of `tools/retrods-harness.c`. The Android build runs on GitHub Actions (`scripts/build-android.sh`); see `CLAUDE.md`.

## AI disclosure

RetroDS is written almost entirely by AI: the code, documentation and commit messages are written by Claude, Anthropic's AI model, in Claude Code sessions. Its owner decides what gets built and how it should behave, and tests it on the device. GBA DS, which RetroDS is built from, was made the same way (see its README). The RetroDS logo and icon are the owner's. Emulation itself is the cores', long-established, human-written emulators.

## Not affiliated

RetroDS is an independent fan project. It is not affiliated with, endorsed by or sponsored by Nintendo, Sony Interactive Entertainment, Konami, Square Enix, Intelligent Systems, Retroid, the GBA DS project, or any other company whose games it can run. All trademarks belong to their owners. RetroDS includes no games, BIOS files or game graphics: it reads them from copies you own.

## Licence

GPLv3, as GBA DS (see [LICENSE](LICENSE)). bsnes is GPLv3. Third-party code in `third_party/` and `src/retro/libretro.h` keeps its own licence.
