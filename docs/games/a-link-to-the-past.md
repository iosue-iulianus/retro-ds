# The Legend of Zelda: A Link to the Past

| | |
|---|---|
| Releases | USA (this page); Japan 1.0 to 1.2 and Europe have their own ROMs and addresses |
| Dumps | `777AAC2F` "Legend of Zelda, The - A Link to the Past (USA)" (MD5 `608c22b8...`, the ROM spannerisms/usdasm assembles to) |
| Profile | none yet |
| Tests | by hand: the harness with the owner's states in `test-data/a-link-to-the-past/` (none yet: see "Test states needed") |
| Status | researched, draft rules (M3); nothing built |

What SNES DS is to do with this game. Each rule is a promise the profile will keep. Change a rule here first when the behaviour is meant to change, then the profile, then the tests. Rule numbers are never reused: a dropped rule stays, marked *retired*. These are drafts for the owner's review: nothing below is built yet.

GBA DS's page for the GBA version (`docs/reference/gba/zelda-a-link-to-the-past.md`) is a reference only: it captured the zoomed-out world map and the dungeon map on a hidden copy, still, dimmed when unsure.

## Rules (draft)

**ALTTP-1. The world map outdoors.** While Link is outdoors, in the Light World or the Dark World, the second screen shows the game's own world map of the world he's in: the picture the X button's map shows, drawn by SNES DS from the cartridge, flat instead of tilted. Nothing is captured and no hidden copy runs. Checked against the game's own map: its tiles, tilemap and colours, word for word (`tools/alttp_map.py --check`).

