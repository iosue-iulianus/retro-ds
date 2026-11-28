# Changelog

RetroDS's history. It started as a fork of GBA DS v1.5.6, then SNES DS;
GBA DS's own changelog up to then is `git show 3b452dc:CHANGELOG.md` (and
on [GBA DS's GitHub](https://github.com/kyannon/gba-ds)).

## v1.0.2 (November 28, 2026)

A bugfix release.

- **Symphony of the Night couldn't be started.** Start on the title
  screen turned the second screen's page instead of starting the game.
  Start is now the game's everywhere but in the castle.
- **Rewind could crash.** Rewinding across a big change on screen (a
  room or scene change) could corrupt memory and crash the game.
- **Symphony of the Night's menu could crash** on the second screen,
  depending on where the menu's window sat.
- **A save state could crash RetroDS** when memory ran short; now the
  save just fails.
- **Moving RetroDS's folder** (Settings > Folders) now moves a
  PlayStation games folder chosen inside it, as it does the Super
  Nintendo one.
- `retrods --supported` counts Symphony of the Night as supported.

## v1.0.1 (November 26, 2026)

A bugfix release.

- **Game filtering.** By default only the supported games are shown.
  To see all games, open Supported Games and press Show all games. For
  games without a second screen, a standard emulator is recommended:
  RetroDS plays them on one screen only.
- **The game list shows every game.** It stopped after the 256th: in a
  bigger collection, games further down the alphabet (Super Metroid, say)
  couldn't be reached.

## v1.0.0 (November 25, 2026)

The first release.

RetroDS plays Super Nintendo and PlayStation games on dual-screen Android
handhelds, the game on one screen and, for the games below, the second
screen the game never had on the other. Built for and tested on the
Retroid Pocket Duo only.

- **Super Metroid:** the area map as the pause screen draws it, explored
  rooms and all, dragged and flung at the screen's refresh rate; the SAMUS
  page, where a tap switches a beam, suit or item on or off. The game's
  mini-map is covered: the full map is on the second screen.
- **The Legend of Zelda: A Link to the Past:** the world map built from the
  ground the game has shown you, the game's clouds over the rest, Link
  walking on it and the game's markers; tap and pinch zoom; the items
  screen sliding over the map with X, a tap giving an item to Y; your own
  markers; the dungeon's map, floor by floor. At the title the Master
  Sword falls from the second screen into the game's.
- **Castlevania: Symphony of the Night:** the castle map as the game's map
  screen draws it, in a window of the game's (its colour from the System
  menu), with the logo, the rooms explored and the castle's percentage;
  tap and pinch zoom. Start or a swipe pauses the game and moves its own
  menu to the second screen, widened, with the logo on the game screen.
  Nothing in the game is written: RetroDS only presses its buttons.
- **Every other game** plays on one screen, with its logo or name on the
  other.
- **Super Nintendo** with bsnes; **PlayStation** with SwanStation, `.cue`
  and `.bin` discs, your own BIOS (`scph5501.bin` for US games) in
  `RetroDS/bios/`.
- The game list (each game's logo, the picture you'll continue from on the
  other screen, Supported Games, light and dark themes); a folder for each
  system's games; RetroDS's menu (a tap on the game screen): big Save
  State and Load State with Undo, the game at 4:3 or Pixel Perfect,
  scanlines, overlays (off by default), controls for both systems (each
  button's choices name the Super Nintendo's and the PlayStation's
  button), hotkeys; updates from this repository's releases; cheats;
  on the Super Nintendo, rewind (L2), fast forward (R2), patches applied
  as the game loads (IPS, UPS, BPS, xdelta) and RetroAchievements (casual
  mode); ten languages.

Known limits: only the releases each game's rules page lists get its
second screen (USA; Super Metroid's Japan / USA cartridge); no
RetroAchievements hardcore mode, and none yet for the PlayStation; tested
on the Retroid Pocket Duo only; at 120 Hz a dragged map can judder.

