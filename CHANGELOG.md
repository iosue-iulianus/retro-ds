# Changelog

SNES DS's history. It starts as a fork of GBA DS v1.5.6; GBA DS's own
changelog up to then is `git show 3b452dc:CHANGELOG.md` (and on
[GBA DS's GitHub](https://github.com/kyannon/gba-ds)).

## v0.1.0 (not yet released)

- **SNES DS is now RetroDS**, widening to other consoles (the PlayStation
  next). A new app on Android (`io.github.iosue_iulianus.retrods`), so it
  installs beside SNES DS. On its first start it copies the "SNES DS"
  folder's states, cheats, logos, screenshots, overlays and settings into
  its own "RetroDS" folder; SNES DS's folder is left as it was, so keep
  SNES DS until RetroDS shows your states. Battery saves stay beside the
  ROMs. `retrods.cfg` on Linux (it reads `~/.config/snes-ds/snes-ds.cfg`
  until there is one). A new logo and icon, by the owner.
- The menus are RetroDS's own: Barlow instead of Jost italic, red as the
  default colour. The game list's other screen shows the picture you'll
  continue from large, with when it was taken. In the pause menu, Save
  State and Load State are big and well apart, and a load or a save over a
  state can be undone (Undo Load, Undo Save). Map refresh only shows for
  games whose map is captured.

Nothing playable yet: SNES DS still runs GBA games until the emulator is
swapped. What has changed from GBA DS v1.5.6 so far:

- The app is SNES DS: its name, its Android app (it installs beside GBA DS
  instead of replacing it), its "SNES DS" folder on Android, and
  `snes-ds.cfg` on Linux. Updates come from SNES DS's own releases (there
  are none yet).
- Android is the target (the Retroid Pocket Duo). The ROCKNIX, BananaOS and
  Stock Linux OS packages, the web player and State Saver are gone, and so
  is GBA DS's EmulationStation and BananaOS integration.
- GBA DS's per-game pages, test cases and wiki guides are gone.
- A new SNES DS logo, at the top of the game list and printed on the
  generated bezel.
