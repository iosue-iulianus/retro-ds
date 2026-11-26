# Final Fantasy VI (Final Fantasy III in the USA)

| | |
|---|---|
| Releases | USA 1.0 (this page). 1.1 (`C0FA0464`) and Japan's Final Fantasy VI (`45EF5AC8`) are other ROMs: not handled yet |
| Dumps | `A27F1C7A` "Final Fantasy III (USA)" (MD5 `e986575b...`, the ROM everything8215/ff6 assembles to) |
| Profile | `src/profiles.c`: the `A27F1C7A` entry (logo only, the game on the first screen) |
| Code | `src/ff6.c` (reading the game), `src/game_ff6.c` (the second screen) |
| Tests | the harness (`ff6 part`, `ff6 battle`, `ff6 tap ROW`, `ff6 menu NAME`, `ff6 battlepic NAME`) and the app headless with the test files in `test-data/final-fantasy-vi/` (`harness-*`: the opening at Narshe's gate, the six slots of two players' saves loaded through Continue) |
| Status | FF6-1 to 6 built Nov 26, 2026 and checked on the host (FF6-5 pixel-exact against the game's menu); FF6-1 to 4 and 6 installed on the Duo, not yet tried by the owner |

What RetroDS does with this game. Each rule is a promise; change the rule here first, then the code, then the tests. Rule numbers are never reused. `everything8215/ff6` (65816 disassembly, GPL-3.0, byte-identical for this ROM) is a reference for addresses and formats only: no code from it.

*The owner, Nov 26, 2026, later: "That's the whole point of RetroDS, drawing our own screen with data and art from the game": every FF6 screen drawn by RetroDS, as Super Metroid's and A Link to the Past's are, so it can be laid out for the second screen. The game's own pictures moved over (FF6-2, FF6-4) are stopgaps until their drawn screens replace them.*

*The owner, Nov 26, 2026: FF6 next (SCOPE.md's update). The game above and its menus below, especially for battles, touch to fight, as the DS's Chrono Trigger did, and the menus taken off the game screen so they aren't on both. The rules below are the overnight session's reading of that, by earlier decisions (the game's own screens moved over, as Symphony of the Night's menu; press the game's buttons rather than write; when unsure, the logo): for the owner to confirm on the Duo.*

## Rules

**FF6-1. The logo.** The US release's splash screen lettering, "FINAL FANTASY III" (power-on, frame 500, the box 24, 60 to 237, 130 of its picture), its navy background made clear: on the second screen wherever it shows nothing else, and in the game list. A logo of the player's own wins, as for every game.

**FF6-2. The battle screens.** In battle, laid out as the DS's Chrono Trigger lays its own (pages of big buttons on the touch screen, the battle above), all in the game's own windows (the player's style and colours, the menu's gradient), font, portraits and hand cursor, drawn by RetroDS from the game's data, at 320 x 240 of the game's pixels (4 screen pixels each on the Duo's bottom screen):
- **On the game screen, the party:** where the game's battle windows were (the strip of rows 150 to 213), a window with a line for each character: name (yellow: whose turn it is), HP and its maximum, MP and its maximum (teal; for those with MP), the time gauge (the game's own pieces and colours: filling, full). Below it to the picture's bottom, black. The battle is shown once: the game's own windows are under it. Battle messages the game puts at the top of the picture (an attack's name) stay there.
- **Below, someone's turn:** top left, a window with whose turn it is (portrait, name, HP, MP, time gauge); under it, a button (a window) for each of their commands, greyed ones grey, the game's hand on the one its cursor is on; right, the enemies' names, to read.
- **Below, choosing a target:** the same window with whose turn it is; beside it, the command and the target the cursor is on (yellow); a button for every enemy alive, two across, top to bottom as they stand on the battlefield (the game's own list names each kind once: a button each lets a finger pick one); a row with a button for each character and Back. The game's hand marks the buttons the cursor covers.
- **Below, nobody's turn** (for 20 frames: between turns the menu closes for a few, and the last page stays meanwhile): the party two by two (portraits, HP, MP, gauges) and the enemies' names.
- **While one of the game's own lists is open** (Magic, Item, Blitz...: not drawn yet; open for 4 frames, as the menu passes through other states for a frame after a target is chosen or as a list opens), the game's battle windows are shown below instead, moved over (a stopgap: the strip of its picture, where it would be were the whole picture shown as the game's is).

*(Exception to Philosophy 1, listed: the game screen's windows replaced by RetroDS's drawing of the party, where Super Metroid's mini-map is covered. The layout is the DS's Chrono Trigger's arrangement in the game's own pieces: the owner, Nov 26, 2026, with the party list on the game screen in the windows' place.)*

**FF6-3. Touch to fight.** A tap on a command's button chooses it, as the player would with the pad: RetroDS presses Down as many times as the game's cursor needs to reach it (the game skips greyed commands, and so does the count), then A. Choosing a target (after Fight, a spell, an item...), a tap on an enemy's button attacks that enemy and a tap on a character's aims at them (a cure, an item): RetroDS moves the game's own target cursor there with the D-pad, as a player would (toward the monster's place on the battlefield, Right over to the party, Up and Down through it), then presses A; one tap, no confirming, as the DS's Chrono Trigger. If the cursor can't get there in 12 presses, it stays where it got to and nothing is chosen. A target the game's cursor already covers (a spell on all of them) is confirmed as it is. Back is B (back to the commands). A tap on the window of whose turn it is (the command page's top left) is X: the next character whose gauge is full, as on the pad. Everything else in battle (the lists that open, Row and Defend) stays on the pad for now. While the presses play (12 frames each), the player's own buttons wait. Not with Config's Command set "short" (a cross of four, not a list): there the pad only. *(Writes: none. RetroDS presses the game's own buttons. The owner, Nov 26, 2026: targets by touch, Back, the next character.)*

**FF6-4. The main menu, below.** While the game's menu program runs (the main menu on X and everything it opens: Item, Skills, Equip, Relic, Status, Config, Save; shops and the name screens, which use it too), its whole picture is the second screen's, as the game draws it, shown as the game's own picture is (its Scale); the game screen shows the logo meanwhile (as Symphony of the Night's menu page). Worked with the pad, as in the game; no touch yet.

**FF6-5. The party, drawn.** On the field and the world map, the second screen shows the party as the game's main menu shows it, drawn by RetroDS from the game's data and art: the party window with each character's portrait (the back row's further right; an imp's portrait for an imp), name, LV, HP and MP (the maximums with a relic's boost, at most 9999 and 999; MP only for those the game shows it for: any esper in the party, Gogo, or a spell known), and the Time and Steps / Gp windows, in the player's own window style and colours (Config) and font colour, with the game's gradient. The command list is left out (the game's own menu is one button away). Shown as the game's own picture is (its Scale: at 4:3 the whole 4:3 screen, at Pixel Perfect whole pixels: the two screens match); drawn again only when something on it changes. Checked against the game's own menu pixel for pixel (the seven test states: 0 pixels differ, but for the minute's digit where the clock ticked). Status icons (poison and the like, beside the name) aren't drawn yet. On the title, the opening, cutscenes, the ending, and with no party: the logo (FF6-1).

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
| The battle's party | `$2EAE` + 32 x slot: graphic (portrait), name (6), HP, max HP, MP, max MP | `$201D` bits: shown |
| Time gauges | `$619E` + slot (0 to 255; `FF` full); Config's gauge `$2021` | `GaugeTextTbl`: a left end, 4 pieces `F0` + 0 to 8, a right end; battle palettes `ED62C0` (5 filling, 2 full) |
| Enemies | `$200D` 4 groups (monster numbers; `FFFF` none) | names `CFC050`, 10 letters |
| Command names | `D8CEA0`, 7 letters | |
| The hand cursor | `ED5AC0` 4bpp tiles 0, 1, 16, 17; palette `D8E8B8` | |
| Battle menu states | `$7BC2`: 00 closed, 01 opening, 05 commands, 38 target, 0E Magic (the lists above 05) | |
| The target cursor | `$7B7D` the party it covers (bits 0-3), `$7B7E` the monsters (bits 0-5) | D-pad: Left/Right/Up/Down by place among the monsters; Right from the right-most to the party; Up / Down through the party (Up: the slot before, wrapping) |
| The monsters | `$2001` + 2 x n, n 0 to 5: its monster number (`FFFF` none); alive: `$3AA8` + 2n bit 0 (here) and not `$3EEC` + 2n bit 7 (dead); `$80C3` + 2n and `$80CF` + 2n its place on the battlefield (x, y) | |
| Characters (FF6-5, planned) | `$1600`, 16 x 37 bytes | +8 level, +9 HP, +B max HP, +D MP, +F max MP (the low 14 bits; the top 2 a relic's boost, `CalcMaxHPMP`) |
| Gp, steps | `$1860`, `$1866` (3 bytes each) | (the disassembly's RAM notes have them the other way round; its code has them as here) |
| The window style | `$1D4E` bits 0-2, the colours `$1D57` (8 x 7) | |
| The party (field) | the field's objects `$0867` + 41 x n (settings `verbbppp`), the save's `$1850` elsewhere; the current party `$1A6D` | the field writes them back to `$1850` as the menu opens |
| Game time | `$021B` hours, `$021C` minutes; a countdown timer instead while `$1188` bit 4 (`$1189` frames) | |
| Window art | `ED0000` + style x `0x380`: 28 tiles (a 4 x 4 inside, borders, corners: `WindowTileTbl`); colours 1 to 7 the player's | the menu's colour math: 11 down to 0 added over the top 112 lines (every 10), 1 up subtracted below, on the windows only |
| Text | the fixed-width font `C47FC0` (2bpp); palettes `D8E800` (BG1 palette n: its 4 n; BG3: its n), the main colour the player's `$1D55` | the main menu's text layers scrolled up 3 (BG1) and 1 (BG3), its windows 1 |
| Portraits | `ED1D00` (0x320 each), palettes `ED5860`; which: `C36F00` (palette), `C36F1B` (graphics); the menu's sprites `C30B8F` | at x 14 (front row) or 26 (back), y 21 + 48 n |

The title: the US splash ("FINAL FANTASY III, ©1994 Square") takes no input; the title proper, over the clouds after it, leaves for New Game / Continue on A only (`$06` bit 7).

## Planned (not built)

- **The battle's lists, drawn** (Magic, Item, Blitz, Rage, Dance, Slots, Tools, Lore...: each a FF6-2 fallback until then), touched as the commands are.
- **The main menu, drawn** (to replace FF6-4): its pages laid out for touch.
- **More touch in battle**: the Magic and Item lists, Row and Defend.
