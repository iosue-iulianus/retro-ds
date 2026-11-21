# Custom asset mode (design, not built yet)

Some games get a user-supplied image with a live position marker on the
second screen instead of a captured screen. First case: A Link to the Past's
overworld, whose X map is a Mode 7 zoom that captures badly. This is the
standing exception in `wiki/Philosophy.md` (rule 6); the rules sessions
follow are in `CLAUDE.md`. Scheduled after M0.

## Behaviour

- A profile opts in with an `.asset` section: manifest path, coordinate
  addresses, when-conditions. While its conditions match it takes priority;
  otherwise the game falls back to map capture, then the logo.
- The position comes from the live game's RAM every frame (read only). No
  hidden copy is needed, and hidden-copy captures are skipped while the
  asset is showing.
- The marker hides during menus, transitions and cutscenes, from a reliable
  "in normal gameplay" check (to be found per game).

### A Link to the Past's overworld

- 8x8 areas of 512x512 px per world, so Link's absolute coordinates should
  map 1:1 to a 4096x4096 image per world. **Unverified:** check it.
- Candidate addresses, **unverified** (confirm with `tools/ramdiff.py` and a
  disassembly before use): Link Y `$7E0020`, Link X `$7E0022` (16-bit),
  overworld area `$7E008A` (`0x40` and up: Dark World), indoors flag
  `$7E001B`.
- The game doesn't record visited overworld screens, so there's no fog of
  war in v1.

## Asset format

- **Image:** lossless PNG (indexed allowed) or lossless WebP, one per world,
  1 image pixel = 1 game pixel, no baked-in upscaling. JPEG and lossy WebP
  are rejected.
- **Location:** `assets/user/<game>/`, gitignored, never committed or
  shipped. A missing or invalid asset is logged and the game silently falls
  back to capture.
- **Manifest:** a JSON file beside each image:

```json
{
  "image": "lightworld.png",
  "world": 0,
  "transform": {
    "points": [
      { "game": [0, 0],       "image": [16, 16] },
      { "game": [4096, 4096], "image": [4112, 4112] }
    ]
  },
  "marker": { "image": "link.png", "hotspot": [8, 12] },
  "overlays": [
    { "image": "hints.png", "label": "Heart pieces", "default": false }
  ]
}
```

- `transform.points`: 2 points give scale and offset; 3 points a full affine
  transform (hand-drawn or stylised maps, borders, both worlds in one image).
  Degenerate point sets (coincident points, or 3 collinear ones) are rejected
  with a clear log message.
- `marker`: a small PNG; `hotspot` is the pixel placed on the player's
  position.
- `overlays`: optional transparent PNGs in the same image space, each
  switched on or off from the pause menu, remembered per game.

## Rendering

- The top screen's native resolution, nearest neighbour only.
- A setting chooses between the whole world fitted to the screen and a pan
  window centred on the player.
- The renderer's max texture size comes from `SDL_GetRendererInfo`; an
  image bigger than that is split into 512x512 tiles at load.
- Images load with stb_image and libwebp (already in the tree).

## Tools

- A harness command renders a manifest at a given game coordinate to PNG,
  so transforms can be checked without the device.
