/* Super Metroid's second screen in a play session (game.c): the map and
 * the SAMUS page drawn by RetroDS (src/smmap.c), scrolled and tapped.
 * Moved out of game.c as it was (Nov 24, 2026). */
#include "game_screens.h"
#include "android.h"
#include "i18n.h"
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Super Metroid's map, drawn by RetroDS (src/smmap.c; SM-7 to SM-9)
 * The chrome (frame, name, plate) is a picture like a captured map; the
 * map's layer and the marker are textures of their own, placed at the
 * view in screen pixels, so it scrolls smoothly at the display's rate. */
SmMap* nat;
const MapProfile* natProf;
bool natShown;  /* the second screen shows it (else the logo, SM-4) */
static int natDirty;   /* pictures to upload again (SMMAP_CHROME, SMMAP_LAYER) */
static SDL_Texture *natChromeTex[2], *natOverTex[2], *natLayerTex[2], *natMarkerTex[2], *natPageTex[2];
static int natPage; /* 0 the map, 1 the equipment page (SM-10) */
/* The view shown (the game's BG1 scroll, SNES pixels): resting where the
 * game would open its map (SM-9), gliding there; or the player's, from a
 * drag (and its fling), until Samus reaches another square (SM-7) */
static float natVX, natVY, natVelX, natVelY;
static bool natHaveView, natUser, natDrag, natDragMoved;
static int natDownX, natDownY;
static float natDownVX, natDownVY, natPrevX, natPrevY;
static bool natTapOk; /* the finger came down on the shown map or page */
static Uint64 natStepT, natPrevT;

void native_free(void) {
	smmap_free(nat);
	nat = NULL;
	natProf = NULL;
	natShown = natHaveView = natUser = natDrag = false;
	natPage = 0;
	for (int i = 0; i < 2; ++i) {
		SDL_Texture** t[] = { &natChromeTex[i], &natOverTex[i], &natLayerTex[i], &natMarkerTex[i], &natPageTex[i] };
		for (int k = 0; k < 5; ++k) {
			if (*t[k]) SDL_DestroyTexture(*t[k]);
			*t[k] = NULL;
		}
	}
}

/* The game's profile asks for it: the ROM as played, read again */

void native_open(const char* rom, const MapProfile* p) {
	native_free();
	alt_free();
	if (p && p->native == NATIVE_ALTTP) alt_open(rom);
	if (!p || p->native != NATIVE_SUPER_METROID) return;
	size_t n = 0;
	uint8_t* data = emu_rom_read_played(rom, &n, NULL, NULL, NULL);
	nat = smmap_new(data, n);
	natProf = nat ? p : NULL;
	fprintf(stderr, "second screen: %s\n", nat ? "drawn by RetroDS (Super Metroid's map)" : "can't draw Super Metroid's map");
}

SDL_Texture* native_texture(int v, int w, int h, SDL_BlendMode blend) {
	SDL_Texture* t = SDL_CreateTexture(views[v].ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, w, h);
	if (t) SDL_SetTextureBlendMode(t, blend);
	return t;
}

void native_textures(void) {
	if (!nat) return;
	for (int i = 0; i < nViews; ++i) {
		SDL_Texture** t[] = { &natChromeTex[i], &natOverTex[i], &natLayerTex[i], &natMarkerTex[i], &natPageTex[i] };
		for (int k = 0; k < 5; ++k) {
			if (*t[k]) SDL_DestroyTexture(*t[k]);
		}
		natChromeTex[i] = native_texture(i, SMMAP_W, SMMAP_H, SDL_BLENDMODE_NONE);
		natOverTex[i] = native_texture(i, SMMAP_W, SMMAP_H, SDL_BLENDMODE_BLEND);
		natLayerTex[i] = native_texture(i, SMMAP_LW, SMMAP_LH, SDL_BLENDMODE_BLEND);
		natMarkerTex[i] = native_texture(i, SMMAP_MARKER, SMMAP_MARKER * 3, SDL_BLENDMODE_BLEND);
		natPageTex[i] = native_texture(i, SMMAP_W, SMMAP_H, SDL_BLENDMODE_NONE);
	}
	natDirty = SMMAP_CHROME | SMMAP_LAYER | SMMAP_MARKER_CHANGED | SMMAP_PAGE;
}

