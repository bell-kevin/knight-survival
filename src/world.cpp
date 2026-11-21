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

// A fresh level: the coins move to the new map, and the bad guys and bullets of the old one are
// gone. The new level's own bad guys come out once somebody is there (see step).
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

// Where the next bad guy comes from (its middle), and which kind it is: one of the map's enemy
// spawn points, each bringing its own kind. When those are all too close to the players, or the
// map has none, anywhere players can get to (with a kind one of the spawn points brings, or a
// red knight). Never right on top of a player: the first spot that's far from everybody, or
// else the farthest.
EnemySpot World::randomEnemySpot()
{
    const Level &map = level();
    std::vector<EnemySpot> spots = map.enemySpawns;
    std::shuffle(spots.begin(), spots.end(), rng);
    for (int tries = 0; tries < 20; tries++)
    {
        int kind = RED_KNIGHT;
        if (!map.enemySpawns.empty())
        {
            std::uniform_int_distribution<size_t> which(0, map.enemySpawns.size() - 1);
            kind = map.enemySpawns[which(rng)].kind;
        }
        if (!map.open.empty())
        {
            std::uniform_int_distribution<size_t> pick(0, map.open.size() - 1);
            Point p = map.spotPosition(map.open[pick(rng)]);
            spots.push_back({p.x + KNIGHT_SIZE / 2, p.y + KNIGHT_SIZE / 2, kind});
        }
        else
        {
            std::uniform_int_distribution<int> x(0, W - (int)KNIGHT_SIZE), y(0, H - (int)KNIGHT_SIZE);
            spots.push_back({(float)x(rng) + KNIGHT_SIZE / 2, (float)y(rng) + KNIGHT_SIZE / 2, kind});
        }
    }
    EnemySpot best = spots[0];
    float bestDistance = -1;
    for (const EnemySpot &spot : spots)
    {
        float nearest = 1e9f;
        for (auto &player : players)
        {
            if (player.rect.x >= 0)
                nearest = std::min(nearest, std::hypot(player.rect.x + PLAYER_SIZE / 2 - spot.x, player.rect.y + PLAYER_SIZE / 2 - spot.y));
        }
        if (nearest >= 200)
            return spot;
        if (nearest > bestDistance)
        {
            best = spot;
            bestDistance = nearest;
        }
    }
    return best;
}

void World::addEnemy(int kind, Point middle)
{
    if ((int)enemies.size() >= MAX_ENEMIES)
        return;
    kind = kind >= 0 && kind < ENEMY_KINDS ? kind : RED_KNIGHT;
    const EnemyType &type = enemyType(kind);
    enemies.push_back({{middle.x - type.size / 2, middle.y - type.size / 2, type.size, type.size}, kind, type.health, 0});
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
        if ((int)enemies.size() >= MAX_ENEMIES)
            break;
        int kind = e.kind >= 0 && e.kind < ENEMY_KINDS ? e.kind : RED_KNIGHT;
        const EnemyType &type = enemyType(kind);
        int health = e.health > 0 ? std::min(e.health, type.health) : type.health;
        enemies.push_back({{e.x, e.y, type.size, type.size}, kind, health, 0});
    }
    bullets.clear();
    for (auto &b : s.bullets)
        addBullet(b.x, b.y, b.vx, b.vy);
    started = anyoneAlive(); // a level in progress: its first bad guys came out long ago
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
    for (auto &e : enemies)
        e.hurt = std::max(0.0f, e.hurt - dt);

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

    // A bullet that touches a bad guy is used up, and takes one of its hits. The hit that beats it
    // wins the team its points (10 for a red knight, more for the tougher kinds).
    for (auto b = bullets.begin(); b != bullets.end();)
    {
        auto e = std::find_if(enemies.begin(), enemies.end(), [&](const Enemy &enemy) { return checkCollision(b->rect, enemy.rect); });
        if (e == enemies.end())
        {
            ++b;
            continue;
        }
        b = bullets.erase(b);
        e->hurt = HURT_TIME;
        if (--e->health <= 0)
        {
            score += enemyType(e->kind).points;
            enemies.erase(e);
        }
    }

    // A player who touches a coin gets a point. Every 5 points brings a bad guy, and enough coins
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
                    EnemySpot e = randomEnemySpot();
                    addEnemy(e.kind, {e.x, e.y});
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
        // Somebody is here, so the level begins and its bad guys come out. Every time round all
        // the maps brings one more.
        started = true;
        for (const EnemySpot &e : level().enemyStarts)
            addEnemy(e.kind, {e.x, e.y});
        int extra = (levelNumber - 1) / (int)levels_->levels.size();
        for (int i = 0; i < extra; i++)
        {
            EnemySpot e = randomEnemySpot();
            addEnemy(e.kind, {e.x, e.y});
        }
    }
}

// Bad guys walk toward the nearest player who's alive, each at its own speed: straight at them
// when nothing is in the way, and otherwise along the shortest way around the walls.
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
        const EnemyType &type = enemyType(enemy.kind);
        Rect &body = enemy.rect;
        float half = type.size / 2, middleX = body.x + half, middleY = body.y + half;
        const Rect *target = nullptr;
        float best = 0;
        for (auto &player : players)
        {
            const Rect &p = player.rect;
            float d = std::hypot(p.x + PLAYER_SIZE / 2 - middleX, p.y + PLAYER_SIZE / 2 - middleY);
            if (p.x >= 0 && (!target || d < best))
            {
                target = &p;
                best = d;
            }
        }
        if (!target || best <= 0)
            continue;
        Point goal{target->x + PLAYER_SIZE / 2 - half, target->y + PLAYER_SIZE / 2 - half}; // middle on middle
        if (!map.clearPath(body, goal))
        {
            // The way round is worked out for a knight-sized body with the same middle. A bigger one
            // fits too, since every way through a map is at least 2 cells (64 pixels) wide.
            if (distance.empty())
                distance = map.distances(playerSpots);
            float shift = (type.size - KNIGHT_SIZE) / 2;
            int here = map.spotNear(body.x + shift, body.y + shift);
            if (here >= 0 && distance[(size_t)here] > 0)
            {
                Point next = map.spotPosition(map.downhill(here, distance));
                goal = {next.x - shift, next.y - shift};
            }
        }
        float dx = goal.x - body.x, dy = goal.y - body.y, length = std::hypot(dx, dy);
        if (length > 0.01f)
        {
            float step = std::min(length, type.speed * dt); // no further than the goal: no wobbling on it
            map.slide(body, dx / length * step, dy / length * step);
        }
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
        s.enemies.push_back({e.rect.x, e.rect.y, e.kind, e.health, e.hurt > 0});
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
        add(msg, e.kind);
        add(msg, e.health);
        add(msg, e.hurt ? 1 : 0);
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
        EnemyState e;
        int hurt = 0;
        if (!in.number(e.x) || !in.number(e.y) || !in.count(e.kind, ENEMY_KINDS - 1) || !in.count(e.health, 100000) || !in.count(hurt, 1))
            return false;
        e.hurt = hurt != 0;
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
