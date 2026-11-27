/* Final Fantasy VI's second screen in a play session (FF6-2 to 5,
 * docs/games/final-fantasy-vi.md): the game's own battle windows and main
 * menu moved over from the game screen (as Symphony of the Night's menu
 * is), touch on the battle commands done with the game's own buttons;
 * src/ff6.c reads the game. */
#include "game_screens.h"
#include "ff6.h"

#include <stdlib.h>
#include <string.h>

bool f6; /* this game is Final Fantasy VI */

static Ff6Part part = FF6_OTHER;
static Ff6Battle bat;
static int listFrames; /* (see ff6_tick) */
/* the pictures moved over: the battle strip, the whole menu screen */
static SDL_Texture* stripTex[2];
static SDL_Texture* menuTex[2];
static int texW, texH;
static bool stripDirty, menuDirty;

/* FF6-3: the presses a tap makes, played one at a time as a player's
 * taps (down 3 frames, up 9) */
static uint32_t queue[8];

/* FF6-5: the party screen RetroDS draws (src/ff6.c), while walking */
static Ff6Rom* rom6;
static Ff6Party party, partyDrawn;
static bool partyValid, partyDirty;
static uint32_t* partyPx;
static SDL_Texture* partyTex[2];

/* FF6-2: the battle screen RetroDS draws */
static Ff6BattleView bview, bviewDrawn;
static bool bviewValid, bviewDirty;
static uint32_t* battlePx;
static SDL_Texture* battleTex[2];
/* FF6-2: the party on the game screen, where the battle windows were */
static uint32_t* stripPx;
static SDL_Texture* partyStripTex[2];
static bool stripPxDirty, stripValid;
static Ff6BattleView stripDrawn;
static int waitFrames;
static int queued, queueAt, pressFrame;
/* FF6-3: the list page's first row shown, and the game's cursor's row
 * when it was last seen (the page follows the pad's cursor) */
static int listTop, listSeen = -2, listMenu;

void ff6_open(const char* rom, const char* code) {
	f6 = ff6_is(code);
	if (f6 && !rom6) rom6 = ff6_rom_load(rom);
	if (f6 && !partyPx) partyPx = malloc(sizeof(uint32_t) * FF6_PIC_W * FF6_PIC_H);
	if (f6 && !battlePx) battlePx = malloc(sizeof(uint32_t) * FF6_BATTLE_W * FF6_BATTLE_H);
	if (f6 && !stripPx) stripPx = malloc(sizeof(uint32_t) * FF6_STRIP_W * FF6_STRIP_H);
	bviewValid = stripValid = false;
	waitFrames = 0;
	partyValid = false;
	part = FF6_OTHER;
	queued = queueAt = pressFrame = 0;
	listTop = 0, listSeen = -2, listMenu = 0;
	listFrames = 0;
	memset(&bat, 0, sizeof(bat));
	if (f6) fprintf(stderr, "ff6: Final Fantasy VI (FF6-1 to 5)\n");
}

static void free_tex(SDL_Texture** t) {
	for (int i = 0; i < 2; ++i) {
		if (t[i]) SDL_DestroyTexture(t[i]);
		t[i] = NULL;
	}
}

void ff6_free(void) {
	free_tex(partyTex);
	ff6_rom_free(rom6), rom6 = NULL;
	free(partyPx), partyPx = NULL;
	free_tex(battleTex);
	free(battlePx), battlePx = NULL;
	free_tex(partyStripTex);
	free(stripPx), stripPx = NULL;
	bviewValid = false;
	partyValid = false;
	free_tex(stripTex);
	free_tex(menuTex);
	texW = texH = 0;
	f6 = false;
}

void ff6_textures(void) {
	free_tex(partyTex);
	partyDirty = true;
	free_tex(battleTex);
	bviewDirty = true;
	free_tex(partyStripTex);
	stripPxDirty = true;
	free_tex(stripTex);
	free_tex(menuTex);
	texW = texH = 0; /* (made again with the next frame) */
}