static void native_upload(void) {
	for (int i = 0; i < nViews && natDirty; ++i) {
		if (natDirty & SMMAP_CHROME) {
			if (natChromeTex[i]) ui_update_texture(natChromeTex[i], smmap_chrome(nat), SMMAP_W * 4);
			if (natOverTex[i]) ui_update_texture(natOverTex[i], smmap_over(nat), SMMAP_W * 4);
		}
		if ((natDirty & SMMAP_LAYER) && natLayerTex[i]) ui_update_texture(natLayerTex[i], smmap_layer(nat), SMMAP_LW * 4);
		if ((natDirty & SMMAP_MARKER_CHANGED) && natMarkerTex[i]) ui_update_texture(natMarkerTex[i], smmap_markers(nat), SMMAP_MARKER * 4);
		if ((natDirty & SMMAP_PAGE) && natPageTex[i]) ui_update_texture(natPageTex[i], smmap_page(nat), SMMAP_W * 4);
	}
	natDirty = 0;
}

/* After the frames of a pass of the loop: the game's RAM read again */
void native_tick(void) {
	size_t n = 0;
	const uint8_t* w = emu_memory(&emu, EMU_MEM_WRAM, &n);
	if (!w || n < 0x20000) return;
	int ch = smmap_update(nat, w);
	natDirty |= ch & (SMMAP_CHROME | SMMAP_LAYER | SMMAP_PAGE);
	if (smmap_live(nat)) {
		if (!natShown) natShown = true, natHaveView = false, natUser = false;
	} else if (smmap_in_menu(nat)) {
		natShown = false; /* SM-4: the logo */
	}
	/* SM-7: Samus on another square puts the map back where it rests,
	 * and the map back on the second screen */
	if ((ch & SMMAP_SQUARE) && !natDrag) natUser = false, natVelX = natVelY = 0, natPage = 0;
	/* SM-6, while the map is up */
	hudHide = natShown && natProf->hide[0].w > 0 && cfg->map && memprobes_match(natProf->hideIf, 3, &emu);
	native_upload();
}

/* The chrome's place on screen s, and its scale */
static SDL_Rect native_rect(int s, float* kx, float* ky) {
	SDL_Rect img = ui_image_rect(s, SMMAP_W, SMMAP_H, cfg->mapScale);
	*kx = img.w / (float) SMMAP_W, *ky = img.h / (float) SMMAP_H;
	return img;
}

static void native_clamp(float* vx, float* vy, bool stopX[1], bool stopY[1]) {
	int x0, x1, y0, y1;
	smmap_limits(nat, &x0, &x1, &y0, &y1);
	float cx = SDL_clamp(*vx, (float) x0, (float) x1), cy = SDL_clamp(*vy, (float) y0, (float) y1);
	if (stopX) *stopX = cx != *vx;
	if (stopY) *stopY = cy != *vy;
	*vx = cx, *vy = cy;
}

/* Once a drawn frame: the view moves on (a glide home, a fling) */
void native_step(void) {
	Uint64 now = SDL_GetPerformanceCounter();
	float dt = natStepT ? (float) ((double) (now - natStepT) / SDL_GetPerformanceFrequency()) : 0;
	natStepT = now;
	dt = SDL_min(dt, 0.1f);
	int hx, hy;
	smmap_home(nat, &hx, &hy);
	if (!natHaveView) natVX = (float) hx, natVY = (float) hy, natHaveView = true;
	if (natDrag) return; /* (the finger holds it) */
	if (natUser) {
		/* a fling: slowing down, stopped at the map's limits */
		if (natVelX == 0 && natVelY == 0) return;
		natVX += natVelX * dt, natVY += natVelY * dt;
		float slow = expf(-dt / 0.35f);
		natVelX *= slow, natVelY *= slow;
		bool sx, sy;
		native_clamp(&natVX, &natVY, &sx, &sy);
		if (sx || fabsf(natVelX) < 4) natVelX = 0;
		if (sy || fabsf(natVelY) < 4) natVelY = 0;
		return;
	}
	/* gliding to where the map rests (SM-9) */
	float a = 1 - expf(-dt / 0.09f);
	natVX += (hx - natVX) * a, natVY += (hy - natVY) * a;
	if (fabsf(hx - natVX) < 0.02f) natVX = (float) hx;
	if (fabsf(hy - natVY) < 0.02f) natVY = (float) hy;
}

