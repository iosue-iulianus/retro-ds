# Original Xbox: a research note

Could RetroDS run original Xbox games, with NFL 2K5's play calling on the
second screen as the first idea? Looked into on Nov 27, 2026, with the
owner, from the sources below (GitHub and press; nothing run yet). This
note is the starting point for a spike; the spike's findings are added
here as they come, as `docs/research/the-wind-waker.md` (branch
`tww-spike`) did for the GameCube.

**Status: not started.** The emulator and its licence fit; the work is
getting it into RetroDS, which only talks to libretro cores. Next: the
owner tries hakuX itself on the Duo (below, "The spike").

## The emulator: hakuX

[rfandango/hakuX](https://github.com/rfandango/hakuX), "xbox emulator for
Android" (990 stars; v1.0.0 Oct 23, 2026, v1.3.0 Oct 31, 2026).

- **Licence:** GPL-3.0 or later (its `LICENSE`, `COPYING`), so RetroDS
  (GPLv3 only) can ship it. Its audio (the MCPX APU) is xemu's, keeping
  its LGPL-2.1+ / GPL-2.0+ / BSD-3 file licences; its JIT is a FEX fork
  ([rfandango/FEX](https://github.com/rfandango/FEX), MIT). All compatible.
- **How it works** (its README): the game's x86 code runs on the FEX JIT,
  under a reimplementation of the Xbox kernel (HLE: **no BIOS, MCPX ROM or
  hard disk image needed**, only the player's own ISO / XISO); drawing
  decodes the push buffer the game's Direct3D sends the NV2A into Vulkan.
  arm64 Android with Vulkan, tuned for Adreno (custom Turnip drivers can be
  loaded).
- **Speed** (press, secondary): v1.2 brought most games tested on the
  Retroid Pocket 5 (Adreno 650) to their own frame rate caps at double
  resolution; some needed Turnip. **The Duo (Adreno 722) is unknown**:
  roughly comparable, not established.
- **Its code** (`native/`): `core/` (the VM, XBE loading, the disc's
  file system, a memory scanner for its cheats, per-game mods), `kernel/`,
  `hle/`, `dev/` (devices), `cpu/`, `state/` (savestates, a versioned
  layout), `run/`, and `app/` (the Vulkan driver and present, vsync), built
  as `libhakuxcore.so` and loaded by a Kotlin / Java Android app over JNI.
- **A community fork:** [jreinach-alt/hakuX](https://github.com/jreinach-alt/hakuX)
  ("hakuX CE"). Also Xbased, a Windows emulator on its core (LaunchBox
  forums).

### What fits RetroDS

- The licence, and no BIOS to ask the player for.
- What a second screen needs exists in its core: savestates, and a memory
  scanner, so the guest's RAM is reachable. The Xbox's RAM is one flat
  64 MB block: once exposed, reading it is as for the SNES.
- Made for the kind of handheld RetroDS is for.

### What doesn't, yet

- **Not a libretro core.** `emu.c` talks only to libretro cores
  (CLAUDE.md, decision 1). hakuX's core is a separate library, but its
  presenting (its own Vulkan device and surface), audio (AAudio) and input
  sit with its Android app. A libretro wrapper means video (Vulkan, handed
  to GLES: the path the Wind Waker study proved on the Duo, "option 1"),
  audio, input and savestates through the libretro API. Weeks, about what
  SCOPE 3.3 estimated for GameCube.
- **The JIT in a shared process.** FEX reserves address space and installs
  signal handlers for the guest; RetroDS's process also holds bsnes's
  copies and SDL. Whether they coexist is the first thing to test.
- **Young and fast-moving:** a month old, one main author, a large core
  review still being worked through (its `HANDOFF.md`). A wrapper has to
  follow it.

## Other original Xbox sources

Looked up on GitHub, Nov 27, 2026; completion figures unverified unless
said.

**Matching decompilations** (C / C++ that rebuilds the game byte for byte):

| Game | Source | Licence | Notes |
|---|---|---|---|
| Halo 2 | [kirklandsig/halo2-decompiled](https://github.com/kirklandsig/halo2-decompiled) | GPL-3.0 | The most active (162 stars); forks report about 70% of game functions matched |
| Jet Set Radio Future | [andrewnakas/jsrf-decomp](https://github.com/andrewnakas/jsrf-decomp) | CC0 | AI-assisted; an earlier one by KeybadeBlox is on Codeberg (not checked) |
| Azurik: Rise of Perathia (EU) | [Azurik-Modding-Hub/Azurik-Decomp](https://github.com/Azurik-Modding-Hub/Azurik-Decomp) | MIT | Starter stage |
| Halo: Combat Evolved | [halo-re/halo](https://github.com/halo-re/halo) | none | A research reimplementation (2023), not matching |

**Static recompilations** (native ports; they document less of a game's
data than a decomp): Conker: Live & Reloaded (several, e.g.
[Mabec-games/Conker-Recomped-and-Reloaded](https://github.com/Mabec-games/Conker-Recomped-and-Reloaded),
GPL-3.0), Panzer Dragoon Orta ([wootbeer/ortarecomp](https://github.com/wootbeer/ortarecomp),
GPL-3.0; [an Android port](https://github.com/almost-drowned-rat/Orta-Recompiled-Android)),
Castlevania: Curse of Darkness, MechAssault 2, Splinter Cell: Double Agent,
Auto Modellista, three Harry Potter games; a general recompiler,
[Sewer56/XboxRecomp2](https://github.com/Sewer56/XboxRecomp2) (MIT).

## NFL 2K5 (ESPN NFL 2K5)

The owner's idea: play calling on the touch screen.

- **No decompilation or recompilation** found.
- **[cruuz/2k-football-mod-tools](https://github.com/cruuz/2k-football-mod-tools)**
  (MIT, active): mod editors for NFL 2K5 and All-Pro Football 2K8. It
  parses the game's playbook file (`MASTER PLAY`: 586 plays x 11 player
  slots, every route node, which formations each play belongs to), so
  **the plays could be drawn by RetroDS from the player's own disc**
  (Philosophy 6, as Super Metroid's map), not moved over from the game's
  overlay. Its many research reports also cover in-game behaviour
  (scorebug, clock, play rules). It doesn't give the live play-call menu's
  place in RAM: that is found by hand (hakuX's memory scanner, or RAM dumps
  compared, as `tools/ramdiff.py` does).
- **Touch:** play calling drives a game menu, the kind that made Final
  Fantasy VI's battle touch costly (`docs/games/final-fantasy-vi.md`).
  What makes it easier here, **if** the game shows a few plays at a time,
  each on a face button, paged with the shoulders (to check in the game): a
  tap presses that button, no memory writes, as Symphony of the Night's
  menu and FF6's Blitz codes are pressed. The lesson from FF6 holds: show
  first, touch second.
- **Second screen first idea:** the play-call page as the game lays it out
  (formation, then plays), drawn from `MASTER PLAY`; during a play, look
  only.

## The spike

In order; stop at the first that fails.

1. **The owner plays NFL 2K5 in hakuX itself on the Duo** (its own app,
   the player's own disc image). Speed, sound, controls. If it doesn't run
   well there, this ends here. Back up the Duo's states first only if
   RetroDS is touched (it isn't, at this step).
2. **The core without its app:** build `libhakuxcore.so` (its
   `android/README.md`), and from a small test program on the Duo (as
   `tools/gamecube/lrtest.c` did for Dolphin) boot a game with no window,
   run frames, read guest RAM. Then the same program with SDL and a bsnes
   copy loaded beside it (the JIT in a shared process).
3. **A frame on screen:** hakuX's Vulkan output handed to GLES (the Wind
   Waker study's option 1), timed on the device.
4. **Then decide:** a libretro wrapper (RetroDS's `emu.c` unchanged), or
   not. Only after that, NFL 2K5's addresses and `MASTER PLAY`.

Pass: a game boots and runs at its own speed inside a process with
RetroDS's other pieces, and its RAM is readable. Fail: it can't share the
process, or the Duo can't run it at speed.

## Sources

- [rfandango/hakuX](https://github.com/rfandango/hakuX): README, LICENSE,
  HANDOFF.md, `native/`, releases (looked at Nov 27, 2026)
- [VideoCardz: hakuX 1.2](https://videocardz.com/newz/original-xbox-emulator-hakux-1-2-now-runs-halo-forza-and-other-games-on-android-handhelds),
  [Android Authority](https://www.androidauthority.com/android-xbox-emulator-hakux-update-3729329/),
  [Retro Dodo](https://retrododo.com/we-finally-have-a-new-android-xbox-emulator-with-promising-performance/)
  (secondary: speed reports)
- The repositories linked in the tables above (GitHub's API: licence, last
  push, description)
