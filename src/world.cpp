// world.cpp - The game rules, moved here from server/server.cpp so every build shares them.
//
// Copyright (C) 2026 Kevin Bell. SPDX-License-Identifier: GPL-3.0-or-later
#include "world.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

World::World(double epoch, unsigned seed, const LevelSet *levels)
    : epoch_(epoch), rng(seed), levels_(levels && !levels->levels.empty() ? levels : &builtInLevels())
{
    startLevel(1, 0);
}

// A fresh level: the coins move to the new map, and the red knights and bullets of the old one
// are gone. The new level's own red knights come out once somebody is there (see step).
void World::startLevel(int number, int map)
{
    levelNumber = std::max(1, number);
    mapIndex = map >= 0 && map < (int)levels_->levels.size() ? map : 0;
    coinsLeft = level().coins;
    started = false;
    enemies.clear();
    bullets.clear();
    coins.clear();
    for (int i = 0; i < COIN_COUNT; i++)
    {
        Point p = randomCoinSpot();
        coins.push_back({p.x, p.y, COIN_SIZE, COIN_SIZE});
    }
}

bool World::anyoneAlive() const
{
    return std::any_of(players.begin(), players.end(), [](const Player &p) { return p.rect.x >= 0; });
}

// In the middle of a floor cell that a knight can get to
Point World::randomCoinSpot()
{
    const Level &map = level();
    if (map.coinCells.empty()) // a map without any floor: anywhere, like the original game
    {
        std::uniform_int_distribution<int> x(0, W - 21), y(50, H - 21); // y from 50: not under the score
        return {(float)x(rng), (float)y(rng)};
    }
    std::uniform_int_distribution<size_t> pick(0, map.coinCells.size() - 1);
    int cell = map.coinCells[pick(rng)];
    float margin = (CELL - COIN_SIZE) / 2;
    return {(float)(cell % map.cols * CELL) + margin, (float)(cell / map.cols * CELL) + margin};
}

// Where the map says red knights come from (or else anywhere players can get to), but not
// right on top of a player: the first spot that's far from everybody, or else the farthest.
Point World::randomEnemySpot()
{
    const Level &map = level();
    std::vector<Point> spots;
    for (Point p : map.enemySpawns)
        spots.push_back({p.x - ENEMY_SIZE / 2, p.y - ENEMY_SIZE / 2});
    std::shuffle(spots.begin(), spots.end(), rng);
    for (int tries = 0; tries < 20; tries++)
    {
        if (!map.open.empty())
        {
            std::uniform_int_distribution<size_t> pick(0, map.open.size() - 1);
            spots.push_back(map.spotPosition(map.open[pick(rng)]));
        }
        else
        {
            std::uniform_int_distribution<int> x(0, W - (int)ENEMY_SIZE), y(0, H - (int)ENEMY_SIZE);
            spots.push_back({(float)x(rng), (float)y(rng)});
        }
    }
    Point best{0, 0};
    float bestDistance = -1;
    for (Point p : spots)
    {
        float nearest = 1e9f;
        for (auto &player : players)
        {
            if (player.rect.x >= 0)
                nearest = std::min(nearest, std::hypot(player.rect.x - p.x, player.rect.y - p.y));
        }
        if (nearest >= 200)
            return p;
        if (nearest > bestDistance)
        {
            best = p;
            bestDistance = nearest;
        }
    }
    return best;
}