/* The second screen: the chrome, then through its window the map's layer
 * (repeated, as the SNES's is), the marker, and the frame's edge over them */
void draw_native(int s) {
	if (natPage) {
		ui_image_pixels(s, natPageTex, SMMAP_W, SMMAP_H, cfg->mapScale, SHADER_ON);
		return;
	}
	float kx, ky;
	SDL_Rect img = native_rect(s, &kx, &ky);
	ui_image_pixels(s, natChromeTex, SMMAP_W, SMMAP_H, cfg->mapScale, SHADER_ON);
	int wx0 = img.x + (int) lroundf(SMMAP_WIN_X * kx), wx1 = img.x + (int) lroundf((SMMAP_WIN_X + SMMAP_WIN_W) * kx);
	int wy0 = img.y + (int) lroundf(SMMAP_WIN_Y * ky), wy1 = img.y + (int) lroundf((SMMAP_WIN_Y + SMMAP_WIN_H) * ky);
	SDL_Rect clip = { wx0 + screens[s].rect.x, wy0 + screens[s].rect.y, wx1 - wx0, wy1 - wy0 };
	ui_clip(s, &clip);
	/* the layer's top left: chrome line y shows map row y + SMMAP_Y0 + view y + 1 */
	int bx = img.x + (int) lroundf(-natVX * kx), by = img.y + (int) lroundf(-(natVY + SMMAP_Y0 + 1) * ky);
	int lw = (int) lroundf(SMMAP_LW * kx), lh = (int) lroundf(SMMAP_LH * ky);
	if (lw > 0 && lh > 0) {
		int x0 = bx - (int) ceilf((bx - wx0) / (float) lw) * lw, y0 = by - (int) ceilf((by - wy0) / (float) lh) * lh;
		for (int y = y0; y < wy1; y += lh) {
			for (int x = x0; x < wx1; x += lw) ui_image_part(s, natLayerTex, (SDL_Rect) { 0, 0, SMMAP_LW, SMMAP_LH }, (SDL_Rect) { x, y, lw, lh });
		}
	}
	/* the marker, in the layer's pixels */
	int lx, ly, mx, my;
	smmap_place(nat, 0, 0, &lx, &ly, &mx, &my);
	int f = smmap_marker_frame(nat);
	SDL_Rect md = { bx + (int) lroundf((mx - lx) * kx), by + (int) lroundf((my - ly) * ky), (int) lroundf(SMMAP_MARKER * kx), (int) lroundf(SMMAP_MARKER * ky) };
	ui_image_part(s, natMarkerTex, (SDL_Rect) { 0, f * SMMAP_MARKER, SMMAP_MARKER, SMMAP_MARKER }, md);
	ui_image_part(s, natOverTex, (SDL_Rect) { 0, 0, SMMAP_W, SMMAP_H }, img);
	ui_clip(s, NULL);
}

/* A tap on the second screen (SM-7, SM-10): the plate switches the page;
 * on the equipment page, an item is switched on or off, written to the
 * game between frames as its own equipment screen would (an exception to
 * "the game is only read", listed in docs/games/super-metroid.md).
 * (RetroAchievements plays in casual mode only for now, ra.c; with
 * hardcore, this is to be switched off.) */
