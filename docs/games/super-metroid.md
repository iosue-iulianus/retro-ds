# Super Metroid

| | |
|---|---|
| Releases | Japan and USA (one ROM) |
| Dumps | `D63ED5F8` "Super Metroid (Japan, USA) (En,Ja)" (tested) |
| Profile | `src/profiles.c`: the `D63ED5F8` entry |
| Tests | by hand for now: the harness with the states in the owner's test data (Crateria, at the ship and paused) |
| Status | in progress (M2) |

What SNES DS does with this game. Each rule is a promise the profile keeps. Change a rule here first when the behaviour is meant to change, then the profile, then the tests. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the general rules GBA DS set out (`docs/reference/gba/README.md`, GEN-1 to GEN-13), read with SNES sizes: the second screen is a 256x224 picture.

## Rules

**SM-1. Map during play.** While Samus is being played anywhere but Ceres Station, the second screen shows the pause screen's map, captured on the hidden copy every `map_interval` frames (60: every second) and soon after each new room.

**SM-2. The map and its plates.** What's shown is the map in its frame, with the area's name above it and the row of plates below it, changed for touch (SM-7). The energy row above the frame is left out.

**SM-3. Last good map stays.** Where the pause screen can't be opened (going through a door, riding an elevator, Ceres Station, a cutscene), the last good map stays up.

**SM-4. Logo before the first map.** Until the first map is captured, and at the title screen, the intro, the file select and the options, the second screen shows the logo, taken from the title screen.

**SM-5. A still map.** The map is shown still, each part that blinks on the pause screen (Samus's marker, the scroll arrows, the plates) at its brightest. *(Until Nov 21, 2026's test builds the marker blinked, but the recorded loop was shorter than the game's cycles, 24 and 54 frames, and everything that blinks pulsed too fast.)*

**SM-6. No mini-map on the game screen.** While the second screen shows the map and Samus is played (going through doors and pausing included), the mini-map in the energy bar's top right corner is covered in black: the second screen's map makes it pointless. Ceres Station, which has no map on the second screen, keeps it. Only what's shown changes: the game, its screenshots and states are as the game draws them. *Exception to "the live game's picture isn't changed" (the owner's choice, Nov 21, 2026).*

