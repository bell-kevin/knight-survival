# Levels

Every `.tmx` file in this folder is a level, made with [Tiled](https://www.mapeditor.org/), the
free map editor. They are built into the game, so the LAN server, the desktop game and the
browser game always play the same levels.

Levels are played in file-name order (`level2` before `level10`). Collect enough coins and the
level's door opens; walk through it and the whole team goes on to the next level. After the last
level the first one comes round again, with one more bad guy each time.

| Level | Map | Coins to open the door | Bad guys |
| ----- | --- | ---------------------- | -------- |
| 1 | [`level1.tmx`](level1.tmx): The Mossy Cellar, a hall with pillars | 8 | red knights |
| 2 | [`level2.tmx`](level2.tmx): The Twin Halls, joined by two passages | 10 | scouts, red knights |
| 3 | [`level3.tmx`](level3.tmx): The Wooden Bridges, four rooms around a square | 10 | scouts, red knights, a champion |
| 4 | [`level4.tmx`](level4.tmx): The Labyrinth | 12 | red knights, a champion, a brute |
| 5 | [`level5.tmx`](level5.tmx): The Pillar Hall, with a carpet to the door | 12 | scouts, red knights, champions |
| 6 | [`level6.tmx`](level6.tmx): The Crossroads, four rooms around a wooden cross | 12 | scouts, red knights, brutes |
| 7 | [`level7.tmx`](level7.tmx): The Inner Court, a walled court with four gates | 14 | all but the warlord |
| 8 | [`level8.tmx`](level8.tmx): The Warlord's Hall | 15 | all of them, and the warlord before the door |

## Making a level

1. In Tiled, choose **File > New > New Map**: orthogonal, tile layer format **CSV**, tiles
   **16 x 16** px, map size **25 x 19** tiles (or smaller). Save it in this folder.
2. **Map > Add External Tileset**: [`tiles.tsx`](tiles.tsx), the game's own tiles from
   [`assets/Tiles`](../assets/Tiles).
3. Add these layers. The names matter; capital letters don't.

   | Layer | Kind | What it is |
   | ----- | ---- | ---------- |
   | `floor` | Tile layer | What the ground looks like. Any tile layer not called `walls` is just for looks. |
   | `walls` | Tile layer | Every tile on it is solid: knights and bullets can't go through. |
   | `doors` | Object layer | Rectangles. A door opens when the level's coins are collected. To choose where it leads, give it a `destination` property with a map's file name, like `level3`; without one it leads to the next level. |
   | `spawn points` | Object layer | Points named `player` (where players start) and `enemy` (where new bad guys come from: every 5th coin brings one, from a spawn point far enough from the players). |
   | `enemies` | Object layer | Points where a bad guy is already waiting when the level starts. |

   Which kind of bad guy a point is (or brings) is its **Class** in Tiled: `red`, `scout`,
   `champion`, `brute` or `warlord` (see the [README](../README.md#the-bad-guys) for what they
   do). Without one it's a red knight. A point in the `enemies` layer can also just be named
   after its kind, like `scout`.

4. **Map > Map Properties**: add `name` (shown when the level starts) and `coins` (an int: how
   many coins open the doors; 10 if you leave it out).
5. Build the game; it picks up new and changed maps by itself. Then run the tests
   (`ctest --test-dir build`): they check that every map loads, that the players can get from
   where they start to every door, bad guy and coin, that every bad guy fits where it stands,
   and that no coin sits next to a door.

Good to know:

- On screen a tile is 32 px (16 px tiles, drawn twice as big) and a knight is 40 px, so a
  knight needs passages at least 2 tiles wide. Put spawn points in the middle of a 2 x 2 patch
  of floor. The biggest bad guys still fit through those passages (a brute is 52 px, the warlord
  60 px), but they are easier to dodge with more room.
- The field is 800 x 600 px, which is 25 x 18¾ tiles: the 19th row is only partly on screen.
  The score is drawn over the top row and the level name over the bottom one, so make both wall.
- Coins only appear where a knight can get to, never on or next to a door.
- If a map can't be read, the game leaves it out and says why (in the terminal, or in the
  browser's console), and the tests fail. They also fail for a Class the game doesn't know (a
  typo like `brutte` would be played as a red knight).
- Tile pictures must be `.bmp` files in the [`assets`](../assets) folder (the game can only read
  BMP), and like everything here they must be free to share under the GPL.
