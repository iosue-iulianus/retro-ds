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
- The game list's header is taller, with bigger buttons, and the list
  shows every game; Supported Games (where Supported / All was) lists the
  games with a second screen and what's on it.
- The quick bar (a swipe up from the bottom edge) is gone: a tap opens the
  pause menu, and the hint at a game's start says so. The pause menu's Game
  page fills the screen, with big Save, Load and Close buttons; Patches and
  Submit a Bug Report are off it.
- The menus are regrouped: Screens (where the game goes and how big),
  Look (shader and overlays), Controls, Fast forward and System. Overlays
  is now the on/off with the screens, Overlay style picks the bezel or your
  own PNGs. Gone: the Second screen page (always on), Screen shapes, Screen
  placement and the brightness rows. Stepping through a setting for one
  game now goes round its whole list both ways (Pixel Perfect was
  unreachable going right).
- Game screen scale is 4:3 (as on a TV; fills the Duo's bottom screen) or
  Pixel Perfect. Full Width and Zoom were the same picture, narrower than
  a TV's; they and Stretch become 4:3. Until the player picks, the game goes
  on the screen where a 4:3 picture is physically biggest: the Duo's bottom
  screen, the AYN Thor's 6" top one.
- A Link to the Past: the items screen floats over the world map and slides
  up and away with X at the game's own speed, with the game's own opening
  and closing sounds (in dungeons over black, the dungeon map once it's
  away); a tap zooms the map in; the zoom indicator is the game's size; and
  markers: drag the item you'll need (bombs, hammer, glove, hookshot,
  flippers, boots, a fairy, a heart piece) onto the map to remember a place.

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
