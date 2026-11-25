What RetroDS is meant to be, and the principles behind what it does and doesn't do. If you've wondered why a game's second screen looks or works the way it does, or why a feature was turned down, this is usually why.

## 1. Your favourite classics, with the second screen they never had.

Imagine your favourite Super Nintendo and PlayStation games had been made for a handheld with a second screen under the first, the way the DS games were. The map you used to pause for is always in view, your items are a tap away, and the game screen is left for playing. That's RetroDS.

What this means in practice:

- **The game screen is the game.** It plays as it did on the console, on your buttons, and shows the game's picture as the console drew it. It changes only where the second screen takes something over: Super Metroid's mini-map is covered because the full map is always on the second screen, and while Symphony of the Night's menu is on the second screen, the game screen shows the game's logo. Each change is listed in the game's rules.
- **The second screen is the game's companion.** It shows what the game would have put there: its map, its items, its menu. It's live, and you can touch it.
- **RetroDS's own screens are separate.** The game list, RetroDS's menu and settings belong to RetroDS, not to the game, and have their own look.

## 2. Stay within the game's own design.

Whatever is on the second screen should look as if the game's own artists put it there. A player who knows the game should feel at home at once.

What this means in practice:

- **The game's screens, rearranged for touch at most.** The second screen shows screens the game already has (its map, its items screen, its menu), in its own art: tiles, colours, fonts and frames from the game itself. They can be laid out for the screen (a whole area's map you can scroll, a menu widened to fill a wide screen), but nothing is invented: no RetroDS panels, fonts or icons, no presentations the game never had.
- **RetroDS ships no game art.** Everything comes from your copy of the game, as it runs.
- **Small marks only where touch needs them.** A touch screen sometimes needs something the game never had, such as A Link to the Past's zoom indicator. Those are kept small, in the game's style, and listed in the game's rules.
- **Pixels at the game's own size.** Pictures are scaled the way the game screen is, by whole pixels, so they look the same on both screens.

## 3. Useful at a glance, and smooth.

The second screen gives you what you'd otherwise pause for, and it should feel like a modern handheld's screen, not an emulator taking screenshots.

What this means in practice:

- **Show what matters right now.** The second screen follows what you're doing: the room you're in, the area you've reached, what you're carrying.
- **It moves like a real screen.** A map scrolls under your finger at the screen's own refresh rate. A marker blinks the way the game blinks it. Delays and jumps are bugs.
- **When unsure, show less.** On a title screen, in a cutscene, or anywhere RetroDS can't be sure what's right, the second screen shows the game's logo, or keeps the last good picture. Better nothing new than something wrong.
- **No enhancement for its own sake.** No smoothing or recolouring of the game's pictures. Display options (scaling, shaders) are yours to choose in RetroDS's menu.

## 4. Touch the second screen; play with the buttons.

The second screen is a touch screen, and that's the point of it. The game itself is played with the buttons.

What this means in practice:

- **Touch is for looking and choosing.** Drag, pinch or tap a map to look around; swipe between pages; tap an item to switch to it. Things you'd have done in a pause menu.
- **The game stays on the buttons.** Running, jumping and fighting aren't moved onto the touch screen, and there are no on-screen controls.
- **Where the buttons are better, they stay.** Symphony of the Night's menu moves to the second screen but is still worked with the pad, as it always was: it's quicker that way.
- **Touch on the game screen belongs to RetroDS.** Tapping it opens RetroDS's menu.

## 5. Read the game freely, write to it rarely.

RetroDS learns what to show by reading the game: its memory (where you are, what you've explored, what you're carrying), its video memory (what it has drawn) and its cartridge (maps, graphics). Reading changes nothing. Writing to the game you're playing could upset it, your saves or your achievements, so it's kept to a minimum.

What this means in practice:

- **Reading is how it knows.** Your position, what you've explored and what you carry come from the game, every frame.
- **Writing only for something you chose.** When a tap changes the game (switching an item in Super Metroid or A Link to the Past), RetroDS writes exactly what the game's own menu would, between frames, and only to the places listed in the game's rules.
- **Better still, press the game's buttons.** Where the game can do it itself, RetroDS presses its buttons instead of writing: Symphony of the Night's menu is opened and closed with the game's own Start and triangle, and nothing in it is ever written.
- **The game's code runs as it was made.** The emulators are patched only so RetroDS can read the game's memory and video memory; the games themselves are never patched for a second screen.

## 6. The best way for each game.

Different games need different second screens, and each game gets whichever way works best for it, checked against the game itself.

- **Drawn from the game's data.** RetroDS reads the game's maps and graphics and what you've explored, and draws the game's own screen itself, live. Super Metroid's map and SAMUS page, A Link to the Past's items screen and dungeon maps.
- **Assembled from what you've seen.** RetroDS keeps the ground the game draws as you play and puts it together into a map of everywhere you've been, the game's own clouds over the rest. A Link to the Past's world, which the game never shows in one piece.
- **Taken from the game's own drawing.** Where the game keeps a screen's picture in video memory, RetroDS shows that picture as the game draws it. Symphony of the Night's castle map.
- **The game's own screen, moved over.** The game opens its own screen and RetroDS shows it on the second screen instead of the game screen. Symphony of the Night's menu, widened to the second screen's shape.
- **The logo,** when nothing else fits, and for every game without a second screen of its own.

Whichever way a screen is made, it's checked against the game: a drawn map has to match the game's own map pixel for pixel.

## 7. Keep it simple and testable.

Every feature has to be built, tested on the real device and kept working. A smaller, solid feature beats a bigger, fragile one.

What this means in practice:

- **Rules first.** Each game's second screen is written down as plain rules before it's built, and checked against saved moments from real play whenever something changes.
- **Know the cost.** The handheld runs an accurate emulator at full speed; the second screen has to fit beside it without draining the battery.
- **Not "never", but "not at the cost of everything else working".** That's often the honest answer to a feature request.

## 8. Rules are written to be broken.

These principles are a guide, not a cage. An exception is fine when it makes a game clearly better on the device, as long as it's deliberate: written down in the game's rules and tested like everything else.
