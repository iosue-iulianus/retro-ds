/* Symphony of the Night's second screen in a play session (SOTN-2 to 7,
 * docs/games/symphony-of-the-night.md): the castle map as the game draws
 * it, in the status screen's window, with the logo and a ROOMS pane,
 * zoomed by touch as A Link to the Past's map is; and, a page across, the
 * game's own menu, widened, the game paused for it; src/sotn.c reads and
 * composes them. */
#include "game_screens.h"
#include "sotn.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

bool sot;          /* this game is Symphony of the Night */
static bool sotShown; /* the map is up (else the logo, SOTN-1) */
static SotnView view;
static SotnLayout layout;
static uint32_t viewCrc;
static bool composed;
static int cw, ch, scale;  /* the window's size in the game's pixels; screen pixels to one at rest */
static int curFrame;       /* the map cursor's animation (SOTN-3) */
static uint32_t* bgPx;     /* the big window, cw x ch */
static uint32_t mapPx[SOTN_PIC_W * SOTN_PIC_H];
static uint32_t* panePx;
static uint32_t cursorPx[32 * 32];
static SDL_Texture *bgTex[2], *mapTex[2], *paneTex[2], *curTex[2];
static bool bgDirty, mapDirty, paneDirty, curDirty;

/* ---- SOTN-5: the view. The map's picture (Select's) is drawn with its
 * origin at (ax, ay) of the second screen, z screen pixels to one of its
 * pixels; at rest, z is `scale` and the map where the layout puts it. */
#define ZOOMS 4
/* the Back button (SOTN-7): its size in the widened picture's pixels, at
 * the bottom right inside the window */
#define BACK_W 60
#define BACK_H 20
/* the game's menu window: its top and bottom edges' lines in the game's
 * picture (measured on the status page), put on the map's window's */
#define MENU_TOP 16
#define MENU_BOTTOM 215
static float z, ax, ay;
static int zoomI;          /* the level: z is scale x (zoomI + 1) */
static bool viewSet;
static bool user;          /* moved by the player: stays till Alucard's next room */
static int userRoomX, userRoomY;
static bool focus;         /* a tap's place gliding to the middle */
static float focusX, focusY; /* (in the map's picture) */
static bool drag, dragMoved, tapOk, pinch;
static int downX, downY;
static float downAX, downAY, prevX, prevY, velX, velY;
static Uint64 prevT, stepT;
static float pinchSpan, pinchZ, pinchPX, pinchPY;

/* ---- SOTN-6: the pages and the game's menu */
enum { M_OFF, M_OPENING, M_OPEN, M_FROZEN, M_CLOSING };
static int phase = M_OFF, phaseFrames;
static int page;           /* 0 the map, 1 the menu */
static float pageX;        /* where the pages are: 0 the map's up, 1 the menu's */
static bool pageDrag, swipeOk;
static float pageDownX, pageDownY;
static int injectFrames;   /* frames left of a button RetroDS presses for the game */
static uint32_t injectKey;
static int injectGap;      /* frames before the next press */
static int backPresses;    /* △ presses asked for by the Back button */
static bool startHeld;
/* the game's menu widened (SOTN-7) */
static uint32_t* widePx;
static int wideW, wideH;
static bool wideOk, wideDirty;
static SDL_Texture* wideTex[2];
static uint32_t backPx[BACK_W * BACK_H];
static SDL_Texture* backTex[2];
static bool backDirty;

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
	ui_free_image(bgTex);
	ui_free_image(mapTex);
	ui_free_image(paneTex);
	ui_free_image(curTex);
	ui_free_image(wideTex);
	ui_free_image(backTex);
	free(bgPx);
	free(panePx);
	free(widePx);
	bgPx = panePx = widePx = NULL;
	wideW = wideH = 0;
	sot = sotShown = composed = viewSet = user = focus = drag = pinch = wideOk = pageDrag = false;
	phase = M_OFF, page = 0, pageX = 0, injectFrames = injectGap = backPresses = 0, startHeld = false;
	viewCrc = 0;
	cw = ch = 0;
	zoomI = 0;
	stepT = 0;
}

