What RetroDS is meant to be, and the principles behind what it does and doesn't do. If you've wondered why a game's second screen looks or works the way it does, or why a feature was turned down, this is usually why.

RetroDS started as a fork of [GBA DS](https://github.com/kyannon/gba-ds) and owes it a lot: the game list, the menus, the idea of a second screen filled from the game itself. It isn't a port of GBA DS's rules, though. A Super Nintendo is a different machine, and RetroDS takes its own liberties.

## 1. A portable Super Nintendo with a touch second screen.

Imagine Nintendo had made the Super Nintendo portable, with a second, touch screen under the first, the way the DS did for its own games. Imagine the games were updated for it: the map you used to pause for is now always in view, your equipment is a tap away, and the main screen is free for playing. That's RetroDS.

What this means in practice:

- **The game screen is the game.** It plays as it would on a Super Nintendo, on your buttons, and shows the game's picture as the console draws it. The few changes made to it come from the second screen taking something over: once the second screen always shows Super Metroid's map, the game's own mini-map is redundant and can go. Changes like that are listed for each game and can be switched off.
- **The second screen is the game's companion.** It shows what the game would have put there if it had been made for this handheld: a map, a status screen, your equipment, the party's health. It's live, and you can touch it.
- **The emulator's own screens are separate.** The game list, pause menu, settings and update screens belong to RetroDS, not to the imagined console. They have their own look.

## 2. In the game's own style.

Whatever is on the second screen should look as if the game's own artists made it. A player who knows the game should feel at home there at once.

What this means in practice:

- **The game's own art.** Tiles, colours, fonts, icons and frames all come from the game itself: from its own screens, its graphics in the cartridge, or what it draws while you play. RetroDS ships no art for any game.
- **New layouts are fine, new art isn't.** RetroDS can arrange the game's own pieces in ways the game never did: a whole area's map you can scroll freely, an equipment page you can tap, a world map assembled from the places you've been. It doesn't draw new pictures in a style of its own: no RetroDS font, icons or panels on the second screen.
- **Small marks where touch needs them.** A touch screen sometimes needs a hint the game never had, such as an arrow on a button that used to say L. Those are kept small, drawn from the game's own pieces where possible, and listed with the game's rules.
- **Pixels at the game's own size.** Pictures are scaled up the way the game screen is, nearest neighbour, so pixels look the same on both screens.

## 3. Useful at a glance, and smooth.

The second screen gives you what you'd otherwise pause for, and it should feel like a modern handheld, not like an emulator taking screenshots.

What this means in practice:

- **Show what matters right now.** The second screen follows what you're doing: the room you're in, the area you've reached, the party after the last battle.
- **It moves like a real screen.** A map scrolls under your finger at the screen's own refresh rate, without waiting for anything. A marker blinks the way the game blinks it. Delays and jumps are bugs, not the price of the approach.
- **When there's nothing useful, say so plainly.** On a title screen, in a cutscene, or anywhere RetroDS can't be sure what's right, the second screen shows the game's logo, or keeps the last good picture. Better nothing new than something wrong.
- **No enhancement for its own sake.** No smoothing or recolouring of the game's pictures. Display options (scaling, shaders) are your choice, in the pause menu.

## 4. Touch the second screen; play with the buttons.

The second screen is a touch screen, and that's the point of it. The game itself is still played with the buttons.

What this means in practice:

- **Touch is for looking and choosing.** Drag a map to look around; tap a page's button to switch pages; tap an item to switch it on or off. Those are things you'd have done in a pause menu.
- **The game stays on the buttons.** Running, jumping and fighting aren't moved onto the touch screen. There are no on-screen controls for playing.
- **Touch on the game screen belongs to RetroDS.** Tapping it opens the pause menu, as before.

## 5. Read the game freely, write to it rarely.

RetroDS learns what to show by reading the game: its memory (where you are, what you've explored, what you're carrying) and its cartridge (maps, graphics). Reading changes nothing. Writing to the game you're playing is different: it can upset the game, your saves or achievements, so it's kept to a minimum.

What this means in practice:

- **Reading is how it knows.** Your position, what you've explored and the items you have all come from the game's memory, every frame.
- **Writing only for a deliberate action.** When you tap something that changes the game (switching an item in Super Metroid), RetroDS makes the change the way the game's own pause screen would, between frames, and nothing else. Each one is listed with the game's rules.
- **No patching the game's code where it can be avoided.** The game runs as it was made.
- **Achievements stay fair.** With RetroAchievements' hardcore mode on, anything that writes to the game is switched off.

## 6. The best tool for each game.

Different games need different second screens, and RetroDS has several ways to make them. Each game uses whichever gives the best result, with the simplest one that's good enough.

- **Drawn from the game's data.** RetroDS reads the game's maps and graphics from the cartridge and what you've explored from its memory, and draws the screen itself, in the game's style. It's live, smooth and costs little. Super Metroid's map and equipment page work this way.
- **Assembled from what you've seen.** As you play, RetroDS keeps the pictures the game draws of each place, and puts them together into a map of everywhere you've been, with the rest left dark until you get there. That suits a world map the game never shows in one piece, such as A Link to the Past's overworld.
- **Opened on a hidden copy.** For a screen the game already draws well (a party's status, an inventory), a hidden second copy of the game opens it and the picture is shown on the second screen, without your game ever pausing. It works for almost any game, but it costs battery and is never instant, so it's for screens that don't need to be.
- **The logo,** when nothing else fits.

Whichever way a screen is made, it has to be checked against the game itself: a drawn map must match the game's own map, pixel for pixel, where the game has one.

## 7. Keep it simple and testable.

Every feature has to be built, tested on the real device and kept working by one developer. A smaller, solid feature beats a bigger, fragile one.

What this means in practice:

- **Rules first.** Each game's second screen is written down as a list of plain rules, checked against saved moments from real play whenever something changes.
- **Know the cost.** The handheld runs an accurate SNES emulator at full speed; anything else has to fit beside it without draining the battery.
- **Not "never", but "not at the cost of everything else working".** That's often the honest answer to a feature request.

## 8. Rules are written to be broken.

These principles are a guide, not a cage. An exception is fine when it makes a game clearly better on the device, as long as it's deliberate: written down with the game's rules, tested like everything else, and, where it changes how the game plays or looks, something you can switch off.

### Your own pictures

For a game whose profile allows it, you can also supply your own picture of a map (a lossless PNG or WebP, one pixel per game pixel), and RetroDS shows it on the second screen with a live marker at your position. RetroDS never ships these pictures: they're yours, in a folder of your own. Without one, the game uses its usual second screen.
