// enemies.h - The bad guys: how big, fast, tough and strong each kind is.
//
// They are all the red knight from assets/RedKnight, recolored by the game (see main.cpp). Maps
// say which kind goes where by its name or class (see maps/README.md).
//
// Copyright (C) 2026 Kevin Bell. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

enum EnemyKind
{
    RED_KNIGHT, // the original bad guy
    SCOUT,      // small, quick, and weak
    CHAMPION,   // takes a few hits, and hits harder
    BRUTE,      // big and slow, takes a beating, hits very hard
    WARLORD,    // the boss
    ENEMY_KINDS
};

const int PLAYER_HEALTH = 100; // what a knight has when it (re)appears or starts a new level

struct EnemyType
{
    const char *name;  // what a map calls it (in lowercase), and the start of its name on screen
    float size;        // on the field, in pixels (a player's knight is 40)
    float speed;       // pixels per second
    int health;        // how many hits it takes
    int damage;        // how much of a player's health one touch takes
    int points;        // for the team when it's beaten. Always a multiple of 5, so that every
                       // 5th coin still brings another bad guy (see World::step).
};

inline const EnemyType ENEMY_TYPES[ENEMY_KINDS] = {
    // name       size  speed health damage points
    {"red", 40, 80, 1, 30, 10},
    {"scout", 32, 135, 1, 15, 15},
    {"champion", 40, 95, 3, 40, 30},
    {"brute", 52, 55, 8, 60, 60},
    {"warlord", 60, 72, 25, 100, 250},
};

inline const EnemyType &enemyType(int kind)
{
    return ENEMY_TYPES[kind >= 0 && kind < ENEMY_KINDS ? kind : RED_KNIGHT];
}

// The kind a map means by a word like "Brute" or "red knight" (-1: not one of them)
int enemyKindNamed(const std::string &word);
