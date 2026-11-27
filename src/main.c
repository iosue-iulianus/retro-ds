/* RetroDS: Super Nintendo and PlayStation games on dual-screen handhelds,
 * with the second screen they never had, drawn from the game's own data.
 *
 *   emu.c      the libretro cores (bsnes, SwanStation); system.c per console
 *   library.c  game list (rescans the ROM folders every time it opens)
 *   game.c     play session, pause menu, auto save/resume; the second
 *              screens in game_sm.c, game_alttp.c, game_sotn.c
 *   profiles.c per game; mapcap.c capture on a hidden copy (no game uses it)
 *   ui.c       windows, screens, touch/buttons, drawing
 */
#include "android.h"
#include "config.h"
#include "esde.h"
#include "game.h"
#include "library.h"
#include "logo.h"
#include "paths.h"
#include "ui.h"
#include "ra.h"
#include "update.h"
#include "clocks.h"
#include "i18n.h"
#include "emu.h"
#include "profiles.h"

#include <dirent.h>
#include <libgen.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

/* true if the folder has a .png in it */
static bool dir_has_png(const char* path) {
	DIR* d = opendir(path);
	if (!d) return false;
	bool found = false;
	struct dirent* e;
	while (!found && (e = readdir(d))) {
		size_t n = strlen(e->d_name);
		found = n > 4 && !strcasecmp(e->d_name + n - 4, ".png");
	}
	closedir(d);
	return found;
}

static void on_signal(int sig) {
	(void) sig;
	uiQuit = 1;
}

#if defined(__linux__) && !defined(__ANDROID__) && defined(__GLIBC__)
#include <execinfo.h>
/* A crash: where it was, into the log (for reports from systems we can't
 * run ourselves); then the default action. */
static void on_crash(int sig) {
	char msg[64];
	int n = snprintf(msg, sizeof(msg), "crashed: signal %d; backtrace:\n", sig);
	if (write(2, msg, n) < 0) { /* (nothing to do) */ }
	void* frames[48];
	int k = backtrace(frames, 48);
	backtrace_symbols_fd(frames, k, 2);
	signal(sig, SIG_DFL);
	raise(sig);
}
#endif

int main(int argc, char** argv) {
	for (int i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "--version")) {
			printf("RetroDS %s\n", RETRODS_VERSION);
			return 0;
		}
		/* --supported ROM: exit 0 if RetroDS has second-screen support for the
		 * game (a profile for its code), 1 if not, 2 if it can't be read */
		if (!strcmp(argv[i], "--supported") && i + 1 < argc) {
			char code[GAME_CODE_N];
			if (!emu_rom_code(argv[i + 1], code)) return 2;
			bool yes = profile_find(code) != NULL;
			printf("%s: %s\n", code, yes ? "supported" : "not supported");
			return yes ? 0 : 1;
		}
	}
#ifdef __ANDROID__
	/* RetroDS's folder where the player chose it (Settings > Folders), its
	 * files moved there first if it's new: before the log, which goes in it */
	android_data_setup();
	android_init_log();
#endif
	fprintf(stderr, "RetroDS %s\n", RETRODS_VERSION);
	Config cfg;
	config_defaults(&cfg);
#ifdef __ANDROID__
	android_config_defaults(&cfg);
#endif

	char exeDir[1024] = ".";
	char exe[1024];
	ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (n > 0) {
		exe[n] = 0;
		snprintf(exeDir, sizeof(exeDir), "%s", dirname(exe));
	}
	char binDir[1024]; /* (where the program is, whatever RETRODS_HOME says) */
	snprintf(binDir, sizeof(binDir), "%s", exeDir);
	/* RETRODS_HOME: the app's folder when the executable runs from a copy
	 * elsewhere (launch.sh, where the card won't run programs) */
	if (getenv("RETRODS_HOME") && getenv("RETRODS_HOME")[0]) snprintf(exeDir, sizeof(exeDir), "%s", getenv("RETRODS_HOME"));
#ifdef __ANDROID__
	/* the executable is Android's app_process: use the app's own folder */
	if (android_home()) snprintf(exeDir, sizeof(exeDir), "%s", android_home());
