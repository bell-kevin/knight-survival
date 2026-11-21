// level.h - The levels: maps made with Tiled (https://www.mapeditor.org/), a free map editor.
//
// Every maps/*.tmx file is built into the game (see CMakeLists.txt), so the LAN server, the
// desktop game and the browser game all play exactly the same levels. maps/README.md explains
// how to make one.
//
// Copyright (C) 2026 Kevin Bell. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "enemies.h" // the kinds of bad guys a map can place

const int W = 800, H = 600;      // size of the playing field in pixels
const int CELL = 32;             // one map tile on the field: 16 pixel tiles, drawn twice as big
const float KNIGHT_SIZE = 40.0f; // every knight, white, blue or red, is this big

// Tiled keeps a tile's mirroring in the top bits of its number
const unsigned FLIPPED_HORIZONTALLY = 0x80000000u;
const unsigned FLIPPED_VERTICALLY = 0x40000000u;
const unsigned TILE_NUMBER = 0x0FFFFFFFu; // the rest is the tile itself (0 = no tile)

struct Rect
{
    float x, y, w, h;
};

inline bool checkCollision(const Rect &a, const Rect &b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

struct Point
{
    float x, y;
};

// Where a tile's picture is: a .bmp file (relative to the assets folder), or part of one
struct TilePicture
{
    std::string file; // "" if there's no such tile
    int x = 0, y = 0, w = 0, h = 0; // w == 0: the whole file
};

struct Door
{
    Rect area;                 // on the field
    std::string destinationId; // the level it leads to, as the map names it ("" = the next one)
    int destination = 0;       // the same level, as an index into LevelSet::levels
};

// Where a bad guy is, or comes from: the middle of it, and which kind (see enemies.h)
struct EnemySpot
{
    float x, y;
    int kind = RED_KNIGHT;
};

// One level. The field is a grid of CELL x CELL cells, and a knight needs 2 x 2 free cells to
// stand: a "spot", known by the number of its top-left cell (row * cols + col).
struct Level
{
    std::string id;   // the file name without ".tmx": what a door's "destination" names
    std::string name; // shown when the level starts
    int cols = 0, rows = 0;
    int coins = 10; // coins to collect before the doors open

    std::vector<std::vector<unsigned>> layers; // the tile layers to draw, bottom one first
    std::vector<TilePicture> pictures;         // tile number -> its picture
    std::vector<unsigned char> walls;          // for every cell: 1 if it's solid

    std::vector<Door> doors;
    std::vector<Point> playerSpawns;     // where players appear (the middle of the knight)
    std::vector<EnemySpot> enemySpawns;  // where new bad guys come from, each spot its own kind
    std::vector<EnemySpot> enemyStarts;  // bad guys already there when the level starts
    std::vector<std::string> warnings;   // things in the map the game didn't understand

    // Worked out by prepare()
    std::vector<unsigned char> standable; // for every spot: does a knight fit there?
    std::vector<int> open;                // every spot a knight can walk to from the player spawns
    std::vector<int> coinCells;           // every cell a coin can go on (reachable, and not by a door)

    bool wall(int col, int row) const; // outside the map counts as wall
    bool canStand(int col, int row) const;
    bool blocked(const Rect &r) const; // does r touch a wall or stick out of the field?
    // Moves r by (dx, dy), stopping at walls and sliding along them
    void slide(Rect &r, float dx, float dy) const;
    // Could a knight at `from` walk in a straight line to top-left corner `to`?
    bool clearPath(const Rect &from, Point to) const;

    Point spotPosition(int spot) const;   // top-left corner of a knight standing in the middle of it
    int spotNear(float x, float y) const; // the spot a knight with top-left corner (x, y) is on (-1: none)
    // How many steps every spot is from the nearest of `from` (-1: can't get there from any)
    std::vector<int> distances(const std::vector<int> &from) const;
    // Of a spot and the ones around it, the one that is fewest steps away (by `distances`)
    int downhill(int spot, const std::vector<int> &distance) const;

    void prepare(); // call once the map is loaded
};

struct LevelSet
{
    std::vector<Level> levels;         // in the order they are played: by file name, level2 before level10
    std::vector<std::string> problems; // maps that could not be loaded (and why), and the levels' warnings
};

struct MapFile
{
    std::string name; // file name, like "level1.tmx" or "tiles.tsx"
    std::string text;
};

// Reads the levels from Tiled .tmx maps (and the .tsx tilesets they use, from the same list).
// Maps that can't be read are left out and explained in `problems`. There is always at least
// one level: if no map can be read, it's openField().
LevelSet loadLevels(const std::vector<MapFile> &files);

// Just one level, for tests. Returns false and explains why if it can't be read.
bool loadLevel(const std::string &fileName, const std::vector<MapFile> &files, Level &out, std::string &problem);

const std::vector<MapFile> &builtInMapFiles(); // maps/*.tmx and maps/*.tsx (generated by CMake)
const LevelSet &builtInLevels();               // the levels the game plays
const Level &builtInLevel(int index);          // one of them (the first one if there's no such level)

// No walls and no doors, like the game was before it had levels
Level openField();