void World::seed(const Snapshot &s)
{
    score = s.score;
    levelNumber = std::max(1, s.level);
    mapIndex = s.map >= 0 && s.map < (int)levels_->levels.size() ? s.map : 0;
    coinsLeft = std::max(0, s.coinsLeft);
    players.clear();
    for (auto &p : s.players)
    {
        if (p.id > 0 && !hasPlayer(p.id) && (int)players.size() < MAX_PLAYERS)
            players.push_back({p.id, {p.x, p.y, PLAYER_SIZE, PLAYER_SIZE}});
    }
    coins.clear();
    for (auto &c : s.coins)
        coins.push_back({c.x, c.y, COIN_SIZE, COIN_SIZE});
    while ((int)coins.size() < COIN_COUNT)
    {
        Point p = randomCoinSpot();
        coins.push_back({p.x, p.y, COIN_SIZE, COIN_SIZE});
    }
    enemies.clear();
    for (auto &e : s.enemies)
    {
        if ((int)enemies.size() < MAX_ENEMIES)
            enemies.push_back({e.x, e.y, ENEMY_SIZE, ENEMY_SIZE});
    }
    bullets.clear();
    for (auto &b : s.bullets)
        addBullet(b.x, b.y, b.vx, b.vy);
    started = anyoneAlive(); // a level in progress: its first red knights came out long ago
}

int World::addPlayer()
{
    if ((int)players.size() >= MAX_PLAYERS)
        return 0;
    int id = 1; // the smallest id nobody is using, like the original server
    while (hasPlayer(id))
        id++;
    players.push_back({id, {DEAD, DEAD, PLAYER_SIZE, PLAYER_SIZE}});
    return id;
}

void World::removePlayer(int id)
{
    std::erase_if(players, [id](const Player &p) { return p.id == id; });
}

bool World::hasPlayer(int id) const
{
    return std::any_of(players.begin(), players.end(), [id](const Player &p) { return p.id == id; });
}

void World::setPlayerPosition(int id, float x, float y)
{
    if (!std::isfinite(x) || !std::isfinite(y))
        return;
    for (auto &p : players)
    {
        if (p.id != id)
            continue;
        if (x < 0)
        {
            p.rect.x = p.rect.y = DEAD; // dead, or waiting to respawn
        }
        else
        {
            p.rect.x = std::clamp(x, 0.0f, W - PLAYER_SIZE);
            p.rect.y = std::clamp(y, 0.0f, H - PLAYER_SIZE);
        }
    }
}

void World::addBullet(float x, float y, float vx, float vy)
{
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(vx) || !std::isfinite(vy))
        return;
    if ((int)bullets.size() >= MAX_BULLETS)
        return;
    float speed = std::hypot(vx, vy);
    if (speed <= 0)
        return;
    if (speed > BULLET_SPEED) // nobody gets faster bullets than everyone else
    {
        vx = vx / speed * BULLET_SPEED;
        vy = vy / speed * BULLET_SPEED;
    }
    x = std::clamp(x, 0.0f, (float)W);
    y = std::clamp(y, 0.0f, (float)H);
    bullets.push_back({{x, y, BULLET_SIZE, BULLET_SIZE}, vx, vy});
}

