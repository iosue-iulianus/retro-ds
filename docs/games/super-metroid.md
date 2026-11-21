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

**SM-7. Touch on the map.** The map is centred on Samus, as the game centres its pause map. A drag on the second screen scrolls it; it stays where it was dragged until Samus moves again, then it's centred on her again. A tap switches the second screen between the map and Samus's equipment page. Both are opened on the hidden copy with the game's own buttons (the D-pad to scroll, R for the equipment page), never on the game being played. Tapping the second screen doesn't open the pause menu. The second screen then shows the pause screen's bottom row of plates too: tapping SAMUS opens the equipment page, tapping MAP on it goes back. Each page shows only the plate to the other one: the map SAMUS, the equipment page MAP, each with its pill beside it, where a small arrow takes the place of the L or R so it doesn't read as the handheld's shoulder button. The rest of the row (the other plate and pill, EXIT / START) is covered. On the equipment page, the line from SUPPLY up to the energy row's reserve tanks (not shown) is taken out. *Exception to "nothing on the second screen needs a tap" (the owner's choice, Nov 21, 2026).*

## Exceptions

- SM-6: the mini-map is covered (the game's picture changed).
- SM-7: touch on the second screen, and arrows drawn on the plates.

## How it's found

- Game state, `$7E0998`: `08` playing, `09` to `0B` going through a door, `0C` to `0E` pausing, `0F` the pause screen up, `10` to `12` unpausing; `00` to `05` the title, file and options screens. Area, `$7E079F`: `00` Crateria to `05` Tourian, `06` Ceres Station. Room, `$7E079B` (2 bytes).
- Start opens the pause screen in about 67 frames (the fade); it has settled by about 76.
- Scrolling the pause map: the D-pad moves it a pixel a frame, in steps of 8 (a tap: 8; a 16-frame hold: 16), up to the edges of the area's map; the view's x grows in `$7E00B1`. Measured with the owner's state after the Map Station.
- The mini-map: x 208 to 248, y 6 to 30 in the energy bar (covered: 206, 4, 46 x 28).
- Pause screen page, `$7E0727` (2 bytes): `0000` the map, `0001` the equipment, other values while switching (R from the map: about 30 frames, fading).
- The map screen is told by its frame and the SAMUS plate at the bottom right (the equipment page has MAP there). Not by the grid: it scrolls with the map.

## Not covered yet

- Crateria and the first rooms of Brinstar have been played on the Duo (and the way back up, the owner's slot 0 state in the test data: the map once stuck on Brinstar there). Other areas, Ceres Station, bosses, the escape and the ending haven't.
- Death and the game over screen: what the second screen shows there isn't checked.
- The Europe release (its own ROM) has no profile yet.
