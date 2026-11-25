# Castlevania: Symphony of the Night

| | |
|---|---|
| Releases | USA (this page): serial `SLUS-00067`, one disc (track 1 data, track 2 audio) |
| Game code | `SLUS-00067` (the serial its `SYSTEM.CNF` boots: `src/disc.c`) |
| Core | SwanStation with `scripts/cores/swanstation-vram.patch` (the game's video RAM, 1 MB) |
| Code | `src/sotn.c` (reading and composing), `src/game_sotn.c` (the second screen) |
| Tests | the harness with the disc and `RETRODS_BIOS_DIR`; test files in `test-data/symphony-of-the-night/` (`duo-auto-2026-11-25-first-save.state`: Alucard in the first save room, 33 rooms) |
| Status | SOTN-1 to 4 built and checked on the host (`retrods-harness ... sotn NAME`, the app); not yet on the Duo |

What RetroDS does with this game. Each rule is a promise; change the rule here first, then the code, then the tests. Rule numbers are never reused. `Xeeynamo/sotn-decomp` (AGPL-3.0) is a reference for addresses and formats only: no code from it.

## Rules

**SOTN-1. The logo.** Until the game has its own second screen, and wherever it shows none, the second screen shows the game's logo: the title screen's "Castlevania, Symphony of the Night" lettering, taken from the game's video RAM while the title is up (game state 1, its picture and palette checked against the ones measured), its clear pixels transparent, on black. It's kept as the game's extracted logo (`states/logos/SLUS-00067.png`, 460 x 242: the title's 512-wide pixels, each line twice, so it shows at the size the game screen shows it) and used from then on, in the game list too. Nothing runs hidden: the PlayStation has one core copy, so the logo comes the first time the player passes the title (a game started from its auto-saved state skips it: Restart Game brings the title). Until then, the game's name. A logo of the player's own (`logos/SLUS-00067.png` in the data folder) or a scraped one wins, as for every game.

## The map

*The owner, Nov 25, 2026: the map on the second screen should look better than GBA DS's Castlevania maps (`docs/cotm.png` upstream: the map alone in a plain frame), using the game's own art for the background, borders and the like. Of three mock-ups (`sotn-map-mockups/` beside the repo) the owner chose C, the status window with the logo, with ROOMS and the percentage together in one small window as in B.*

What the game itself shows: Select brings up the castle map over the darkened game, at the game's own pixels (one map square is 4 x 4 of them, the castle 64 x 64 squares), explored rooms in blue with light grey walls, save rooms red, teleporters orange, Alucard's room marked by the map cursor. Start's status screen gives ROOMS in a window with a white edge; the file screen gives each save's percentage.

**SOTN-2. The castle map, as the game draws it.** While Alucard is played in the castle, the second screen shows the game's own map picture, read from its video RAM as it changes (the game draws a square there as he explores it), in the game's own palette: exactly the pixels Select shows, nothing more (no unexplored rooms: the game keeps the whole castle's plan in memory, but the map shows only what's been explored). The inverted castle's map turned half round, as Select shows it (*the inverted castle: from the game's code, not yet seen*). At a whole number of screen pixels per game pixel, as large as the castle's 190 lines allow with a little room: 5 on the Duo's top screen (the castle's 242 x 190 pixels as 1210 x 950). Before Alucard has explored a room (the prologue), at the title and the file screens, at game over and the ending: the logo (SOTN-1). While the game loads between areas, the map stays as it was.

**SOTN-3. Where Alucard is.** The game's own map cursor (its cream disc from video RAM, half see-through as the game draws it) closes onto Alucard's square over and over, every 64 frames, as on Select's map; his square from the game's memory (the room's place on the map and his place in the room).

**SOTN-4. Around the map.** The whole second screen is a window as Start's status screen draws it: a gradient from the player's window colour (the System menu's; 0, 0, 8 at first: navy) less 32 at the top to it plus 32 at the bottom, upside down in the inverted castle as the game's are, dithered as the PlayStation dithers it, a white edge a pixel wide, black round it. The map in it at the left. At the top right the title's logo (SOTN-1), at its own shape. At the bottom right a small window of the same kind holds ROOMS and the rooms explored, as the status screen writes them, and under them the percentage as the file screen gives it (rooms x 1000 / 942, in tenths: `3.5%`), in the status screen's own lettering (its 8 x 8 font from video RAM). Nothing on it is touched or written: the game is only read.

## How it's found (measured in the harness, Nov 25, 2026)

| What | Where | Notes |
|---|---|---|
| Game state | `0x8003C734` (32-bit) | 1 title, 2 playing, 8 the file screens (`g_GameState`) |
| Stage | `0x800974A0` | `0x41` the first save room; bit `0x20` the inverted castle (`g_StageId`) |
| Rooms explored | `0x8003C760` (32-bit) | 33 in the test state (`g_RoomCount`); the game's percentage: rooms x 100 / 942 |
| Explored squares | `0x8006BB74`, 0x800 bytes | 2 bits a square, 64 x 64 squares; `+0x400` the inverted castle's (`g_CastleMap`) |
| The castle's plan | `0x801E0000`, 256 x 256 at 4 bits | every room, explored or not (`CASTLE_MAP_PTR`) |
| The map picture | video RAM (832, 256), 64 x 256 halfwords, 4 bits a pixel | the game's own drawing of the explored squares; Select draws it 1:1 from (0, 1) of its 256 x 240 picture |
| Its palette | video RAM (0, 252), 16 colours | via the game's palette table (`g_ClutIds` `0x8003C104`, entry `0x1C0`) |
| The title's logo | video RAM (780, 2), 230 x 121 halfwords, 8 bits a pixel; palette (512, 240) | CRC32 `BE361342`, palette `1876343F` (SOTN-1) |
| The file select's relief | video RAM (576, 0), 192 x 240 halfwords, 8 bits; palette (256, 241) | while the file select is up |
| Alucard's square | `0x800730B0`, `0x800730B4` (the room's place in squares, `g_Tilemap.left/top`) plus `0x800973F0` / `0x800973F4` over 256 (his place in the room) | the map cursor's (SOTN-3) |
| Window colour | `0x8003CAC0`, three 32-bit values 0 to 31 | the System menu's (`g_Settings.windowColors`) |
| The status screen's font | video RAM (896, 256), 4 bits, 8 x 8 from `' '`; palette entry `0x196` | resident while playing |
| The map cursor | video RAM (640, 256), 32 x 32, 4 bits; palette entry `0x170` | |

Checked: the map picture with that palette, placed 1:1, matches the game's Select screen pixel for pixel (but where Alucard's cursor blends over it).
