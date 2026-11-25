# Castlevania: Symphony of the Night

| | |
|---|---|
| Releases | USA (this page): serial `SLUS-00067`, one disc (track 1 data, track 2 audio) |
| Game code | `SLUS-00067` (the serial its `SYSTEM.CNF` boots: `src/disc.c`) |
| Core | SwanStation with `scripts/cores/swanstation-vram.patch` (the game's video RAM, 1 MB) |
| Code | `src/sotn.c` |
| Tests | the harness with the disc and `RETRODS_BIOS_DIR`; test files in `test-data/symphony-of-the-night/` (`duo-auto-2026-11-25-first-save.state`: Alucard in the first save room, 33 rooms) |
| Status | SOTN-1 built and checked on the host; the map (SOTN-2 on) a proposal for the owner's review |

What RetroDS does with this game. Each rule is a promise; change the rule here first, then the code, then the tests. Rule numbers are never reused. `Xeeynamo/sotn-decomp` (AGPL-3.0) is a reference for addresses and formats only: no code from it.

## Rules

**SOTN-1. The logo.** Until the game has its own second screen, and wherever it shows none, the second screen shows the game's logo: the title screen's "Castlevania, Symphony of the Night" lettering, taken from the game's video RAM while the title is up (game state 1, its picture and palette checked against the ones measured), its clear pixels transparent, on black. It's kept as the game's extracted logo (`states/logos/SLUS-00067.png`, 460 x 242: the title's 512-wide pixels, each line twice, so it shows at the size the game screen shows it) and used from then on, in the game list too. Nothing runs hidden: the PlayStation has one core copy, so the logo comes the first time the player passes the title (a game started from its auto-saved state skips it: Restart Game brings the title). Until then, the game's name. A logo of the player's own (`logos/SLUS-00067.png` in the data folder) or a scraped one wins, as for every game.

## The map: a proposal (for the owner's review, Nov 25, 2026)

*The owner, Nov 25, 2026: the map on the second screen should look better than GBA DS's Castlevania maps (`docs/cotm.png` upstream: the map alone in a plain frame); the game's own art may be used for the background, borders and the like.*

What the game itself shows: Select brings up the castle map over the darkened game, at the game's own pixels (one map square is 4 x 4 of them, the castle 64 x 64 squares), explored rooms in blue (`80,112,248`) with light grey walls (`192,192,192`), save rooms red, teleporters orange, Alucard's room marked with a blinking cursor. Start's status screen gives ROOMS (the count of rooms explored) in a navy window with a white edge. The file select screen gives the percentage per save (rooms out of 942).

**Proposed SOTN-2. The castle map, as the game draws it.** The second screen shows the game's own map picture, read from its video RAM each time it changes (the game redraws a square there as Alucard explores it), with the game's own palette: exactly the pixels Select shows, nothing more (no unexplored rooms: the game keeps the whole castle's plan in memory, but the map shows only what's been explored). The inverted castle shows its own map as the game does (turned upside down). At a whole number of screen pixels per map pixel (5 on the Duo's top screen: the castle's 242 x 190 pixels as 1210 x 950).

**Proposed SOTN-3. Where Alucard is.** His room's square blinks as the game's map cursor does, at his place on the map (his room's square from the game's memory).

**Proposed SOTN-4. Around the map** (one of the three, the owner's choice; mock-ups beside this handoff, `sotn-map-mockups/`):

- **A. The status screen's window.** The whole second screen is a window as Start's status screen draws it (its navy gradient and white edge, redrawn at the screen's size), the map in it on the left, and on the right ROOMS and the percentage in the status screen's own lettering.
- **B. The file select's relief.** Behind the map, the file select screen's carved gargoyle (the game's own background picture, 384 x 240, darkened), and ROOMS and the percentage in a small status-screen window in a corner. The richest, and still only the game's pictures.
- **C. As A, with the title's logo** in the map's empty top-right corner.

The pictures used are the game's: the map and its palette from video RAM while playing; the gargoyle (B) from video RAM while the file select is up (every start passes it), kept as a file beside the logo; the status window redrawn from its colours (`0,0,41` at the top to `24,24,90` at the bottom, a 1-pixel white `255,251,255` edge); the lettering from the game's font in video RAM.

Not proposed: touch on the map (scrolling isn't needed: the whole castle fits), relics and equipment (SCOPE.md's later steps, each a write exception if wanted).

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

Checked: the map picture with that palette, placed 1:1, matches the game's Select screen pixel for pixel (but where Alucard's cursor blends over it).