#endif

	/* defaults < retrods.cfg < states/prefs.cfg (choices made in the menu),
	 * library.cfg and hotkeys.cfg (the game list's Settings) < command line */
	char path[1100];
	if (getenv("RETRODS_CONFIG")) {
		config_load_file(&cfg, getenv("RETRODS_CONFIG"));
	} else {
		snprintf(path, sizeof(path), "%s/retrods.cfg", exeDir);
		config_load_file(&cfg, path);
		if (getenv("HOME")) {
			snprintf(path, sizeof(path), "%s/.config/retrods/retrods.cfg", getenv("HOME"));
			/* RetroDS was SNES DS: without a retrods.cfg, read the old one */
			if (access(path, R_OK) != 0)
				snprintf(path, sizeof(path), "%s/.config/snes-ds/snes-ds.cfg", getenv("HOME"));
			config_load_file(&cfg, path);
		}
#if !defined(__ANDROID__)
		/* data-folder.cfg: data_dir, where RetroDS keeps its own files (in a
		 * file of its own, which neither the zip nor an update has) */
		snprintf(path, sizeof(path), "%s/data-folder.cfg", exeDir);
		config_load_file(&cfg, path);
#endif
	}
	if (!config_parse_args(&cfg, argc, argv)) {
		config_usage(argv[0]);
		return 1;
	}
	/* RetroDS's data folder (states/, cheats/, overlays/, logos/...): the
	 * app's folder, or data_dir, moved there when that changes (paths.c) */
	char dataDir[480];
	snprintf(dataDir, sizeof(dataDir), "%s", exeDir);
#if !defined(__ANDROID__)
	snprintf(path, sizeof(path), "%s/data-folder.cfg", exeDir);
	data_dir_setup(exeDir, getenv("RETRODS_CONFIG") ? NULL : path, cfg.dataDir, false, dataDir, sizeof(dataDir));
#endif
	if (strcmp(dataDir, exeDir) || data_dir_note()[0])
		fprintf(stderr, "data folder: %s%s%s\n", dataDir, data_dir_note()[0] ? " - " : "", data_dir_note());
	if (!cfg.assetDir[0]) snprintf(cfg.assetDir, sizeof(cfg.assetDir), "%s", dataDir);
	if (!cfg.patchDir[0]) snprintf(cfg.patchDir, sizeof(cfg.patchDir), "%s/patches", cfg.assetDir); /* GEN-9 */
	if (!cfg.stateDir[0]) snprintf(cfg.stateDir, sizeof(cfg.stateDir), "%s/states", dataDir);
	mkdirs(cfg.stateDir);
	{
		/* The emulators' core copies (emu.h), and a cache for them and the
		 * ROMs as played. Android: the cores built into the app's native
		 * libraries (RETRODS_CORE_DIR, from the Java side): bsnes under
		 * several sonames, libsnes_a.so..., SwanStation once,
		 * libswanstation.so. The desktop: RETRODS_CORE, else
		 * bsnes_libretro.so beside the program, and RETRODS_PSX_CORE, else
		 * swanstation_libretro.so, copied into the cache once for each
		 * copy. The player's BIOS files: bios/ in the data folder
		 * (RetroDS/bios on Android). */
		char cache[1100];
		snprintf(cache, sizeof(cache), "%s/cache", cfg.stateDir);
		char bios[1100];
		snprintf(bios, sizeof(bios), "%s/bios", dataDir);
		emu_set_bios_dir(bios);
#ifdef __ANDROID__
		const char* dir = getenv("RETRODS_CORE_DIR");
		char paths[EMU_CORES][1100];
		const char* list[EMU_CORES];
		for (int i = 0; i < EMU_CORES; ++i) {
			snprintf(paths[i], sizeof(paths[i]), "%s/libsnes_%c.so", dir ? dir : ".", 'a' + i);
			list[i] = paths[i];
		}
		emu_set_cores(SYS_SNES, list, EMU_CORES, cache);
		snprintf(paths[0], sizeof(paths[0]), "%s/libswanstation.so", dir ? dir : ".");
		emu_set_cores(SYS_PSX, list, 1, cache);
#else
		char core[1100];
		if (getenv("RETRODS_CORE") && getenv("RETRODS_CORE")[0]) snprintf(core, sizeof(core), "%s", getenv("RETRODS_CORE"));
		else snprintf(core, sizeof(core), "%s/bsnes_libretro.so", binDir);
		if (!emu_set_core_copies(SYS_SNES, core, cache)) fprintf(stderr, "no emulator core: SNES games won't start (RETRODS_CORE, or %s/bsnes_libretro.so)\n", binDir);
		if (getenv("RETRODS_PSX_CORE") && getenv("RETRODS_PSX_CORE")[0]) snprintf(core, sizeof(core), "%s", getenv("RETRODS_PSX_CORE"));
		else snprintf(core, sizeof(core), "%s/swanstation_libretro.so", binDir);
		if (!emu_set_core_copies(SYS_PSX, core, cache)) fprintf(stderr, "no PlayStation core: its games won't start (RETRODS_PSX_CORE, or %s/swanstation_libretro.so)\n", binDir);
#endif
	}
	snprintf(path, sizeof(path), "%s/prefs.cfg", cfg.stateDir);
	/* (map_interval and show_fps were left to retrods.cfg until v1.5.4; the game list's Settings can change them now) */
	config_load_file(&cfg, path);
	/* the game list's Settings (Android: the ROM folder) */
	snprintf(path, sizeof(path), "%s/library.cfg", cfg.stateDir);
	config_load_file(&cfg, path);
	/* Settings > Hotkeys */
	snprintf(path, sizeof(path), "%s/hotkeys.cfg", cfg.stateDir);
	config_load_file(&cfg, path);
	config_parse_args(&cfg, argc, argv);
	if (cfg.overlayMode == OVM_LEGACY) {
		/* overlays = top / both... from before v1.5 (the player's PNGs, which
		 * had no bezel to fall back on): Custom if overlays/ has any, else
		 * the generated bezel */
		snprintf(path, sizeof(path), "%s/overlays", cfg.assetDir);
		cfg.overlayMode = dir_has_png(path) ? OVM_CUSTOM : OVM_GENERATED;
	}

