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

**SM-2. Just the map.** What's shown is the map in its frame, with the area's name above it. The energy row above the frame and the button prompts below it (L, R, EXIT, START, SAMUS) are left out.

**SM-3. Last good map stays.** Where the pause screen can't be opened (going through a door, riding an elevator, Ceres Station, a cutscene), the last good map stays up.

**SM-4. Logo before the first map.** Until the first map is captured, and at the title screen, the intro, the file select and the options, the second screen shows the logo, taken from the title screen.

**SM-5. Samus's marker blinks.** With Map Animations on, Samus's marker blinks as it does on the pause screen.

## How it's found

- Game state, `$7E0998`: `08` playing, `0F` the pause screen up; `00` to `05` the title, file and options screens. Area, `$7E079F`: `00` Crateria to `05` Tourian, `06` Ceres Station. Room, `$7E079B` (2 bytes).
- Start opens the pause screen in about 67 frames (the fade); it has settled by about 76.
- The map screen is told by its frame (grey and white at both sides) and the purple grid.

## Not covered yet

- Only Crateria, at the start of a new game, has been run. Other areas, Ceres Station, bosses, the escape and the ending haven't.
- Death and the game over screen: what the second screen shows there isn't checked.
- The Europe release (its own ROM) has no profile yet.