/* After a frame, the core idle: where the game is, its battle menu */
void ff6_tick(void) {
	if (!f6 || !emu.fb) return;
	part = ff6_part(&emu);
	if (part == FF6_BATTLE) ff6_battle(&emu, &bat);
	else bat.open = false;
	/* how long one of the game's lists has been open: the menu passes
	 * through other states for a frame (10, 04, 0D: after a target, as a
	 * list opens), which aren't lists */
	listFrames = bat.open && bat.menu > FF6_MENU_COMMANDS && bat.menu != FF6_MENU_TARGET && bat.menu != FF6_MENU_MAGIC && bat.menu != FF6_MENU_ITEM ? listFrames + 1 : 0;
	/* FF6-2: the battle screen, drawn again when anything on it changes */
	if (part == FF6_BATTLE && rom6 && battlePx) {
		ff6_battle_view(&emu, &bview);
		/* the list page: opened on the page with the game's cursor, which it
		 * follows when the pad moves it; the arrows' pages otherwise */
		int lm = ff6_battle_page(&bview) == FF6_PAGE_LIST ? bview.menu.menu : 0;
		if (lm != listMenu) listMenu = lm, listSeen = -2;
		if (lm && bview.listCursor != listSeen) {
			listSeen = bview.listCursor;
			if (listSeen >= 0 && (listSeen < listTop || listSeen >= listTop + FF6_LIST_PAGE || listSeen == -2))
				listTop = listSeen / FF6_LIST_PAGE * FF6_LIST_PAGE;
		}
		if (listTop >= bview.nList) listTop = bview.nList > 0 ? (bview.nList - 1) / FF6_LIST_PAGE * FF6_LIST_PAGE : 0;
		bview.listTop = listTop;
	}
	if (part == FF6_BATTLE && rom6 && battlePx && bview.ready) {
		/* the waiting page only once nobody's turn has been up for 20
		 * frames (between turns the menu closes for a few), the last page
		 * kept till then */
		waitFrames = ff6_battle_page(&bview) == FF6_PAGE_WAIT ? waitFrames + 1 : 0;
		bool hold = waitFrames && waitFrames < 20 && bviewValid && ff6_battle_page(&bviewDrawn) != FF6_PAGE_WAIT;
		if (!hold && (!bviewValid || memcmp(&bview, &bviewDrawn, sizeof(bview)))) {
			ff6_battle_draw(rom6, &bview, battlePx);
			bviewDrawn = bview, bviewValid = bviewDirty = true;
		}
		/* the party on the game screen: as it is */
		if (stripPx && (!stripValid || memcmp(&bview, &stripDrawn, sizeof(bview)))) {
			ff6_strip_draw(rom6, &bview, stripPx);
			stripDrawn = bview, stripValid = stripPxDirty = true;
		}
	} else {
		bviewValid = stripValid = false;
	}
	/* FF6-5: the party, drawn again when anything it shows has changed */
	if ((part == FF6_FIELD || part == FF6_WORLD) && rom6 && partyPx) {
		ff6_party_read(&emu, &party);
		bool any = false;
		for (int i = 0; i < 4; ++i) any |= party.m[i].here;
		if (any && (!partyValid || memcmp(&party, &partyDrawn, sizeof(party)))) {
			ff6_party_draw(rom6, &party, partyPx);
			partyDrawn = party, partyValid = partyDirty = true;
		} else if (!any) {
			partyValid = false;
		}
	}
	stripDirty = menuDirty = true;
}

/* FF6-2: the battle screen RetroDS draws, but while one of the game's own
 * lists is open (Magic, Item...: not drawn yet, the game's windows moved
 * over instead) */
static bool battle_drawn(void) {
	/* ($7BC2: 00 closed, 01 opening, 05 the command list, 38 choosing a
	 * target; the lists above 05: Magic 0E...) */
	return bviewValid && listFrames < 4;
}

/* FF6-2: the battle windows are the second screen's */
static bool battle_shown(void) {
	return f6 && nScreens == 2 && part == FF6_BATTLE;
}

/* FF6-4: the main menu is the second screen's (the game screen: the logo) */
static bool menu_shown(void) {
	return f6 && nScreens == 2 && part == FF6_MENU;
}

/* FF6-5: the party screen while walking */
static bool party_shown(void) {
	return f6 && nScreens == 2 && (part == FF6_FIELD || part == FF6_WORLD) && partyValid;
}

bool ff6_screen_shown(void) {
	return battle_shown() || menu_shown() || party_shown();
}

bool ff6_cover_game(void) {
	return menu_shown();
}

static void make_tex(SDL_Texture** t, int w, int h);

/* FF6-2: the strip covered on the game screen (img: where the game's
 * picture is) */