void World::step(float dt)
{
    const Level &map = level();

    // Bullets fly, and disappear off screen or in a wall
    for (auto it = bullets.begin(); it != bullets.end();)
    {
        it->rect.x += it->vx * dt;
        it->rect.y += it->vy * dt;
        float middleX = it->rect.x + BULLET_SIZE / 2, middleY = it->rect.y + BULLET_SIZE / 2;
        bool offScreen = it->rect.x < 0 || it->rect.x > W || it->rect.y < 0 || it->rect.y > H;
        if (offScreen || map.wall((int)std::floor(middleX / CELL), (int)std::floor(middleY / CELL)))
            it = bullets.erase(it);
        else
            ++it;
    }

    // A bullet that touches an enemy removes both: +10 points
    for (auto b = bullets.begin(); b != bullets.end();)
    {
        auto e = enemies.begin();
        while (e != enemies.end() && !checkCollision(b->rect, *e))
            ++e;
        if (e != enemies.end())
        {
            enemies.erase(e);
            b = bullets.erase(b);
            score += 10;
        }
        else
        {
            ++b;
        }
    }

    // A player who touches a coin gets a point. Every 5 points brings an enemy, and enough coins
    // open the doors.
    for (auto &player : players)
    {
        for (auto &coin : coins)
        {
            if (checkCollision(player.rect, coin))
            {
                score++;
                if (coinsLeft > 0)
                    coinsLeft--;
                Point spot = randomCoinSpot();
                coin.x = spot.x;
                coin.y = spot.y;
                if (score % POINTS_PER_ENEMY == 0 && (int)enemies.size() < MAX_ENEMIES)
                {
                    Point e = randomEnemySpot();
                    enemies.push_back({e.x, e.y, ENEMY_SIZE, ENEMY_SIZE});
                }
            }
        }
    }

    moveEnemies(dt);

    // Somebody walked through an open door: everybody goes on to the level behind it
    if (coinsLeft == 0)
    {
        for (const Door &door : map.doors)
        {
            if (std::any_of(players.begin(), players.end(),
                            [&](const Player &p) { return p.rect.x >= 0 && checkCollision(p.rect, door.area); }))
            {
                startLevel(levelNumber + 1, door.destination);
                break;
            }
        }
    }

    // If nobody is alive, start the world over from the first level. (The original checked this
    // inside the enemy loop, so it could reset the world while another player was still alive.)
    if (!anyoneAlive())
    {
        if (levelNumber != 1 || mapIndex != 0)
            startLevel(1, 0);
        score = 0;
        coinsLeft = level().coins;
        started = false;
        enemies.clear();
        bullets.clear();
    }
    else if (!started)
    {
        // Somebody is here, so the level begins and its red knights come out. Every time round
        // all the maps brings one more.
        started = true;
        for (Point p : level().enemyStarts)
        {
            if ((int)enemies.size() < MAX_ENEMIES)
                enemies.push_back({p.x - ENEMY_SIZE / 2, p.y - ENEMY_SIZE / 2, ENEMY_SIZE, ENEMY_SIZE});
        }
        int extra = (levelNumber - 1) / (int)levels_->levels.size();
        for (int i = 0; i < extra && (int)enemies.size() < MAX_ENEMIES; i++)
        {
            Point e = randomEnemySpot();
            enemies.push_back({e.x, e.y, ENEMY_SIZE, ENEMY_SIZE});
        }
    }
}

// Enemies walk toward the nearest player who's alive: straight at them when nothing is in the
// way, and otherwise along the shortest way around the walls.
void World::moveEnemies(float dt)
{
    const Level &map = level();
    std::vector<int> playerSpots;
    for (auto &player : players)
    {
        if (player.rect.x >= 0)
            playerSpots.push_back(map.spotNear(player.rect.x, player.rect.y));
    }
    if (playerSpots.empty())
        return;
    std::vector<int> distance; // how far every spot is from a player, worked out when needed

    for (auto &enemy : enemies)
    {
        const Rect *target = nullptr;
        float best = 0;
        for (auto &player : players)
        {
            const Rect &p = player.rect;
            float d = std::hypot(p.x - enemy.x, p.y - enemy.y);
            if (p.x >= 0 && (!target || d < best))
            {
                target = &p;
                best = d;
            }
        }
        if (!target || best <= 0)
            continue;
        Point goal{target->x, target->y};
        if (!map.clearPath(enemy, goal))
        {
            if (distance.empty())
                distance = map.distances(playerSpots);
            int here = map.spotNear(enemy.x, enemy.y);
            if (here >= 0 && distance[(size_t)here] > 0)
                goal = map.spotPosition(map.downhill(here, distance));
        }
        float dx = goal.x - enemy.x, dy = goal.y - enemy.y, length = std::hypot(dx, dy);
        if (length > 0.01f)
            map.slide(enemy, dx / length * ENEMY_SPEED * dt, dy / length * ENEMY_SPEED * dt);
    }
}

Snapshot World::snapshot(int hostId) const
{
    Snapshot s;
    s.epoch = epoch_;
    s.hostId = hostId;
    s.score = score;
    s.level = levelNumber;
    s.map = mapIndex;
    s.coinsLeft = coinsLeft;
    for (auto &p : players)
        s.players.push_back({p.id, p.rect.x, p.rect.y});
    for (auto &c : coins)
        s.coins.push_back({c.x, c.y});
    for (auto &e : enemies)
        s.enemies.push_back({e.x, e.y});
    for (auto &b : bullets)
        s.bullets.push_back({b.rect.x, b.rect.y, b.vx, b.vy});
    return s;
}