#ifdef __ANDROID__
	/* keep ES-DE's entry current (ES-DE's RetroArch SAF mode may have changed) */
	esde_integration_refresh();
#endif

#if defined(__linux__) && !defined(__ANDROID__) && defined(__GLIBC__)
	signal(SIGSEGV, on_crash);
	signal(SIGBUS, on_crash);
	signal(SIGILL, on_crash);
	signal(SIGFPE, on_crash);
	signal(SIGABRT, on_crash);
#endif
	/* the Performance setting (clocks.c; the game list's Settings) */
	if (cfg.perfMax || clocks_available()) clocks_set(cfg.perfMax);
	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);
	signal(SIGHUP, on_signal);

	/* the interface's language (Settings > System > Language): before anything is drawn */
	i18n_init(cfg.language);

	if (!ui_init(&cfg, true)) return 1;
	ui_language_changed(); /* (the fonts the language's script needs) */
	ui_accent(cfg.accent);
	ui_theme(cfg.lightTheme);
	ra_init(&cfg); /* signs in to RetroAchievements in the background */
	update_init(&cfg, exeDir, argc, argv);
#ifdef __ANDROID__
	fprintf(stderr, "startup: screens ready\n"); /* logcat's timestamps time the start-up steps */
#endif
	if (cfg.inputScript[0]) ui_load_script(cfg.inputScript);

	if (cfg.rom[0]) {
		while (game_run(&cfg, cfg.rom, false) == GAME_RELOAD && !uiQuit) fprintf(stderr, "restarting %s\n", cfg.rom);
	} else {
		char rom[1024];
		while (!uiQuit && library_run(&cfg, rom, sizeof(rom))) {
			fprintf(stderr, "starting %s\n", rom);
			enum GameExit x;
			while ((x = game_run(&cfg, rom, true)) == GAME_RELOAD && !uiQuit) fprintf(stderr, "restarting %s\n", rom);
			if (x == GAME_QUIT_APP) break;
		}
	}
	logo_shutdown();
	ra_shutdown();
	ui_shutdown();
	if (cfg.perfMax) clocks_set(false); /* (the governors as they were) */
	return update_restart_if_installed(); /* an update was installed: start it */
}
