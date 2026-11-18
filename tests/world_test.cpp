// world_test.cpp - Checks the shared game rules and the snapshot text format.
// Run with: ctest --test-dir build   (or just ./build/world-test)
//
// Copyright (C) 2026 Kevin Bell. SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <cstdio>
#include <string>

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

static void snapshotRoundTrip()
{
    Snapshot s;
    s.epoch = 1794983104123.0;
    s.generation = 3;
    s.hostId = 2;
    s.score = 42;
    s.players = {{1, 100, 200}, {2, DEAD, DEAD}, {3, 760, 560}};
    s.coins = {{10, 20}, {30, 40}};
    s.enemies = {{300, 300}};
    s.bullets = {{400, 300, 600, 0}, {12, 34, -424, -424}};

    Snapshot back;
    CHECK(decodeSnapshot(encodeSnapshot(s), back));
    CHECK(back.epoch == s.epoch);
    CHECK(back.generation == 3 && back.hostId == 2 && back.score == 42);
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
    // fields: W epoch generation hostId score players coins enemies bullets
    CHECK(!decodeSnapshot("W 1 0 0 0", out));                     // cut short
    CHECK(!decodeSnapshot("W 1 0 0 0 1 1 nan 3 0 0 0", out));      // not a number
    CHECK(!decodeSnapshot("W 1 0 0 0 999999 1 2 3", out));        // absurd count
    CHECK(!decodeSnapshot("W 1 0 0 -5 0 0 0 0", out));            // negative score
    CHECK(!decodeSnapshot("X 1 0 0 0 0 0 0 0", out));             // not a world at all
    CHECK(out.score == 7);                                        // left alone on failure
    CHECK(decodeSnapshot("W 1 0 0 0 0 0 0 0", out) && out.score == 0);
}

static void coinsScoreAndBringEnemies()
{
    World world(1);
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
    World world(1);
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
    World world(1);
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
    World world(1);
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
    World world(1);
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
    World old(123);
    int a = old.addPlayer(), b = old.addPlayer();
    old.setPlayerPosition(a, 50, 50);
    old.setPlayerPosition(b, 600, 400);
    old.addBullet(300, 300, 0, -BULLET_SPEED);
    Snapshot last = old.snapshot(a);

    World next(last.epoch);
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
    World world(1);
    world.addBullet(100, 100, 1e9f, 0);
    world.addBullet(NAN, 100, 10, 0);
    world.addBullet(100, 100, 0, 0);
    Snapshot s = world.snapshot(0);
    CHECK(s.bullets.size() == 1);
    if (!s.bullets.empty())
        CHECK(std::fabs(s.bullets[0].vx - BULLET_SPEED) < 0.01f);
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
    if (failures == 0)
        std::printf("All world tests passed\n");
    return failures == 0 ? 0 : 1;
}