void ff6_draw_game_cover(int s, SDL_Rect img) {
	if (!battle_shown() || picH <= 0) return;
	/* (to the picture's bottom: below the windows only their edges' shadow) */
	int y0 = img.y + FF6_STRIP_Y0 * img.h / picH, y1 = img.y + img.h;
	ui_fill(s, (SDL_Rect) { img.x, y0, img.w, y1 - y0 }, (SDL_Color) { 0, 0, 0, 255 });
	/* the party, drawn where the windows were */
	if (!stripValid || !stripPx) return;
	make_tex(partyStripTex, FF6_STRIP_W, FF6_STRIP_H);
	if (stripPxDirty) {
		for (int i = 0; i < nViews; ++i) {
			if (partyStripTex[i]) ui_update_texture(partyStripTex[i], stripPx, FF6_STRIP_W * 4);
		}
		stripPxDirty = false;
	}
	int x0 = img.x + (picW - FF6_STRIP_W) / 2 * img.w / picW;
	ui_image_part(s, partyStripTex, (SDL_Rect) { 0, 0, FF6_STRIP_W, FF6_STRIP_H },
	              (SDL_Rect) { x0, y0, FF6_STRIP_W * img.w / picW, FF6_STRIP_H * img.h / picH });
}

static void make_tex(SDL_Texture** t, int w, int h) {
	for (int i = 0; i < nViews; ++i) {
		if (t[i]) continue;
		t[i] = SDL_CreateTexture(views[i].ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, w, h);
		if (!t[i]) continue;
		SDL_SetTextureBlendMode(t[i], SDL_BLENDMODE_NONE);
		SDL_SetTextureScaleMode(t[i], SDL_ScaleModeNearest);
	}
}

/* the game's picture rows y0 .. y1 (its pixels) into t */
static void upload_rows(SDL_Texture** t, int y0, int y1) {
	int k = picH > 0 ? emu.h / picH : 1; /* (interlaced: two lines a pixel) */
	if (k < 1) k = 1;
	for (int i = 0; i < nViews; ++i) {
		if (t[i]) ui_update_texture(t[i], emu.fb + (size_t) y0 * k * emu.w, emu.w * 4);
	}
	(void) y1;
}

/* where a picture goes on the second screen: as the game's own picture is
 * shown (its Scale: 4:3, a TV's shape, or Pixel Perfect, whole pixels),
 * so the two screens match. A picture the game's size (the party screen,
 * the menu) or 4:3 (the battle screen's 320 x 240) */
static SDL_Rect place(int s, int w, int h) {
	return ui_image_rect(s, w, h, cfg->scale);
}

/* rows y0 .. y0 + h of the game's picture, where they'd be were the whole
 * picture placed (the moved battle strip), in the middle */
static SDL_Rect place_rows(int s, int h) {
	SDL_Rect r = place(s, picW, picH);
	int hh = h * r.h / picH;
	return (SDL_Rect) { r.x, r.y + (r.h - hh) / 2, r.w, hh };
}

void draw_ff6(int s) {
	if (!emu.fb || picW <= 0 || picH <= 0) {
		draw_logo_at(s, 0);
		return;
	}
	if (texW != emu.w || texH != emu.h) {
		free_tex(stripTex);
		free_tex(menuTex);
		texW = emu.w, texH = emu.h;
	}
	int kh = emu.h / picH;
	if (kh < 1) kh = 1;
	if (party_shown()) {
		make_tex(partyTex, FF6_PIC_W, FF6_PIC_H);
		if (partyDirty) {
			for (int i = 0; i < nViews; ++i) {
				if (partyTex[i]) ui_update_texture(partyTex[i], partyPx, FF6_PIC_W * 4);
			}
			partyDirty = false;
		}
		ui_image_part(s, partyTex, (SDL_Rect) { 0, 0, FF6_PIC_W, FF6_PIC_H }, place(s, FF6_PIC_W, FF6_PIC_H));
		return;
	}
	if (battle_shown() && !bviewValid) {
		/* (the battle starting: nothing to show yet) */
		draw_logo_at(s, 0);
		return;
	}
	if (battle_shown() && battle_drawn()) {
		make_tex(battleTex, FF6_BATTLE_W, FF6_BATTLE_H);
		if (bviewDirty) {
			for (int i = 0; i < nViews; ++i) {
				if (battleTex[i]) ui_update_texture(battleTex[i], battlePx, FF6_BATTLE_W * 4);
			}
			bviewDirty = false;
		}
		ui_image_part(s, battleTex, (SDL_Rect) { 0, 0, FF6_BATTLE_W, FF6_BATTLE_H }, place(s, FF6_BATTLE_W, FF6_BATTLE_H));
		return;
	}
	if (battle_shown()) {
		int rows = FF6_STRIP_Y1 - FF6_STRIP_Y0;
		make_tex(stripTex, emu.w, rows * kh);
		if (stripDirty) upload_rows(stripTex, FF6_STRIP_Y0, FF6_STRIP_Y1), stripDirty = false;
		ui_image_part(s, stripTex, (SDL_Rect) { 0, 0, emu.w, rows * kh }, place_rows(s, rows));
	} else if (menu_shown()) {
		make_tex(menuTex, emu.w, emu.h);
		if (menuDirty) upload_rows(menuTex, 0, picH), menuDirty = false;
		ui_image_part(s, menuTex, (SDL_Rect) { 0, 0, emu.w, emu.h }, place(s, picW, picH));
	} else {
		draw_logo_at(s, 0);
	}
}

