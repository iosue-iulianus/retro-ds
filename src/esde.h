/* SNES DS in ES-DE (ES-DE Frontend, the EmulationStation Desktop Edition) on
 * Android.
 *
 * ES-DE reads ES-DE/custom_systems/es_systems.xml and es_find_rules.xml on
 * top of its own lists; a system there replaces ES-DE's own one of the same
 * name. Turning this on:
 * - adds the find rule SNES-DS (this app's LauncherActivity) to
 *   es_find_rules.xml, and
 * - puts "SNES DS (Standalone)" first among the gba system's emulators in
 *   es_systems.xml, so it's the default: into the player's own gba entry if
 *   they have one, else into a copy of ES-DE's own (marked, so it can be
 *   brought up to date and taken out again).
 * Other emulators stay in the list, so a game SNES DS doesn't support can be
 * switched to one of them (ES-DE: the game's metadata > Alternative emulator).
 * Turning it off takes out only what SNES DS put in, and deletes a file it
 * made once nothing else is left in it. ES-DE reads the files when it starts. */
#ifndef RETRODS_ESDE_H
#define RETRODS_ESDE_H

#include <stdbool.h>
#include <stddef.h>

/* ES-DE's folder was found (it has settings/es_settings.xml). */
bool esde_present(void);
bool esde_integration_on(void);
/* Turns it on or off; msg gets a short message for the player. */
bool esde_integration_set(bool on, char* msg, size_t msgSize);
/* If it's on, brings SNES DS's entries up to date (ES-DE's RetroArch SAF
 * mode switched, a newer copy of ES-DE's list in a newer SNES DS). */
void esde_integration_refresh(void);

/* For tests: the edits on files' text, without touching the disk.
 * Each returns a new string (malloc) or NULL if nothing changes. */
char* esde_systems_add(const char* xml, bool saf);
char* esde_systems_remove(const char* xml);
char* esde_rules_add(const char* xml);
char* esde_rules_remove(const char* xml);

#endif