void sot_textures(void) {
	ui_free_image(bgTex);
	ui_free_image(mapTex);
	ui_free_image(paneTex);
	ui_free_image(curTex);
	ui_free_image(wideTex);
	ui_free_image(backTex);
	bgDirty = mapDirty = paneDirty = curDirty = composed;
	wideDirty = wideOk, backDirty = true;
}

/* the window's size for the second screen: as many of the game's pixels
 * as fit at a whole number of screen pixels each, the castle's 190 lines
 * and a little room round them at the most */
static void fit(int* w, int* h, int* k) {
	SDL_Rect r = screens[map_screen()].rect;
	int s = r.h / (SOTN_MAP_H + 26);
	if (s < 1) s = 1;
	*k = s, *w = r.w / s, *h = r.h / s;
}

/* the window's top left on the screen */
static void origin(int* ox, int* oy) {
	SDL_Rect r = screens[map_screen()].rect;
	*ox = (r.w - cw * scale) / 2, *oy = (r.h - ch * scale) / 2;
}

/* the map's inside of the window, on the screen */
static SDL_FRect inside(void) {
	int ox, oy;
	origin(&ox, &oy);
	return (SDL_FRect) { (float) (ox + layout.winX * scale), (float) (oy + layout.winY * scale), (float) (layout.winW * scale),
		                 (float) (layout.winH * scale) };
}

/* where the map rests at level 0 */
static void rest(float* x, float* y) {
	int ox, oy;
	origin(&ox, &oy);
	*x = (float) (ox + layout.mapX * scale), *y = (float) (oy + layout.mapY * scale);
}

/* The castle kept in the window: bigger than it, its edges never inside
 * the window's; smaller, all of it inside */
static void clamp_view(void) {
	SDL_FRect w = inside();
	float c0 = ax + layout.castleX * z, c1 = c0 + SOTN_MAP_W * z;
	if (c1 - c0 >= w.w) ax += c0 > w.x ? w.x - c0 : c1 < w.x + w.w ? w.x + w.w - c1 : 0;
	else ax += c0 < w.x ? w.x - c0 : c1 > w.x + w.w ? w.x + w.w - c1 : 0;
	c0 = ay + layout.castleY * z, c1 = c0 + SOTN_MAP_H * z;
	if (c1 - c0 >= w.h) ay += c0 > w.y ? w.y - c0 : c1 < w.y + w.h ? w.y + w.h - c1 : 0;
	else ay += c0 < w.y ? w.y - c0 : c1 > w.y + w.h ? w.y + w.h - c1 : 0;
}

/* where the view goes by itself: at rest, the layout's place; closer,
 * Alucard's square in the middle of the window */
static void home(float* x, float* y, float atZ) {
	if (zoomI == 0) {
		rest(x, y);
		return;
	}
	SDL_FRect w = inside();
	float px = view.sqX * 4 + 2.5f, py = view.sqY * 4 + 3.5f; /* (his square's middle, in Select's picture) */
	float sx = ax, sy = ay, sz = z;
	z = atZ, ax = w.x + w.w / 2 - px * atZ, ay = w.y + w.h / 2 - py * atZ;
	clamp_view();
	*x = ax, *y = ay;
	ax = sx, ay = sy, z = sz;
}

/* After a frame, the core idle: read the game, compose again if anything
 * shown has changed. */
static void menu_fit(int w, int h, float* kx, float* ky, int* ow, SDL_FRect* at);

static void press(uint32_t key, int frames) {
	injectKey = key, injectFrames = frames, injectGap = frames + 6;
}

/* SOTN-6: to the menu page (the game paused, its menu opened if it would) */
static void to_menu(void) {
	page = 1;
	if (phase == M_OFF) {
		if (sotn_menu_openable(&emu)) {
			phase = M_OPENING, phaseFrames = 0;
			press(K_START, 2);
			fprintf(stderr, "sotn: menu page, the game's menu opened (Start pressed for it)\n");
		} else {
			phase = M_FROZEN;
			fprintf(stderr, "sotn: menu page, the game stopped (it wouldn't open its menu now: engine %d)\n", sotn_engine(&emu));
		}
	} else if (phase == M_CLOSING) {
		phase = M_OPEN; /* (still open: stays) */
	}
}

