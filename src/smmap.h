/* Super Metroid's second screen, drawn by SNES DS (docs/games/super-metroid.md,
 * SM-8 and on): the pause screen's map as the game draws it, from the
 * cartridge and the game's RAM, without running the game for it.
 *
 * Pictures are 0xAABBGGRR (SDL_PIXELFORMAT_ABGR8888). Three of them:
 * - the chrome (SMMAP_W x SMMAP_H): the frame, the area's name, the grid
 *   inside the frame and the plate to the other page, as on the game's
 *   screen from line SMMAP_Y0 down;
 * - the layer (SMMAP_LW x SMMAP_LH): the area's whole map with its icons,
 *   in map pixels, transparent where there's nothing, shown through the
 *   chrome's window at a view (smmap_place);
 * - the marker: Samus's position, blinking, drawn over the layer;
 * - the chrome's pixels that cover the map (smmap_over), drawn last.
 * Views are the game's own (its BG1 scroll, $B1 and $B3). */
#ifndef SNESDS_SMMAP_H
#define SNESDS_SMMAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SMMAP_W 256
#define SMMAP_H 178
#define SMMAP_Y0 38 /* the chrome's first line on the game's screen */
#define SMMAP_LW 512
#define SMMAP_LH 256
#define SMMAP_MARKER 32 /* the marker's picture, its origin in the middle */

/* The map's window inside the frame, in chrome pixels */
#define SMMAP_WIN_X 6
#define SMMAP_WIN_Y 8
#define SMMAP_WIN_W 244
#define SMMAP_WIN_H 146

typedef struct SmMap SmMap;

/* rom: the game as played (no copier header), taken over (freed by
 * smmap_free). NULL if it isn't a ROM this knows. */
SmMap* smmap_new(uint8_t* rom, size_t size);
void smmap_free(SmMap* m);

/* What changed in smmap_update */
enum { SMMAP_CHROME = 1, SMMAP_LAYER = 2, SMMAP_MARKER_CHANGED = 4, SMMAP_SQUARE = 8, SMMAP_PAGE = 16 };
/* Once per emulated frame, with the game's 128 KB of work RAM: reads where
 * Samus is and what she's explored, and moves the blinking on a frame. */
int smmap_update(SmMap* m, const uint8_t* wram);

/* The map can be shown: Samus is played (doors, elevators and pausing
 * included), and not on Ceres Station (SM-8). */
bool smmap_live(const SmMap* m);
/* The title, file select or options screens (SM-4: the logo) */
bool smmap_in_menu(const SmMap* m);

const uint32_t* smmap_chrome(const SmMap* m);
/* The chrome's pixels that cover the map (the frame's inner edge),
 * transparent elsewhere: drawn over the layer and the marker */
const uint32_t* smmap_over(const SmMap* m);
const uint32_t* smmap_layer(const SmMap* m);
/* The marker's picture now (SMMAP_MARKER square) */
const uint32_t* smmap_marker(const SmMap* m);
/* All three of the marker's pictures, one under the other (SMMAP_MARKER
 * wide, 3 x SMMAP_MARKER high), and which one shows now */
const uint32_t* smmap_markers(const SmMap* m);
int smmap_marker_frame(const SmMap* m);

/* The view the game's pause screen would open on (SM-9) */
void smmap_home(const SmMap* m, int* vx, int* vy);
/* How far the game's pause screen lets the view scroll (SM-7) */
void smmap_limits(const SmMap* m, int* x0, int* x1, int* y0, int* y1);

/* Where things go in the chrome at view (vx, vy): the layer's top left,
 * and the marker picture's */
void smmap_place(const SmMap* m, int vx, int vy, int* lx, int* ly, int* mx, int* my);

/* The equipment page (SM-10), SMMAP_W x SMMAP_H like the chrome: the
 * pause screen's SAMUS page as the game draws it from Samus's equipment,
 * with the MAP plate to go back */
const uint32_t* smmap_page(const SmMap* m);

/* What's under a tap at (x, y), chrome pixels: on the map (page 0), 1 for
 * the SAMUS plate; on the equipment page (page 1), 1 for the MAP plate,
 * 2 for an item (its category and index set), else 0 */
int smmap_hit(const SmMap* m, int page, int x, int y, int* category, int* index);

/* The equipment page's items (SM-10): a category (1 beams, 2 suit and
 * misc., 3 boots, as the game numbers them) and its place in the list */
enum { SMMAP_BEAMS = 1, SMMAP_SUITS = 2, SMMAP_BOOTS = 3 };
/* One byte to write to the game's work RAM (an offset in its 128 KB) */
typedef struct {
	uint32_t addr;
	uint8_t val;
} SmWrite;
/* Switching an item on or off, as the game's equipment screen does
 * (EquipmentScreen_Main_ButtonResponse, $82:B568; equipping Spazer
 * unequips Plasma and the other way round) and then what unpausing
 * refreshes (the suit's colours, $91:DEBA, $90:ECB6; the beam's tiles
 * through the game's own VRAM queue and its colours, $90:AC8D). Fills
 * out (at most max) and returns how many; 0 if the item isn't collected
 * or can't be switched (Hyper Beam). wram: the RAM now, unchanged. To
 * be written between two frames. */
int smmap_toggle(const SmMap* m, const uint8_t* wram, int category, int index, SmWrite* out, int max);

/* Tests: the blinking at a given point (the marker's frame 0 to 2, the
 * ring's palette step 0 to 13), to match a shot of the game */
void smmap_set_blink(SmMap* m, int markerFrame, int ringFrame);

/* The whole picture at a view, as the second screen shows it (for tests
 * and screenshots): out is SMMAP_W x SMMAP_H */
void smmap_compose(const SmMap* m, int vx, int vy, uint32_t* out);

#endif
