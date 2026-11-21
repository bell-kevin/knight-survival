// world.h - The shared game world: the rules that used to live in server/server.cpp.
//
// The same code runs in three places:
//   * the native dedicated server (server/server.cpp),
//   * a native client that found no server and plays solo,
//   * the browser of whichever player is currently hosting an online room.
//
// Copyright (C) 2026 Kevin Bell. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <random>
#include <string>
#include <vector>

#include "level.h" // the field, its walls and doors

const float PLAYER_SIZE = KNIGHT_SIZE;
const float PLAYER_SPEED = 250.0f; // pixels per second
const float ENEMY_SIZE = KNIGHT_SIZE;
const float ENEMY_SPEED = 80.0f;
const float COIN_SIZE = 15.0f;
const float BULLET_SIZE = 8.0f;
const float BULLET_SPEED = 600.0f;
const int COIN_COUNT = 5;
const int POINTS_PER_ENEMY = 5; // every 5 points brings another enemy
const int MAX_PLAYERS = 8;
const int MAX_ENEMIES = 60;
const int MAX_BULLETS = 256;
const float DEAD = -1000.0f;      // a player whose x is below 0 is dead (or not spawned yet)
const float TICK = 1.0f / 60.0f;  // the world runs 60 times a second

struct PlayerState
{
    int id;
    float x, y;
};

struct BulletState
{
    float x, y, vx, vy;
};

// Everything a client needs to draw a frame, and everything a new host needs to take over.
struct Snapshot
{
    double epoch = 0;   // when this world was created (ms since 1970); older worlds win merges
    int generation = 0; // how many times the world has changed hosts; the newest host wins
    int hostId = 0;     // player id of the host (0 = dedicated server)
    int score = 0;
    int level = 1;      // the level number: 1, 2, 3, ... (it keeps counting when the maps come round again)
    int map = 0;        // which map that level is played on (an index into builtInLevels().levels)
    int coinsLeft = 0;  // coins still to collect before the doors open (0: they're open)
    std::vector<PlayerState> players;
    std::vector<Point> coins;
    std::vector<Point> enemies;
    std::vector<BulletState> bullets;
};

// The whole world as one line of text, like the original server sent:
// "W <epoch> <generation> <hostId> <score> <level> <map> <coinsLeft>
//    <n> id x y ... <n> x y ... <n> x y ... <n> x y vx vy ..."
std::string encodeSnapshot(const Snapshot &s);
// Reads a line made by encodeSnapshot. Returns false (and leaves `out` alone) on bad input.
bool decodeSnapshot(const std::string &line, Snapshot &out);

class World
{
public:
    // The game always plays builtInLevels(); tests can bring levels of their own.
    explicit World(double epoch, unsigned seed = std::random_device{}(), const LevelSet *levels = nullptr);

    // Continue a world that another host was running (used when the host leaves).
    void seed(const Snapshot &s);

    int addPlayer(); // returns the new player's id, or 0 when the world is full
    void removePlayer(int id);
    bool hasPlayer(int id) const;
    int playerCount() const { return (int)players.size(); }
    void setPlayerPosition(int id, float x, float y);
    void addBullet(float x, float y, float vx, float vy);

    void step(float dt); // move everything forward by dt seconds
    Snapshot snapshot(int hostId) const;
    double epoch() const { return epoch_; }
    const Level &level() const { return levels_->levels[(size_t)mapIndex]; } // the map being played

private:
    struct Player
    {
        int id;
        Rect rect;
    };
    struct Bullet
    {
        Rect rect;
        float vx, vy;
    };

    void startLevel(int number, int map); // a fresh level: new coins, no red knights yet
    void moveEnemies(float dt);
    bool anyoneAlive() const;
    Point randomCoinSpot();
    Point randomEnemySpot();

    double epoch_;
    std::mt19937 rng;
    const LevelSet *levels_;
    int score = 0;
    int levelNumber = 1, mapIndex = 0, coinsLeft = 0;
    bool started = false; // has anybody played this level yet (are its first red knights out)?
    std::vector<Player> players;
    std::vector<Rect> coins, enemies;
    std::vector<Bullet> bullets;
};