/* back to the map page: the game's menu closed (△ till it is), or the
 * game let go */
static void to_map(void) {
	page = 0;
	if (phase == M_FROZEN) phase = M_OFF;
	else if (phase == M_OPENING || phase == M_OPEN) phase = M_CLOSING, phaseFrames = 0, backPresses = 0;
}

/* After a frame, the core idle: the game's menu followed (SOTN-6) */
static void menu_tick(void) {
	if (phase == M_OFF || phase == M_FROZEN) return;
	int eng = sotn_engine(&emu);
	++phaseFrames;
	if (injectGap > 0) --injectGap;
	if (phase == M_OPENING) {
		if (eng == SOTN_ENGINE_MENU) phase = M_OPEN;
		else if (phaseFrames > 12 && !injectFrames) {
			phase = M_FROZEN; /* (it didn't take Start: stopped instead) */
			fprintf(stderr, "sotn: the game didn't open its menu: stopped instead\n");
		}
	} else if (phase == M_OPEN) {
		if (eng == SOTN_ENGINE_PLAYING) {
			/* (the game closed its menu itself: △ at its top) */
			phase = M_OFF, page = 0;
		} else if (backPresses > 0 && !injectGap) {
			press(K_X, 3); /* (△: the game's back) */
			--backPresses;
		}
	} else if (phase == M_CLOSING) {
		if (eng == SOTN_ENGINE_PLAYING) {
			phase = M_OFF;
			fprintf(stderr, "sotn: the game's menu closed, the game carries on\n");
		} else if (phaseFrames > 600) {
			phase = M_OPEN, page = 1; /* (it won't close: the menu page again) */
			fprintf(stderr, "sotn: the game's menu wouldn't close\n");
		} else if (!injectGap && eng == SOTN_ENGINE_MENU) {
			press(K_X, 3);
		}
	}
}

/* SOTN-6: the player's keys for the next frame: Start is RetroDS's (the
 * pages), nothing of the player's while the menu opens or closes, and
 * what RetroDS presses for the game */
uint32_t sot_keys(uint32_t keys) {
	if (!sot) return keys;
	bool start = keys & K_START;
	if (start && !startHeld) {
		if (page == 0) to_menu();
		else to_map();
	}
	startHeld = start;
	keys &= ~(uint32_t) K_START;
	if (phase == M_OPENING || phase == M_CLOSING || phase == M_FROZEN) keys = 0;
	if (injectFrames > 0) keys |= injectKey, --injectFrames;
	return keys;
}

bool sot_frozen(void) {
	return sot && phase == M_FROZEN;
}

bool sot_cover_game(void) {
	return sot && (phase != M_OFF || page == 1);
}