**SM-7. Touch on the map.** The map is centred on Samus, as the game centres its pause map. A drag on the second screen scrolls it at once, following the finger, as far as the area's map goes; it stays where it was dragged until Samus moves again, then it's centred on her again. A tap switches the second screen between the map and Samus's equipment page (R on the hidden copy, about a second). For the drag, each capture takes the area's whole map: the hidden copy's view is set in turn over all of it (nine views, the middle of each, clear of the scroll arrows) and the pieces are put together, so scrolling needs no capture. Only the hidden copy is changed for this (its view's position in its RAM), never the game being played. The scroll arrows aren't shown: the map moves under the finger instead. Tapping the second screen doesn't open the pause menu. The second screen then shows the pause screen's bottom row of plates too: tapping SAMUS opens the equipment page, tapping MAP on it goes back. Each page shows only the plate to the other one: the map SAMUS, the equipment page MAP, each with its pill beside it, where a small arrow takes the place of the L or R so it doesn't read as the handheld's shoulder button. The rest of the row (the other plate and pill, EXIT / START) is covered. On the equipment page, the line from SUPPLY up to the energy row's reserve tanks (not shown) is taken out. *Exception to "nothing on the second screen needs a tap" (the owner's choice, Nov 21, 2026).*

## Exceptions

- SM-6: the mini-map is covered (the game's picture changed).
- SM-7: touch on the second screen, and arrows drawn on the plates.

## How it's found

- Game state, `$7E0998`: `08` playing, `09` to `0B` going through a door, `0C` to `0E` pausing, `0F` the pause screen up, `10` to `12` unpausing; `00` to `05` the title, file and options screens. Area, `$7E079F`: `00` Crateria to `05` Tourian, `06` Ceres Station. Room, `$7E079B` (2 bytes).
- Start opens the pause screen in about 67 frames (the fade); it has settled by about 76.
- The whole map: the pause map is a 512 x 256 layer; setting the view (`$7E00B1`, `$7E00B3`, 2 bytes each, any value: the game doesn't hold it to the map's edges) shows any part of it three frames later. Views at x -30, 166, 362 and y -60, 56, 172, each kept from x 30 to 225 and y 60 to 175, cover it.
- Scrolling the pause map: the D-pad moves it a pixel a frame, in steps of 8 (a tap: 8; a 16-frame hold: 16), up to the edges of the area's map; the view's x grows in `$7E00B1`. Measured with the owner's state after the Map Station.
- The mini-map: x 208 to 248, y 6 to 30 in the energy bar (covered: 206, 4, 46 x 28).
- Pause screen page, `$7E0727` (2 bytes): `0000` the map, `0001` the equipment, other values while switching (R from the map: about 30 frames, fading).
- The map screen is told by its frame and the SAMUS plate at the bottom right (the equipment page has MAP there). Not by the grid: it scrolls with the map.

## The map's data (for drawing it natively, SM-8 planned)

From P.JBoy's bank logs (github.com/insanefirebat/sm_disassembly), checked against the game's own screen with `tools/sm_map.py`:

- The pause map is BG1, a 64 x 32 tilemap (two 32 x 32 screens, left then right), 4 bits a pixel. Its tiles: `$B6:8000` (0x4000 bytes, uncompressed); palettes `$B6:F000` (256 colours); menu sprites' tiles `$B6:C000`.
- Each area's map: 2048 tilemap words through `AreaMapPointers` (`$82:964A`, 3 bytes an area; Crateria `$B5:9000`, Brinstar `$B5:8000`...). The map station's reveal mask: `MapData_pointers` (`$82:9717`, 2 bytes an area, bank `$82`), a bit a tile.
- RAM: `AreaIndex` `$079F`; the area's explored tiles `$7E:07F7` (256 bytes, a bit a tile, the high bit first); every area's `$7E:CD52` (256 bytes each); map stations `$7E:D908` (a byte an area); the room's map position `$07A1` / `$07A3`; Samus `$0AF6` / `$0AFA`; equipment `$09A2` (equipped items), `$09A4` (collected), `$09A6` / `$09A8` (beams), `$09C0` (reserve mode).
- A tile is drawn (`LoadPauseMenuMapTilemap`, `$82:943D`): explored: its word with bit 10 cleared (pink); else, with the area's map station, its word as it is (blue) where the reveal mask has it; else tile `$1F` (empty).
- Drawn on top as sprites: the gunship, Samus's position (`MapScreen_DrawSamusPositionIndicator`, `$82:B9C8`), map icons by type (`Draw_MapIcons_ofGivenType`, `$82:B81C`, positions from `MapIcon_PositionTablePointers_*` at `$82:C7CB`...), elevator destinations (`MenuSpritemap_59`... `5E`), the scroll arrows.

Sprites on the map (from the routines named above; drawn by `tools/sm_map.py`, which matches the game's own pause screen pixel for pixel, Nov 22, 2026: every frame of the marker's cycle, the owner's test states, and every area with all of it explored, nine views each):

- The pause screen's sprites are 8 x 8 or 16 x 16 (OBSEL `$01`), tiles from `$B6:C000` (loaded at VRAM `$4000`; a 16 x 16 sprite's tiles are n, n+1 above n+16, n+17), colours 128 and up of the pause palette. Order (the first on top): the marker, bosses, missile, energy and map stations, the save station, the gunship, the elevator labels.
- Lines: the picture's first line shows the map layer's row (view y + 1), and a sprite starts on the line below its Y. So map row y is on line y - view y - 1, and in map pixels a sprite sits one row lower than the game's numbers.
- Sprites reach the picture a frame after the game places them (OAM is copied in the next vertical blank): a shot taken just as the marker changes frame still shows the one before.

- Spritemaps: pointer table `$82:C569` (by ID); each: a count word, then 5-byte entries `s000000xxxxxxxxx` (size, 9-bit X), Y byte, `YXppPPPttttttttt` (flips, priority, palette, tile). The routine's palette bits ($03: `$0E00`, `$0C00`, `$0400` or 0) replace the entry's.
- Samus's marker: x = (`RoomMapX` + high byte of Samus's X) x 8, y = (`RoomMapY` + high byte of Samus's Y + 1) x 8; spritemaps `$5F`, `$60`, `$61`, `$60` for 8, 4, 8, 4 frames; palette bits `$0E00`.
- Icons (`Draw_Map_Icons`, `$82:B672`): boss icons (spritemap 9; a dead boss, `$7E:D828` a byte an area, a bit an icon, gets the cross, spritemap `$62`, palette `$0E00`, on top of its icon in palette `$0C00`; a live one is shown only while the area's map is collected, `$0789`), missile stations (`$0B`), energy stations (`$0A`), map stations (`$4E`), all palette `$0E00`, each only where its map square is explored (`Check_if_MapPosition_isExplored`); save stations (8, palette `$0400`) only in the area saved in (`$7E:D918`), and only the one the game was loaded from (`$078B`, its bit in `$82:B7C9`) if it's marked used (`$7E:D8F8`, 2 bytes an area, a bit a station). Position tables: 2 words (x, y map pixels) an icon, ending `$FFFF` (`$FFFE` skips a boss); per-area pointers at `$82:C7CB` (bosses), `C7DB` (missile), `C7EB` (energy), `C7FB` (map), `C80B` (save). In Crateria the gunship (spritemap `$63`, palette `$0E00`) at `Crateria_MapIconPositions_savePoints` / `savePoints2`.
- Elevator destinations (`Display_Map_Elevator_Destinations`, `$82:BB30`-ish): only with the area's map station; per-area pointers `Map_Elevator_Destinations`, 3 words an entry (x, y, spritemap ID `$59`-`$5E`), ending `$FFFF`; palette 0.

## Not covered yet

- Crateria and the first rooms of Brinstar have been played on the Duo (and the way back up, the owner's slot 0 state in the test data: the map once stuck on Brinstar there). Other areas, Ceres Station, bosses, the escape and the ending haven't.
- Death and the game over screen: what the second screen shows there isn't checked.
- The Europe release (its own ROM) has no profile yet.
