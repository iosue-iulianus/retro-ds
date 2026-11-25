/* Hotkeys: the hotkey button (HOME, SELECT, START or L3) held with an
 * action's button does the action in a game (game.c). Set in retrods.cfg
 * (hotkey, hotkey_quit ...) or with Settings > Hotkeys (hotkeys_run), which
 * keeps its choices in <state_dir>/hotkeys.cfg, read after retrods.cfg. */
#ifndef RETRODS_HOTKEYS_H
#define RETRODS_HOTKEYS_H

#include "config.h"

/* retrods.cfg's name for a button (B_*: "a", "start", "l3" ...) */
extern const char* const hotkeyBtnNames[];
/* The hotkey button and each action's button (B_*, or -1 for none), as the
 * game uses them: unknown names are none, HOME and the hotkey button itself
 * are no action's, a button is only the first action's that has it, and
 * Quit on START with START as the hotkey is SELECT. Logs what it drops when
 * log is set. */
void hotkeys_resolve(const Config* cfg, int* mod, int btn[HK_COUNT], bool log);
/* Settings > Hotkeys: a screen of its own, until Done, on the touch screen;
 * other (may be NULL) draws the other screen meanwhile. */
void hotkeys_run(Config* cfg, void (*other)(int scr));

#endif