void sot_tick(void) {
	if (!sot) return;
	++curFrame;
	menu_tick();
	if (phase == M_OPEN || phase == M_CLOSING || phase == M_OPENING) {
		/* SOTN-7: the game's picture widened, from the frame just run */
		float kx, ky;
		int ow;
		SDL_FRect at;
		menu_fit(emu.w, emu.h, &kx, &ky, &ow, &at);
		(void) kx, (void) ky, (void) at;
		if (ow <= emu.w) ow = emu.w + 4;
		if (ow != wideW || emu.h != wideH) {
			free(widePx);
			widePx = malloc((size_t) ow * emu.h * 4);
			wideW = ow, wideH = emu.h;
			ui_free_image(wideTex);
		}
		wideOk = widePx && emu.fb && sotn_widen(emu.fb, emu.w, emu.h, widePx, ow);
		wideDirty = wideOk;
	} else {
		wideOk = false;
	}
	if (!sotn_read(&emu, &view)) {
		sotShown = false;
		return;
	}
	{
		/* the Back button: a small window, △ and BACK (redrawn as the
		 * window colour or the font changes, with the map) */
		static uint32_t lastCrc;
		uint32_t c = emu_crc32(0, view.window, sizeof(view.window));
		c = emu_crc32(c, view.fontPal, sizeof(view.fontPal));
		if (c != lastCrc || backDirty) {
			memset(backPx, 0, sizeof(backPx));
			sotn_window(&view, backPx, BACK_W, 0, 0, BACK_W, BACK_H, 0, 0);
			sotn_button(&view, backPx, BACK_W, 8, 6, 3);
			sotn_text(&view, backPx, BACK_W, 20, 6, "BACK");
			lastCrc = c, backDirty = true;
		}
	}
	/* the map while playing in the castle; the title, the file screens,
	 * the prologue, game over, the ending: the logo; loading and the
	 * like: as it was */
	if (view.state == 1 || view.state == 8 || view.state == 3 || view.state == 7) sotShown = false;
	else if (view.state == 2) sotShown = sotn_has_map(&view);
	if (view.state != 2) return; /* (the window composed even without a map: the menu page's, SOTN-6) */
	if (user && (view.roomX != userRoomX || view.roomY != userRoomY)) user = false; /* (another room: the view glides back) */
	int w, h, k;
	fit(&w, &h, &k);
	uint32_t c = sotn_view_crc(&view);
	if (composed && c == viewCrc && w == cw && h == ch) return;
	if (w != cw || h != ch) {
		free(bgPx);
		bgPx = malloc((size_t) w * h * 4);
		ui_free_image(bgTex);
		if (!bgPx) return;
		cw = w, ch = h, scale = k;
		viewSet = false;
	}
	int pw = layout.paneW, ph = layout.paneH;
	sotn_compose(&view, bgPx, cw, ch, &layout);
	if (!panePx || pw != layout.paneW || ph != layout.paneH) {
		free(panePx);
		panePx = calloc((size_t) (layout.paneW ? layout.paneW : 1) * (layout.paneH ? layout.paneH : 1), 4);
		ui_free_image(paneTex);
	}
	if (panePx) sotn_pane_pixels(&view, &layout, panePx);
	sotn_map_pixels(&view, mapPx);
	sotn_cursor_pixels(&view, cursorPx);
	viewCrc = c;
	composed = bgDirty = mapDirty = paneDirty = curDirty = true;
	if (debug) fprintf(stderr, "frame %llu: sotn map composed, %dx%d at %d, rooms %d\n", (unsigned long long) frameCount, cw, ch, k, view.rooms);
}

bool sot_map_shown(void) {
	return sot && sotShown && composed;
}

/* the second screen is SotN's own (the map or the menu page), not the logo */
bool sot_screen_shown(void) {
	return sot && (sot_map_shown() || page == 1 || pageX > 0.001f || phase != M_OFF);
}

/* ---- SOTN-5: zoom, drag and glide (as ALTTP-3) */

static float level_z(int i) {
	return (float) (scale * (i + 1));
}

/* each drawn frame: the zoom glides to its level, a tap's place to the
 * middle, a fling slows, an untouched view goes home */
static void sot_step(void) {
	Uint64 now = SDL_GetPerformanceCounter();
	float dt = stepT ? (float) ((double) (now - stepT) / SDL_GetPerformanceFrequency()) : 0;
	stepT = now;
	dt = SDL_min(dt, 0.1f);
	if (!viewSet) {
		zoomI = SDL_clamp(zoomI, 0, ZOOMS - 1);
		z = level_z(zoomI);
		home(&ax, &ay, z);
		viewSet = true;
	}
	if (drag || pinch) return;
	SDL_FRect w = inside();
	float to = level_z(zoomI);
	if (z != to || focus) {
		/* round the window's middle; a tap's place glides to it meanwhile */
		float mx = (w.x + w.w / 2 - ax) / z, my = (w.y + w.h / 2 - ay) / z;
		float k = 1 - expf(-dt / 0.06f);
		z = fabsf(to - z) < 0.002f * to ? to : z * powf(to / z, k);
		if (focus) {
			mx += (focusX - mx) * k, my += (focusY - my) * k;
			if (fabsf(focusX - mx) < 0.2f && fabsf(focusY - my) < 0.2f) mx = focusX, my = focusY, focus = false;
		}
		ax = w.x + w.w / 2 - mx * z, ay = w.y + w.h / 2 - my * z;
		if (user) {
			float bx = ax, by = ay;
			clamp_view();
			/* (the castle's edge stops it short of the middle: there it stays) */
			if (z == to && (fabsf(bx - ax) > 0.5f || fabsf(by - ay) > 0.5f)) focus = false;
		}
	}
	if (user) {
		if (velX == 0 && velY == 0) return;
		ax += velX * dt, ay += velY * dt;
		float slow = expf(-dt / 0.35f);
		velX *= slow, velY *= slow;
		float bx = ax, by = ay;
		clamp_view();
		if (bx != ax || fabsf(velX) < 4) velX = 0;
		if (by != ay || fabsf(velY) < 4) velY = 0;
		return;
	}
	if (focus || z != to) return;
	float hx, hy;
	home(&hx, &hy, z);
	float a = 1 - expf(-dt / 0.09f);
	ax += (hx - ax) * a, ay += (hy - ay) * a;
	if (fabsf(hx - ax) < 0.05f) ax = hx;
	if (fabsf(hy - ay) < 0.05f) ay = hy;
}