**ALTTP-2. What Link has seen.** The map shows the places Link has seen and keeps the rest dark. A part of the map counts as seen once it has been on the game screen outdoors (a tile of the map, 8 x 8 of its pixels, is 128 x 128 pixels of the world: crossing an area reveals the strip the camera showed, not the whole area). Seen parts stay seen. The Light and Dark Worlds are kept apart, and each of the game's three files has its own. The clouds round the world are always shown. How unseen parts look is the owner's choice (see "Decisions for the owner": dimmed, or covered with the map's own sky).

**ALTTP-3. Where the map rests.** As the game's map opens: zoomed in, centred on Link, held inside the map's edges. A drag moves it with the finger, smoothly, at the screen's refresh rate (as SM-7); it stays where it was dragged until Link enters another area, then glides back. A tap switches between the zoomed-in map and the whole world, as L and R do on the game's map. *(Zoom level and gesture: owner's choice.)*

**ALTTP-4. Link and the goals on the map.** Link's marker (the game's own, from its map's sprites) sits where he is and blinks as on the game's map (16 frames shown, 16 hidden). In the areas the game's map can't show (the Master Sword's grove, Zora's waterfall and the like: area `$80` and up), it stays where he entered them, as the game's does. The goal markers the game's map shows at this point of the story (the red X, the pendants, the crystals) are shown where and when the game's map would show them, animated as the game animates them.

**ALTTP-5. The dungeon map in dungeons.** In a dungeon, the second screen shows that dungeon's map as the game's own dungeon map screen (X in a dungeon) draws it: the floor list, the floor Link is on, the rooms. Without the dungeon's map only the rooms (and quarters of rooms) Link has seen are drawn; with it, every room, the unseen ones in the game's paler colour; with the compass, the boss's room blinks until the boss is beaten. Link's room blinks. It's live: a room appears as Link enters it. Drawn by SNES DS from the cartridge and the game's memory; checked pixel for pixel against the game's own screen.

**ALTTP-6. Floors by touch.** On the dungeon map, a tap on a floor in the list (or a vertical drag) shows that floor, as Up and Down do on the game's screen. The map goes back to Link's floor when he changes floor. *(Owner's choice: tap, drag or both.)*

**ALTTP-7. Indoors outside dungeons.** In houses and caves (not a dungeon: `$040C` is `$FF`), where the game's X does nothing, the world map stays as it was outdoors, with Link's marker at the door he went in by. *(Alternative for the owner: dim it as GBA DS did.)*

**ALTTP-8. What stays when unsure.** While the game is between places or busy (doors and stairs, the screen fading, falling into a hole, the mirror, the flute's flight, warps, text boxes, the item menu, the game's own maps, cutscenes, the death sequence), the second screen keeps the last map it showed, unchanged: no jumps (Link's and the area's numbers change at different frames inside a transition, as Super Metroid's did). It changes again once Link is playable.

**ALTTP-9. Logo at the menus.** Until the first map is shown, and at the title, the attract mode, the file screens, Save and Quit, the ending and the credits, the second screen shows the logo, taken from the title screen.

## Exceptions

- Touch on the second screen (ALTTP-3, ALTTP-6), as for Super Metroid.
- ALTTP-2 keeps a record the game doesn't: SNES DS saves which parts of the world each file has seen, beside the battery save (its own file, never the game's).
- No writes to the game are planned. Nothing on the second screen changes the game: switching floors or worlds only changes what's shown.
- Nothing on the game screen is covered: A Link to the Past's HUD has no mini-map.

## Decisions for the owner

1. **The world map's source.** The recommendation is the game's own world map art (ALTTP-1), revealed as Link sees each place (ALTTP-2). It is the game's map, in its own style, small (256 x 256 for the world's square), exact and cheap. The alternative CLAUDE.md names, a map assembled from the screens themselves (real 16 x 16 tiles, 4096 x 4096 pixels a world), needs the overworld's compressed map data, every area's graphics set and palette, and animated tiles; on a 256 x 224 screen at the game's pixel size it shows a quarter of one area, much what the game screen already shows; shrunk to fit it no longer looks like the game. It could come later as a third zoom level. If the owner agrees, CLAUDE.md's "assembled from the live game's own pictures" and Philosophy's section 6 are reworded to "the game's own map, revealed as the player sees it".
2. **How unseen parts look.** Mock-ups: `tools/alttp_map.py ROM OUT.png --crop --fog dim|sky --seen ...` (not committed: they're the game's art). Dimmed to a quarter (the game dims its own map this way in the rain) or covered with the map's plain sky tile. A third way: the cloud tiles round the map.
3. **Zoom.** Rest zoomed in (the game's map at 2 x, about 4 x 3.5 areas in view) and tap for the whole world, or the other way round.
4. **What "seen" survives.** Seen parts are kept per file slot. Loading an older save state keeps what was seen since (it's what the player knows); erasing a file or starting a new game on it clears it. Is that right?
5. **Indoors** (ALTTP-7): the world map as it was, or dimmed.

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

- A Mode 7 picture. Its tiles: `$18:C000`, 256 tiles of 8 x 8, a byte a pixel (`$4000` bytes, uncompressed); the game copies them to VRAM's high bytes (`TransferMode7Characters`, `$00:E399`).
- Its tilemap: `WorldMap_LightWorldTilemap`, `$0A:C727`, 4096 bytes: four quadrants of 32 x 32 tiles (top left, top right, bottom left, bottom right), together 64 x 64 tiles, 512 x 512 pixels, at the top left of Mode 7's 128 x 128 tilemap (the rest is tile `$EF`). The Dark World: `WorldMap_DarkWorldTilemap`, `$0A:D727`, 1024 bytes, 32 x 32 tiles laid over the middle (tiles 16 to 47 both ways); the edges are the Light World's tiles in the Dark World's colours.
- Colours: `Palettes_LWMAP`, `$0A:DB27`, 128 colours; `Palettes_DWMAP`, `$0A:DC27`, 128 more; the world is picked by `$8A` bit `$40` (`LoadOverworldMapPalette`).
- **Checked:** with the map open in the harness, the state's VRAM matches the picture `tools/alttp_map.py` builds, word for word (16384 tile bytes, 4096 tilemap entries); its colours match the palette in the ROM.
- **Where the world is on it:** map pixel = 128 + world pixel / 16, both ways. The world's 4096 x 4096 pixels are the map's middle 256 x 256 (the square the Dark World's tilemap replaces); a 512 x 512 area is 32 x 32 map pixels, 4 x 4 tiles. Checked: Link's house (x 2232, y 2879) falls on the house drawn at map pixel (267, 307).
- The game shows it tilted (HDMA tables `WorldMapHDMA_ZoomedOut` / `ZoomedIn`), zoomed in at about 2 x around Link, or zoomed out with the whole world; L and R (or X while zoomed in) switch, the D-pad pans (`WorldMap_PlayerControl`, `$0A:BAE6`; limits `WorldMap_PanBoundaries`). SNES DS draws it flat: a new layout of the game's own picture.
- Sprites on it (`WorldMap_HandleSprites`, `$0A:BF66`): Link's marker (16 x 16, tile `$00`, attributes `$3E`, shown while frame counter `$1A` bit `$10` is set); the goal markers by the story's stage, `$7EF3C7` (`savegame_map_icons_indicator`): seven slots, positions `WorldMapIcon_posx_spr0` ... `spr6` / `posy` (`$0A:BDE4` ...; world pixels, `$FF00` none), tiles and attributes `WorldMapIcon_tile_spr0` ... (`$0A:BEE0` ...), the red X's animation `WorldMap_RedXChars` (`$0A:BF5E`); a pendant or crystal already won is skipped (`$7EF374` pendants, `$7EF37A` crystals); none at all once `$7EF2DB` bit `$20` is set or when the stage's markers belong to the other world. The flute's bird when called. Their tiles are a sprite sheet the game decompresses (`WorldMap_LoadSpriteGFX`, half slot `$10`): to find and decode (the game's graphics compression).
- In special areas (`$8A` `$80` and up) the game places Link from where he entered (`$7EC108` y, `$7EC10A` x).

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

### The HUD

- No mini-map. Nothing on the game screen is covered.
- The item menu (START) shows the map, compass and big key of the dungeon Link is in.

## The plan

Steps, each small enough to test, rule first:

1. **States from the owner** (below), pulled into `test-data/a-link-to-the-past/`, read-only.
2. **Measure** on them: an area change frame by frame, Dark World, stairs, the dungeon map; find the file slot; the world map's sprite sheet and the dungeon map's tile set in VRAM (from the states, as `--check` finds Mode 7's), then where they come from in the ROM.
3. **`tools/alttp_map.py`** grows: Link's marker and the goal markers, checked against the game's map in the harness where the tilt allows (the zoomed-in map's middle line is close to flat) or through VRAM and OAM in the state; the dungeon map (BG tilemap rebuilt from ROM and RAM, compared word for word with the game's, as SM-10 was), then its sprites, pixel for pixel against the game's screen.
4. **The graphics decompression** (the game's LZ format) in Python, checked against VRAM, then in C.
5. **`src/alttpmap.c`** (like `smmap.c`): the world map layer (512 x 512, both worlds), the seen record (per file, its own file beside the battery save, and SNES DS's state block), Link's marker; the harness's `alttpmap NAME [world|dungeon]`, pixel-exact against the Python renderer.
6. **`game.c`**: the profile's `native` for this game: which screen when (ALTTP-1, 5, 7, 8, 9), the frozen picture over transitions, drag, glide and tap (reusing Super Metroid's), the logo steps. A test build; what to check on the Duo.
7. **The dungeon map** in C, then floors by touch (ALTTP-6). A test build.
8. Rules final, CLAUDE.md and Philosophy reworded if decision 1 goes that way.

### Risks

- **Transitions:** `$8A` is `00` indoors and the coordinates jump to the room's; area changes and the mirror move things at different frames. ALTTP-8 freezes the picture from the module (`$10` not `07` / `09` / `0B`, `$11` not `00`); measure first, as for Super Metroid's doors.
- **The seen record lives outside the game:** a save made on another emulator, a copied or erased file, or a state from before the record existed all start dark (or wrong). Mitigation: per file slot, cleared when a new file starts, kept in states too.
- **Compressed graphics:** the markers and the dungeon map's tiles are compressed in the ROM. The format is well known and small to decode, but it's new code to check. A fallback: copy the tiles from the live game's VRAM when the player opens a map (needs the core to expose VRAM, as `bsnes-memory.patch` does for work RAM).
- **The tilt:** the flat map can't be compared with the game's picture pixel for pixel (Mode 7 with HDMA); it's checked through VRAM and CGRAM instead, and the marker's placement by the world-to-map rule above.
- **Other releases:** Japan (three revisions) and Europe have other addresses; USA only at first.
- **Randomizers and hacks** move things; the profile is for the clean ROM's CRC only.

### Test states needed

On the Duo, saved as states (slot or auto), with the battery save as it is then:

1. **Light World, outdoors**, after the opening (Link has the sword; any area), standing still. Better two: one near an area's edge to walk across.
2. **Dark World, outdoors**, any area.
3. **A special area** (the Master Sword's grove or Zora's waterfall), if handy.
4. **Inside a house or cave** (not a dungeon), just inside the door.
5. **A dungeon without its map**, a few rooms in (Eastern Palace is ideal), near stairs if possible.
6. **The same dungeon with its map** (and, if you get it, the compass): a second state after opening the map's chest.
7. **The pause screen (item menu, START)** open in a dungeon.
8. **The game's own maps open:** the world map (X outdoors, Light and Dark if possible) and the dungeon map (X in the dungeon).

The harness made its own scratch states for the measurements above (a new game to the rain outside Link's house); they aren't the owner's and aren't kept.

## Not covered yet

- Everything in the plan: nothing is built.
- Europe and Japan releases.
- The ending, the credits and the attract mode's map scene.
