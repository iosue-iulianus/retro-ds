# RetroDS: scope proposal

Status: proposal for the owner, written Nov 23, 2026 on branch `scope-retrods`
(cut from `m3-a-link-to-the-past`). Plan only: nothing here has been renamed,
refactored or added yet. Every licence and decompilation figure was checked on
Nov 23, 2026 against the primary source linked next to it; anything that
couldn't be checked that way says **unverified**.

Contents:

1. [Summary and recommendation](#1-summary-and-recommendation)
2. [Architecture](#2-architecture)
3. [Cores per system](#3-cores-per-system)
4. [Rename to RetroDS](#4-rename-to-retrods)
5. [Philosophy and CLAUDE.md drafts](#5-philosophy-and-claudemd-drafts)
6. [Game list and settings](#6-game-list-and-settings)
7. [Titles](#7-titles)
8. [Open questions for the owner](#8-open-questions-for-the-owner)
9. [Sources](#9-sources)

---

## 1. Summary and recommendation

**What RetroDS is:** your favourite games from several consoles, each given the
second screen it would have had as a DS game. The game plays on one screen as
the console draws it; the second screen is drawn by RetroDS from the game's own
memory and graphics, and you can touch it. Games without a module still play,
on one screen, with their logo or name on the other.

**What the repo already proves.** Super Metroid and A Link to the Past are drawn
entirely from RAM, VRAM and ROM, without the hidden copy; touch writes go
through per-game routines that mirror the game's own menu code, applied between
frames. The structure that grew around them is SNES-shaped (SNES addresses,
128 KB WRAM offsets, 256 x 224 constants, ~560 lines of per-game code inside
`game.c`), and the capture machinery (`mapcap.c`, the third core copy) no
longer runs for any game.

**Findings that shape the order:**

- **PSX is ready now; N64 and GameCube aren't.** All three PSX cores hand over a
  software framebuffer, which is all `emu.c` supports today. N64 at good speed
  and GameCube need a GPU context (libretro hardware rendering), which RetroDS
  doesn't have.
- **N64 has a licence trap.** mupen64plus-next always compiles in GLideN64,
  whose licence says "GNU General Public License version 2" with no "or later":
  incompatible with RetroDS's GPLv3-only licence. A build without GLideN64
  (ParaLLEl-RDP, MIT, needs Vulkan; or angrylion, software, licence unverified)
  is possible but is a build patch plus real work.
- **DuckStation is out** (CC BY-NC-ND 4.0 since September 2024, confirmed in
  its repo today). SwanStation, its GPL fork, is GPLv3 and active: the PSX pick.
- **Several decomp figures in the brief are out of date**, mostly upward:
  Wind Waker is at 80.2% code (not ~60%), Metroid Prime at 99.4% (not ~24%),
  Silent Hill is 100% code and GPLv3, Paper Mario US is 100%. Metal Gear Solid's
  project targets the Japanese *Integral* release, not the US disc.

**Recommended order:**

| Step | What | Why now |
|---|---|---|
| 0 | **Groundwork, SNES only**: merge M2 and M3; split `game.c`'s per-game code into game modules behind one interface; one write gate (whitelist, gameplay check, log, hardcore off); remove the hidden copy (`mapcap.c`); rename to RetroDS with a migration path for the SNES DS folder. | Every later system builds on this; doing it with two finished games as tests keeps it honest. |
| 1 | **PSX platform + Castlevania: Symphony of the Night.** SwanStation, BIOS, discs (cue/bin, CHD, m3u), disc identity by serial. Castle map with % first (read only), then relics by touch, then equipment. | Owner's pick; SOTN's map is Super Metroid's design problem again, so the approach transfers directly. |
| 2 | **Silent Hill** (suggested addition). | The best PSX source there is (100% code, GPLv3), and its paper maps are the most natural second screen of any candidate. |
| 3 | **N64 spike, then Paper Mario (menus).** Time-boxed experiment first: angrylion's speed on the Duo (needs no GPU work) vs ParaLLEl-RDP (needs Vulkan); licence decision. Then Paper Mario's badges, items, partners, map, stats. | Best sources of all (100%) and a proven touch design, but the platform has to be settled first. |
| 4 | **Metal Gear Solid**: Soliton radar, weapon and item select by touch; codec later. | Owner's pick; harder because the source is for Integral and stage overlays are partly done. |
| 5 | **Ocarina of Time / Majora's Mask** on the N64 path Paper Mario opened. | The 3DS remakes already proved the layout. |
| Later | Paper Mario battle touch; Wind Waker and Twilight Princess (GameCube). | Battle touch drives a menu, a riskier kind of write. GameCube: "not yet" (section 3.3). |

Cheap SNES fillers whenever a break from platform work is wanted: **Final
Fantasy VI** (party status; it replaces the old M4 plan's FF entries, with a
better source than Chrono Trigger has) and **Harvest Moon** (calendar, weather,
farm).

**N64 before PSX?** No, after. The N64's sources and design precedent are the
strongest, but its platform risk (GPU context, licence, an unmeasured software
renderer) is the largest, and the owner's top two picks are PSX. If the step 3
spike shows angrylion at full speed on the Duo, N64 needs no GPU work and can
run alongside PSX instead of after it.

---

## 2. Architecture

### 2.1 What the code does today

Read from `src/` on `m3-a-link-to-the-past`:

- **`emu.c`** loads one libretro core into a pool of copies (`EMU_CORES` = 3:
  the game, the map's hidden copy, the logo's), each a separate `.so` because
  cores keep state in globals. It loads ROMs as bytes (copier header stripped,
  patches applied, cached by CRC32), reads memory through
  `retro_get_memory_data` (WRAM and SRAM, and VRAM through
  `scripts/cores/bsnes-memory.patch`), and maps SNES CPU addresses (`$7E0000`)
  onto WRAM in `emu_read8` / `emu_write8`. Environment calls: no
  `SET_HW_RENDER`, no disc control interface, no core options beyond defaults.
- **`smmap.c`** (Super Metroid) and **`alttp.c` / `alttp_dungeon.c`** (A Link to
  the Past) are self-contained: `*_new(rom)`, `*_update(wram, vram, sram)`
  returning change flags, state queries (`smmap_live`, `alttp_playing`,
  `*_in_menu`), pictures as ABGR8888 buffers, hit testing
  (`smmap_hit`, `alttp_items_hit`), and write generators (`smmap_toggle`,
  `alttp_select`) that return lists of `{offset, byte}` pairs.
- **`game.c`** owns everything around them: `native_*` (130 references) and
  `alt_*` (431 references) for textures, scrolling, pinch, zoom, page state,
  worker threads, state blocks and touch. The modules are switched by
  `MapProfile.native` (`NATIVE_SUPER_METROID`, `NATIVE_ALTTP`).
- **`profiles.c`** has two profiles. Most of `MapProfile` (open macros, probes,
  crops, masks, glyph erasing, recolouring, paint, capture touch) serves the
  capture path, which neither profile uses: `game.c:6557` creates a
  `MapCapture` only when neither native module is active.

**What is SNES-specific in the modules today:**

| Where | SNES assumption |
|---|---|
| addresses | `$7E` bank prefix added at each call site (`emu_write8(&emu, 0x7E0000 + out[i].addr, ...)`), offsets into a 128 KB WRAM |
| ROM access | `r16(m, 0x82C04C)`: LoROM bank:address reads from the cartridge image |
| graphics | 4bpp planar tiles from VRAM or decompressed from ROM, BGR555 palettes, 8 x 8 tiles |
| picture size | `SMMAP_W` 256, `ALTTP_W` x `ALTTP_H` 256 x 224, `SNES_W` / `SNES_H` in `game.c`'s second-screen code |
| save data | `sram` 8 KB, its `$1FFE` slot byte (ALttP) |
| identity | ROM CRC32 with the copier header stripped |
| logo | the profile's `logo` steps run on a third core from power-on |

**What is already general:** the shape. Update once a frame from memory, report
what changed, expose pictures and layers, test hits, generate a list of writes,
keep an extra state block (SDSX) and a side file. That's the interface below.

### 2.2 A system

A system is static data plus a few functions, one per console. Proposed
`src/system.h`:

```c
typedef enum { MEM_LITTLE, MEM_BIG, MEM_WORD_SWAP32 } MemOrder;

typedef struct {
	int id;              /* SYSMEM_RAM, SYSMEM_VRAM, SYSMEM_SAVE ... */
	unsigned retroId;    /* RETRO_MEMORY_SYSTEM_RAM ..., or a patch's own id */
	uint32_t base;       /* the CPU's address of byte 0 (0x7E0000, 0x80000000) */
	uint32_t mirrorMask; /* address bits that select the byte (0x1FFFFF for PSX RAM) */
	MemOrder order;      /* N64 RDRAM in mupen64plus: 32-bit words, host order */
	bool needsPatch;     /* only with RetroDS's core patch (bsnes VRAM) */
} SysMemRegion;

typedef struct {
	const char* name, *md5;  /* "scph5501.bin", known good dumps */
	bool required;
	const char* region;      /* "NTSC-U" */
} SysBios;

typedef struct System {
	const char* id;             /* "snes", "psx", "n64", "gc": folders, config keys */
	const char* name;           /* "Super Nintendo", "PlayStation" */
	const char* const* exts;    /* what the game list takes */
	const char* coreSoname;     /* "libretro_psx.so" (one copy) */
	int coreCopies;             /* 1, or 2 for SNES while the logo core stays */
	bool fullPath;              /* hand the core a path (discs), not bytes */
	bool hwRender;              /* needs a GL or Vulkan context (N64 fast path, GC) */
	bool discControl;           /* multi-disc swapping */
	const SysBios* bios;
	const SysMemRegion* mem;
	int nativeW, nativeH;       /* the usual picture (it still varies at run time) */
	unsigned raConsole;         /* rcheevos console id */
	/* identity: SNES CRC32 (header stripped); PSX serial from SYSTEM.CNF
	 * ("SLUS-00067"); N64 header id + version ("NZLE00"), any byte order;
	 * GC disc id ("GZLE01") */
	bool (*identify)(const char* path, GameId* out);
	/* the title for the list (SNES header, PSX from a DAT or the file name) */
	bool (*title)(const char* path, char* out, size_t n);
} System;
```

Notes per field, from what each core actually does (section 3):

- **Memory regions**: SNES WRAM 128 KB, SRAM, VRAM 64 KB (patch). PSX main RAM
  2 MB in all three PSX cores (SwanStation, Beetle, PCSX ReARMed all answer
  `RETRO_MEMORY_SYSTEM_RAM`); PSX VRAM (1 MB) isn't exposed by any of them and
  would need a patch like bsnes's if a module reads it. N64 RDRAM 8 MB
  (`g_dev.rdram.dram` in mupen64plus-next), stored as 32-bit words in host
  order, so a byte at N64 address `a` sits at `a ^ 3` (to confirm on the host).
  GameCube MEM1, 24 MB, big-endian (Dolphin's libretro core sets
  `RETRO_MEMDESC_BIGENDIAN`).
- **Framebuffer sizes** are always run-time values: SNES 256 or 512 wide, 224,
  239, 448 or 478 tall; PSX 256 to 640 wide, 224 to 480 tall, changing between
  screens of one game; N64 usually 320 x 240; GameCube 640 x 480 or 528.
  `SNES_W` / `SNES_H` / `EMU_MAX_*` go; per-game coordinates (Super Metroid's
  covered mini-map) are given in the mode they were measured in and dropped
  when the picture isn't that size.
- **Media**: SNES as bytes (today's path, keeps patching and the CRC cache);
  discs by path (`need_fullpath` is true in all three PSX cores). Modules that
  read the game's data get a `GameMedia` handle: the ROM bytes, or a reader for
  files on the disc (section 8, question 6: CHD needs a decompressor).

### 2.3 A game module

Proposed `src/module.h`. Super Metroid and ALttP fit it without changing what
they do; it's mostly moving `game.c`'s `native_*` / `alt_*` behind function
pointers.

```c
typedef enum {
	PHASE_UNKNOWN,    /* show the last good picture or the logo */
	PHASE_MENU,       /* title, file select, options: the logo */
	PHASE_PLAYING,    /* normal gameplay: writes allowed */
	PHASE_BUSY,       /* doors, loading, cutscenes, the game's own pause menu */
} GamePhase;

typedef struct {
	uint32_t addr;     /* the system's own address ($7E0202, 0x800A1234) */
	uint32_t size;     /* bytes covered from addr */
	const char* what;  /* "Y item", as in docs/games/<game>.md */
} WriteRule;

typedef struct {
	uint32_t addr;
	uint8_t value;
} Write;

typedef struct {
	Write w[128];
	int n;
	const char* action;  /* "select hookshot": for the log */
} WriteBatch;

typedef struct GameModule {
	const char* system;          /* "snes" */
	const char* ids[8];          /* "777AAC2F", "SLUS-00067" */
	const char* title;
	const char* rules;           /* "docs/games/a-link-to-the-past.md" */
	const WriteRule* writes;     /* the whitelist; NULL: read only */
	const HideRule* hide;        /* game-screen covers (Super Metroid's mini-map) */
	const LogoSpec* logo;        /* how to find its title screen, or NULL */

	void* (*open)(const GameMedia* media, const char* dataDir);
	void (*close)(void* g);
	/* after each retro_run, on the loop's thread, with the core idle */
	int (*update)(void* g, const MemView* mem);
	GamePhase (*phase)(void* g);
	/* the second screen: layers, scrolling and pinch handled by shared code */
	void (*draw)(void* g, SecondScreen* ss);
	/* a touch on the second screen; may fill a WriteBatch */
	bool (*touch)(void* g, const TouchEvent* t, WriteBatch* out);
	/* SDSX block in states, and side files (ALttP's seen record) */
	void* (*state_pack)(void* g, size_t* n);
	bool (*state_merge)(void* g, const void* data, size_t n);
	void (*save)(void* g), (*load)(void* g);
} GameModule;
```

Shared code that `game.c` grows today for each module and should own once:
GPU-side layers with screen-pixel offsets, drag and fling, pinch zoom, page
switching, the worker thread that makes map pieces, the SDSX block. ALttP's
`alt_*` code is the template: most of it isn't about ALttP.

### 2.4 The hidden copy: remove; the logo core: shrink, then remove

| Part | Today | Recommendation | Why |
|---|---|---|---|
| `mapcap.c` (947 lines), the capture half of `MapProfile` (macros, probes, crops, masks, glyphs, recolour, paint, capture touch, animation recording), the `snap` copy | Unused: both profiles are native | **Remove** in step 0 | Nothing runs it. It's the most SNES-bound code in the tree (macros of SNES buttons, 256 x 224 crops), it costs a whole core copy, and on PSX or N64 a second live core is far more expensive (and impossible for GPU cores without a second context). The memory-driven approach replaced it. |
| Third core copy (`EMU_CORES` 3) | Game, hidden copy, logo | **Shrink to 2 for SNES** (game + logo) in step 0 | The capture copy goes with `mapcap.c`. |
| Logo extraction (`logo.c`, second SNES copy) | Boots the ROM to its title in the background, per profile | **Keep for SNES for now**; for other systems, no second core: the player's own or scraped logo (already supported, `ui_logo_source`), else the game's name (already the fallback, `game.c:6175`), else a title-screen snapshot taken from the *live* game when its module says the title is up | A PSX title screen is 10 to 20 s of BIOS and loading away, needs the BIOS, and a GPU core can't run hidden. Once live snapshots work for SNES too, the second SNES copy and the `libsnes_a/b` soname trick can go entirely. |

The `libsnes_{a,b,c}.so` scheme exists only because two cores of the same kind
are loaded at once. With one copy per system it isn't needed: each core is one
`.so` with its own soname (`libretro_psx.so`, `libretro_n64.so`). SNES keeps
`libsnes_a.so` and `libsnes_b.so` until the logo core goes.

### 2.5 Memory writes: one gate

**Proposed rules**, applied by one function in the frontend (`emu_apply`), never
by modules:

1. **Whitelisted per game**: every address in a batch lies inside one of the
   module's `WriteRule`s, which are the same list as the "Writes" section of
   `docs/games/<game>.md`. A batch with any other address is refused whole.
2. **Between `retro_run` calls**: on the loop's thread after `ahead_sync`, so
   the emulation thread is idle.
3. **Only in normal gameplay**: `phase() == PHASE_PLAYING` on the frame just
   run; plus the module's own conditions (ALttP: when the game would let START
   open its screen).
4. **All or nothing**, and **logged**: frame, action, each address, old value,
   new value, in `retrods.log` (and the bug report's log).
5. **Off in RetroAchievements hardcore**, and with it the touch actions that
   write (shown as unavailable, not silently ignored).
6. **Tested**: a harness test per action checks the batch against the
   whitelist and against what the game's own menu writes (as `smtoggle` does).

**What the current code does, and the gaps:**

| Rule | Today | Gap |
|---|---|---|
| Whitelist | None. `smmap_toggle` and `alttp_select` decide their own addresses; the lists exist only in prose (`super-metroid.md` "Switching an item", `a-link-to-the-past.md` "Selecting an item") | Make the list data, checked at run time and in a test |
| Between frames | Yes: taps are handled in `ui_events` after `ahead_sync()` (`game.c:6943`, `6968`) | Keep, but make it structural (`emu_apply` asserts the worker is idle) |
| Gameplay gate | ALttP: `alttp_playing` plus the game's own menu-block flags. Super Metroid: only `natShown`, i.e. `smmap_live`, which is true during doors, elevators and while the game's own pause screen is open | Super Metroid can write while its own equipment screen is up or mid-transition; restrict to plain gameplay |
| Hardcore | `ra.c` forces casual mode ("hardcore mode isn't supported yet"), so the question never arises; the comments say "to be switched off" | Hardcore support itself is missing; when it comes, the gate must check it |
| Logging | One line per tap with the byte count | Log addresses, old and new values, frame |
| All or nothing | Writes are a loop of `emu_write8`; a refused byte (outside the mapped range) is skipped silently | Validate the batch first |
| Address space | Each call site adds `0x7E0000`; `emu_write8` knows only SNES banks | Writes carry the system's own addresses; the system maps them |
| Duplication | `SmWrite` and `AltWrite` are the same struct | One `Write` type |

**A second kind of write to plan for:** Paper Mario's battle touch and MGS's
codec can't be done as "set these bytes" safely; they'd be "move the game's
menu cursor, then press the button for one frame". That's input injection, not
memory writes, and it needs its own rules (section 7.3).

### 2.6 Games without a module

The path holds today for SNES and carries over unchanged: a game with no profile
is listed, plays on one screen, and the second screen shows the player's own
logo, a scraped one, or the game's name (`game.c:6175`). Per system:

- **SNES**: as today, plus the extracted title-screen logo where a profile has
  logo steps.
- **PSX, N64**: same fallbacks; no extraction core. Nothing in the path depends
  on SNES except `SNES_W` / `SNES_H` in the idle logo's layout, which step 0
  removes. Multi-disc games need the disc menu entry even without a module.
- **GameCube**: same, once a core exists.

---

## 3. Cores per system

Licences read from each repository's LICENSE file and source headers (shallow
clones in a scratch folder outside the repo, deleted afterwards; nothing added
to the repo). RetroDS is **GPLv3-only**, so a core must be GPLv3, "GPLv2 or
later", LGPL, or permissive. "GPLv2 only" and non-commercial licences are out.

### 3.1 PlayStation

| Core | Licence (checked) | Fits GPLv3-only? | Software frame | RAM | Discs | Notes |
|---|---|---|---|---|---|---|
| **SwanStation** ([libretro/swanstation](https://github.com/libretro/swanstation)) | LICENSE: GPLv3. Source headers: "version 2 of the License, or (at your option) any later version" | **Yes** | Yes (software renderer; GL/Vulkan optional) | `RETRO_MEMORY_SYSTEM_RAM` = `Bus::g_ram` (2 MB, 8 MB with the dev-console option); `SAVE_RAM` = memory card 1 when the card mode is "libretro" | `exe psexe cue bin img iso chd pbp ecm mds psf m3u`, `need_fullpath`, disc control (m3u) | Active (last push Oct 24, 2026). Fork of DuckStation from before the relicence. Needs a BIOS. **Recommended.** |
| **PCSX ReARMed** ([libretro/pcsx_rearmed](https://github.com/libretro/pcsx_rearmed)) | GPLv2 "or later" headers (core, `new_dynarec`, `gpu_neon`); lightrec LGPL-2.1-or-later | **Yes** | Yes (NEON software GPU, no GPU context ever) | `SYSTEM_RAM` = `psxM`; `SAVE_RAM` = card 1 | `bin cue img mdf pbp toc cbn m3u chd iso exe` | Fastest on ARM; has an HLE BIOS fallback (lower compatibility). **Fallback for the Duo Lite.** |
| **Beetle PSX / HW** ([libretro/beetle-psx-libretro](https://github.com/libretro/beetle-psx-libretro)) | Mednafen headers: GPLv2 "or later"; parallel-psx ISC | **Yes** | Software build yes; HW build needs GL/Vulkan | `SYSTEM_RAM` = `MainRAM` | `exe cue toc ccd m3u pbp chd bin img iso` | Most accurate, heaviest. Not needed. |
| **DuckStation** ([stenzek/duckstation](https://github.com/stenzek/duckstation)) | LICENSE: **CC BY-NC-ND 4.0**; sources `SPDX-License-Identifier: CC-BY-NC-ND-4.0` (one file "+ Packaging Restriction") | **No** | | | | Relicensed in September 2024: PolyForm Strict on Sep 1, CC BY-NC-ND on Sep 13 ([GamingOnLinux](https://gamingonlinux.com/2024/09/playstation-1-emulator-duckstation-changes-license-for-no-commercial-use-and-no-derivatives), [Time Extension](https://timeextension.com/news/2024/09/creator-of-ps1-emulator-duckstation-threatens-to-shut-the-whole-thing-down-following-license-change)). **Excluded.** |

- **BIOS**: user-supplied, never shipped. US `scph5501.bin` (or 7001, 1001),
  JP `scph5500.bin`, EU `scph5502.bin`; checked by MD5 and listed in settings.
  SwanStation and Beetle refuse to boot without one; PCSX ReARMed falls back to
  HLE.
- **Disc formats**: CHD (recommended: one file per disc, compressed), cue/bin,
  and `.m3u` playlists for multi-disc games (Metal Gear Solid has 2 discs, FF7
  has 3). RetroDS needs the libretro disc control interface for swapping, and
  the game list should show an m3u as one game and hide the files it lists.
- **Performance**: no head-to-head benchmark on a Snapdragon 7-class chip was
  found (**unverified**). Indirect evidence: libretro's own tests found
  SwanStation much faster than Beetle HW; PCSX ReARMed is the usual pick for
  weak ARM devices; a Snapdragon 845 ran Beetle HW at full speed at 1x. The Duo
  (Q-7790, Adreno 722) is well above that; the Duo Lite (QCS6125, Adreno 610,
  per [Android Authority](https://androidauthority.com/retroid-pocket-duo-specs-pricing-3710136); sources conflict) is not.
  A device measurement is part of step 1.
- **Save states**: all three serialize; SwanStation's include VRAM and are a
  few MB. RetroArch interchange (RASTATE) works per core and version, as with
  bsnes today.
- **RetroAchievements**: rcheevos supports PlayStation; `ra.c` needs the
  console id and memory map per system (today SNES only).

### 3.2 Nintendo 64

| Core | Licence (checked) | Fits? | Renderers | RAM |
|---|---|---|---|---|
| **mupen64plus-next** ([libretro/mupen64plus-libretro-nx](https://github.com/libretro/mupen64plus-libretro-nx)) | LICENSE: GPLv2 text. `mupen64plus-core`: headers "version 2 ... or any later version" (203 files) | Core: **yes** | GLideN64 (OpenGL), ParaLLEl-RDP (Vulkan), angrylion (software) | `SYSTEM_RAM` = `g_dev.rdram.dram`, size `RDRAM_MAX_SIZE` (8 MB) |
| GLideN64 (inside it, always compiled: `Makefile.common` lists its sources unconditionally) | `GLideN64/LICENSE`: "licensed under the GNU General Public License version 2", no "or later"; most files carry no header; upstream [gonetz/GLideN64](https://github.com/gonetz/GLideN64) says the same | **No** (treat as GPLv2-only) | | |
| ParaLLEl-RDP (inside it) | MIT | Yes | Needs a Vulkan context from the frontend | |
| ParaLLEl-RSP (inside it) | MIT or LGPLv3 | Yes | | |
| angrylion (inside it) | No licence text in `mupen64plus-video-angrylion`; upstream [ata4/angrylion-rdp-plus](https://github.com/ata4/angrylion-rdp-plus) has none detected | **Unverified** | Software: outputs a plain frame (`video_cb(prescale, ...)`), no GPU context | |
| **ParaLLEl N64** ([libretro/parallel-n64](https://github.com/libretro/parallel-n64)) | No top-level licence; bundles Glide64, Rice, gln64, GLideN64 | **Unclear, avoid** | | |

- **What it means:** a distributable RetroDS N64 core is mupen64plus-next built
  **without GLideN64** (a build patch, like `bsnes-memory.patch`), with either
  ParaLLEl-RDP (needs RetroDS to provide a Vulkan context: libretro's Vulkan
  hardware-render interface next to SDL2's renderer on two windows, the biggest
  unknown in this document) or angrylion (no GPU work at all, but its licence
  needs confirming and its speed on the Duo is unmeasured; it's a pixel-accurate
  software RDP and was historically slow). Asking GLideN64's author whether
  "version 2" means "or later" is a cheap third route.
- **Performance**: secondary sources rate N64 as comfortable on the Duo with
  RetroArch (GLideN64 or ParaLLEl), **unverified** for angrylion.
- **RDRAM exposure**: yes, 8 MB; byte order as noted in 2.2. Paper Mario, OoT
  and MM are 4 MB games with the Expansion Pak optional (MM requires it).
- **Save states**: supported (with ParaLLEl-RDP, states don't carry GPU state;
  RetroArch uses them routinely).
- **Size**: ~2.2 MB zipped on libretro's Android buildbot (with GLideN64).

### 3.3 GameCube (The Wind Waker)

- **Dolphin** ([dolphin-emu/dolphin](https://github.com/dolphin-emu/dolphin)):
  `SPDX-License-Identifier: GPL-2.0-or-later`, so **compatible**.
- **The libretro core** ([libretro/dolphin](https://github.com/libretro/dolphin))
  is alive: commits on Nov 11 and 22, 2026; an Android build on libretro's
  buildbot (6.4 MB zipped, Nov 22, 2026); RAM exposed as `SYSTEM_RAM` with
  big-endian memory descriptors. But it requires a GL or Vulkan context, and a
  libretro forum thread (Oct 2025) reports dual-core mode crashing because
  Dolphin's threading doesn't fit libretro's model (**unverified** whether
  fixed).
- **Standalone Dolphin as a library**: not a supported configuration; Dolphin's
  Android app is a full app with its own JNI layer. Embedding it means
  maintaining a fork. Not realistic for one developer.
- **The Duo**: secondary sources call GameCube "playable but with less
  headroom"; the Duo Lite's Adreno 610 won't run it.
- **Honest estimate**: GameCube needs (a) GPU-context support in RetroDS (the
  same work as N64 via ParaLLEl-RDP, so do that first), (b) Dolphin's libretro
  core built, tuned and stable inside RetroDS on the Duo, (c) the module. (a)
  and (b) together are larger than everything done so far. **Not yet**: revisit
  once N64 runs on a GPU path and Wind Waker's decomp is further along.

### 3.4 Android packaging

Measured from the latest CI build (`snes-ds-v0.1.0-dev-android.apk`, m3 branch):
APK 8.9 MB; `libmain.so` 11.2 MB, `libSDL2.so` 1.3 MB, `libsnes_{a,b,c}.so`
1.8 MB each (stored compressed in the APK).

| Core | Buildbot zip (Nov 22, 2026) | Copies |
|---|---|---|
| bsnes | 0.8 MB | 2 (game, logo), later 1 |
| SwanStation | 1.7 MB | 1 |
| PCSX ReARMed | 0.5 MB | 1, if shipped as the Lite fallback |
| mupen64plus-next | 2.2 MB | 1 |
| Dolphin | 6.4 MB | 1 (later) |

So the APK grows by roughly 2 to 5 MB per system: one APK with every core is
fine. One `.so` per system, loaded only when a game of that system starts.
The `a/b/c` soname scheme is needed only for SNES while it keeps two copies
(2.4).

---

### 3.5 Original Xbox (added Nov 27, 2026)

- **hakuX** ([rfandango/hakuX](https://github.com/rfandango/hakuX)): an
  original Xbox emulator for Android, **GPL-3.0 or later**, so
  **compatible**; no BIOS needed (it reimplements the Xbox kernel). Tuned
  for Adreno handhelds; press reports most games at full speed on the
  Retroid Pocket 5. A month old (v1.0.0 Oct 23, 2026).
- **Not a libretro core:** its core is a library inside its own Android
  app (its own Vulkan, audio, input). A libretro wrapper is about the
  GameCube's size of work (3.3), plus the question of its JIT sharing
  RetroDS's process.
- **First game idea:** NFL 2K5's play calling on the touch screen (no
  decomp; a mod tool documents its playbook file).
- **Detail and the spike's plan:** `docs/research/original-xbox.md`. **Not
  yet**: first the owner tries hakuX itself on the Duo.

## 4. Rename to RetroDS

### 4.1 Checklist

| Area | From | To |
|---|---|---|
| Binary, harness | `snes-ds`, `snes-harness` | `retrods`, `retrods-harness` |
| Config | `snes-ds.cfg`, `library.cfg` keys | `retrods.cfg`; read `snes-ds.cfg` once if no `retrods.cfg` |
| Log | `snes-ds.log` | `retrods.log` |
| Variables, macros | `SNESDS_*` (about 50 names: `SNESDS_HOME`, `SNESDS_CORE`, `SNESDS_DEBUG`, `SNESDS_WINDOW`, `SNESDS_SCREENS` ...), include guards `SNESDS_*_H` | `RETRODS_*` |
| CI secrets | `SNESDS_KEYSTORE`, `_PASSWORD`, `KEY_ALIAS`, `KEY_PASSWORD` | `RETRODS_*` (owner renames them in GitHub settings; same key file) |
| Android package, application ID | `io.github.iosue_iulianus.snesds` | `io.github.iosue_iulianus.retrods` (a new app: 4.2) |
| JNI names | `Java_io_github_iosue_1iulianus_snesds_...` | `Java_io_github_iosue_1iulianus_retrods_...` |
| Classes | `SnesDsActivity`, `SnesDsHereActivity`, `LauncherActivity`, `SecondScreen` | `RetroDsActivity`, `RetroDsHereActivity` (others keep their names) |
| Folder on Android | `/storage/emulated/0/SNES DS/` | `/storage/emulated/0/RetroDS/` |
| ROM folders | `ROMs/snes` | `ROMs/snes`, `ROMs/psx`, `ROMs/n64`, `ROMs/gc` (ES-DE's names) |
| ES-DE integration (`esde.c`) | adds SNES DS to ES-DE's `gba` system (inherited) | adds RetroDS to `snes`, `psx`, `n64` |
| Logo | `src/brand_data.c` from `tools/make_brand.py`, `snes-ds-logo.png` | New artwork (font still not settled) |
| Player-facing text | "SNES DS" in strings, `.pot` / `.po`, wiki, README, CHANGELOG | "RetroDS"; i18n catalogues regenerated, translations updated |
| Update checker (`update.c`) | releases of `iosue-iulianus/snes-ds` | the renamed repo (GitHub redirects the old URL) |
| Licence notice in APK | "SNES DS ... is free software" | "RetroDS ..." with each shipped core's licence file |
| State file blocks | `SDSC`, `SDSX` | **Keep** (format ids; old states keep loading) |
| GitHub repo | `iosue-iulianus/snes-ds` | `iosue-iulianus/retrods` (old links redirect) |
| Branches | `snes` (the line), `m1-port`, `m2-super-metroid`, `m3-a-link-to-the-past` | New line `retrods` from `snes` after M2 and M3 merge; old milestone branches keep their names; `main` / `dev` still mirror upstream |
| Docs | CLAUDE.md, `wiki/Philosophy.md`, `wiki/Home.md`, `README.md`, `docs/ANDROID.md`, `docs/TESTING.md` | Section 5 drafts |

Name check: no app or emulator called RetroDS was found; a zero-star GitHub repo
`konsumer/retrods` ("Retroarch cores on Nintendo DS") exists, which shows
exactly the confusion the tagline has to prevent.

### 4.2 Moving players from SNES DS to RetroDS

A new application ID installs as a separate app; Android gives it none of the old
app's data. What lives where today:

- **Battery saves** (`.srm`): beside the ROMs by default (`ROMs/snes`): RetroDS
  finds them where they are.
- **States, logos, cheats, screenshots, overlays, `library.cfg`, ALttP's seen
  records** (`<save>.alttp1..3`, beside the save): in `SNES DS/` on shared
  storage with All files access, or in the app's private folder without it
  (which Android deletes on uninstall).

Plan:

1. **A last SNES DS update** (same ID, same key) that, if its data is in the
   private folder, offers to copy it to `SNES DS/` on shared storage (needs All
   files access), and says RetroDS replaces it.
2. **RetroDS on first start**: if `SNES DS/` exists, offer "Bring over your SNES
   DS states and settings": **copy** (never move) `states/`, `logos/`,
   `cheats/`, `screenshots/`, `overlays/`, and `snes-ds.cfg` into `RetroDS/`,
   translating keys, rewriting paths that pointed into `SNES DS/` (as
   `android.c` already does for a moved home folder). Show what was copied.
3. **Tell players not to uninstall SNES DS** until RetroDS shows their states.
   The device loop's rule ("never suggest uninstalling") applies doubly.
4. The owner's own states are pulled to `test-data/` before the switch (as the
   device loop already does).

### 4.3 Tagline and notice

Tagline (pick one):

- **"RetroDS: your favourite classics, with the second screen they never had.
  Not a Nintendo DS emulator."**
- "Second screens for SNES, PlayStation and N64 classics. (It doesn't play DS
  games.)"

Also: when the game list finds `.nds` files, say once "RetroDS doesn't play
Nintendo DS games" rather than ignoring them silently.

Notice, for the README and the About screen:

> RetroDS is an independent fan project. It is not affiliated with, endorsed
> by or sponsored by Nintendo, Sony Interactive Entertainment, Konami, Square
> Enix, Intelligent Systems, or any other company whose games it can run. All
> trademarks belong to their owners. RetroDS includes no games, BIOS files or
> game graphics: it reads them from copies you own.

---

## 5. Philosophy and CLAUDE.md drafts

Drafts only; the files themselves change in step 0. (Note: on `snes` both files
still say "the live game is read-only", "rebuilding a screen from the game's
data is not" and "No in-game touch"; on M2 and M3 they were partly updated but
still present the hidden copy as one of the main tools.)

### 5.1 Draft `wiki/Philosophy.md`

> What RetroDS is meant to be, and the principles behind what it does and
> doesn't do.
>
> RetroDS started as SNES DS, a fork of [GBA DS](https://github.com/kyannon/gba-ds),
> and owes it the game list, the menus and the idea of a second screen made
> from the game itself.
>
> **1. Your favourite games, given the DS treatment they never got.**
> Imagine each of these games had come out on a handheld with a second, touch
> screen, the way the DS's own games did: the map you used to pause for is
> always in view, your equipment is a tap away, and the main screen is free
> for playing. RetroDS does that for a short, chosen list of games, on several
> consoles. Every other game still plays, on one screen, with its logo on the
> other.
>
> - **The game screen is the game.** It shows the console's picture as the
>   game draws it, played on your buttons. It changes only where a game's rules
>   say so, because the second screen took something over (Super Metroid's
>   mini-map); each change is listed and can be switched off.
> - **The second screen is the game's companion.** It shows what the game
>   would have put there: a map, a status page, your items. It's live, and you
>   can touch it.
> - **RetroDS's own screens are separate.** The game list, pause menu and
>   settings have their own look.
>
> **2. In the game's own style.** Everything on the second screen is made from
> the game's own graphics, read from your copy of the game: its tiles, fonts,
> palettes and frames. New layouts are fine (a whole map to scroll, items
> moved where a thumb reaches them); new art isn't. Touch hints the game never
> had are kept small, made from its own pieces where possible, and listed.
> Pixels are scaled the way the game screen's are.
>
> **3. Drawn from the game's memory.** RetroDS reads the running game's memory
> every frame (where you are, what you've explored, what you carry) and draws
> the second screen itself, from the game's own data. Nothing waits for a
> capture, so it scrolls and blinks at the screen's own rate. When RetroDS
> can't be sure what's right (a title screen, a cutscene, something it doesn't
> recognise) it shows the logo or the last good picture: better nothing new
> than something wrong. Each screen is checked against the game's own, pixel
> for pixel where the game has one.
>
> **4. Touch the second screen; play with the buttons.** Touch is how you use
> the second screen: drag a map, tap a page, choose an item, switch equipment,
> the things you'd have done in a pause menu. Running, jumping and fighting
> stay on the buttons; there are no on-screen controls for playing. Touching
> the game screen opens RetroDS's pause menu.
>
> **5. Write to the game only as its own menus would.** A tap that changes the
> game (selecting an item) writes exactly what the game's own menu would have
> written, and nothing else: only addresses on the game's list, only during
> normal play, between frames, and logged. With RetroAchievements' hardcore mode
> on, nothing writes. The game's code is never patched.
>
> **6. A short list, done well.** Each game with a second screen is a piece of
> work of its own, built from the community's decompilations and disassemblies
> as documentation and tested against the real game. A smaller list of games
> that feel right beats a long list that almost do.
>
> **7. Keep it simple and testable.** Every feature is built, tested on the real
> device and kept working by one developer. Rules first, then code. Know the
> cost: the console's emulator runs at full speed first; the second screen fits
> beside it.
>
> **8. Rules are written to be broken, deliberately.** An exception is fine
> when it makes a game clearly better on the device, written down with the
> game's rules, tested, and switchable where it changes how the game plays or
> looks.
>
> *Your own pictures:* custom asset mode stays as an option (unchanged text).

### 5.2 Draft CLAUDE.md sections

**Opening paragraph:**

> RetroDS (formerly SNES DS, a fork of kyannon/gba-ds) plays a curated list of
> favourite games from several consoles with a second screen they never had,
> on dual-screen Android handhelds: built for the **Retroid Pocket Duo**
> (Android 16; bottom 4.5" 1280x960 4:3, top 5.5" 1920x1080 16:9, both 120 Hz
> OLED; Q-7790, 8 GB), and meant to work on the Duo Lite and AYN Thor. The game
> runs on one screen through a libretro core; the other shows the game's own
> second screen, drawn by RetroDS from the game's memory and graphics, or the
> game's logo. The owner (Joshua) decides what gets built and tests on the
> device. Nothing counts as working until it has run on the Duo.

**Philosophy (how sessions apply it)**, replacing today's bullets:

> - **Check requests against `wiki/Philosophy.md`, and say so.** If a request
>   conflicts with a principle, say which and suggest something that fits; if
>   the owner still wants it, write it as an exception in `docs/games/<game>.md`.
> - **Rule first, then code.** A change to what a game shows starts with its
>   rules in `docs/games/<game>.md`, in the player's terms; then the code, then
>   the tests.
> - **Second screens are drawn from memory.** Read the live game's RAM (and
>   VRAM where a core patch exposes it) after each frame; decode graphics from
>   the player's own ROM or disc; use decompilations and disassemblies as
>   documentation for addresses and formats. Never copy their code; never
>   commit or download game data. No hidden copy of the game.
> - **Writes go through the gate.** A touch action produces a batch of writes
>   that mirrors what the game's own menu code writes (cite the routine). The
>   addresses are the game's whitelist, listed in its rules doc; the frontend
>   applies a batch between frames, only in normal gameplay, logs frame,
>   address, old and new value, and refuses it in RetroAchievements hardcore.
>   Each action has a harness test comparing the batch with the game's own
>   menu.
> - **The game screen is left as the game draws it** unless a game's rules
>   say otherwise (Super Metroid's covered mini-map), as a frontend draw,
>   listed and switchable.
> - **When unsure, show less,** and say what you couldn't do (changelog,
>   release notes).
> - **Keep changes small enough to test.** Every change ends with a test build
>   and a short note of what to check on the device.

**Decisions already made**, replacing items 1, 2 and 4:

> 1. **Cores:** `emu.c` is written against the libretro API; each system's core
>    is a swappable `.so`. Releases ship only cores whose licence is compatible
>    with RetroDS's GPLv3-only licence: bsnes (SNES), SwanStation (PSX); see
>    `docs/SCOPE.md` for N64 and GameCube. Snes9x and DuckStation are never
>    distributed, committed or bundled.
> 2. **One core per game.** No hidden copy. SNES keeps a second copy for
>    title-screen logos until live logo snapshots replace it.
> 3. **Game identity** per system: SNES CRC32 without a copier header; PSX the
>    disc's serial; N64 the header's id and version; GameCube the disc id.
> 4. **Second screens** are game modules (`src/games/<game>.c`) drawn from
>    memory and the game's own graphics. Custom asset mode and the logo remain.
> 5. **Framebuffer size is a runtime value** on every system.

**Style** keeps: readable over clever; **no em dashes** in docs, comments or
commit messages; player-facing text in plain sentence case; small commits, one
idea each.

---

## 6. Game list and settings

### 6.1 The game list

- **One list, grouped by system**, in the order systems were added (SNES,
  PlayStation, Nintendo 64), each group headed by the system's name; a system
  with no games is hidden. A setting to show one system only.
- **"DS" mark** on games with a second-screen module: a small two-screen
  glyph beside the title, and a filter "Games with a second screen". The game's
  page says what its second screen does in one line ("Castle map and relics"),
  from the module.
- **Plain games** look as they do today, with no mark.
- **Multi-disc** games appear once (their `.m3u`); a cue's `.bin` files and an
  m3u's discs are hidden.
- **Missing BIOS**: a PSX game's tile says "Needs a PlayStation BIOS" and opens
  the BIOS settings instead of failing to start.
- **Unplayable files** (`.nds`, `.3ds`, `.iso` of unknown systems): ignored,
  with the one-time notice from 4.3 for DS files.
- **Version note**: a module is written for specific releases (Super Metroid
  `D63ED5F8`, SOTN `SLUS-00067`); another release of the same game plays as a
  plain game and the game page says which version has the second screen.

### 6.2 Settings per system

| System | Setting | Default |
|---|---|---|
| All | ROM folder | `ROMs/<system>` |
| All | Core options the player may want (renderer scale, etc.), shown from the core's own option list | the core's defaults, except where Philosophy says (no enhancement by default) |
| PSX | BIOS folder, with each file's status (found, MD5 known, region) | `RetroDS/bios` (also RetroArch's `system` folder if readable) |
| PSX | Controller: DualShock (analog, with the analog button) or digital pad, per game | DualShock; digital for games that need it |
| PSX | Memory cards: one per game or shared | one per game |
| PSX | Renderer: software (default) or GPU, internal resolution, PGXP | software, 1x, PGXP off (game screen as the game draws it) |
| PSX | Fast boot (skip the BIOS logo) | on |
| N64 | Controller mapping: left stick as the N64 stick, right stick as C buttons, Z on L2 (or L) | as listed |
| N64 | Controller Pak or Rumble Pak per game | per the game's needs (Paper Mario: none) |
| N64 | Expansion Pak | on (MM needs it) |
| N64 | Renderer (once there's a choice) | the one that runs full speed on the device |
| SNES | unchanged | |

**Which screen has the game** on other devices: today the game goes on the
screen closer to 4:3. On the AYN Thor (top 6" 1920x1080, bottom 3.92"
1240x1080, per secondary sources) that puts the game on the small bottom screen.
Question 8 in section 8.

---

## 7. Titles

### 7.1 How they're scored

Each 1 to 5, higher is better: **Value** of a second screen for that game;
**Source** quality (complete, matching, documented, for the release players
own); **Core** readiness (5: runs today; 1: needs a platform RetroDS lacks);
**Effort** (5: smallest). **Score** = 2 x Value + Source + Core + Effort (max
25): value counts double because a second screen nobody looks at isn't worth
building.

Source policy for every title: decomps and disassemblies are read as
documentation (addresses, structures, formats); RetroDS's module code is
written from scratch; tables and graphics are decoded at run time from the
player's own copy. So a project's licence (AGPL, none, non-commercial) doesn't
block using it this way, but it does mean **no code is copied**, and "no
licence" projects (OoT, MM, Paper Mario, MGS) are reference only in the
strictest sense.

### 7.2 Owner's picks

| Game | Source (checked Nov 23, 2026) | Core | Second screen | Touch | V | S | C | E | Score |
|---|---|---|---|---|---|---|---|---|---|
| **Castlevania: Symphony of the Night** (PSX) | [Xeeynamo/sotn-decomp](https://github.com/Xeeynamo/sotn-decomp): C, matching, **AGPL-3.0**; [decomp.dev](https://decomp.dev/Xeeynamo/sotn-decomp): US (SLUS-00067) **72.3% code** (engine 86.1%, stage code 63.4% with 99.5% of stage functions); also builds JP, EU, PSP, Saturn | SwanStation | Both castles' maps, scrollable, explored rooms as the game records them, Alucard's marker, map %; a second page with equipment, relics and stats | Drag the map; toggle relics (the game's own relic menu switches them on and off); later, equip weapons and armour | 5 | 4 | 4 | 3 | **21** |
| **Paper Mario** (N64), menus | [pmret/papermario](https://github.com/pmret/papermario): C, matching, **no licence file**; US **100%** ([papermar.io shield](https://papermar.io/progress-us)), JP, PAL, iQue builds | mupen64plus-next (2.2, 3.2) | Badges with BP, items, key items, partners, star spirits, the map, stats; design reference [ChrZazueta/PaperRPG-3DS](https://github.com/ChrZazueta/PaperRPG-3DS) (no licence: design only, no code) | Equip badges (the game's badge menu writes the equipped list and recomputes BP; mirror it), quick partner swap (outside battle: a write the partner menu makes, plus the game's swap sequence, check), map look | 5 | 5 | 2 | 3 | **20** |
| Paper Mario, battle touch | as above | as above | Command wheel and targets on the second screen | Choose a command and a target: drives the battle menu (cursor + a pressed button for a frame), a new kind of action (2.5). Risk: menu timing, partner turns, action commands stay on buttons. Effort roughly twice the menus; behind its own switch | 4 | 5 | 2 | 1 | 16 |
| **Metal Gear Solid** (PSX) | [FoxdieTeam/mgs_reversing](https://github.com/FoxdieTeam/mgs_reversing): C, matching, **no licence file**; target is **MGS Integral (Japan, SLPM-86247/8/9)**: main executables **100%**, per-stage overlays: 69 marked done, 18 in progress (README); **no US build** | SwanStation; 2 discs (m3u) | Soliton radar (top-down map, guards and their view cones, alert state), life and grip, the equipped weapon and item with ammo | Weapon and item select (what the in-game quick-select writes); codec on the second screen is a later, harder step (it's a game mode, not a screen) | 4 | 3 | 4 | 2 | 17 |
| **The Legend of Zelda: The Wind Waker** (GC) | [zeldaret/tww](https://github.com/zeldaret/tww): C++, matching, **CC0-1.0**; [decomp.dev](https://decomp.dev/zeldaret/tww): GZLE01 **80.2% code** (main DOL 88.7%, modules 73.8%), data 91.2% | Dolphin libretro: **not yet** (3.3) | Sea chart (the Wii U version's GamePad screen as precedent), items | Assign items to X/Y/Z by drag, as Wind Waker HD did | 5 | 4 | 1 | 1 | 16 |

**Effort and order for the picks:** SOTN first (needs the PSX platform anyway;
read-only map before any write). Paper Mario menus after the N64 spike. MGS
after SOTN: the radar is the valuable part and is mostly reading positions and
map collision data, but every address has to be found again for the US disc
(or the owner plays Integral; question 5). Wind Waker last.

### 7.3 Candidates

| Game | Source (checked Nov 23, 2026) | Core | Second screen | Touch | V | S | C | E | Score |
|---|---|---|---|---|---|---|---|---|---|
| **Silent Hill** (PSX) | [shdecompilations/silent-hill-decomp](https://github.com/shdecompilations/silent-hill-decomp): C, matching, **GPLv3** ("version 3", only); [decomp.dev](https://decomp.dev/shdecompilations/silent-hill-decomp): SLUS-00707 **100% code** (6530/6530 functions), data 51.9%; also EU, JP builds | SwanStation | The town and building maps with the red marks Harry adds, his position where the game shows it; inventory, health state | Equip a weapon (as the item menu does); using items stays in the game's menu | 5 | 5 | 4 | 3 | **22** |
| **Final Fantasy VI** (SNES) | [everything8215/ff6](https://github.com/everything8215/ff6): 65816 assembly, byte-identical for FF3 US 1.0 / 1.1 and FF6 JP, **GPL-3.0** | bsnes (today) | Party status (HP, MP, status, level, the ATB gauges in battle), the world map (the game's own Mode 7 mini-map could move here, an SM-style cover) | None at first (look only) | 4 | 4 | 5 | 4 | **21** |
| **Harvest Moon** (SNES) | [RodoLy/HarvestMoonSnesDecomp](https://github.com/RodoLy/HarvestMoonSnesDecomp): C rewrite, **100%** of 778 routines verified, **PolyForm Noncommercial 1.0** (reference only); built on the disassembly [ElisaS-Codes/HM-Decomp](https://github.com/ElisaS-Codes/HM-Decomp) (licence not detected) | bsnes | Calendar and festivals, weather forecast, the farm map with crops (watered or not) and animals, stamina, money | Look only | 4 | 4 | 5 | 4 | **21** |
| **Ocarina of Time** (N64) | [zeldaret/oot](https://github.com/zeldaret/oot): C, matching, **no licence file**, **100%** ([zelda.deco.mp](https://zelda.deco.mp/games/oot)); NTSC 1.0 to 1.2, PAL, GameCube builds | mupen64plus-next | Items, equipment, quest status, the area and dungeon maps (as the 3DS remake's touch screen did) | Assign C-button items by touch; switch boots and tunics (equipment subscreen writes) | 5 | 5 | 2 | 3 | 20 |
| **Majora's Mask** (N64) | [zeldaret/mm](https://github.com/zeldaret/mm): C, matching, **no licence file**, **100%** (N64 US only) | mupen64plus-next (Expansion Pak) | As OoT, plus the clock and the Bombers' Notebook | As OoT, masks included | 5 | 5 | 2 | 3 | 20 |
| **EarthBound** (SNES) | [Herringway/ebsrc](https://github.com/Herringway/ebsrc): 65816 disassembly, builds US, prototype, Mother 2; "Decompile C code" still a TODO; **no licence file**. [TheRunaway5/Phase-Distorter](https://github.com/TheRunaway5/Phase-Distorter): C++20 PC port, **MIT**, v0.5 (the "native" engine path experimental) | bsnes | Party with rolling HP/PP meters (the battle mechanic, always in view), town maps (the game's own Town Map art), money, status | Look only | 4 | 3 | 5 | 3 | 19 |
| **Mario Kart 64** (N64), not on the brief | [n64decomp/mk64](https://github.com/n64decomp/mk64): C, **100%**, **no licence file** | mupen64plus-next | The course map with every racer, positions and items, as Mario Kart DS put on its bottom screen | Look only | 4 | 5 | 2 | 4 | 19 |
| **Super Mario World** (SNES) | [snesrev/smw](https://github.com/snesrev/smw): C reimplementation, playable start to end, **MIT** | bsnes | The overworld map with exits found, the reserve item, lives, coins, Dragon Coins | Drop the reserve item (what Select does) | 2 | 5 | 5 | 4 | 18 |
| **Legacy of Kain: Soul Reaver** (PSX) | [fmil95/soul-re](https://github.com/fmil95/soul-re): C, matching, **MIT**; [decomp.dev](https://decomp.dev/fmil95/soul-re): SLUS-00708 **85.1% code**, data 15.6% | SwanStation | No map in the original; health and the realm, glyphs, collectibles, abilities | Choose a glyph (as its menu does) | 3 | 4 | 4 | 3 | 17 |
| **Final Fantasy VII** (PSX) | [Xeeynamo/ff7-decomp](https://github.com/Xeeynamo/ff7-decomp): C, matching, **AGPL-3.0**; [decomp.dev](https://decomp.dev/Xeeynamo/ff7-decomp): US **68.4% code**, data 24.4% | SwanStation; 3 discs | Party status, materia, the field's position, the world map | Look only at first | 4 | 3 | 4 | 2 | 17 |
| **Super Mario 64** (N64) | [n64decomp/sm64](https://github.com/n64decomp/sm64): C, complete (JP, US, EU, Shindou, iQue), **CC0-1.0** | mupen64plus-next | Course map and the star list per course (SM64 DS had a map on its touch screen) | Look only | 3 | 5 | 2 | 4 | 17 |
| **Banjo-Kazooie** (N64) | [n64decomp/banjo-kazooie](https://github.com/n64decomp/banjo-kazooie): C, **100%**, **CC0-1.0** | mupen64plus-next | Notes, Jiggies, Jinjos and honeycombs per world; moves learned | Look only | 3 | 5 | 2 | 4 | 17 |
| **Twilight Princess** (GC) | [zeldaret/tp](https://github.com/zeldaret/tp): C++, matching, **CC0-1.0**; [decomp.dev](https://decomp.dev/zeldaret/tp): GZ2E01 **100% code and data** | Dolphin: not yet | Map and items, as the Wii U HD version's GamePad | Drag items onto buttons; wolf toggle | 5 | 5 | 1 | 1 | 17 |
| **Metroid Prime** (GC), note only | [PrimeDecomp/prime](https://github.com/PrimeDecomp/prime): [decomp.dev](https://decomp.dev/PrimeDecomp/prime) GM8E01 **99.4% code** (not ~24% as the brief had); licence **unverified** (GitHub's API didn't return the repo) | Dolphin: not yet | The 3D map | Rotate the map | 5 | 5 | 1 | 1 | 17 |
| **Chrono Trigger** (SNES) | No SNES disassembly or decompilation found; [Data Crystal's RAM map](https://datacrystal.tcrf.net/wiki/Chrono_Trigger) only. [power-pudu/egg-of-time](https://github.com/power-pudu/egg-of-time) is a recompilation of the **DS** version (not SNES; and the DS version already has two screens, useful as a design reference) | bsnes | Party status, techs | Look only | 3 | 1 | 5 | 2 | 14 |
| **Vagrant Story** (PSX) | No public decomp found on GitHub (the "~30%" figure is **unverified**) | SwanStation | The room map, Ashley's status, weapon risk and affinities | | 4 | 1 | 4 | 1 | 14 |
| **Legend of Mana** (PSX) | No public decomp found (the "NTSC-U complete" figure is **unverified**) | SwanStation | The land map you build | | 3 | 1 | 4 | 1 | 12 |
| **ESPN NFL 2K5** (Xbox), added Nov 27, 2026 | No decomp. [cruuz/2k-football-mod-tools](https://github.com/cruuz/2k-football-mod-tools) (MIT) parses its playbook (`MASTER PLAY`: 586 plays, routes, formations); see `docs/research/original-xbox.md` | hakuX: **not a libretro core** (3.5) | The play-call page drawn from the playbook | Call plays by touch: a tap presses the game's button for the play (no writes), if its play-call screen works that way (to check) | 4 | 2 | 1 | 1 | 12 |

Also seen, not evaluated: [sonicdcer/sf64](https://github.com/sonicdcer/sf64)
(Star Fox 64, 100%, CC0: radar), [DavidSM64/Diddy-Kong-Racing](https://github.com/DavidSM64/Diddy-Kong-Racing)
(100%, CC0: course map), [rickdgray/MegaManLegendsDecomp](https://github.com/rickdgray/MegaManLegendsDecomp)
(MIT, status not checked), [khasinski/parasite-eve-decomp](https://github.com/khasinski/parasite-eve-decomp)
(status not checked), [Yoshifanatic1/Super-Mario-RPG-Disassembly](https://github.com/Yoshifanatic1/Super-Mario-RPG-Disassembly)
(GPL-3.0, last pushed 2021).

**Titles that fit better than the brief's:** Silent Hill (best PSX source, and
the maps are exactly what a second screen is for) and Mario Kart 64 (a proven
DS layout). Final Fantasy VI and Harvest Moon are the cheapest wins on the SNES
platform that already exists.

### 7.4 Next 3 to 5

1. **Symphony of the Night** (with the PSX platform).
2. **Silent Hill.**
3. **Paper Mario menus** (after the N64 spike settles the renderer and licence).
4. **Metal Gear Solid** (radar and item select; codec later).
5. **Ocarina of Time** then **Majora's Mask**.

With **Final Fantasy VI** or **Harvest Moon** in between whenever the platform
work needs a break, since they need nothing new.

---

## 8. Open questions for the owner

1. **Order**: PSX first (SOTN), N64 after a spike, as recommended? Or N64
   first because Paper Mario and the Zeldas matter more to you?
2. **Silent Hill** as an addition (step 2)? And FF6 / Harvest Moon as SNES
   fillers, replacing the old M4 (Chrono Trigger, then FF IV, V, VI)?
3. **N64 renderer and licence**: try angrylion first (no GPU work, licence to
   confirm), or go straight to ParaLLEl-RDP with Vulkan? Should we ask
   GLideN64's author whether "version 2" means "or later"?
4. **Duo Lite**: support it fully (PCSX ReARMed for PSX, probably no N64), or
   best effort only?
5. **Metal Gear Solid**: which disc do you play, US (SLUS-00594/00776) or
   Integral? The decomp is for Integral; the US disc means finding every
   address again.
6. **Reading discs**: modules that decode graphics from a PSX disc need to read
   files from it. cue/bin is simple; CHD needs a decompressor (libchdr, BSD-3,
   a new dependency, against "no new dependencies"). Allow libchdr, or support
   modules on cue/bin only, or take graphics only from RAM once the game has
   loaded them?
7. **Logos**: keep the second SNES core for title-screen logos, or move all
   systems to snapshots taken from the live game's title screen and drop the
   second core (and the soname trick) entirely?
8. **Which screen gets the game** on the AYN Thor (its bottom screen is nearly
   square and small): keep "closer to 4:3", or prefer the larger screen?
9. **Branches and versions**: a new `retrods` line as the default branch (old
   milestone branches untouched)? Does RetroDS restart its version number?
10. **Hardcore**: RetroAchievements hardcore isn't supported at all yet. Part
    of step 0 (with the write gate), or later?
11. **Battle touch** (Paper Mario) and codec (MGS) need "press a button for the
    game" input injection, a new kind of action. Acceptable under Philosophy
    5, as a listed and switchable exception?
12. **Tagline**: one of the two in 4.3, or your own?

---

## 9. Sources

Checked Nov 23, 2026. "API" means GitHub's repository API (licence detection,
last push) through `gh`.

**Cores**

- SwanStation: [repo](https://github.com/libretro/swanstation), LICENSE (GPLv3), `src/core/*.cpp` headers (GPLv2 or later), `src/libretro/libretro_host_interface.cpp` (memory, extensions, disc control).
- Beetle PSX: [repo](https://github.com/libretro/beetle-psx-libretro), `mednafen/psx/cpu.c`, `gpu.c`, `cdc.c` headers, `parallel-psx/COPYING` (ISC), `libretro.c` (memory), `libretro_options.h` (extensions).
- PCSX ReARMed: [repo](https://github.com/libretro/pcsx_rearmed), `libpcsxcore/psxmem.c`, `libpcsxcore/new_dynarec/new_dynarec.c`, `plugins/gpu_neon/psx_gpu/psx_gpu.c` headers, `deps/lightrec` (LGPL-2.1-or-later), `frontend/libretro.c`.
- DuckStation: [repo](https://github.com/stenzek/duckstation) LICENSE (CC BY-NC-ND 4.0), source SPDX lines; history: [GamingOnLinux, Sep 2024](https://gamingonlinux.com/2024/09/playstation-1-emulator-duckstation-changes-license-for-no-commercial-use-and-no-derivatives), [Time Extension, Sep 2024](https://timeextension.com/news/2024/09/creator-of-ps1-emulator-duckstation-threatens-to-shut-the-whole-thing-down-following-license-change).
- mupen64plus-next: [repo](https://github.com/libretro/mupen64plus-libretro-nx), LICENSE, `mupen64plus-core/LICENSES` and headers, `GLideN64/LICENSE`, `Makefile.common`, `libretro/libretro.c` (RDRAM, renderers, Vulkan context), `mupen64plus-video-paraLLEl/parallel-rdp/LICENSE` (MIT), `mupen64plus-rsp-paraLLEl/LICENSE` (MIT or LGPLv3). [GLideN64 upstream](https://github.com/gonetz/GLideN64). [angrylion-rdp-plus](https://github.com/ata4/angrylion-rdp-plus).
- ParaLLEl N64: [repo](https://github.com/libretro/parallel-n64) (no top-level licence).
- Dolphin: [repo](https://github.com/dolphin-emu/dolphin) (`Source/Core/Core/Core.cpp`: GPL-2.0-or-later); [libretro/dolphin](https://github.com/libretro/dolphin) (`Source/Core/DolphinLibretro/Main.cpp`, commits); [libretro forum, Oct 2025](https://forums.libretro.com/t/dolphin-cores-has-been-updated-to-current-stream-october-2025/48924).
- Core sizes: [libretro buildbot, Android arm64-v8a](https://buildbot.libretro.com/nightly/android/latest/arm64-v8a/).
- PSX performance (indirect): [libretro forum, Beetle HW on Snapdragon 845](https://forums.libretro.com/t/beetle-psx-hw-android-64bit-snapdragon-845-performance/21519), [libretro blog, PlayStation](https://www.libretro.com/index.php/category/playstation/).

**Devices** (secondary sources)

- Duo and Duo Lite: [Android Authority](https://androidauthority.com/retroid-pocket-duo-specs-pricing-3710136), [retrohandhelds.gg](https://retrohandhelds.gg/?p=44557), [heldgames comparison](https://heldgames.com/comparisons/retroid-pocket-duo-vs-pocket-duo-lite), [Notebookcheck](https://www.notebookcheck.net/Retroid-announces-pricing-for-the-Pocket-Duo-and-Pocket-Duo-Lite.1395674.0.html).
- AYN Thor: [Android Authority](https://www.androidauthority.com/ayn-thor-screen-size-3589297), [Notebookcheck](https://www.notebookcheck.com/Retroids-neue-Dual-Screen-Handhelds-sind-guenstiger-als-der-Ayn-Thor.1396892.0.html).

**Decompilations and design precedent**

- decomp.dev project reports (`https://decomp.dev/<owner>/<repo>.json`): tww, tp, sotn-decomp, soul-re, ff7-decomp, silent-hill-decomp, prime.
- Progress pages: [papermar.io](https://papermar.io/progress-us), [zelda.deco.mp OoT](https://zelda.deco.mp/games/oot), [zelda.deco.mp MM](https://zelda.deco.mp/games/mm), [mk64 progress](https://n64decomp.github.io/mk64/).
- READMEs and LICENSE files of every repository linked in section 7.
- Silent Hill news (secondary, matches decomp.dev): [frvr.com, Oct 2026](https://frvr.com/blog/news/silent-hill-decompilation-with-no-ai-generated-code-has-hit-100-decompilation-reviving-konamis-lost-source-code/).
- Twilight Princess HD and Wind Waker HD GamePad: [Nintendo TP HD manual](https://www.nintendo.com/eu/media/downloads/games_8/emanuals/wii_u_6/the_legend_of_zelda__twilight_princess_hd/ElectronicManual_WiiU_TheLegendOfZeldaTwilightPrincessHD_EN.pdf), [Nintendo WW HD manual](https://www.nintendo.com/eu/media/downloads/games_8/emanuals/wii_u_6/thelegendofzelda_windwaker_hd/ElectronicManual_WiiU_TLOZWWHD_EN.pdf), [Gematsu](https://www.gematsu.com/2016/02/legend-zelda-twilight-princess-hd-details-gamepad-features-ghost-lantern-item-hero-mode).
- The 3DS remakes of OoT and MM (items and maps on the touch screen), SM64 DS (map on the touch screen) and Mario Kart DS (course map on the bottom screen) are cited from general knowledge, not separately sourced.

**Experiments:** none run on the host. There's no PSX or N64 test content on this
machine, and the questions (RAM exposure, frame output, extensions) were
answered from the cores' source. The first host experiment of step 1 should be:
SwanStation in the harness with a user-supplied disc and BIOS, checking
`retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM)`, the software frame and
serialize size; of step 3, angrylion's frame time.
