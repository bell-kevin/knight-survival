// world_test.cpp - Checks the shared game rules, the levels and the snapshot text format.
// Run with: ctest --test-dir build   (or just ./build/world-test)
//
// Copyright (C) 2026 Kevin Bell. SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "world.h"

static int failures = 0;

#define CHECK(condition)                                                     \
    do                                                                       \
    {                                                                        \
        if (!(condition))                                                    \
        {                                                                    \
            std::printf("FAILED line %d: %s\n", __LINE__, #condition);       \
            failures++;                                                      \
        }                                                                    \
    } while (0)

// Runs the world for `seconds`, the way every host does: in 1/60 s steps.
static void run(World &world, float seconds)
{
    for (int i = 0; i < (int)std::lround(seconds / TICK); i++)
        world.step(TICK);
}

// The original rules are checked on a field without walls, like the game had before levels
static const LevelSet OPEN = [] {
    LevelSet set;
    set.levels.push_back(openField());
    return set;
}();

// A small map for tests, drawn as text: '#' wall, '.' floor. 'P' (where players start),
// 'E' (a red knight) and 'D' (a door) mark the top-left cell of a 2 x 2 spot of floor.
static MapFile drawnMap(const std::string &name, const std::vector<std::string> &rows, int coins = 10,
                        const std::string &destination = "")
{
    int cols = (int)rows[0].size(), height = (int)rows.size();
    std::string walls, objects;
    int id = 1;
    for (int r = 0; r < height; r++)
    {
        for (int c = 0; c < cols; c++)
        {
            char ch = rows[(size_t)r][(size_t)c];
            walls += std::string(ch == '#' ? "1" : "0") + (r == height - 1 && c == cols - 1 ? "\n" : ",");
            std::string x = std::to_string(c * 16), y = std::to_string(r * 16);
            std::string middleX = std::to_string(c * 16 + 16), middleY = std::to_string(r * 16 + 16);
            if (ch == 'P')
                objects += "<objectgroup name=\"spawn points\"><object id=\"" + std::to_string(id++) + "\" name=\"player\" x=\"" +
                           middleX + "\" y=\"" + middleY + "\"><point/></object></objectgroup>\n";
            if (ch == 'E')
                objects += "<objectgroup name=\"enemies\"><object id=\"" + std::to_string(id++) + "\" name=\"knight\" x=\"" +
                           middleX + "\" y=\"" + middleY + "\"><point/></object></objectgroup>\n";
            if (ch == 'D')
                objects += "<objectgroup name=\"doors\"><object id=\"" + std::to_string(id++) + "\" x=\"" + x + "\" y=\"" + y +
                           "\" width=\"32\" height=\"32\"><properties><property name=\"destination\" value=\"" +
                           destination + "\"/></properties></object></objectgroup>\n";
        }
    }
    std::string text = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<map version=\"1.10\" orientation=\"orthogonal\" width=\"" +
                       std::to_string(cols) + "\" height=\"" + std::to_string(height) +
                       "\" tilewidth=\"16\" tileheight=\"16\" infinite=\"0\">\n"
                       "<properties><property name=\"coins\" type=\"int\" value=\"" + std::to_string(coins) + "\"/></properties>\n"
                       "<tileset firstgid=\"1\" name=\"t\" tilewidth=\"16\" tileheight=\"16\" tilecount=\"1\" columns=\"1\">"
                       "<image source=\"../assets/Tiles/MossyStone.bmp\" width=\"16\" height=\"16\"/></tileset>\n"
                       "<layer id=\"1\" name=\"walls\" width=\"" + std::to_string(cols) + "\" height=\"" + std::to_string(height) +
                       "\"><data encoding=\"csv\">\n" + walls + "</data></layer>\n" + objects + "</map>\n";
    return {name, text};
}

static bool isOpen(const Level &level, int spot)
{
    return std::find(level.open.begin(), level.open.end(), spot) != level.open.end();
}

// Puts a knight's top-left corner in the middle of the 2 x 2 spot whose top-left cell is (col, row)
static Point at(int col, int row)
{
    return {col * CELL + (2 * CELL - KNIGHT_SIZE) / 2, row * CELL + (2 * CELL - KNIGHT_SIZE) / 2};
}

static void snapshotRoundTrip()
{
    Snapshot s;
    s.epoch = 1794983104123.0;
    s.generation = 3;
    s.hostId = 2;
    s.score = 42;
    s.level = 7;
    s.map = 2;
    s.coinsLeft = 4;
    s.players = {{1, 100, 200}, {2, DEAD, DEAD}, {3, 760, 560}};
    s.coins = {{10, 20}, {30, 40}};
    s.enemies = {{300, 300}};
    s.bullets = {{400, 300, 600, 0}, {12, 34, -424, -424}};

    Snapshot back;
    CHECK(decodeSnapshot(encodeSnapshot(s), back));
    CHECK(back.epoch == s.epoch);
    CHECK(back.generation == 3 && back.hostId == 2 && back.score == 42);
    CHECK(back.level == 7 && back.map == 2 && back.coinsLeft == 4);
    CHECK(back.players.size() == 3 && back.players[2].id == 3 && back.players[2].x == 760);
    CHECK(back.players[1].x < 0);
    CHECK(back.coins.size() == 2 && back.coins[1].y == 40);
    CHECK(back.enemies.size() == 1 && back.enemies[0].x == 300);
    CHECK(back.bullets.size() == 2 && back.bullets[1].vx == -424);
}

static void snapshotRejectsGarbage()
{
    Snapshot out;
    out.score = 7;
    CHECK(!decodeSnapshot("", out));
    CHECK(!decodeSnapshot("W", out));
    // fields: W epoch generation hostId score level map coinsLeft players coins enemies bullets
    CHECK(!decodeSnapshot("W 1 0 0 0 1 0 0", out));                  // cut short
    CHECK(!decodeSnapshot("W 1 0 0 0 1 0 0 1 1 nan 3 0 0 0", out));  // not a number
    CHECK(!decodeSnapshot("W 1 0 0 0 1 0 0 999999 1 2 3", out));     // absurd count
    CHECK(!decodeSnapshot("W 1 0 0 -5 1 0 0 0 0 0 0", out));         // negative score
    CHECK(!decodeSnapshot("W 1 0 0 0 1 -1 0 0 0 0 0", out));         // negative map
    CHECK(!decodeSnapshot("X 1 0 0 0 1 0 0 0 0 0 0", out));          // not a world at all
    CHECK(!decodeSnapshot("W 1 0 0 0 0 0 0 0", out));                // the format before levels
    CHECK(out.score == 7);                                           // left alone on failure
    CHECK(decodeSnapshot("W 1 0 0 0 1 0 0 0 0 0 0", out) && out.score == 0 && out.level == 1);
}

static void coinsScoreAndBringEnemies()
{
    World world(1, 1, &OPEN);
    int id = world.addPlayer();
    CHECK(id == 1);
    Point stoodAt{0, 0};
    for (int coin = 1; coin <= 5; coin++)
    {
        Snapshot s = world.snapshot(0);
        stoodAt = s.coins[0];
        world.setPlayerPosition(id, stoodAt.x, stoodAt.y); // stand on a coin
        world.step(TICK);
        world.setPlayerPosition(id, DEAD, DEAD); // step away (dead players don't collect)
    }
    Snapshot s = world.snapshot(0);
    CHECK(s.score == 5);
    CHECK(s.enemies.size() == 1); // every 5 points brings an enemy...
    if (!s.enemies.empty())       // ...but not right on top of the player who was standing there
        CHECK(std::hypot(s.enemies[0].x - stoodAt.x, s.enemies[0].y - stoodAt.y) >= 150);
}

static void bulletsHitEnemies()
{
    World world(1, 1, &OPEN);
    Snapshot seed;
    seed.players = {{1, 20, 20}};
    seed.enemies = {{400, 300}};
    seed.coins = {{700, 50}, {700, 80}, {700, 110}, {700, 140}, {700, 170}};
    world.seed(seed);
    world.addBullet(100, 316, BULLET_SPEED, 0); // fly right, into the enemy
    run(world, 1.0f);
    Snapshot s = world.snapshot(0);
    CHECK(s.enemies.empty());
    CHECK(s.bullets.empty());
    CHECK(s.score == 10);
}

static void enemiesChaseTheLiving()
{
    World world(1, 1, &OPEN);
    Snapshot seed;
    seed.players = {{1, 100, 300}, {2, DEAD, DEAD}};
    seed.enemies = {{600, 300}};
    seed.coins = {{700, 50}, {700, 80}, {700, 110}, {700, 140}, {700, 170}};
    world.seed(seed);
    run(world, 1.0f);
    Snapshot s = world.snapshot(0);
    CHECK(s.enemies.size() == 1);
    if (!s.enemies.empty())
        CHECK(std::fabs(s.enemies[0].x - (600 - ENEMY_SPEED)) < 2); // walked ~80 px toward player 1
}

static void worldResetsOnlyWhenEveryoneIsDead()
{
    World world(1, 1, &OPEN);
    Snapshot seed;
    seed.score = 30;
    seed.players = {{1, DEAD, DEAD}, {2, 500, 500}};
    seed.enemies = {{100, 100}};
    seed.coins = {{700, 50}, {700, 80}, {700, 110}, {700, 140}, {700, 170}};
    world.seed(seed);
    world.step(TICK);
    CHECK(world.snapshot(0).score == 30); // player 2 is still alive
    world.setPlayerPosition(2, DEAD, DEAD);
    world.step(TICK);
    CHECK(world.snapshot(0).score == 0);
    CHECK(world.snapshot(0).enemies.empty());
}

static void playersComeAndGo()
{
    World world(1, 1, &OPEN);
    for (int i = 1; i <= MAX_PLAYERS; i++)
        CHECK(world.addPlayer() == i);
    CHECK(world.addPlayer() == 0); // full
    world.removePlayer(3);
    CHECK(!world.hasPlayer(3));
    CHECK(world.addPlayer() == 3); // the smallest free id, like the original server
    world.setPlayerPosition(1, -0.5f, 300);
    Snapshot s = world.snapshot(0);
    CHECK(s.players[0].x < 0); // x below 0 means dead
    world.setPlayerPosition(1, 5000, 5000);
    s = world.snapshot(0);
    CHECK(s.players[0].x == W - PLAYER_SIZE && s.players[0].y == H - PLAYER_SIZE); // kept on the field
}

static void hostMigrationKeepsTheWorld()
{
    World old(123, 1, &OPEN);
    int a = old.addPlayer(), b = old.addPlayer();
    old.setPlayerPosition(a, 50, 50);
    old.setPlayerPosition(b, 600, 400);
    old.addBullet(300, 300, 0, -BULLET_SPEED);
    Snapshot last = old.snapshot(a);

    World next(last.epoch, 1, &OPEN);
    next.seed(last);
    next.removePlayer(last.hostId); // the old host left
    Snapshot s = next.snapshot(b);
    CHECK(s.epoch == 123);
    CHECK(s.players.size() == 1 && s.players[0].id == b);
    CHECK(s.coins.size() == (size_t)COIN_COUNT);
    CHECK(s.bullets.size() == 1 && s.bullets[0].vy == -BULLET_SPEED);
}

static void cheatyBulletsAreTamed()
{
    World world(1, 1, &OPEN);
    world.addBullet(100, 100, 1e9f, 0);
    world.addBullet(NAN, 100, 10, 0);
    world.addBullet(100, 100, 0, 0);
    Snapshot s = world.snapshot(0);
    CHECK(s.bullets.size() == 1);
    if (!s.bullets.empty())
        CHECK(std::fabs(s.bullets[0].vx - BULLET_SPEED) < 0.01f);
}

// ---- Levels ----------------------------------------------------------------------------------

static void tiledMapsAreRead()
{
    std::vector<MapFile> files = {
        {"tiles.tsx", R"(<?xml version="1.0" encoding="UTF-8"?>
<tileset version="1.10" tiledversion="1.11.2" name="t" tilewidth="16" tileheight="16" tilecount="2" columns="0">
 <grid orientation="orthogonal" width="1" height="1"/>
 <tile id="0"><image source="../assets/Tiles/MossyStone.bmp" width="16" height="16"/></tile>
 <tile id="1"><image source="..\assets\Tiles\Wood &amp; Nails.bmp" width="16" height="16"/></tile>
</tileset>
)"},
        {"Cave.tmx", R"(<?xml version="1.0" encoding="UTF-8"?>
<map version="1.10" tiledversion="1.11.2" orientation="orthogonal" renderorder="right-down" width="4" height="3" tilewidth="16" tileheight="16" infinite="0" nextlayerid="6" nextobjectid="5">
 <properties>
  <property name="Name" value="The Test Cave"/>
  <property name="coins" type="int" value="3"/>
 </properties>
 <tileset firstgid="1" source="tiles.tsx"/>
 <tileset firstgid="3" name="sheet" tilewidth="16" tileheight="16" spacing="1" margin="1" tilecount="4" columns="2">
  <image source="../assets/Tiles/sheet.bmp" width="35" height="35"/>
 </tileset>
 <!-- the floor, with the first tile mirrored -->
 <layer id="1" name="floor" width="4" height="3">
  <data encoding="csv">
2147483650,2,2,2,
2,2,2,2,
2,2,2,6
</data>
 </layer>
 <layer id="2" name="Walls" width="4" height="3">
  <data>
   <tile gid="1"/><tile/><tile/><tile gid="1"/>
   <tile/><tile/><tile/><tile/>
   <tile gid="1"/><tile/><tile/><tile gid="1"/>
  </data>
 </layer>
 <layer id="3" name="notes" width="4" height="3" visible="0">
  <data encoding="csv">0,0,0,0,0,0,0,0,0,0,0,0</data>
 </layer>
 <objectgroup id="3" name="Doors">
  <object id="1" x="16" y="16" width="16" height="16">
   <properties>
    <property name="destination" value="LEVEL2.tmx"/>
   </properties>
  </object>
 </objectgroup>
 <objectgroup id="4" name="spawn points">
  <object id="2" name="player" x="24" y="24">
   <point/>
  </object>
  <object id="3" name="enemy" x="40" y="24" width="8" height="8">
   <ellipse/>
  </object>
 </objectgroup>
 <objectgroup id="5" name="enemies">
  <object id="4" name="bat" x="8" y="8">
   <point/>
  </object>
 </objectgroup>
</map>
)"},
        drawnMap("level2.tmx", {"#####", "#P..#", "#..D#", "#...#", "#####"}),
        drawnMap("level10.tmx", {"#####", "#P..#", "#...#", "#...#", "#####"}),
    };
    LevelSet set = loadLevels(files);
    for (auto &problem : set.problems)
        std::printf("unexpected problem: %s\n", problem.c_str());
    CHECK(set.problems.empty());
    CHECK(set.levels.size() == 3 && set.levels[0].id == "Cave" && set.levels[1].id == "level2" && set.levels[2].id == "level10");
    if (set.levels.size() != 3)
        return;

    const Level &cave = set.levels[0];
    CHECK(cave.name == "The Test Cave" && cave.coins == 3 && cave.cols == 4 && cave.rows == 3);
    CHECK(cave.pictures.size() == 7);
    if (cave.pictures.size() == 7)
    {
        CHECK(cave.pictures[1].file == "Tiles/MossyStone.bmp" && cave.pictures[1].w == 0);
        CHECK(cave.pictures[2].file == "Tiles/Wood & Nails.bmp");
        CHECK(cave.pictures[3].file == "Tiles/sheet.bmp" && cave.pictures[3].x == 1 && cave.pictures[3].y == 1 && cave.pictures[3].w == 16);
        CHECK(cave.pictures[6].x == 18 && cave.pictures[6].y == 18 && cave.pictures[6].h == 16);
    }
    CHECK(cave.layers.size() == 2); // the hidden layer isn't drawn
    if (!cave.layers.empty())
        CHECK((cave.layers[0][0] & TILE_NUMBER) == 2 && (cave.layers[0][0] & FLIPPED_HORIZONTALLY) && !(cave.layers[0][0] & FLIPPED_VERTICALLY));
    CHECK(cave.wall(0, 0) && !cave.wall(1, 0) && cave.wall(3, 2) && !cave.wall(1, 1));
    CHECK(cave.wall(-1, 0) && cave.wall(4, 0) && cave.wall(0, 3)); // outside the map is wall
    CHECK(cave.doors.size() == 1);
    if (!cave.doors.empty())
    {
        const Rect &area = cave.doors[0].area; // map pixels are drawn twice as big on the field
        CHECK(area.x == 32 && area.y == 32 && area.w == 32 && area.h == 32);
        CHECK(cave.doors[0].destination == 1); // "LEVEL2.tmx" is level2
    }
    CHECK(cave.playerSpawns.size() == 1 && cave.playerSpawns[0].x == 48 && cave.playerSpawns[0].y == 48);
    CHECK(cave.enemyStarts.size() == 1 && cave.enemyStarts[0].x == 16 && cave.enemyStarts[0].y == 16);
    CHECK(cave.enemySpawns.size() == 2 && cave.enemySpawns[0].x == 88 && cave.enemySpawns[0].y == 56);

    CHECK(set.levels[1].doors.size() == 1 && set.levels[1].doors[0].destination == 2); // no destination: the next level
    CHECK(set.levels[1].name == "level2" && set.levels[1].coins == 10);                  // the defaults
}

static void badMapsAreExplained()
{
    const std::string head = R"(<?xml version="1.0"?><map orientation="orthogonal" tilewidth="16" tileheight="16" )";
    std::vector<MapFile> files = {
        {"a.tmx", head + R"(width="4" height="4" infinite="1"></map>)"},
        {"b.tmx", head + R"(width="2" height="2"><layer name="walls" width="2" height="2"><data encoding="base64" compression="zlib">eJxjYGBgAAAABAAB</data></layer></map>)"},
        {"c.tmx", head + R"(width="30" height="20"></map>)"},
        {"d.tmx", head + R"(width="2" height="2"><tileset firstgid="1" source="nowhere.tsx"/></map>)"},
        {"e.tmx", head + R"(width="2" height="2"><tileset firstgid="1" tilewidth="16" tileheight="16"><image source="../assets/stone.png" width="16" height="16"/></tileset></map>)"},
        {"f.tmx", head + R"(width="2" height="2"><layer name="walls" width="2" height="2"><data encoding="csv">1,0,0</data></layer></map>)"},
        {"g.tmx", "<map width=\"2\""},
        drawnMap("h.tmx", {"####", "#PD.", "#...", "####"}, 5, "nowhere"),
    };
    LevelSet set = loadLevels(files);
    auto mentions = [&](const std::string &file, const std::string &words) {
        return std::any_of(set.problems.begin(), set.problems.end(), [&](const std::string &p) {
            return p.rfind(file, 0) == 0 && p.find(words) != std::string::npos;
        });
    };
    CHECK(mentions("a.tmx", "infinite"));
    CHECK(mentions("b.tmx", "CSV"));
    CHECK(mentions("c.tmx", "25 x 19"));
    CHECK(mentions("d.tmx", "nowhere.tsx is missing"));
    CHECK(mentions("e.tmx", "not a .bmp"));
    CHECK(mentions("f.tmx", "3 tiles instead of 4"));
    CHECK(mentions("g.tmx", "ends in the middle"));
    CHECK(mentions("h.tmx", "no such map"));
    CHECK(set.levels.size() == 1 && set.levels[0].id == "h"); // the one that loads is still played

    LevelSet none = loadLevels({files[0]});
    CHECK(none.levels.size() == 1 && none.levels[0].id == "open-field"); // never nothing to play
}

static void wallsStopKnightsAndBullets()
{
    LevelSet set = loadLevels({drawnMap("walls.tmx", {"##########",
                                                      "#P.......#",
                                                      "#........#",
                                                      "#...##...#",
                                                      "#...##...#",
                                                      "#........#",
                                                      "#........#",
                                                      "##########"})});
    const Level &level = set.levels[0];
    CHECK(set.problems.empty() && level.id == "walls");

    // The wall block is cells 4-5, rows 3-4: x 128..192, y 96..160
    CHECK(level.blocked({100, 100, 40, 40}) && !level.blocked({40, 40, 40, 40}));
    CHECK(level.blocked({-1, 40, 40, 40}) && level.blocked({300, 250, 40, 40})); // off the field, or in the bottom wall
    Rect knight{40, 104, 40, 40};
    level.slide(knight, 100, 0);
    CHECK(knight.x == 88 && knight.y == 104); // stopped right at the wall
    knight = {40, 104, 40, 40};
    level.slide(knight, 100, 30);
    CHECK(knight.x == 88 && std::fabs(knight.y - 134) < 0.01f); // and slid along it
    knight = {200, 104, 40, 40};
    level.slide(knight, -100, 0);
    CHECK(knight.x == 192);
    CHECK(!level.clearPath({40, 104, 40, 40}, {240, 104}) && level.clearPath({40, 40, 40, 40}, {240, 40}));

    World world(1, 1, &set);
    Snapshot seed;
    seed.players = {{1, 40, 40}};
    seed.enemies = {{240, 104}};
    seed.coins = {{40, 200}, {40, 200}, {40, 200}, {40, 200}, {40, 200}};
    world.seed(seed);
    world.addBullet(60, 120, BULLET_SPEED, 0); // flies right, into the wall in front of the red knight
    run(world, 0.2f);
    Snapshot s = world.snapshot(0);
    CHECK(s.bullets.empty() && s.enemies.size() == 1 && s.score == 0);
}

static void enemiesFindTheWayAroundWalls()
{
    LevelSet set = loadLevels({drawnMap("around.tmx", {"##############",
                                                       "#............#",
                                                       "#............#",
                                                       "#.....##.....#",
                                                       "#.....##.....#",
                                                       "#.....##.....#",
                                                       "#.....##.....#",
                                                       "#P....##...E.#",
                                                       "#.....##.....#",
                                                       "##############"})});
    const Level &level = set.levels[0];
    World world(1, 1, &set);
    int id = world.addPlayer();
    Point me = at(1, 7);
    world.setPlayerPosition(id, me.x, me.y);
    world.step(TICK); // the level starts with its red knight
    CHECK(world.snapshot(0).enemies.size() == 1);
    bool everInWall = false;
    float closest = 1e9f;
    for (int i = 0; i < 15 * 60; i++)
    {
        world.step(TICK);
        Snapshot s = world.snapshot(0);
        if (s.enemies.empty())
            break;
        everInWall = everInWall || level.blocked({s.enemies[0].x, s.enemies[0].y, ENEMY_SIZE, ENEMY_SIZE});
        closest = std::min(closest, std::hypot(s.enemies[0].x - me.x, s.enemies[0].y - me.y));
    }
    CHECK(!everInWall);
    CHECK(closest < 5); // it went round the wall (over the top) and got to the player
}

static void coinsOnlyWhereKnightsCanGo()
{
    LevelSet set = loadLevels({drawnMap("pocket.tmx", {"##########",
                                                       "#P...#...#",
                                                       "#....#...#",
                                                       "#....#####",
                                                       "#........#",
                                                       "#........#",
                                                       "##.#######",
                                                       "#........#",
                                                       "##########"})});
    const Level &level = set.levels[0];
    CHECK(!level.coinCells.empty());
    for (int cell : level.coinCells)
    {
        int col = cell % level.cols, row = cell / level.cols;
        bool inPocket = col >= 6 && row <= 2;
        bool behindGap = row == 7; // only a 1-cell gap leads there: too narrow for a knight
        CHECK(!inPocket && !behindGap && !level.wall(col, row));
    }
    CHECK(level.canStand(6, 1) && !isOpen(level, 1 * level.cols + 6)); // room for a knight, but no way in

    World world(1, 1, &set);
    for (int i = 0; i < 200; i++)
    {
        Snapshot noCoins = world.snapshot(0);
        noCoins.coins.clear();
        world.seed(noCoins); // new coins every time
        for (Point coin : world.snapshot(0).coins)
            CHECK(!level.wall((int)(coin.x / CELL), (int)(coin.y / CELL)) && coin.y < 6 * CELL);
    }
}

static void doorsLeadToTheNextLevel()
{
    LevelSet set = loadLevels({
        drawnMap("one.tmx", {"##########",
                             "#P.......#",
                             "#.......D.",
                             "#.........",
                             "##########"},
                 2, "two"),
        drawnMap("two.tmx", {"##########",
                             "#P.....E.#",
                             "#........#",
                             "#D.......#",
                             "#........#",
                             "##########"},
                 3, ""),
    });
    CHECK(set.problems.empty() && set.levels.size() == 2);
    World world(1, 7, &set);
    int id = world.addPlayer();
    Snapshot s = world.snapshot(0);
    Point here{1 * CELL + 8.5f, 1 * CELL + 8.5f}, away{4 * CELL + 8.5f, 3 * CELL + 8.5f};
    s.coins = {away, away, away, away, away}; // out of the way: only the coins we walk to count
    world.seed(s);
    Point start = at(1, 1);
    world.setPlayerPosition(id, start.x, start.y);
    world.step(TICK);
    s = world.snapshot(0);
    CHECK(s.level == 1 && s.map == 0 && s.coinsLeft == 2 && s.enemies.empty() && s.score == 0);

    // A shut door does nothing
    Point door = at(8, 2);
    world.setPlayerPosition(id, door.x, door.y);
    world.step(TICK);
    CHECK(world.snapshot(0).level == 1);

    // Collect the last coin: the door opens
    s = world.snapshot(0);
    s.coinsLeft = 1;
    s.coins = {here, away, away, away, away};
    world.seed(s);
    world.setPlayerPosition(id, start.x, start.y);
    world.step(TICK);
    s = world.snapshot(0);
    CHECK(s.level == 1 && s.coinsLeft == 0 && s.score == 1);

    // ...and going through it takes everybody to the next level
    world.setPlayerPosition(id, door.x, door.y);
    world.step(TICK);
    s = world.snapshot(0);
    CHECK(s.level == 2 && s.map == 1 && s.coinsLeft == 3 && s.score == 1);
    CHECK(s.enemies.size() == 1);   // two.tmx starts with a red knight
    CHECK(s.coins.size() == (size_t)COIN_COUNT);
    for (Point coin : s.coins)       // and its coins are on its own floor
        CHECK(!set.levels[1].wall((int)(coin.x / CELL), (int)(coin.y / CELL)));

    // The last level's door leads back to the first one, with one more red knight this time
    s.coinsLeft = 0;
    s.enemies.clear();
    world.seed(s);
    Point door2 = at(1, 3);
    world.setPlayerPosition(id, door2.x, door2.y);
    world.step(TICK);
    s = world.snapshot(0);
    CHECK(s.level == 3 && s.map == 0 && s.coinsLeft == 2);
    CHECK(s.enemies.size() == 1); // one.tmx has none of its own, plus 1 for the second time round

    // Everybody out: the world starts over at level 1
    world.setPlayerPosition(id, DEAD, DEAD);
    world.step(TICK);
    s = world.snapshot(0);
    CHECK(s.level == 1 && s.map == 0 && s.score == 0 && s.enemies.empty() && s.coinsLeft == 2);

    // A new host carries on with the same level
    s.level = 2;
    s.map = 1;
    s.coinsLeft = 1;
    World next(1, 1, &set);
    next.seed(s);
    Snapshot carried = next.snapshot(0);
    CHECK(carried.level == 2 && carried.map == 1 && carried.coinsLeft == 1);
}

// The maps that ship with the game: every one loads, and everything in it can be reached
static void builtInLevelsArePlayable()
{
    const LevelSet &set = builtInLevels();
    for (auto &problem : set.problems)
        std::printf("map problem: %s\n", problem.c_str());
    CHECK(set.problems.empty());
    CHECK(set.levels.size() >= 2);
    for (const Level &level : set.levels)
    {
        auto reachable = [&](Point middle) {
            int spot = level.spotNear(middle.x - KNIGHT_SIZE / 2, middle.y - KNIGHT_SIZE / 2);
            Rect knight{middle.x - KNIGHT_SIZE / 2, middle.y - KNIGHT_SIZE / 2, KNIGHT_SIZE, KNIGHT_SIZE};
            return spot >= 0 && isOpen(level, spot) && !level.blocked(knight);
        };
        bool ok = !level.playerSpawns.empty() && !level.doors.empty() && level.name != level.id;
        ok = ok && (int)level.coinCells.size() >= 4 * COIN_COUNT && level.coins > 0;
        for (Point p : level.playerSpawns)
            ok = ok && reachable(p);
        for (Point p : level.enemySpawns)
            ok = ok && reachable(p);
        for (const Door &door : level.doors)
        {
            bool canGetThere = std::any_of(level.open.begin(), level.open.end(), [&](int spot) {
                Point p = level.spotPosition(spot);
                return checkCollision({p.x, p.y, KNIGHT_SIZE, KNIGHT_SIZE}, door.area);
            });
            ok = ok && canGetThere && door.destination >= 0 && door.destination < (int)set.levels.size();
            for (int cell : level.coinCells) // picking up a coin never takes you through the door
            {
                Rect knight{(float)(cell % level.cols * CELL) + (CELL - KNIGHT_SIZE) / 2,
                            (float)(cell / level.cols * CELL) + (CELL - KNIGHT_SIZE) / 2, KNIGHT_SIZE, KNIGHT_SIZE};
                ok = ok && !checkCollision(knight, door.area);
            }
        }
        if (!ok)
            std::printf("FAILED: %s.tmx (\"%s\") is not playable\n", level.id.c_str(), level.name.c_str());
        CHECK(ok);
    }
}

int main()
{
    snapshotRoundTrip();
    snapshotRejectsGarbage();
    coinsScoreAndBringEnemies();
    bulletsHitEnemies();
    enemiesChaseTheLiving();
    worldResetsOnlyWhenEveryoneIsDead();
    playersComeAndGo();
    hostMigrationKeepsTheWorld();
    cheatyBulletsAreTamed();
    tiledMapsAreRead();
    badMapsAreExplained();
    wallsStopKnightsAndBullets();
    enemiesFindTheWayAroundWalls();
    coinsOnlyWhereKnightsCanGo();
    doorsLeadToTheNextLevel();
    builtInLevelsArePlayable();
    if (failures == 0)
        std::printf("All world tests passed\n");
    return failures == 0 ? 0 : 1;
}