// ---------------------------------------------------------------------------------------------
// Text encoding of a snapshot
// ---------------------------------------------------------------------------------------------

namespace
{
// Adds a number to a message, rounded to a whole pixel (like the original server)
void add(std::string &msg, double value)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), " %.0f", value);
    msg += buf;
}

// Reads numbers one after another from a line, rejecting anything that isn't a sane number.
struct Reader
{
    const char *p;

    bool number(double &out)
    {
        char *end = nullptr;
        out = std::strtod(p, &end);
        if (end == p || !std::isfinite(out) || std::fabs(out) > 1e15)
            return false;
        p = end;
        return true;
    }
    bool number(float &out)
    {
        double d;
        if (!number(d) || std::fabs(d) > 1e6)
            return false;
        out = (float)d;
        return true;
    }
    bool count(int &out, int max)
    {
        double d;
        if (!number(d) || d < 0 || d > max || d != std::floor(d))
            return false;
        out = (int)d;
        return true;
    }
};
} // namespace

std::string encodeSnapshot(const Snapshot &s)
{
    std::string msg = "W";
    add(msg, s.epoch);
    add(msg, s.generation);
    add(msg, s.hostId);
    add(msg, s.score);
    add(msg, s.level);
    add(msg, s.map);
    add(msg, s.coinsLeft);

    add(msg, s.players.size());
    for (auto &p : s.players)
    {
        add(msg, p.id);
        add(msg, p.x);
        add(msg, p.y);
    }

    add(msg, s.coins.size());
    for (auto &c : s.coins)
    {
        add(msg, c.x);
        add(msg, c.y);
    }

    add(msg, s.enemies.size());
    for (auto &e : s.enemies)
    {
        add(msg, e.x);
        add(msg, e.y);
    }

    add(msg, s.bullets.size());
    for (auto &b : s.bullets)
    {
        add(msg, b.x);
        add(msg, b.y);
        add(msg, b.vx);
        add(msg, b.vy);
    }
    return msg;
}

bool decodeSnapshot(const std::string &line, Snapshot &out)
{
    if (line.size() < 2 || line[0] != 'W' || line[1] != ' ')
        return false;
    Reader in{line.c_str() + 1};
    Snapshot s;
    int n = 0;

    if (!in.number(s.epoch) || !in.count(s.generation, 1000000000) || !in.count(s.hostId, 1000000) ||
        !in.count(s.score, 1000000000) || !in.count(s.level, 1000000000) || !in.count(s.map, 100000) ||
        !in.count(s.coinsLeft, 1000000))
        return false;

    if (!in.count(n, MAX_PLAYERS * 4))
        return false;
    for (int i = 0; i < n; i++)
    {
        PlayerState p;
        if (!in.count(p.id, 1000000) || !in.number(p.x) || !in.number(p.y))
            return false;
        s.players.push_back(p);
    }

    if (!in.count(n, 1000))
        return false;
    for (int i = 0; i < n; i++)
    {
        Point c;
        if (!in.number(c.x) || !in.number(c.y))
            return false;
        s.coins.push_back(c);
    }

    if (!in.count(n, 1000))
        return false;
    for (int i = 0; i < n; i++)
    {
        Point e;
        if (!in.number(e.x) || !in.number(e.y))
            return false;
        s.enemies.push_back(e);
    }

    if (!in.count(n, 4096))
        return false;
    for (int i = 0; i < n; i++)
    {
        BulletState b;
        if (!in.number(b.x) || !in.number(b.y) || !in.number(b.vx) || !in.number(b.vy))
            return false;
        s.bullets.push_back(b);
    }

    out = std::move(s);
    return true;
}
