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

const int W = 800, H = 600; // size of the playing field in pixels

const float PLAYER_SIZE = 40.0f;
const float PLAYER_SPEED = 250.0f; // pixels per second
const float ENEMY_SIZE = 40.0f;
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

struct Rect
{
    float x, y, w, h;
};

inline bool checkCollision(const Rect &a, const Rect &b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

struct PlayerState
{
    int id;
    float x, y;
};

struct BulletState
{
    float x, y, vx, vy;
};

struct Point
{
    float x, y;
};

// Everything a client needs to draw a frame, and everything a new host needs to take over.
struct Snapshot
{
    double epoch = 0;   // when this world was created (ms since 1970); older worlds win merges
    int generation = 0; // how many times the world has changed hosts; the newest host wins
    int hostId = 0;     // player id of the host (0 = dedicated server)
    int score = 0;
    std::vector<PlayerState> players;
    std::vector<Point> coins;
    std::vector<Point> enemies;
    std::vector<BulletState> bullets;
};

// The whole world as one line of text, like the original server sent:
// "W <epoch> <generation> <hostId> <score> <n> id x y ... <n> x y ... <n> x y ... <n> x y vx vy ..."
std::string encodeSnapshot(const Snapshot &s);
// Reads a line made by encodeSnapshot. Returns false (and leaves `out` alone) on bad input.
bool decodeSnapshot(const std::string &line, Snapshot &out);

class World
{
public:
    explicit World(double epoch, unsigned seed = std::random_device{}());

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

    Point randomCoinSpot();
    Point randomEnemySpot();

    double epoch_;
    std::mt19937 rng;
    int score = 0;
    std::vector<Player> players;
    std::vector<Rect> coins, enemies;
    std::vector<Bullet> bullets;
};