/* FF6-3: the writes for a tap (the menu's cursor where the tap was),
 * then A as the player's: the game chooses as from its own pad */
static void choose_keys(const char* what, const Write* w, int n, const uint32_t* keys, int nKeys) {
	if (n < 0) return;
	if (n > 0) {
		int nOk;
		const WriteRange* ok = ff6_write_ranges(&nOk);
		char label[96];
		snprintf(label, sizeof label, "(frame %llu) %s", (unsigned long long) frameCount, what);
		if (emu_apply(&emu, label, w, n, ok, nOk) != n) return;
	}
	for (int i = 0; i < nKeys && i < 8; ++i) queue[i] = keys[i];
	queued = nKeys, queueAt = 0, pressFrame = 0;
	fprintf(stderr, "ff6: %s: %d presses\n", what, nKeys);
}

static void choose(const char* what, const Write* w, int n) {
	static const uint32_t a[1] = { K_A };
	choose_keys(what, w, n, a, 1);
}

static void press(const char* what, uint32_t key) {
	queue[0] = key, queued = 1, queueAt = 0, pressFrame = 0;
	fprintf(stderr, "ff6: %s\n", what);
}

/* FF6-3: a tap on the drawn battle screen (on the moved windows, the
 * stopgap while a list not drawn yet is open: the pad's) */
int ff6_touch(const Pointer* p) {
	if (!battle_shown() || p->screen == game_screen()) return 0;
	if (p->kind != 1) return 1;
	if (queued > queueAt) return 1; /* (the last tap's press still playing) */
	if (!bviewValid || !battle_drawn()) return 1;
	SDL_Rect d = place(p->screen, FF6_BATTLE_W, FF6_BATTLE_H);
	if (p->x < d.x || p->y < d.y || p->x >= d.x + d.w || p->y >= d.y + d.h) return 1;
	int bx = (p->x - d.x) * FF6_BATTLE_W / d.w, by = (p->y - d.y) * FF6_BATTLE_H / d.h;
	int arg, hit = ff6_battle_hit(&bviewDrawn, bx, by, &arg); /* (what's on the screen) */
	Write w[4];
	char what[64];
	if (hit == FF6_HIT_COMMAND) {
		snprintf(what, sizeof what, "command %d (%d)", arg, bat.cmd[arg & 3]);
		choose(what, w, ff6_command_writes(&bat, arg, w));
	} else if (hit == FF6_HIT_STATUS && bat.open && bat.menu == FF6_MENU_COMMANDS) {
		press("whose turn it is: X (the next character ready)", K_X);
	} else if (hit == FF6_HIT_BACK && bat.open) {
		press("Back: B", K_B);
	} else if ((hit == FF6_HIT_ENEMY || hit == FF6_HIT_PARTY) && bat.open && bat.menu == FF6_MENU_TARGET) {
		int want = hit == FF6_HIT_ENEMY ? 4 + arg : arg;
		snprintf(what, sizeof what, "target %d", want);
		choose(what, w, ff6_target_writes(&bat, want, w));
	} else if (hit == FF6_HIT_ROW && arg < bviewDrawn.nList) {
		snprintf(what, sizeof what, "list row %d (%d)", bviewDrawn.listAt[arg], bviewDrawn.listId[arg]);
		uint32_t keys[3];
		int nk = ff6_list_keys(&bat, bviewDrawn.listAt[arg], keys);
		if (!bviewDrawn.listOff[arg]) choose_keys(what, w, ff6_list_writes(&bat, bviewDrawn.listAt[arg], w), keys, nk);
	} else if (hit == FF6_HIT_UP || hit == FF6_HIT_DOWN) {
		listTop += hit == FF6_HIT_UP ? -FF6_LIST_PAGE : FF6_LIST_PAGE;
		if (listTop < 0) listTop = 0;
	}
	return 1;
}

/* The game's keys for the next frame: the queued presses while they play
 * (the player's own buttons held back meanwhile) */
uint32_t ff6_keys(uint32_t keys) {
	if (!f6 || queueAt >= queued) return keys;
	uint32_t k = pressFrame < 3 ? queue[queueAt] : 0;
	if (++pressFrame >= 12) pressFrame = 0, ++queueAt;
	return k;
}