static void be_user(void) {
	user = true;
	userRoomX = view.roomX, userRoomY = view.roomY;
}

/* A touch on the second screen: a tap zooms in a level (from the closest
 * back out to the whole castle), the place tapped gliding to the middle;
 * two fingers pinch the zoom round the point between them; a drag moves
 * the map. 0: not this screen's; 1: taken. */
/* the map's window on the screen, its white edge included: the menu's
 * goes on it */
static SDL_FRect frame(void) {
	SDL_FRect w = inside();
	return (SDL_FRect) { w.x - scale, w.y - scale, w.w + 2 * scale, w.h + 2 * scale };
}

/* SOTN-7: screen pixels to one of the game's picture's (w x h, shown 4:3)
 * with its menu window's edges on the map's window's; the widened
 * picture's width; and where it goes */
static void menu_fit(int w, int h, float* kx, float* ky, int* ow, SDL_FRect* at) {
	SDL_FRect f = frame();
	*ky = f.h / (MENU_BOTTOM - MENU_TOP + 1);
	*kx = *ky * 4 / 3 * h / (w > 0 ? w : 1);
	*ow = (int) lroundf(f.w / *kx);
	*at = (SDL_FRect) { f.x, f.y - MENU_TOP * *ky, *ow * *kx, h * *ky };
}

/* the Back button on the screen: the bottom right inside the window */
static SDL_Rect back_rect(int s) {
	float kx, ky;
	int ow;
	SDL_FRect a;
	menu_fit(wideW ? wideW * 3 / 4 : 365, wideH ? wideH : 224, &kx, &ky, &ow, &a);
	SDL_FRect f = frame();
	int w = (int) (BACK_W * kx), h = (int) (BACK_H * ky);
	return (SDL_Rect) { (int) (f.x + f.w - 6 * kx) - w, (int) (f.y + f.h - 3 * ky) - h, w, h };
}

/* the two page squares: inside the window, just above its bottom edge */
static SDL_Rect dots_rect(int s) {
	SDL_Rect r = screens[s].rect;
	SDL_FRect f = frame();
	int d = scale > 0 ? scale * 4 : 16, gap = d * 2, y = (int) (f.y + f.h) - scale * 4 - d;
	return (SDL_Rect) { r.w / 2 - d - gap / 2, y, d * 2 + gap, d };
}

