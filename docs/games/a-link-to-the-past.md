# The Legend of Zelda: A Link to the Past

| | |
|---|---|
| Releases | USA (this page); Japan 1.0 to 1.2 and Europe have their own ROMs and addresses |
| Dumps | `777AAC2F` "Legend of Zelda, The - A Link to the Past (USA)" (MD5 `608c22b8...`, the ROM spannerisms/usdasm assembles to) |
| Profile | `src/profiles.c`: the `777AAC2F` entry, native (`src/alttp.c`) |
| Tests | by hand: the harness (`alttp` commands) and the Python references with the test files in `test-data/a-link-to-the-past/` (`harness-*`, made in the harness from `tools/alttp_save.py`'s saves) |
| Status | M3: every rule built and checked on the host (ALTTP-1 to ALTTP-10); not yet on the Duo |

What SNES DS does with this game. Each rule is a promise the profile keeps. Change a rule here first when the behaviour is meant to change, then the profile, then the tests. Rule numbers are never reused: a dropped rule stays, marked *retired*. Drafts for the owner's review, all built (ALTTP-4 with the items screen's ring as Link's marker); checked on the host, not yet on the Duo.

GBA DS's page for the GBA version (`docs/reference/gba/zelda-a-link-to-the-past.md`) is a reference only: it captured the zoomed-out world map and the dungeon map on a hidden copy, still, dimmed when unsure.

## Rules (draft)

*Owner's decisions, Nov 22, 2026: the world map is made from the game's own screens, not from the X map's picture; unseen parts are covered with the world map's clouds (light in the Light World, dark in the Dark World); the second screen shows the items screen unless X has brought up the map; the items screen's panes are moved so the items are at the bottom right, for touch.*

**ALTTP-1. Items, or the map.** While Link is played, the second screen shows the items screen (ALTTP-10). X brings up the map instead (the world map outdoors, ALTTP-2; the dungeon's map in a dungeon, ALTTP-5), and X again goes back to the items. X no longer opens the game's own map on the game screen: SNES DS takes the press, as the map now has a screen of its own. *Exception to "play with the buttons" (the owner's choice, Nov 22, 2026).*

**ALTTP-2. The world map, made from what Link has seen.** Outdoors, the map is the world as the game screen has shown it: the ground the game draws (not Link, enemies, the HUD or the rain), kept in the game's own pixels and colours as the camera passes over it, and put together into one picture of each world. It's live: what's on the game screen is on the map, as it is now (a lifted rock, a bombed wall, a cut bush as last seen). Only whole 16 x 16 squares the screen has shown count as seen; the rest is covered with the clouds of the game's world map, light clouds in the Light World and dark ones in the Dark World. The Light and Dark Worlds are kept apart, and each of the game's three files has its own. Nothing is captured on a hidden copy: the ground is read from the game's video memory and palette as it plays. Checked against the game's own picture (`tools/alttp_world.py --check`): the same pixels but for Link, sprites and the HUD.

**ALTTP-3. Where the map rests.** Centred on Link, inside the world's edges. A drag moves it with the finger, smoothly, at the screen's refresh rate (as SM-7); it stays where it was dragged until Link enters another area, then glides back. A tap on the map switches its zoom (the game's own pixels, or halved, or quartered). *(The zoom levels and which one it starts at: owner's choice.)*

**ALTTP-4. Link on the map.** Link's marker is the items screen's selection ring round where he is, blinking with the game's frame counter as the game's own map blinks his head (16 frames shown, 16 hidden). In houses, caves and the areas the map doesn't take (area `$80` and up: the Master Sword's grove, Zora's waterfall), it stays where he was last outdoors. *(The game's world map's Link head would need the game's compressed sprite graphics: later, if the ring isn't enough.)*

**ALTTP-5. The dungeon map in dungeons.** In a dungeon, X brings up that dungeon's map as the game's own dungeon map screen (X in a dungeon) draws it: the frame with MAP or LEVEL n, the floor list with Link pointing at his floor, two floors' rooms. Without the dungeon's map only the rooms (and quarters of rooms) Link has seen are drawn; with it, every room, the unseen ones in the game's darker colour; with the compass, the boss's skull blinks on its room and by its floor until the boss is beaten. Link's room has its brackets and blinking dot, the shown floors light up in the list, as on the game's screen. It's live. Drawn by SNES DS from the cartridge (its tiles decompressed, its tables) and the game's memory, without opening the game's map; checked pixel for pixel against the game's own screen (`tools/alttp_dungeon.py`, `src/alttp_dungeon.c`).

**ALTTP-6. Floors by touch.** On the dungeon map, a tap on a floor in the list shows it at the top (with the floor below it under it; the lowest floor under the one above), as Up and Down do on the game's screen; a swipe down shows the floor above, a swipe up the one below. The map goes back to the game's own choice (Link's floor at the top) when he changes floor.

**ALTTP-7. Indoors outside dungeons.** In houses and caves (`$040C` is `$FF`), X brings up the world map as it was outdoors, with Link's marker at the door he went in by.

**ALTTP-8. What stays when unsure.** While the game is between places or busy (doors and stairs, the screen fading, falling into a hole, the mirror, the flute's flight, warps, cutscenes, the death sequence), the map keeps what it had and nothing new is taken from the screen: no jumps, no half-faded colours (Link's and the area's numbers change at different frames inside a transition, as Super Metroid's did). It takes the screen again once Link is playable and the screen is at full brightness.

**ALTTP-9. Logo at the menus.** At the title, the attract mode, the file screens, Save and Quit, the ending and the credits, the second screen shows the logo, taken from the title screen.

**ALTTP-10. The items screen, drawn and tappable.** The second screen shows the game's own items screen (START's), drawn by SNES DS from the cartridge, its tiles in the game's video memory and Link's items in its memory: the Y items with the selected one ringed, its name, the pendants or crystals, DO (the abilities, boots, gloves, flippers, moon pearl) and the equipment (sword, shield, mail, the dungeon's map, compass and big key, the heart pieces). The panes are moved for touch: the Y items at the bottom right, DO above them, and on the left the equipment, the pendants or crystals and the selected item's name; each pane is drawn as the game draws it. Tapping an item selects it for Y, as moving the ring to it on the game's own screen would, and the game takes it at once (the HUD's item box too). With bottles, tapping the bottle selects it (the first bottle if none was), and tapping it again goes on to the next bottle Link has, as Down does in the game's bottle menu. It's live: a new item appears as Link gets it. START still opens the game's own items screen on the game screen. Checked against the game's own screen: the panes word for word (`tools/alttp_items.py`).

## Exceptions

- ALTTP-1: X is taken by SNES DS (the owner's choice, Nov 22, 2026).
- Touch on the second screen (ALTTP-3, ALTTP-6, ALTTP-10), as for Super Metroid.
- ALTTP-10: selecting an item writes to the game being played (the owner's choice, Nov 22, 2026): only when the player taps an item, between two frames, the bytes the game's own items screen would write and nothing else (see "Selecting an item" below). Off in RetroAchievements hardcore.
- ALTTP-2 keeps a record the game doesn't: SNES DS saves what each file has seen beside the battery save (`<save>.alttp1` to `.alttp3`, its own files, never the game's) and in its save states (the "ALTP" section of SNES DS's block: the file, then the record). Loading a state adds what it had seen to that file's record, once Link is played from that file; nothing seen is taken away.
- Nothing on the game screen is covered: A Link to the Past's HUD has no mini-map.

## Decisions for the owner

1. **Decided (Nov 22, 2026):** the map from the game's own screens (ALTTP-2), the world map's clouds for unseen parts (light / dark), the items screen by default and X for the map (ALTTP-1), the items at the bottom right (ALTTP-10). CLAUDE.md and Philosophy already say "assembled from what the player has seen".
2. **Zoom.** The game's pixel size shows half an area at a time; halved, an area and a bit; quartered, about four areas. Which one it starts at, and whether the halved and quartered maps are averaged (smoother, more like a map) or every second / fourth pixel (sharper, but things vanish).
3. **What "seen" survives.** Seen parts are kept per file slot. Loading an older save state keeps what was seen since (it's what the player knows); erasing a file or starting a new game on it clears it. Is that right?

## How it's found (measured in the harness, Nov 22, 2026)

Measured on a new game started in the harness (file 1, "G"): Link's house and the rain outside, the world map opened outdoors, a door in and out. Super Metroid's lessons apply: values below are as measured, frame by frame where it matters.

- Main module, `$7E0010`: `00` intro, `01` file select, `02` copy, `03` erase, `04` name, `05` loading a file, `06` entering a dungeon or house, `07` indoors (dungeons, houses, caves), `08` / `0A` loading the overworld, `09` / `0B` the overworld (`0B`: special areas), `0E` the interface (see below), `0F` the spotlight closing, `10` opening, `11` falling into a hole, `12` game over, `13` / `16` boss victory, `14` attract mode, `15` the mirror, `17` save and quit, `18` Ganon emerges, `19` the Triforce room, `1A` credits, `1B` the spawn select. Submodule, `$7E0011`. (Names from the disassembly; `00`, `01`, `04`, `05`, `06`, `07`, `09`, `0E`, `0F` seen in the harness.)
- Interface submodules (`$10` = `0E`, then `$11`): `01` the item menu (START), `02` a text box, `03` the dungeon map, `07` the world map, `0A` the flute's menu, `0B` the save menu. The module the game returns to is in `$7E010C`.
- The world map, `$7E0200` while `$10` = `0E` and `$11` = `07`: `00` fading out, `01` loading, `03`, `04` brightening, `05` up and taking input (about 35 frames after pressing X, outside Link's house). X opens it outdoors (also in the opening's rain); X indoors outside a dungeon does nothing (`$10` stays `07`).
- Indoors, `$7E001B`: `00` outdoors, `01` indoors.
- The overworld area, `$7E008A` (2 bytes): `00` to `3F` the Light World (an 8 x 8 grid, row x 8 + column), `40` to `7F` the Dark World, `80` and up the special areas. Measured `2C` outside Link's house. **It reads `00` once Link is indoors** (measured: `2C` until the door's fade, `00` from the frame `$1B` turns `01`).
- Link, `$7E0020` (y) and `$7E0022` (x), 2 bytes each, world pixels: outdoors 0 to 4095 each way, the same in both worlds (Link's house: x `08B8`, y `0B3F`; `x >> 9` and `y >> 9` give the area's column and row). Indoors they are the room's coordinates (Link's house: `0978`, `21D8`).
- The camera outdoors: `$7E00E2` (x) and `$7E00E8` (y), 2 bytes, world pixels, the screen's top left (Link's house: `0832`, `0AE1`, Link `0086`, `005E` from it). It shakes a pixel or two when Link lands from a ledge.
- Dungeon, `$7E040C`: the dungeon's number times 2 (`00` Sewers, `02` Hyrule Castle, `04` Eastern Palace ... `1A` Ganon's Tower), `FF` outside dungeons (Link's house: `FF`). Room, `$7E00A0` (2 bytes; Link's house `0104`; it keeps its old value outdoors). Floor, `$7E00A4` (signed: `00` 1F, `FF` B1; from the disassembly, to measure).
- Entering a house (holding Up into the door from outdoors): `$10` `09`, then `0F` (frame 47), `06` (frame 82), and at frame 85 `$1B` `01`, `$8A` `00` and Link's coordinates in the room's. Coming out is the mirror image; to measure on the owner's states, with an area change and a dungeon's stairs.
- The item menu (START): `$10` `0E`, `$11` `01`; it slides down over the game.

To measure next (needs the owner's states): an overworld area change frame by frame (`$8A`, `$20`/`$22`, `$E2`/`$E8`); the Dark World; a dungeon with and without its map, the compass, stairs; the file slot in play (perhaps cartridge RAM `$701FFE`, which the harness can't read yet).

## The data

From spannerisms/usdasm (GPL-3.0, a disassembly of exactly this ROM; labels and addresses read, no code copied) and snesrev/zelda3 (MIT, a C reimplementation with the game's RAM named; read for meaning, not copied). Addresses are SNES addresses (LoROM: file offset = bank x `$8000` + address - `$8000`).

### The world map (X outdoors)

*(The game's own Mode 7 map isn't what SNES DS shows (ALTTP-2 makes its own from the screens); its clouds are used for what Link hasn't seen, and its data is kept here.)*

- A Mode 7 picture. Its tiles: `$18:C000`, 256 tiles of 8 x 8, a byte a pixel (`$4000` bytes, uncompressed); the game copies them to VRAM's high bytes (`TransferMode7Characters`, `$00:E399`).
- Its tilemap: `WorldMap_LightWorldTilemap`, `$0A:C727`, 4096 bytes: four quadrants of 32 x 32 tiles (top left, top right, bottom left, bottom right), together 64 x 64 tiles, 512 x 512 pixels, at the top left of Mode 7's 128 x 128 tilemap (the rest is tile `$EF`). The Dark World: `WorldMap_DarkWorldTilemap`, `$0A:D727`, 1024 bytes, 32 x 32 tiles laid over the middle (tiles 16 to 47 both ways); the edges are the Light World's tiles in the Dark World's colours.
- Colours: `Palettes_LWMAP`, `$0A:DB27`, 128 colours; `Palettes_DWMAP`, `$0A:DC27`, 128 more; the world is picked by `$8A` bit `$40` (`LoadOverworldMapPalette`).
- **Checked:** with the map open in the harness, the state's VRAM matches the picture `tools/alttp_map.py` builds, word for word (16384 tile bytes, 4096 tilemap entries); its colours match the palette in the ROM.
- **Where the world is on it:** map pixel = 128 + world pixel / 16, both ways. The world's 4096 x 4096 pixels are the map's middle 256 x 256 (the square the Dark World's tilemap replaces); a 512 x 512 area is 32 x 32 map pixels, 4 x 4 tiles. Checked: Link's house (x 2232, y 2879) falls on the house drawn at map pixel (267, 307).
- The game shows it tilted (HDMA tables `WorldMapHDMA_ZoomedOut` / `ZoomedIn`), zoomed in at about 2 x around Link, or zoomed out with the whole world; L and R (or X while zoomed in) switch, the D-pad pans (`WorldMap_PlayerControl`, `$0A:BAE6`; limits `WorldMap_PanBoundaries`). SNES DS draws it flat: a new layout of the game's own picture.
- Sprites on it (`WorldMap_HandleSprites`, `$0A:BF66`): Link's marker (16 x 16, tile `$00`, attributes `$3E`, shown while frame counter `$1A` bit `$10` is set); the goal markers by the story's stage, `$7EF3C7` (`savegame_map_icons_indicator`): seven slots, positions `WorldMapIcon_posx_spr0` ... `spr6` / `posy` (`$0A:BDE4` ...; world pixels, `$FF00` none), tiles and attributes `WorldMapIcon_tile_spr0` ... (`$0A:BEE0` ...), the red X's animation `WorldMap_RedXChars` (`$0A:BF5E`); a pendant or crystal already won is skipped (`$7EF374` pendants, `$7EF37A` crystals); none at all once `$7EF2DB` bit `$20` is set or when the stage's markers belong to the other world. The flute's bird when called. Their tiles are a sprite sheet the game decompresses (`WorldMap_LoadSpriteGFX`, half slot `$10`): to find and decode (the game's graphics compression).
- In special areas (`$8A` `$80` and up) the game places Link from where he entered (`$7EC108` y, `$7EC10A` x).

### The overworld from the game's screens (ALTTP-2)

Measured in the harness, Nov 22, 2026, with test files made by `tools/alttp_save.py` (Link's house by day, the pyramid in the Dark World):

- The PPU as the game sets it for play (from the disassembly; the picture matches): BG1's tilemap at VRAM word `$1000`, BG2's at `$0000`, each 64 x 64 tiles (four 32 x 32 screens: top left, top right, bottom left, bottom right), their tiles at `$2000`, 4 bits a pixel; BG3 (the HUD) at `$6000`, tiles at `$7000`, 2 bits a pixel.
- The overworld's ground is BG2. Its scroll (`$7E00E2` x, `$7E00E8` y) is the camera in world pixels, and the 512 x 512 tilemap is a ring over the world: world pixel (x, y) is at tilemap pixel (x mod 512, y mod 512) while it's near the camera. Picture line y shows world row camera y + y + 1 (as Super Metroid's pause map: BG row 0 is on line 1).
- BG1 is the rain, the Lost Woods' canopy and other overlays, on the sub screen (`TS`, `$7E001D` bit 0) where it isn't ground; by day in the Light World `TM` (`$7E001C`) is `$16` (BG2, BG3, sprites) and BG1 isn't shown at all. `tools/alttp_world.py` takes BG1 only where it's on the main screen.
- Colours: the game's palette buffer, `$7E:C500` (256 words), the colours it gives the PPU (the rain darkens the picture with colour maths, not in the buffer, so the map keeps the day's colours).
- **Checked:** the ground drawn from video memory and the palette buffer matches the game's own picture pixel for pixel below the HUD (98.0 % of the pixels; the rest is Link, a soldier and the HUD, checked by eye).
- When to take it: `$10` `09` or `0B`, `$11` `00`, `$1B` `00`, the screen at full brightness (`INIDISP` copy `$7E0013` `$0F`). Outside that (transitions, menus, text) nothing is taken.
- The clouds: the world map's top 10 tile rows (512 x 80 pixels of the Mode 7 picture, `$18:C000` tiles, `$0A:C727` tilemap), in the Light World's map colours (`$0A:DB27`) or the Dark World's (`$0A:DC27`), repeated, drawn at the shown size so they keep the game's pixels at every zoom.
- What it costs: a world is 4096 x 4096 pixels; kept 2 bytes a pixel (the SNES colour), 512 KB an area, only the areas seen, at most 64 MB for both worlds.

### Seen and visited

- **The game doesn't record which overworld areas Link has visited.** Its per-area bytes, `$7EF280` + area (`save_ow_event_info`), hold `$40` (the area's heart piece or item taken), `$20` (its secret opened: a bombed wall, a moved stone), `$02` and `$01` (one-off events): not visits. So SNES DS keeps its own record (ALTTP-2): 32 x 32 bits a world (one per map tile of the world's square), 256 bytes for both worlds, per file.
- Dungeon rooms are recorded: `$7EF000` + room x 2 (`save_dung_info`, 2 bytes a room). The low 4 bits are the quarters seen (`$8` top left, `$4` top right, `$2` bottom left, `$1` bottom right), as the dungeon map uses them; the room being played keeps its quarters in `$7E0408` until Link leaves it. Bit `$0800` of the boss's room: the boss is beaten.
- The file in play: to find (the slot's offset in cartridge RAM; zelda3 names `$701FFE`). The seen record is keyed by it.

### The dungeon map (X in a dungeon)

- Opens in any dungeon (`$040C` not `$FF`) in any room but `$00` (Ganon's), map or no map (`Module07_00_PlayerControl`).
- Floors: `DungeonMapFloorCountData`, `$0A:F5D9`, 2 bytes a dungeon: the low byte's high nibble the floors from 1F up, its low nibble the basements; the high byte picks the backdrop (`$01`: the mountain stripes; `$02`: the floor list in other colours). E.g. Eastern Palace `$20`: 1F, 2F; Ice Palace `$17`: 1F and B1 to B7; Ganon's Tower `$71`.
- Each floor: 25 room numbers, 5 x 5 (`$0F` no room), the lowest floor first: per-dungeon pointers `DungeonMapRoomPointers`, `$0A:F605` (bank `$0A`). Each room drawn: 2 x 2 tiles from a template (`DungeonMap_RoomTemplates`, `$0A:F009`, 4 tilemap words a template: top left, top right, bottom left, bottom right), the template's number a byte per non-empty cell, in order, from `DungeonMapRoomOrderPointers`, `$0A:FBE4`.
- A quarter is drawn (`DungeonMap_DrawSingleRowOfRooms`, `$0A:E5BC`): seen, its template word; unseen with the map, the word in palette 3 (where the word has bit `$1000`) or the word + `$0400`; unseen without the map, `$0B00` (blank). For Link's room marker, three rooms count as another (137 as 169, 167 as 119, 79 as 190: `DungeonMap_DrawRoomMarkers`, `$0A:E823`).
- The items, a bit a dungeon (`$8000` >> the dungeon's number): map `$7EF368`, compass `$7EF364`, big key `$7EF366`. The compass shows the boss (`DungeonMap_DrawBossIcon`, `$0A:EDE4`: rooms `DungeonMapBossRooms`, `$0A:E807`; floors `DungeonMap_BossRoomFloor`, `$0A:EE79`). The big key isn't on the dungeon map (the item menu shows the three).
- Sprites: Link's head by the current floor (`DungeonMap_DrawLinkPointing`), the blinking room marker at Link's place in the room (`$0A:EB50`; `$22`, `$20` give the place within the room), the floor numbers (`$0A:EC0A`), the blinking floor (`$0A:ECCF`), the boss icon. The "LEVEL n" heading (`$0A:E1A4`).
- Graphics: the game loads a tile set for the map (`Module0E_03_01_00_PrepMapGraphics`, `$0A:E0E4`: main theme 32, sprite set `$80` + the dungeon) and its palettes (`Palette_Load_DungeonMapBG`, `$1B:EE3A`; `..._DungeonMapSprite`, `$1B:EDDD`). The sets are compressed: SNES DS will need the game's graphics decompression (or the tiles taken from VRAM once, see Risks).

### The dungeon map's picture (ALTTP-5)

Built and checked with `tools/alttp_dungeon.py` (word for word and pixel for pixel) and `src/alttp_dungeon.c`:

- Video RAM as the map has it: BG1 and BG2's tilemaps cleared to `$0300` (words `$0000`-`$1FFF`), BG3's to `$007F` (`$6000`-`$67FF`) (`EraseTileMaps`, `$00:8355`); then the frame stripes (`$0E:DA9C`: the wooden frames and MAP on BG3, the grid on BG1, the panes' frames on BG2).
- Stripes (`HandleStripes14`, `$00:92A1`): a big-endian VRAM word address, a big-endian word (bit 15 down a column, bit 14 one word repeated, the rest the byte length less one), the words; until a byte with bit 7 set.
- LEVEL n for the Dark World's dungeons (`$0A:E196`, a byte a dungeon: the numeral x 2, `$FF` for MAP), the floor list's backdrop (`$0A:E1E1`, `$0A:EFD1`; Death Mountain's stripes `$0A:EFDF`) and boxes (`$0A:E2E5`).
- The rooms: two floors drawn into a buffer of 2048 words (the game's at `$7F:0000`), each floor's name (`$0A:E4E9`, B and F tiles `$1F1C`, `$1F1D`), border (`$0A:E429`...`$0A:E445`) and 5 x 5 rooms (`$0A:E56F` the rows), then copied to BG2's top left screen (the top floor at line 0 of the buffer, the one below it 12 rows lower). Which floors: Link's at the top, or (on the lowest floor) the one above it.
- Sprites (`DungeonMap_DrawSprites`, `$0A:EAB2`), at OAM 0 and up (and `$40` up for the floor blinker): checked against the game's sprite buffer (`$7E:0800`, high bits `$7E:0A00`). Link's head is tile 0 of the first name table, loaded from Link's own graphics (`$10:A9C0`, uncompressed, a row of 16 tiles is `$200` bytes); the rest are the map's sprite set at `$5000`. Tables in the doc's tool and `src/alttp_dungeon.c`'s defines.
- The picture: mode 1 with BG3 on top (BGMODE `$09`): BG3 high, sprites 3, BG2 high, sprites 2, BG2 low, sprites 1, BG3 low, sprites 0; BG1 (the grid) is on the sub screen and added only where the main screen shows the backdrop (CGWSEL `$02`, CGADSUB `$20`, the fixed colour black). Picture line y shows BG row y + 1; a sprite with OAM Y starts on picture line Y (a line below BG row Y).
- Palettes: the HUD's second (`$1B:D6A0`, rows 0-1), the map's BG (`$1B:E544`, rows 2-7), the map's sprite colours (`$1B:D70A`, rows 12-14, colours 1-7); the rest the game's (Link's colours for his head).

### The items screen (ALTTP-10)

- The game builds its items screen (START) as a 32 x 32 BG3 tilemap in work RAM at `$7E1000` (`ItemMenu_Initialize`, `$0D:DDAB`) and slides it down (BG3's scroll, `$7E00EA`, to `$FF18`); the screen's rows 5 to 29 are the panes. Its tiles are BG3's (VRAM `$7000`, 2 bits a pixel), loaded for the HUD and always there while playing; its colours the palette buffer's first 32 (`$7E:C500`).
- The panes (column, row; width x height, in tiles): the Y items (1, 5; 19 x 15), the selected item and its name (21, 5; 10 x 6), the pendants or crystals (21, 11; 10 x 9), DO (1, 21; 19 x 9), the equipment (21, 21; 10 x 9).
- From the cartridge: the item icons (`ItemIconPointers`, `$0D:FA93`, bank `$0D`: one table per item kind, 4 tilemap words per level), the Y items' places (`MenuCursorPositions`, `$0D:FAD5`, addresses in the `$7E1000` buffer), the item names (`$0D:F1C9`, 16 words an item, two rows of 8; the bottle's contents `$0D:F449`, powder `$0D:F549`, flute `$0D:F569`, mirror `$0D:F5A9`, bow and arrows `$0D:F5C9`), the pendants' and crystals' windows (`$0D:E860`, `$0D:E914`, 10 x 9 words), DO's words (`$0D:F959`; the gloves' `$0D:F931`), EQUIPMENT and DUNGEON ITEM (`$0D:ED09`, `$0D:ED19`), the heart pieces (`$0D:F911`). The frames, the ring round the selected item and a few marks are constants in the game's code (`$0D:E3D9` and on), written down in `tools/alttp_items.py`.
- From work RAM: the items `$7EF340` to `$7EF35B` (as the save data), the selected bottle `$7EF34F` and the bottles `$7EF35C`, the selected Y item `$7E0202` (1 bow ... 20 mirror), the ring's blink `$7E0207` (shown while bit `$10` is set), pendants `$7EF374`, crystals `$7EF37A`, abilities `$7EF379`, heart pieces `$7EF36B`, the dungeon (`$7E040C`) with its map, compass and big key bits.
- **Checked:** `tools/alttp_items.py --check` builds the same tilemap as the game's, word for word (696 words), in nine dumps: three sets of items (the opening's, the Light World's with pendants, the Dark World's with crystals and bottles), the ring on and off.

### Selecting an item (ALTTP-10)

What the game's own items screen leaves when the ring is moved to an item and the screen closed (`ItemMenu_Close`, `$0D:DFBA`, with `UpdateHUD`), and so what a tap writes, between two frames:

- `$7E0202`: the item (1 to 20).
- `$7E0303`: the item Y uses, from `MenuToEquipID` (`$0D:FA15`, a byte per item); `$7E0304` follows by itself.
- `$7E034B` = `2` and `$7E020B` = `0`, unless the item is a rod (`$0303` `5` or `6`: then `$034B` = `0`), as the closing does.
- The HUD's item box in the HUD buffer: `$7E:C74A`, `C74C`, `C78A`, `C78C`, the item's 4 icon words (the same icons as the items screen's), and `$7E0016` = `1` so the next vertical blank sends the HUD to video RAM.

**Checked:** selecting the hammer this way and with the game's own items screen leaves the same bytes in `$0202`, `$0303`, `$0304`, `$034B`, `$020B`, the HUD buffer (`$C700` to `$C7FF`) and the save data, but for the selected bottle (`$7EF34F`), which the game's screen sets to the first bottle when it opens and Link has bottles but none selected: a tap on the bottle does that too. Y then uses the hammer (seen in the harness).

### Tile sheets (for the dungeon map, ALTTP-5)

- The game's tiles are in compressed sheets of 64 tiles at 3 bits a pixel. Where each is: `GFXSheetPointers`, bank, high and low bytes in three tables: background sheets `$00:CF80`, `$00:D05F`, `$00:D13E`; sprite sheets `$00:CFF3`, `$00:D0D2`, `$00:D1B1`.
- The compression (`$00:E7DE`): a command byte, its top 3 bits the kind and the rest the length less one (or, with `$E0` and up, a 10-bit length over two bytes and the kind in bits 2 to 4): 0 copy, 1 a byte repeated, 2 two bytes alternating, 3 a byte counting up, 4 and up a copy of earlier output (a 16-bit offset, low byte first); `$FF` ends.
- Widening to 4 bits (`Do3To4Low`, `$00:E63C`; `Do3To4High`, `$00:E5AF`): each tile's 16 bytes of planes 0 and 1, then plane 2 with plane 3 empty, or (high) plane 3 set wherever any of the others is, so the tile uses its palette's upper 8 colours. Which BG slots are high depends on the tileset (main tileset `$20` and up: the slots at VRAM `$2000`, `$3000`, `$3400`, `$3800`; below: the upper four); sprite sheets `$52`, `$53`, `$5A`, `$5B`, `$5C`, `$5E`, `$5F` are high.
- Tilesets (`InitializeTilesets`, `$00:E19B`): main tileset `$0AA1` (`$00:E073`, 8 sheets: BG VRAM `$2000` up, a sheet per `$400` words), aux `$0AA2` (`$00:DD97`, 4 sheets for the middle four slots, 0 keeps the main one's), sprites `$0AA3` (`$00:DB57`, 4 sheets at `$5000` up).
- The dungeon map loads main `$20`, aux `$40`, sprites `$80` + the dungeon's number (`$0A:E0E4`). **Checked:** `tools/alttp_gfx.py --check` builds exactly the dungeon map's tiles in video RAM (16384 bytes of BG tiles, 8192 of sprite tiles, Eastern Palace) and the overworld's BG tiles; in a dungeon while playing, all but the animated tiles' two slots.
- The dungeon map's BG3 (its frames, MAP) uses the HUD's tiles, always in video RAM while playing. Its palettes are its own (`Palette_Load_DungeonMapBG`, `$1B:EE3A`; `Palette_Load_DungeonMapSprite`, `$1B:EDDD`; the HUD's second palette).

### The HUD

- No mini-map. Nothing on the game screen is covered.
- The item menu (START) shows the map, compass and big key of the dungeon Link is in.

## The plan

Done (Nov 22, 2026, on the host):

1. Test files and states made in the harness (`tools/alttp_save.py`; `test-data/a-link-to-the-past/harness-*`), and dungeons entered through the game's own entrance loading (`$010E` the entrance, `$10` = `0F`, `$010C` = `06`: test only).
2. The items screen and item selection: `tools/alttp_items.py`, then `src/alttp.c`; word for word, pixel for pixel, and the same RAM as the game's own screen leaves.
3. The world map from the game's screens: `tools/alttp_world.py`, then `src/alttp.c`; pixel for pixel; the core patch for video RAM.
4. The dungeon map: `tools/alttp_gfx.py` (tiles), `tools/alttp_dungeon.py` (tilemaps, sprites, picture), then `src/alttp_dungeon.c`; pixel for pixel in 40 frames of 5 dungeons, the tilemaps and sprites in 74 dumps of 12.
5. `game.c`: the profile's `native`, X, taps, drags, zooms, floors, the seen record, the logo. Headless runs with the Duo's two screens.

Next:

6. A test build on the Duo (the owner): what to check is in "On the device" below.
7. The owner's choices: the zoom levels and the one the map starts at; whether the ring is enough as Link's marker; what the seen record should survive.
8. *(Done: the record in SNES DS's states, merged on loading: checked headless, a state brings its map to a game with no record. Each 512 x 512 piece is kept compressed and compressed again only when it has changed: with both worlds' 128 areas filled (host, synthetic), the record is 2.7 MB, the first save 583 ms, a save with nothing new 3 ms, after one new screen 6 ms, loading 75 ms.)*
9. The owner's call: a state carries the whole record (up to a few MB once both worlds are explored, on top of the game's 290 KB); if that's too much, states could carry only what changed since the file's record was saved.

### On the device (what to check)

- The items screen on the top screen; a tap on an item selects it (the HUD's box changes, Y uses it); the bottle tapped again goes to the next bottle.
- X: the map; X again: the items. Outdoors the map grows as you walk, the rest under clouds (light in the Light World, dark in the Dark World); drag it, tap it for the zooms; walk into another area and it comes back to Link.
- In a dungeon X shows the dungeon map; tap a floor in the list, swipe up and down.
- Leave the game and come back: what was seen is still there (the file `<battery save>.alttp1` and so on).
- Smoothness: the map dragging at the screen's rate; the game's speed unchanged.

### Risks

- **Transitions:** `$8A` is `00` indoors and the coordinates jump to the room's; area changes and the mirror move things at different frames. ALTTP-8 freezes the picture from the module (`$10` not `07` / `09` / `0B`, `$11` not `00`); measure first, as for Super Metroid's doors.
- **The seen record lives outside the game:** a save made on another emulator, a copied or erased file, or a state from before the record existed all start dark (or wrong). Mitigation: per file slot, cleared when a new file starts, kept in states too.
- **Video RAM from the core:** the world map reads the game's video RAM, which bsnes gives only with its fast PPU (the default; `bsnes-memory.patch`). With the accurate PPU (a core option, or the few games bsnes forces to it) the world map stays empty; the items screen too (its tiles are there).
- **The record's size:** a few MB once both worlds are explored (2.7 MB in the synthetic test), in its file and in each of SNES DS's states.
- **Special areas:** the map doesn't take the ground in areas `$80` and up (the Master Sword's grove, Zora's waterfall), whose coordinates aren't the world's; not checked in the harness.
- **Other releases:** Japan (three revisions) and Europe have other addresses; USA only at first.
- **Randomizers and hacks** move things; the profile is for the clean ROM's CRC only.

### Test states

Made in the harness (the owner had none), in `test-data/a-link-to-the-past/`, read-only: `harness-castle.srm`, `harness-pendants.srm`, `harness-dark.srm` (battery saves from `tools/alttp_save.py`); states `harness-castle-house`, `harness-pendants-house`, `harness-pendants-outside` (Link free outside his house, the Light World's items), `harness-lw-day-outside`, `harness-dw-pyramid`, `harness-dw-pyramid-free`, `harness-eastern-palace-entrance`, `harness-eastern-palace-dungeon-map-open` (and `-with-map-compass`), `harness-ice-palace-entrance`, `harness-opening-rain-outside`, `harness-opening-worldmap-open`. States from the Duo are still welcome, especially after real play (a long walk, the dungeons as they were played).

## Not covered yet

- The Duo: nothing has run there yet.
- Europe and Japan releases.
- Special areas (`$80` and up), the mirror and the flute's flight, the ending and the credits: only what the rules say, not checked.
- The seen record's size and saving time with both worlds well explored (the host only saw a few areas).