static void native_tap(int x, int y) {
	float kx, ky;
	SDL_Rect img = native_rect(1 - game_screen(), &kx, &ky);
	if (kx <= 0 || ky <= 0) return;
	int cx = (int) floorf((x - img.x) / kx), cy = (int) floorf((y - img.y) / ky);
	if (cx < 0 || cy < 0 || cx >= SMMAP_W || cy >= SMMAP_H) return;
	int cat = 0, idx = 0, hit = smmap_hit(nat, natPage, cx, cy, &cat, &idx);
	if (hit == 1) {
		natPage ^= 1;
		if (debug) fprintf(stderr, "second screen: %s\n", natPage ? "equipment page" : "map");
	} else if (hit == 2) {
		size_t n = 0;
		const uint8_t* w = emu_memory(&emu, EMU_MEM_WRAM, &n);
		if (!w || n < 0x20000) return;
		Write out[96];
		int k = smmap_toggle(nat, w, cat, idx, out, 96), nOk;
		const WriteRange* ok = smmap_write_ranges(&nOk);
		char what[96];
		snprintf(what, sizeof what, "(frame %llu) equipment item %d,%d", (unsigned long long) frameCount, cat, idx);
		if (k > 0 && emu_apply(&emu, what, out, k, ok, nOk) > 0) native_tick(); /* (the page shows it now) */
		else if (!k) fprintf(stderr, "equipment: item %d,%d not switched (not collected, or not in plain gameplay)\n", cat, idx);
	}
}

/* A touch on the second screen (SM-7): a drag moves the map with the
 * finger, a fling carries on; a tap goes to native_tap. Never the menu. */
int native_touch(const Pointer* p) {
	if (!cfg->map || nScreens != 2) return 0;
	if (p->screen == game_screen() && !(natDrag && p->kind != 1)) return 0;
	Uint64 now = SDL_GetPerformanceCounter();
	if (p->kind == 1) {
		natDrag = natShown;
		natDragMoved = false;
		natTapOk = natShown;
		natDownX = p->x, natDownY = p->y;
		natDownVX = natVX, natDownVY = natVY;
		natPrevX = (float) p->x, natPrevY = (float) p->y, natPrevT = now;
		natVelX = natVelY = 0;
		return 1;
	}
	if (!natDrag) return 1;
	float kx, ky;
	native_rect(1 - game_screen(), &kx, &ky);
	if (kx <= 0 || ky <= 0) return 1;
	/* a tap until the finger has moved 4 SNES pixels (a finger's
	 * wobble is several screen pixels on a dense screen) */
	if (fabsf((p->x - natDownX) / kx) > 4 || fabsf((p->y - natDownY) / ky) > 4) natDragMoved = true;
	if (natDragMoved && !natPage) {
		/* (the map follows the finger: the view goes the other way) */
		natVX = natDownVX - (p->x - natDownX) / kx, natVY = natDownVY - (p->y - natDownY) / ky;
		native_clamp(&natVX, &natVY, NULL, NULL);
		natUser = true;
		double dt = (double) (now - natPrevT) / SDL_GetPerformanceFrequency();
		if (dt > 0.004) {
			/* the finger's speed, smoothed (SNES pixels a second, the view's way) */
			float vx = -(p->x - natPrevX) / kx / (float) dt, vy = -(p->y - natPrevY) / ky / (float) dt;
			natVelX = natVelX * 0.5f + vx * 0.5f, natVelY = natVelY * 0.5f + vy * 0.5f;
			natPrevX = (float) p->x, natPrevY = (float) p->y, natPrevT = now;
		}
	}
	if (p->kind != 2) return 1;
	natDrag = false;
	if (!natDragMoved) {
		if (natTapOk) native_tap(p->x, p->y);
		return 1;
	}
	if (!natPage) {
		/* a finger that stopped before lifting: no fling */
		if ((double) (now - natPrevT) / SDL_GetPerformanceFrequency() > 0.08) natVelX = natVelY = 0;
		natStepT = 0;
		if (debug) fprintf(stderr, "map view: %.1f,%.1f, fling %.0f,%.0f\n", natVX, natVY, natVelX, natVelY);
	}
	return 1;
}


