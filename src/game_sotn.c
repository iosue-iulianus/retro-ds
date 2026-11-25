/* Symphony of the Night's second screen in a play session (SOTN-2 to 4,
 * docs/games/symphony-of-the-night.md): the castle map as the game draws
 * it, in the status screen's window, with the logo and a ROOMS pane;
 * src/sotn.c reads and composes it. */
#include "game_screens.h"
#include "sotn.h"

#include <stdlib.h>
#include <string.h>

bool sot;          /* this game is Symphony of the Night */
bool sotShown;     /* the map is up (else the logo, SOTN-1) */
static SotnView view;
static SotnLayout layout;
static uint32_t viewCrc;
static bool composed;  /* `pic` holds a composition (and the texture is stale if dirty) */
static bool dirty;
static uint32_t* pic;  /* the composition, picW x picH of the game's pixels */
static int cw, ch, scale; /* its size, and screen pixels to one of its */
static int curFrame;   /* the map cursor's animation (SOTN-3) */
static SDL_Texture* picTex[2];
static SDL_Texture* curTex[2];
static uint32_t cursorPx[32 * 32];
static bool cursorDirty;

/* the second screen: the screen the game isn't on */
static int map_screen(void) {
	return nScreens == 2 ? 1 - game_screen() : 0;
}

void sot_open(const char* rom) {
	sot_free();
	sot = sotn_is(emu.gameCode);
	if (sot) fprintf(stderr, "second screen: drawn by RetroDS (Symphony of the Night's map)\n");
}

void sot_free(void) {
	ui_free_image(picTex);
	ui_free_image(curTex);
	free(pic);
	pic = NULL;
	sot = sotShown = composed = dirty = false;
	viewCrc = 0;
	cw = ch = 0;
}

void sot_textures(void) {
	ui_free_image(picTex);
	ui_free_image(curTex);
	dirty = cursorDirty = composed;
}

/* the composition's size for the second screen: as many of the game's
 * pixels as fit at a whole number of screen pixels each, the castle's 190
 * lines and a little room round them at the most */
static void fit(int* w, int* h, int* k) {
	SDL_Rect r = screens[map_screen()].rect;
	int s = r.h / (SOTN_MAP_H + 26);
	if (s < 1) s = 1;
	*k = s, *w = r.w / s, *h = r.h / s;
}

/* After a frame, the core idle: read the game, compose again if anything
 * shown has changed. */
void sot_tick(void) {
	if (!sot) return;
	++curFrame;
	if (!sotn_read(&emu, &view)) {
		sotShown = false;
		return;
	}
	/* the map while playing in the castle; the title, the file screens,
	 * the prologue: the logo; loading and the like: as it was */
	if (view.state == 1 || view.state == 8 || view.state == 3 || view.state == 7) sotShown = false;
	else if (view.state == 2) sotShown = sotn_has_map(&view);
	if (!sotShown || view.state != 2) return;
	int w, h, k;
	fit(&w, &h, &k);
	uint32_t c = sotn_view_crc(&view);
	if (composed && c == viewCrc && w == cw && h == ch) return;
	if (w != cw || h != ch) {
		free(pic);
		pic = malloc((size_t) w * h * 4);
		ui_free_image(picTex);
		if (!pic) return;
		cw = w, ch = h;
	}
	scale = k;
	sotn_compose(&view, pic, cw, ch, &layout);
	sotn_cursor_pixels(&view, cursorPx);
	viewCrc = c;
	composed = dirty = cursorDirty = true;
	if (debug) fprintf(stderr, "frame %llu: sotn map composed, %dx%d at %d, rooms %d\n", (unsigned long long) frameCount, cw, ch, k, view.rooms);
}

bool sot_map_shown(void) {
	return sot && sotShown && composed;
}

void draw_sot(int s) {
	SDL_Rect r = screens[s].rect;
	if (!pic || !composed) return;
	for (int i = 0; i < nViews; ++i) {
		if (!picTex[i]) {
			picTex[i] = SDL_CreateTexture(views[i].ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, cw, ch);
			if (picTex[i]) SDL_SetTextureBlendMode(picTex[i], SDL_BLENDMODE_NONE);
			dirty = true;
		}
		if (!curTex[i]) {
			curTex[i] = SDL_CreateTexture(views[i].ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, 32, 32);
			if (curTex[i]) {
				SDL_SetTextureBlendMode(curTex[i], SDL_BLENDMODE_BLEND);
				SDL_SetTextureAlphaMod(curTex[i], 128); /* (half and half: the game's) */
			}
			cursorDirty = true;
		}
	}
	if (dirty) {
		for (int i = 0; i < nViews; ++i) {
			if (picTex[i]) ui_update_texture(picTex[i], pic, cw * 4);
		}
		dirty = false;
	}
	if (cursorDirty) {
		for (int i = 0; i < nViews; ++i) {
			if (curTex[i]) ui_update_texture(curTex[i], cursorPx, 32 * 4);
		}
		cursorDirty = false;
	}
	int ox = (r.w - cw * scale) / 2, oy = (r.h - ch * scale) / 2;
	ui_image_part(s, picTex, (SDL_Rect) { 0, 0, cw, ch }, (SDL_Rect) { ox, oy, cw * scale, ch * scale });
	/* SOTN-3: the game's map cursor closing onto Alucard's square, clipped
	 * to the map's window */
	int cx, cy, cs;
	sotn_cursor_rect(&view, &layout, curFrame & 63, &cx, &cy, &cs);
	SDL_Rect clip = { r.x + ox + 5 * scale, r.y + oy + 5 * scale, (cw - 10) * scale, (ch - 10) * scale };
	ui_clip(s, &clip);
	ui_image_part(s, curTex, (SDL_Rect) { 0, 0, 32, 32 }, (SDL_Rect) { ox + cx * scale, oy + cy * scale, cs * scale, cs * scale });
	ui_clip(s, NULL);
	/* SOTN-4: the title's logo, top right */
	if (layout.logoW > 0) draw_logo_in(s, (SDL_Rect) { ox + (int) (layout.logoX * scale), oy + (int) (layout.logoY * scale), (int) (layout.logoW * scale),
	                                                  (int) (layout.logoH * scale) });
}
