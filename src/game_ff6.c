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
static int queued, queueAt, pressFrame;
/* FF6-3: a tapped target the game's cursor is being moved to */
static Ff6Aim aim = { .want = -1 };

void ff6_open(const char* rom, const char* code) {
	f6 = ff6_is(code);
	if (f6 && !rom6) rom6 = ff6_rom_load(rom);
	if (f6 && !partyPx) partyPx = malloc(sizeof(uint32_t) * FF6_PIC_W * FF6_PIC_H);
	if (f6 && !battlePx) battlePx = malloc(sizeof(uint32_t) * FF6_BATTLE_W * FF6_BATTLE_H);
	bviewValid = false;
	partyValid = false;
	part = FF6_OTHER;
	queued = queueAt = pressFrame = 0;
	ff6_aim_start(&aim, -1);
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
	listFrames = bat.open && bat.menu > FF6_MENU_COMMANDS && bat.menu != FF6_MENU_TARGET ? listFrames + 1 : 0;
	/* FF6-2: the battle screen, drawn again when anything on it changes */
	if (part == FF6_BATTLE && rom6 && battlePx) {
		ff6_battle_view(&emu, &bview);
		if (!bviewValid || memcmp(&bview, &bviewDrawn, sizeof(bview))) {
			ff6_battle_draw(rom6, &bview, battlePx);
			bviewDrawn = bview, bviewValid = bviewDirty = true;
		}
	} else {
		bviewValid = false;
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

/* FF6-2: the strip covered on the game screen (img: where the game's
 * picture is) */
void ff6_draw_game_cover(int s, SDL_Rect img) {
	if (!battle_shown() || picH <= 0) return;
	/* (to the picture's bottom: below the windows only their edges' shadow) */
	int y0 = img.y + FF6_STRIP_Y0 * img.h / picH, y1 = img.y + img.h;
	ui_fill(s, (SDL_Rect) { img.x, y0, img.w, y1 - y0 }, (SDL_Color) { 0, 0, 0, 255 });
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

/* where the moved picture goes on the second screen: whole screen pixels
 * per game pixel, as large as fits, in the middle */
static SDL_Rect place(int s, int w, int h) {
	SDL_Rect r = screens[s].rect;
	int k = SDL_min(r.w / w, r.h / h);
	if (k < 1) k = 1;
	return (SDL_Rect) { (r.w - w * k) / 2, (r.h - h * k) / 2, w * k, h * k };
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
		ui_image_part(s, stripTex, (SDL_Rect) { 0, 0, emu.w, rows * kh }, place(s, picW, rows));
	} else if (menu_shown()) {
		make_tex(menuTex, emu.w, emu.h);
		if (menuDirty) upload_rows(menuTex, 0, picH), menuDirty = false;
		ui_image_part(s, menuTex, (SDL_Rect) { 0, 0, emu.w, emu.h }, place(s, picW, picH));
	} else {
		draw_logo_at(s, 0);
	}
}

/* FF6-3: a tap on the moved battle windows. A command in the command
 * list: the presses that choose it; choosing a target: A (the target the
 * game's cursor is on; the D-pad moves it). */
int ff6_touch(const Pointer* p) {
	if (!battle_shown() || p->screen == game_screen()) return 0;
	if (p->kind != 1) return 1;
	if (queued > queueAt || aim.want >= 0) return 1; /* (the last tap's presses still playing) */
	if (battle_drawn()) {
		/* the drawn battle screen: its command rows */
		SDL_Rect d = place(p->screen, FF6_BATTLE_W, FF6_BATTLE_H);
		if (p->x < d.x || p->y < d.y || p->x >= d.x + d.w || p->y >= d.y + d.h) return 1;
		int bx = (p->x - d.x) * FF6_BATTLE_W / d.w, by = (p->y - d.y) * FF6_BATTLE_H / d.h;
		if (bat.open && bat.menu == FF6_MENU_TARGET) {
			/* an enemy's name, a character in the party window: aim
			 * there; elsewhere: A on the target the cursor is on */
			int want = -1;
			bool named = false;
			if (bx >= FF6_BBACK_X0 && by >= FF6_BBACK_Y0) {
				queue[0] = K_B, queued = 1, queueAt = 0, pressFrame = 0;
				fprintf(stderr, "ff6: tap on Back: B\n");
				return 1;
			} else if (bx >= FF6_BCMD_X1 && by >= FF6_BCMD_Y0) {
				int k = (by - FF6_BCMD_Y0) / FF6_BCMD_DY;
				if (k >= 0 && k < 4 && bview.enemy[k] >= 0) named = true, want = ff6_group_target(&bat, bview.enemy[k]);
			} else if (by < FF6_BPARTY_Y1) {
				int s = (bx >= FF6_BATTLE_W / 2) + 2 * (by >= FF6_BPARTY_Y1 / 2);
				if (bview.f[s].here) named = true, want = s;
			}
			if (want >= 0) {
				ff6_aim_start(&aim, want);
				fprintf(stderr, "ff6: tap on a target (%d): aiming\n", want);
			} else if (!named) {
				queue[0] = K_A, queued = 1, queueAt = 0, pressFrame = 0;
				fprintf(stderr, "ff6: tap while choosing a target: A\n");
			}
		} else if (bx < FF6_BCMD_X1 && by >= FF6_BCMD_Y0) {
			int row = (by - FF6_BCMD_Y0) / FF6_BCMD_DY;
			int n = ff6_command_presses(&bat, row, queue);
			if (n > 0) {
				queued = n, queueAt = 0, pressFrame = 0;
				fprintf(stderr, "ff6: tap on command row %d (command %d): %d presses\n", row, bat.cmd[row & 3], n);
			}
		}
		return 1;
	}
	int rows = FF6_STRIP_Y1 - FF6_STRIP_Y0;
	SDL_Rect d = place(p->screen, picW, rows);
	if (p->x < d.x || p->y < d.y || p->x >= d.x + d.w || p->y >= d.y + d.h) return 1;
	int gx = (p->x - d.x) * picW / d.w, gy = FF6_STRIP_Y0 + (p->y - d.y) * rows / d.h;
	if (bat.open && bat.menu == FF6_MENU_TARGET) {
		queue[0] = K_A, queued = 1, queueAt = 0, pressFrame = 0;
		fprintf(stderr, "ff6: tap while choosing a target: A\n");
		return 1;
	}
	if (gx < FF6_CMD_X0 || gx >= FF6_CMD_X1 || gy < FF6_CMD_Y0) return 1;
	int row = (gy - FF6_CMD_Y0) / FF6_CMD_DY;
	int n = ff6_command_presses(&bat, row, queue);
	if (n > 0) {
		queued = n, queueAt = 0, pressFrame = 0;
		fprintf(stderr, "ff6: tap on command row %d (command %d): %d presses\n", row, bat.cmd[row & 3], n);
	}
	return 1;
}

/* The game's keys for the next frame: the queued presses while they play
 * (the player's own buttons held back meanwhile) */
uint32_t ff6_keys(uint32_t keys) {
	if (!f6) return keys;
	if (queueAt >= queued && aim.want >= 0) {
		/* FF6-3: the next press toward the tapped target, by where the
		 * last one took the cursor */
		uint32_t k = ff6_aim_next(&aim, &bat);
		if (k) queue[0] = k, queued = 1, queueAt = 0, pressFrame = 0;
		else fprintf(stderr, "ff6: the target cursor couldn't get there\n");
		if (k == K_A) fprintf(stderr, "ff6: on the target: A\n");
	}
	if (queueAt >= queued) return keys;
	uint32_t k = pressFrame < 3 ? queue[queueAt] : 0;
	if (++pressFrame >= 12) pressFrame = 0, ++queueAt;
	return k;
}