int sot_touch(const Pointer* p) {
	int s = map_screen();
	if (nScreens != 2 || !sot_screen_shown()) return 0;
	SDL_Rect r = screens[s].rect;
	/* SOTN-6: a swipe across turns the page; on the menu page, Back */
	if (p->kind == 1 && p->screen == s) {
		SDL_FRect w = inside();
		bool fits = SOTN_MAP_W * z <= w.w + 1;
		bool edge = p->x < w.x || p->x > w.x + w.w;
		swipeOk = page == 1 || !sot_map_shown() || fits || edge;
		pageDrag = false, pageDownX = (float) p->x, pageDownY = (float) p->y;
		if (page == 1) {
			SDL_Rect b = back_rect(s), d = dots_rect(s);
			if (SDL_PointInRect(&(SDL_Point) { p->x, p->y }, &b)) {
				if (phase == M_OPEN && sotn_menu_step(&emu) != SOTN_MENU_TOP) ++backPresses;
				else to_map();
				return 1;
			}
			if (p->y >= d.y - d.h * 2) {
				to_map();
				return 1;
			}
		} else if (p->y >= dots_rect(s).y - dots_rect(s).h * 2 && abs(p->x - r.w / 2) < r.w / 8) {
			to_menu();
			return 1;
		}
	}
	if (swipeOk && (p->kind == 3 || p->kind == 2) && (pageDrag || (abs(p->x - (int) pageDownX) > r.w / 40 && fabsf(p->x - pageDownX) > fabsf(p->y - pageDownY)))) {
		if (!pageDrag && !pinch) {
			pageDrag = true;
			drag = false; /* (not the map's drag) */
		}
		if (pageDrag) {
			float dx = (p->x - pageDownX) / r.w;
			pageX = SDL_clamp(page - dx, 0.0f, 1.0f);
			if (p->kind == 2) {
				pageDrag = false;
				if (page == 0 && pageX > 0.2f) to_menu();
				else if (page == 1 && pageX < 0.8f) to_map();
			}
			return 1;
		}
	}
	if (page == 1) return p->screen == s ? 1 : 0;
	if (!sot_map_shown()) return 0;
	if (p->kind >= 4) {
		if (p->screen != s) return 1;
		if (p->kind == 5) {
			if (pinch) {
				int best = 0;
				for (int i = 1; i < ZOOMS; ++i) {
					if (fabsf(logf(level_z(i) / z)) < fabsf(logf(level_z(best) / z))) best = i;
				}
				zoomI = best;
				fprintf(stderr, "sotn map zoom: x%d (pinched)\n", zoomI + 1);
			}
			pinch = false;
			drag = true, dragMoved = true, tapOk = false;
			downX = p->x, downY = p->y, downAX = ax, downAY = ay;
			prevX = (float) p->x, prevY = (float) p->y, prevT = SDL_GetPerformanceCounter();
			velX = velY = 0;
			return 1;
		}
		if (p->span < 8) return 1;
		if (!pinch) {
			pinch = true, drag = false, dragMoved = true, tapOk = false, focus = false;
			pinchSpan = p->span, pinchZ = z;
			pinchPX = (p->x - ax) / z, pinchPY = (p->y - ay) / z;
			velX = velY = 0;
		}
		z = SDL_clamp(pinchZ * p->span / pinchSpan, level_z(0), level_z(ZOOMS - 1));
		ax = p->x - pinchPX * z, ay = p->y - pinchPY * z;
		clamp_view();
		be_user();
		return 1;
	}
	if (p->screen != s && !(drag && p->kind != 1)) return 0;
	Uint64 now = SDL_GetPerformanceCounter();
	if (p->kind == 1) {
		focus = false;
		drag = true, dragMoved = false, tapOk = true;
		downX = p->x, downY = p->y, downAX = ax, downAY = ay;
		prevX = (float) p->x, prevY = (float) p->y, prevT = now;
		velX = velY = 0;
		return 1;
	}
	if (!drag) return 1;
	if (abs(p->x - downX) > 4 * scale || abs(p->y - downY) > 4 * scale) dragMoved = true;
	if (dragMoved) {
		ax = downAX + (p->x - downX), ay = downAY + (p->y - downY);
		clamp_view();
		be_user();
		double dt = (double) (now - prevT) / SDL_GetPerformanceFrequency();
		if (dt > 0.004) {
			float vx = (p->x - prevX) / (float) dt, vy = (p->y - prevY) / (float) dt;
			velX = velX * 0.5f + vx * 0.5f, velY = velY * 0.5f + vy * 0.5f;
			prevX = (float) p->x, prevY = (float) p->y, prevT = now;
		}
	}
	if (p->kind != 2) return 1;
	drag = pinch = false;
	if (!dragMoved) {
		if (tapOk) {
			focusX = (p->x - ax) / z, focusY = (p->y - ay) / z;
			focus = true;
			velX = velY = 0;
			zoomI = (zoomI + 1) % ZOOMS;
			if (zoomI == 0) focus = false, user = false; /* (back out: the whole castle at its place) */
			else be_user();
			fprintf(stderr, "sotn map zoom: x%d\n", zoomI + 1);
		}
		return 1;
	}
	if ((double) (now - prevT) / SDL_GetPerformanceFrequency() > 0.08) velX = velY = 0;
	stepT = 0;
	return 1;
}

