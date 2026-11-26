# Final Fantasy VI (Final Fantasy III in the USA)

| | |
|---|---|
| Releases | USA 1.0 (this page). 1.1 (`C0FA0464`) and Japan's Final Fantasy VI (`45EF5AC8`) are other ROMs: not handled yet |
| Dumps | `A27F1C7A` "Final Fantasy III (USA)" (MD5 `e986575b...`, the ROM everything8215/ff6 assembles to) |
| Profile | `src/profiles.c`: the `A27F1C7A` entry (logo only, the game on the first screen) |
| Code | `src/ff6.c` (reading the game), `src/game_ff6.c` (the second screen) |
| Tests | the harness (`ff6 part`, `ff6 battle`, `ff6 tap ROW`) and the app headless with the test files in `test-data/final-fantasy-vi/` (`harness-*`: the opening at Narshe's gate, the six slots of two players' saves loaded through Continue) |
| Status | FF6-1 to 6 built Nov 26, 2026 and checked on the host; not yet on the Duo |

What RetroDS does with this game. Each rule is a promise; change the rule here first, then the code, then the tests. Rule numbers are never reused. `everything8215/ff6` (65816 disassembly, GPL-3.0, byte-identical for this ROM) is a reference for addresses and formats only: no code from it.

*The owner, Nov 26, 2026: FF6 next (SCOPE.md's update). The game above and its menus below, especially for battles, touch to fight, as the DS's Chrono Trigger did, and the menus taken off the game screen so they aren't on both. The rules below are the overnight session's reading of that, by earlier decisions (the game's own screens moved over, as Symphony of the Night's menu; press the game's buttons rather than write; when unsure, the logo): for the owner to confirm on the Duo.*

## Rules

**FF6-1. The logo.** The US release's splash screen lettering, "FINAL FANTASY III" (power-on, frame 500, the box 24, 60 to 237, 130 of its picture), its navy background made clear: on the second screen wherever it shows nothing else, and in the game list. A logo of the player's own wins, as for every game.

**FF6-2. The battle windows, below.** In battle, the game's own battle windows (the party's names, HP and time gauges; the enemies' names; the active character's commands and every list that opens there: Magic, Item, Blitz...) are the second screen's: the strip of the game's picture that holds them (rows 150 to 213) is shown there as the game draws it, live, at whole screen pixels per game pixel, as large as fits (5 on the Duo's bottom screen: its full width), in the middle. On the game screen the same strip, to the picture's bottom, is black: the battle is shown once. Battle messages the game puts at the top of the picture (an attack's name) stay there. *(Exception to Philosophy 1, listed: the game screen covered where the second screen took the windows over, as Super Metroid's mini-map is.)*

**FF6-3. Touch to fight.** A tap on a command in the moved command list chooses it, as the player would with the pad: RetroDS presses Down as many times as the game's cursor needs to reach it (the game skips greyed commands, and so does the count), then A. While a target is being chosen, a tap anywhere on the battle windows confirms it (A): the game's cursor starts on its usual target, and the D-pad still moves it. Everything else in battle (the lists that open, cancelling with B, Row and Defend) stays on the pad for now. While the presses play (12 frames each), the player's own buttons wait. Not with Config's Command set "short" (a cross of four, not a list): there the pad only. *(Writes: none. RetroDS presses the game's own buttons.)*

**FF6-4. The main menu, below.** While the game's menu program runs (the main menu on X and everything it opens: Item, Skills, Equip, Relic, Status, Config, Save; shops and the name screens, which use it too), its whole picture is the second screen's, as the game draws it, at whole pixels; the game screen shows the logo meanwhile (as Symphony of the Night's menu page). Worked with the pad, as in the game; no touch yet.

**FF6-5. Elsewhere, the logo.** On the field, the world map, the title, the opening, cutscenes and the ending: the logo (FF6-1). *(A drawn party screen while walking, the main menu's party from the save data with the game's portraits and window style, is planned: see below.)*

**FF6-6. The game above.** Until the player picks a screen for the game (Screens > Game screen, for this game or all), this game is on the first (top) screen and its windows and menu below: the DS's Chrono Trigger's way round, and the bottom screen's 4:3 takes the battle windows at full width. RetroDS's other games keep their own choice (by the screens' shapes: the Duo's bottom one).

## How it's found (measured in the harness, Nov 26, 2026)

| What | Where | Notes |
|---|---|---|
| Which program runs | `$1500` = `5C` (JML), `$1501`-`$1503` its NMI handler | field `C00182`, battle `C10BA7`, the menu program `C31387`, the world map bank `EE` (`EEA728`), the title and opening `7E7233` (code in RAM) |
| A battle menu open | `$7E7BCA` (non-zero) | |
| Which battle menu | `$7E7BC2` | `05` the command list, `38` choosing a target, `0E` Magic |
| Whose | `$7E62CA` | the party slot, 0 to 3 |
| The command cursor | `$7E890F` + slot | 0 to 3 (the game keeps it per slot) |
| The commands | `$7E202E` + slot x 12: 4 x (number, flags, targeting) | flags bit 7: greyed (`FF` number: none) |
| Command set | `$7E1D4D` bit 7 | 1: short (FF6-3 off) |
| The battle windows | rows 151 to 213 of the picture | the command list's rows at y 160 + 12 r, x 8 to 100 |
| Characters (FF6-5, planned) | `$1600`, 16 x 37 bytes | +8 level, +9 HP, +B max HP, +D MP, +F max MP (the low 14 bits; the top 2 a relic's boost, `CalcMaxHPMP`) |
| Gp, steps | `$1860`, `$1866` (3 bytes each) | (the disassembly's RAM notes have them the other way round; its code has them as here) |
| The window style | `$1D4E` bits 0-2, the colours `$1D57` (8 x 7) | |

The title: the US splash ("FINAL FANTASY III, ©1994 Square") takes no input; the title proper, over the clouds after it, leaves for New Game / Continue on A only (`$06` bit 7).

## Planned (not built)

- **The party on the second screen while walking** (FF6-5): the main menu's party window drawn by RetroDS from the save data and the ROM's art (window graphics `ED0000`, palettes `ED1C00` or the player's `$1D57`, portraits `ED1D00` with palettes `ED5860` and layout `C051DA`, the fixed-width font `C47FC0`), with the game's rules for what it shows (`DrawCharBlock`: the boosted maximums capped at 9999 and 999, MP only for those with magic, `CheckMPVisible`). Checked pixel by pixel against the game's own menu (the six test slots).
- **More touch in battle**: the Magic and Item lists, Row and Defend.
- **The Duo's top screen**: the battle scene is the picture's top two thirds; it could be shown larger there once the windows are below (whole pixels: 7 instead of 4). For the owner.
