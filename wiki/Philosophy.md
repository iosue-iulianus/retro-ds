What SNES DS is meant to be, and the principles behind what it does and doesn't do. If you've asked for a feature and the answer was no, or wondered why a game's second screen looks the way it does, this is usually why.

SNES DS is a fork of [GBA DS](https://github.com/kyannon/gba-ds), and these principles are GBA DS's, carried over to the Super Nintendo.

## 1. It's a Super Nintendo with a second screen.

SNES DS emulates a Super Nintendo with a second screen beside the TV, neither of them a touchscreen, as if Nintendo had sold one as an add-on for the console. On a two-screen handheld, the game plays on one screen and the second screen sits on the other. All the other rules follow from this one.

What this means in practice:

- **The second screen is an SNES screen.** What the game puts there is an SNES picture (256 pixels wide, 224 tall, or the wider picture of a game that switches to the SNES's high-resolution mode), scaled up exactly like the game screen, so a pixel is the same size on both screens where the screens allow it. Whatever goes there is drawn at the game's own size. The one exception is custom asset mode (rule 6), which draws its picture at the second screen's own resolution.
- **Ask what the game would have done.** When deciding what goes on the second screen, the question is: if this game had been made for an SNES with a second screen, what would its developers have put there? Usually the answer is something the game already has: the map, a status screen, the party's health.
- **The game itself isn't changed.** The game you're playing runs as it would on an SNES. Its second screen is filled from a hidden second copy of the game, which opens menus there without ever pausing your game, sending it input or changing its memory. Reading the live game's memory to know where you are or what is on screen is fine; writing to it is not. The game screen shows the game's whole picture, exactly as the SNES draws it.
- **The emulator's own screens are separate.** The game list, pause menu, settings and update screens belong to SNES DS, not to the imagined console. They have their own look and use the touchscreen (rule 5).

## 2. Function over form.

The second screen gives the player information or context they would otherwise have to find in a pause menu or some other way. SNES DS puts that first, ahead of making anything look prettier or better than it did in the original game.

What this means in practice:

- **Useful beats pretty.** A status screen that shows the party's health and magic, even cropped to fit, is better than a nicer-looking screen with less in it, or a slower one.
- **Show what matters right now.** The second screen follows what the player is doing: the room Samus is in, the party as it is after the last battle. Keeping up quickly is part of the function.
- **Nothing there should make you look twice.** The second screen is for looking at; everything you act on stays on the game screen. So the second screen leaves out anything that looks like it's waiting for a button or like the thing you're controlling: button prompts, a menu cursor, the character you're moving. Those belong to the hidden copy, not your game. A map's own marker of where you are is information, and stays. Nothing is split across the two screens or shown on both: half an animation on each screen, or the same box twice, leaves you wondering which one is real.
- **When there's nothing useful, say so plainly.** If there's nothing live to show (a title screen, a cutscene, a menu), the second screen shows the game's logo at half strength so it reads as idle, rather than something stale or invented. If a capture can't be confirmed as correct, the last good picture stays up. Better nothing new than something wrong.
- **No enhancement for its own sake.** No smoothing, recolouring, upscaled art or animated transitions. Display options (scaling, shaders, overlays) are the player's choice, set in the pause menu, and are applied the same way to both screens. Overlays only ever fill the space around the SNES pictures, never what's on them.

## 3. Keep it simple.

SNES games use their layers, mid-frame tricks and extra chips in complex, clever ways. SNES DS shouldn't spend a lot of processing power untangling all of that, as a full decompilation and recompilation project might. Logic and actions should be simple to test, and should cover as many cases as possible in as few steps as possible.

What this means in practice:

- **Use what the game already does.** The usual approach is to open the game's own menu on the hidden copy and take a picture of it. That's simple, works in every situation the menu can be opened, and gets the game's own drawing for free. Reading a few memory addresses to know when to do it (a "screen is open" flag, the room number) is fine. Rebuilding a screen from the game's data is not.
- **Fewer, broader checks.** One check that holds everywhere (the menu's open flag plus a few fixed pixels) is better than many special cases. When a game has an odd situation (a scene that blocks the menu), it's often fine to show the logo there and write it down as a known issue, instead of adding a workaround.
- **Everything has to be testable.** Each game's behaviour is written down as a list of plain rules, and checked against saved moments from real play every time something changes. If a behaviour can't be described simply and checked that way, it's probably too complicated.
- **Know the cost.** The device has to run the game, the hidden copy and the drawing at full speed. An accurate SNES emulator is far more expensive than a GBA one, so a second copy of the game is already most of the budget. Nothing else runs a third.

## 4. Use the original game's aesthetic.

Don't create original UI elements from scratch. Everything drawn on either screen comes directly from the original game's own screens (its pause and status menus, its map), with as little changed as possible. Small changes are fine where they make sense, such as removing button prompts that no longer apply.

What this means in practice:

- **Taken from the game, not drawn for it.** Logos are taken from each game's own title screen. Status screens and maps are the game's own, captured from the hidden copy. SNES DS ships no art for any game.
- **Allowed changes are small and mechanical.** These include:
  - cropping to the part that matters;
  - covering a button prompt that no longer applies with the background around it;
  - turning through a status screen's pages on a timer, where the game splits what matters over several pages.
- **Not allowed:** text in SNES DS's font on the second screen, new icons, panels or frames of SNES DS's own design, redrawn or restyled game art, or a layout so rearranged that it no longer reads as the game's own screen.
- **The exception that proves the rule:** when the logo is all the second screen shows, it's on dark grey rather than black, and at half strength. That's SNES DS signalling where the screen is and that it's idle, kept to a minimum.

## 5. No in-game touch.

Touch is only for things the emulator does, such as the pause menu. SNES DS doesn't add touch controls to SNES games.

What this means in practice:

- **Touch belongs to SNES DS.** Tapping opens the pause menu; the game list, settings and the pause menu are touch controls for SNES DS itself.
- **Nothing on the second screen needs a tap.** Everything the game puts there is to look at. Tapping a map to move there, or any other touch input sent to the game, is out of scope, however natural it might feel on a two-screen handheld.
- **Buttons give the game an SNES pad.** The handheld's buttons map to the SNES controller as they're laid out, and you can change that game by game.

## 6. Rules are written to be broken.

Above all, SNES DS is a fun exercise in making these games more fun to play on a dual-screen handheld. It also has to stay controlled enough that development doesn't get away from one developer and tester. So exceptions are allowed, deliberately.

What this means in practice:

- **An exception has to earn its place.** It should make the game clearly more fun or easier to play on the device, and be something the player would recognise. Being technically possible is not enough.
- **Every exception is deliberate.** It's written down with the game's other rules and tested like everything else, so it isn't "fixed" later by mistake.
- **Optional where it can be.** Where an exception changes how the game plays or looks, there's a way back to the original.
- **Scope is a feature.** Every change has to be built, tested on the real device and maintained by one person. A smaller, solid feature beats a bigger, fragile one, and that's often the honest answer to a feature request: not "never", but "not at the cost of everything else working".

### Custom asset mode

Custom asset mode is the one standing exception, and it is opt-in, game by game. Some screens can't be captured well: A Link to the Past's overworld map is a Mode 7 zoom that never settles into a clean picture. For a game whose profile allows it, you can supply your own picture of the map, and SNES DS shows it on the second screen with a marker at your position.

- **Your picture, not ours.** SNES DS ships no map art. The picture, the marker and any overlays come from you, in a folder of your own, and are never part of a release. Without them, the game behaves as if the mode didn't exist: the captured screen, or the logo.
- **Lossless only.** The picture is a PNG or a lossless WebP, one picture pixel to one game pixel, with no upscaling baked in. It's drawn at the second screen's own resolution with nearest-neighbour scaling, either the whole world fitted to the screen or a window that follows you.
- **The game is still untouched.** Your position is read from the live game's memory; nothing is written to it, and no hidden copy runs while the picture is up.
- **When unsure, show less.** The marker hides in menus, transitions and cutscenes. If the picture is missing or can't be read, SNES DS falls back to the captured screen, then the logo.
- **Extra layers are off by default.** A picture can come with overlays (where the heart pieces are, for example), each one switched on in the pause menu.

Current exceptions: custom asset mode, for the games whose profiles allow it (planned first for A Link to the Past's overworld).