/* ---- drawing */

static void texture(SDL_Texture** t, int w, int h, SDL_BlendMode blend, bool* dirty) {
	for (int i = 0; i < nViews; ++i) {
		if (t[i]) continue;
		t[i] = SDL_CreateTexture(views[i].ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, w, h);
		if (!t[i]) continue;
		SDL_SetTextureBlendMode(t[i], blend);
		SDL_SetTextureScaleMode(t[i], SDL_ScaleModeNearest);
		*dirty = true;
	}
}

static void upload(SDL_Texture** t, const uint32_t* px, int w, bool* dirty) {
	if (!*dirty) return;
	for (int i = 0; i < nViews; ++i) {
		if (t[i]) ui_update_texture(t[i], px, w * 4);
	}
	*dirty = false;
}

static void draw_map_page(int s, int dx) {
	SDL_Rect r = screens[s].rect;
	if (!bgPx || !composed) return;
	texture(bgTex, cw, ch, SDL_BLENDMODE_NONE, &bgDirty);
	texture(mapTex, SOTN_PIC_W, SOTN_PIC_H, SDL_BLENDMODE_BLEND, &mapDirty);
	if (layout.paneW) texture(paneTex, layout.paneW, layout.paneH, SDL_BLENDMODE_NONE, &paneDirty);
	if (!curTex[0] && !curTex[1]) {
		texture(curTex, 32, 32, SDL_BLENDMODE_BLEND, &curDirty);
		for (int i = 0; i < nViews; ++i) {
			if (curTex[i]) SDL_SetTextureAlphaMod(curTex[i], 128); /* (half and half: the game's) */
		}
	}
	upload(bgTex, bgPx, cw, &bgDirty);
	upload(mapTex, mapPx, SOTN_PIC_W, &mapDirty);
	if (layout.paneW && panePx) upload(paneTex, panePx, layout.paneW, &paneDirty);
	upload(curTex, cursorPx, 32, &curDirty);
	int ox, oy;
	origin(&ox, &oy);
	ox += dx;
	ui_image_part(s, bgTex, (SDL_Rect) { 0, 0, cw, ch }, (SDL_Rect) { ox, oy, cw * scale, ch * scale });
	/* SOTN-2, 5: the map, in the window */
	SDL_FRect w = inside();
	SDL_Rect clip = { r.x + (int) w.x + dx, r.y + (int) w.y, (int) w.w, (int) w.h };
	if (clip.x < r.x) clip.w -= r.x - clip.x, clip.x = r.x;
	if (clip.x + clip.w > r.x + r.w) clip.w = r.x + r.w - clip.x;
	if (clip.w <= 0) clip.w = 1, clip.x = r.x - 2; /* (all off the screen) */
	ui_clip(s, &clip);
	float axd = ax + dx;
	int x0 = (int) lroundf(axd), y0 = (int) lroundf(ay);
	ui_image_part(s, mapTex, (SDL_Rect) { 0, 0, SOTN_PIC_W, SOTN_PIC_H },
	              (SDL_Rect) { x0, y0, (int) lroundf(axd + SOTN_PIC_W * z) - x0, (int) lroundf(ay + SOTN_PIC_H * z) - y0 });
	/* SOTN-3: the game's map cursor closing onto Alucard's square, at its
	 * size with the map at rest whatever the zoom (as Link's marker,
	 * ALTTP-4), round his square's middle */
	int cx, cy, cs;
	sotn_cursor_rect(&view, curFrame & 63, &cx, &cy, &cs);
	float mx = axd + (cx + cs / 2.0f) * z, my = ay + (cy + cs / 2.0f) * z, half = cs * scale / 2.0f;
	ui_image_part(s, curTex, (SDL_Rect) { 0, 0, 32, 32 },
	              (SDL_Rect) { (int) lroundf(mx - half), (int) lroundf(my - half), (int) lroundf(half * 2), (int) lroundf(half * 2) });
	ui_clip(s, NULL);
	/* SOTN-4: the ROOMS pane and the title's logo over it */
	if (layout.paneW)
		ui_image_part(s, paneTex, (SDL_Rect) { 0, 0, layout.paneW, layout.paneH },
		              (SDL_Rect) { ox + layout.paneX * scale, oy + layout.paneY * scale, layout.paneW * scale, layout.paneH * scale });
	if (layout.logoW > 0) draw_logo_in(s, (SDL_Rect) { ox + (int) (layout.logoX * scale), oy + (int) (layout.logoY * scale), (int) (layout.logoW * scale),
	                                                  (int) (layout.logoH * scale) });
}

/* SOTN-6, 7: the menu page: the game's menu widened, or the window alone
 * (the game stopped, or its menu opening or closing); the Back button */
static void draw_menu_page(int s, int dx) {
	if (wideOk && widePx) {
		float kx, ky;
		int ow;
		SDL_FRect a;
		menu_fit(emu.w, wideH, &kx, &ky, &ow, &a);
		SDL_FRect f = frame();
		texture(wideTex, wideW, wideH, SDL_BLENDMODE_NONE, &wideDirty);
		upload(wideTex, widePx, wideW, &wideDirty);
		/* (its window's edges on the map's: the picture's black above and below cut off) */
		SDL_Rect r = screens[s].rect;
		SDL_Rect clip = { r.x + (int) f.x + dx, r.y + (int) f.y, (int) f.w, (int) f.h };
		if (clip.x < r.x) clip.w -= r.x - clip.x, clip.x = r.x;
		if (clip.x + clip.w > r.x + r.w) clip.w = r.x + r.w - clip.x;
		if (clip.w > 0) {
			ui_clip(s, &clip);
			ui_image_part(s, wideTex, (SDL_Rect) { 0, 0, wideW, wideH },
			              (SDL_Rect) { (int) lroundf(f.x) + dx, (int) lroundf(a.y), (int) lroundf(f.w), (int) lroundf(a.h) });
			ui_clip(s, NULL);
		}
	} else if (bgPx && composed) {
		texture(bgTex, cw, ch, SDL_BLENDMODE_NONE, &bgDirty);
		upload(bgTex, bgPx, cw, &bgDirty);
		int ox, oy;
		origin(&ox, &oy);
		ui_image_part(s, bgTex, (SDL_Rect) { 0, 0, cw, ch }, (SDL_Rect) { ox + dx, oy, cw * scale, ch * scale });
	}
	if (phase == M_OPEN || phase == M_FROZEN) {
		texture(backTex, BACK_W, BACK_H, SDL_BLENDMODE_BLEND, &backDirty);
		upload(backTex, backPx, BACK_W, &backDirty);
		SDL_Rect b = back_rect(s);
		b.x += dx;
		ui_image_part(s, backTex, (SDL_Rect) { 0, 0, BACK_W, BACK_H }, b);
	}
}

void draw_sot(int s) {
	SDL_Rect r = screens[s].rect;
	sot_step();
	/* the pages slide to where they're going (or follow the finger) */
	static Uint64 t0;
	Uint64 now = SDL_GetPerformanceCounter();
	float dt = t0 ? (float) ((double) (now - t0) / SDL_GetPerformanceFrequency()) : 0;
	t0 = now;
	if (!pageDrag) {
		float k = 1 - expf(-SDL_min(dt, 0.1f) / 0.06f);
		pageX += (page - pageX) * k;
		if (fabsf(page - pageX) < 0.002f) pageX = (float) page;
	}
	int off = (int) lroundf(pageX * r.w);
	if (pageX < 0.999f) {
		if (sot_map_shown()) draw_map_page(s, -off);
		else if (!off) draw_logo_at(s, 0);
	}
	if (pageX > 0.001f) draw_menu_page(s, r.w - off);
	/* which page: two small squares under the window */
	SDL_Rect d = dots_rect(s);
	SDL_Rect a = { d.x, d.y, d.h, d.h }, b = { d.x + d.w - d.h, d.y, d.h, d.h };
	SDL_Color on = { 255, 251, 255, 255 }, off2 = { 90, 90, 110, 255 };
	ui_fill(s, a, page == 0 ? on : off2);
	ui_fill(s, b, page == 1 ? on : off2);
}
