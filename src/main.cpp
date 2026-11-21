// main.cpp - SDL2 Survival Game (Shooting Edition)
// Objective: Collect yellow coins & Shoot red enemies with Left Click. Enough coins open the door
//            to the next level (the levels are maps made with Tiled: see maps/README.md).
// Controls: WASD/Arrows to move. Mouse Left-Click to shoot. Esc to Quit.
//           Touch screens: drag your knight to move, tap anywhere else to shoot there.
//
// Multiplayer: everyone plays in one shared world, run by a host (see session.h).
//   * Native build: start ./knight-server, then ./knight-survival [server-ip] on each computer.
//   * Web build:    players who open the same room link play together (see session_web.cpp).
//
// Copyright (C) 2026 Kevin Bell. SPDX-License-Identifier: GPL-3.0-or-later

#include <SDL2/SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <map>
#include <memory>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include "font.h"
#include "session.h"
#include "world.h"

enum class GameState
{
    PLAYING,
    GAME_OVER
};

// The 8 ways a knight can face, in the order of the sprite files (clockwise from north)
const char *const DIRECTIONS[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
const int SOUTH = 4;

const float SPAWN_PROTECTION = 2.0f; // seconds a fresh knight blinks and can't be caught
const float RESPAWN_DELAY = 0.75f;   // so a click you meant as a shot doesn't respawn you
const float BANNER_TIME = 2.5f;      // seconds "LEVEL 2" or "THE DOOR IS OPEN" stays on screen

const SDL_Color WHITE{255, 255, 255, 255};
const SDL_Color LIGHT{220, 220, 220, 255};
const SDL_Color GOLD{255, 215, 0, 255};

// Player colors: 1 is the White Knight, 2 the Blue Knight, and from 3 on the White Knight tinted
const SDL_Color TINTS[MAX_PLAYERS + 1] = {
    {255, 255, 255, 255}, {255, 255, 255, 255}, {255, 255, 255, 255},
    {130, 235, 130, 255}, {255, 175, 80, 255}, {195, 140, 255, 255},
    {100, 225, 255, 255}, {255, 140, 200, 255}, {245, 230, 100, 255},
};

// One knight color, facing all 8 ways
struct Knight
{
    std::array<SDL_Texture *, 8> look{};
};

// Another player, drawn a little smoother than the 30 updates a second we get about them
struct Remote
{
    float x, y;
    int face;
};

struct Game
{
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    Knight white, blue, red;
    std::map<std::string, SDL_Texture *> pictures; // the tile pictures, by file name (in assets/)
    SDL_Texture *wood = nullptr;                   // what a shut door is made of
    SDL_Texture *mapTexture = nullptr;             // the level, drawn once (see drawMap)
    int mapDrawn = -1;                             // which map is in mapTexture (-1: none yet)
    bool noMapTexture = false;                     // can't draw into a texture: draw the map every frame
    std::unique_ptr<Session> session;

    SDL_FRect player{100, 100, PLAYER_SIZE, PLAYER_SIZE};
    int face = SOUTH;
    GameState state = GameState::PLAYING;
    float protection = SPAWN_PROTECTION;
    float deadTime = 0;
    int finalScore = 0;
    int shownScore = -1, shownLevel = -1;
    Uint64 lastCounter = 0;
    bool running = true;
    std::map<int, Remote> remotes;

    // The level we're in, to notice when the world moves on to another one
    double seenEpoch = -1;
    int seenLevel = -1, seenMap = -1, seenCoinsLeft = -1;
    std::string bannerFor;                 // the level "LEVEL n" was last shown for
    float levelBanner = 0, doorBanner = 0; // seconds left to show them

    // Touch: the finger that is steering our knight, if any
    bool dragging = false;
    SDL_FingerID dragFinger = 0;
    float dragX = 0, dragY = 0;
};

// Helper: Which way is (dx, dy) pointing? Keeps `current` if it's (almost) no direction at all.
int facing(float dx, float dy, int current)
{
    if (std::fabs(dx) < 0.01f && std::fabs(dy) < 0.01f)
        return current;
    float angle = std::atan2(dx, -dy); // 0 = north (screen y grows downward), clockwise
    int dir = (int)std::lround(angle / (std::numbers::pi_v<float> / 4));
    return (dir + 8) % 8;
}

// Helper: Simple distance calculation
float getDistance(float x1, float y1, float x2, float y2)
{
    return std::hypot(x2 - x1, y2 - y1);
}

// Helper: Load a BMP texture safely
SDL_Texture *loadBMP(SDL_Renderer *renderer, const std::string &path, bool whiteIsClear = true)
{
    SDL_Surface *tempSurface = SDL_LoadBMP(path.c_str());
    if (!tempSurface)
    {
        SDL_Log("Failed to load BMP: %s", SDL_GetError());
        return nullptr;
    }
    // The sprites are drawn on white. Make the white see-through so knights can overlap and be tinted.
    if (whiteIsClear)
        SDL_SetColorKey(tempSurface, SDL_TRUE, SDL_MapRGB(tempSurface->format, 255, 255, 255));
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, tempSurface);
    SDL_FreeSurface(tempSurface);
    return texture;
}

// Where are the sprites? In the browser they are packed into the game at /assets. Natively we
// look next to the program, in the current folder and in the source tree it was built from.
std::string findAssets()
{
#ifdef __EMSCRIPTEN__
    return "/assets/";
#else
    std::vector<std::string> candidates;
    if (char *base = SDL_GetBasePath())
    {
        candidates.push_back(std::string(base) + "assets/");
        candidates.push_back(std::string(base) + "../assets/");
        SDL_free(base);
    }
    candidates.push_back("assets/");
#ifdef ASSET_DIR
    candidates.push_back(std::string(ASSET_DIR) + "/");
#endif
    candidates.push_back("/workspace/assets/"); // the original dev container
    for (auto &dir : candidates)
    {
        if (SDL_RWops *file = SDL_RWFromFile((dir + "WhiteKnight/WhiteKnight_S.bmp").c_str(), "rb"))
        {
            SDL_RWclose(file);
            return dir;
        }
    }
    return "assets/";
#endif
}

SDL_Texture *knightTexture(Game &g, int id, int face)
{
    if (id == 2)
        return g.blue.look[face];
    SDL_Texture *texture = g.white.look[face];
    if (texture)
    {
        SDL_Color tint = TINTS[std::clamp(id, 0, MAX_PLAYERS)];
        SDL_SetTextureColorMod(texture, tint.r, tint.g, tint.b);
    }
    return texture;
}

void drawKnight(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_FRect &rect, SDL_Color fallback)
{
    if (texture)
    {
        SDL_RenderCopyF(renderer, texture, nullptr, &rect);
    }
    else
    {
        SDL_SetRenderDrawColor(renderer, fallback.r, fallback.g, fallback.b, 255);
        SDL_RenderFillRectF(renderer, &rect);
    }
}

void drawLabel(SDL_Renderer *renderer, const std::string &text, const SDL_FRect &over)
{
    float x = over.x + over.w / 2 - textWidth(text, 2) / 2;
    drawTextShadowed(renderer, text, x, over.y - 18, 2, LIGHT);
}

void drawCentered(SDL_Renderer *renderer, const std::string &text, float y, int scale, SDL_Color color = WHITE)
{
    drawTextShadowed(renderer, text, W / 2.0f - textWidth(text, scale) / 2, y, scale, color);
}

// Which level a snapshot is in, as a key: "<world>:<level number>:<map>"
std::string levelKey(const Snapshot &world)
{
    return std::to_string((long long)world.epoch) + ":" + std::to_string(world.level) + ":" + std::to_string(world.map);
}

// Somewhere safe to (re)appear: one of the level's entrances, as long as it's away from the red
// knights and not on top of another player (with everyone starting at the same spot, knights and
// their names would pile up). Otherwise the safest place we can find that's reachable.
//
// When a new level starts, everybody moves at the same moment, and the others are still where
// they were on the old map as far as we know. So then each player takes the entrance that goes
// with their player id instead (and the next one if a red knight is too close to it).
SDL_FPoint safeSpot(const Snapshot &world, int myId, bool newLevel = false)
{
    const Level &level = builtInLevel(world.map);
    std::vector<Point> spots;
    for (Point p : level.playerSpawns)
        spots.push_back({p.x - PLAYER_SIZE / 2, p.y - PLAYER_SIZE / 2});
    int entrances = (int)spots.size();
    if (newLevel && entrances > 0)
    {
        std::rotate(spots.begin(), spots.begin() + ((myId - 1) % entrances + entrances) % entrances, spots.end());
    }
    else
    {
        for (size_t i = spots.size(); i > 1; i--) // try the entrances in a random order: players spread out
            std::swap(spots[i - 1], spots[(size_t)std::rand() % i]);
    }
    for (int i = 0; i < 40; i++)
    {
        if (!level.open.empty())
            spots.push_back(level.spotPosition(level.open[(size_t)std::rand() % level.open.size()]));
        else
            spots.push_back({20 + (float)(std::rand() % (int)(W - PLAYER_SIZE - 40)), 60 + (float)(std::rand() % (int)(H - PLAYER_SIZE - 80))});
    }

    SDL_FPoint best{spots[0].x, spots[0].y};
    float bestScore = -1;
    for (Point spot : spots)
    {
        float enemyRoom = 1e9f, playerRoom = 1e9f;
        for (auto &e : world.enemies)
            enemyRoom = std::min(enemyRoom, getDistance(spot.x, spot.y, e.x, e.y));
        for (auto &p : world.players)
        {
            if (p.id != myId && p.x >= 0 && !newLevel)
                playerRoom = std::min(playerRoom, getDistance(spot.x, spot.y, p.x, p.y));
        }
        if (enemyRoom >= 200 && playerRoom >= 90)
            return {spot.x, spot.y};
        float score = std::min(enemyRoom, playerRoom * 2);
        if (score > bestScore)
        {
            best = {spot.x, spot.y};
            bestScore = score;
        }
    }
    return best;
}

void respawn(Game &g, bool newLevel = false)
{
    const Snapshot &world = g.session->world();
    SDL_FPoint spot = safeSpot(world, g.session->myId(), newLevel);
    g.player.x = spot.x;
    g.player.y = spot.y;
    g.state = GameState::PLAYING;
    g.protection = SPAWN_PROTECTION;
    g.dragging = false; // the finger was steering the knight somewhere else
    if (levelKey(world) != g.bannerFor) // a level we haven't been in yet: say which one it is
    {
        g.bannerFor = levelKey(world);
        g.levelBanner = BANNER_TIME;
    }
}

void shootAt(Game &g, float targetX, float targetY)
{
    // Calculate direction from player center to the target
    float centerX = g.player.x + g.player.w / 2.0f;
    float centerY = g.player.y + g.player.h / 2.0f;
    float dx = targetX - centerX;
    float dy = targetY - centerY;
    float dist = std::sqrt(dx * dx + dy * dy);
    if (dist > 0)
    {
        // Bullet speed (adjust BULLET_SPEED in world.h to change how fast bullets fly)
        g.session->sendBullet(centerX, centerY, (dx / dist) * BULLET_SPEED, (dy / dist) * BULLET_SPEED);
        g.face = facing(dx, dy, g.face);
    }
}

// 1. INPUT HANDLING
void handleEvents(Game &g)
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_QUIT)
            g.running = false;
        if (event.type == SDL_RENDER_TARGETS_RESET)
            g.mapDrawn = -1; // the graphics driver lost the map we drew: draw it again
#ifndef __EMSCRIPTEN__
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
            g.running = false;
#endif
        bool playing = g.state == GameState::PLAYING && !g.session->spectating();
        bool canRespawn = g.state == GameState::GAME_OVER && g.deadTime > RESPAWN_DELAY;

        // SHOOTING LOGIC: Detect Left Mouse Click
        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT)
        {
            if (playing)
                shootAt(g, (float)event.button.x, (float)event.button.y);
            else if (canRespawn)
                respawn(g);
        }

        if (event.type == SDL_KEYDOWN && canRespawn && !event.key.repeat && event.key.keysym.sym != SDLK_ESCAPE)
            respawn(g);

        // TOUCH: drag your knight to move it, tap anywhere else to shoot there
        if (event.type == SDL_FINGERDOWN)
        {
            float x = event.tfinger.x * W, y = event.tfinger.y * H;
            float centerX = g.player.x + g.player.w / 2, centerY = g.player.y + g.player.h / 2;
            if (canRespawn)
            {
                respawn(g);
            }
            else if (playing && !g.dragging && getDistance(x, y, centerX, centerY) < 90)
            {
                g.dragging = true;
                g.dragFinger = event.tfinger.fingerId;
                g.dragX = x;
                g.dragY = y;
            }
            else if (playing)
            {
                shootAt(g, x, y);
            }
        }
        if (event.type == SDL_FINGERMOTION && g.dragging && event.tfinger.fingerId == g.dragFinger)
        {
            g.dragX = event.tfinger.x * W;
            g.dragY = event.tfinger.y * H;
        }
        if (event.type == SDL_FINGERUP && g.dragging && event.tfinger.fingerId == g.dragFinger)
            g.dragging = false;
    }
}

// 2. UPDATE LOGIC
void update(Game &g, float deltaTime)
{
    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
    bool playing = g.state == GameState::PLAYING && !g.session->spectating();

    if (playing)
    {
        // Player Movement
        float moveX = 0, moveY = 0;
        if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])
            moveY -= 1;
        if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])
            moveY += 1;
        if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])
            moveX -= 1;
        if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT])
            moveX += 1;
        if (g.dragging) // walk toward the finger
        {
            float dx = g.dragX - (g.player.x + g.player.w / 2), dy = g.dragY - (g.player.y + g.player.h / 2);
            float d = std::hypot(dx, dy);
            if (d > 4)
            {
                moveX += dx / d;
                moveY += dy / d;
            }
        }

        if (moveX != 0 || moveY != 0)
        {
            // Walk, stopping at walls (and sliding along them)
            float length = std::sqrt(moveX * moveX + moveY * moveY);
            Rect me{g.player.x, g.player.y, g.player.w, g.player.h};
            builtInLevel(g.session->world().map).slide(me, (moveX / length) * PLAYER_SPEED * deltaTime, (moveY / length) * PLAYER_SPEED * deltaTime);
            g.player.x = me.x;
            g.player.y = me.y;
            g.face = facing(moveX, moveY, g.face);
        }

        // Keep player in bounds
        g.player.x = std::clamp(g.player.x, 0.0f, (float)W - g.player.w);
        g.player.y = std::clamp(g.player.y, 0.0f, (float)H - g.player.h);
        g.protection = std::max(0.0f, g.protection - deltaTime);
    }
    else if (g.state == GameState::GAME_OVER)
    {
        g.deadTime += deltaTime;
    }

    // Tell the world where we are (far off screen while dead), then catch up on what's new
    if (g.state == GameState::PLAYING)
        g.session->sendPosition(g.player.x, g.player.y);
    else
        g.session->sendPosition(DEAD, DEAD);
    g.session->update(deltaTime);
    const Snapshot &world = g.session->world();

    // A new level (somebody went through the door), or another world (we joined someone else's
    // game): start again at one of its entrances
    if (world.epoch != g.seenEpoch || world.level != g.seenLevel || world.map != g.seenMap)
    {
        bool sameWorld = world.epoch == g.seenEpoch; // then everybody is moving to the new level right now
        g.seenEpoch = world.epoch;
        g.seenLevel = world.level;
        g.seenMap = world.map;
        if (g.state == GameState::PLAYING && !g.session->spectating())
            respawn(g, sameWorld);
    }
    else if (world.coinsLeft == 0 && g.seenCoinsLeft > 0 && !builtInLevel(world.map).doors.empty())
    {
        g.doorBanner = BANNER_TIME; // that was the coin the door was waiting for
    }
    g.seenCoinsLeft = world.coinsLeft;
    g.levelBanner = std::max(0.0f, g.levelBanner - deltaTime);
    g.doorBanner = std::max(0.0f, g.doorBanner - deltaTime);

    // Enemy Collision: every player checks their own knight, like the original game did
    if (playing && g.protection <= 0)
    {
        Rect me{g.player.x, g.player.y, g.player.w, g.player.h};
        for (auto &e : world.enemies)
        {
            if (checkCollision(me, {e.x, e.y, ENEMY_SIZE, ENEMY_SIZE}))
            {
                g.state = GameState::GAME_OVER;
                g.deadTime = 0;
                g.finalScore = world.score;
                g.dragging = false;
                break;
            }
        }
    }

    // The other players: glide toward where the host says they are
    std::map<int, Remote> next;
    for (auto &p : world.players)
    {
        if (p.id == g.session->myId() || p.x < 0)
            continue;
        Remote r{p.x, p.y, SOUTH};
        auto known = g.remotes.find(p.id);
        if (known != g.remotes.end())
        {
            r = known->second;
            float dx = p.x - r.x, dy = p.y - r.y;
            float gap = std::hypot(dx, dy);
            if (gap > 150) // respawned somewhere else: just appear there
            {
                r.x = p.x;
                r.y = p.y;
            }
            else
            {
                float k = std::min(1.0f, deltaTime * 15);
                r.x += dx * k;
                r.y += dy * k;
                if (gap > 1.5f)
                    r.face = facing(dx, dy, r.face);
            }
        }
        next[p.id] = r;
    }
    g.remotes = std::move(next);

#ifndef __EMSCRIPTEN__
    if (world.score != g.shownScore || world.level != g.shownLevel)
    {
        g.shownScore = world.score;
        g.shownLevel = world.level;
        SDL_SetWindowTitle(g.window, ("Level " + std::to_string(world.level) + " - Score: " + std::to_string(world.score)).c_str());
    }
#endif
}

// The level's tiles, layer by layer, and a shadow where a wall meets the floor so the walls
// stand up from it
void drawTiles(Game &g, const Level &level)
{
    for (auto &layer : level.layers)
    {
        for (size_t i = 0; i < layer.size(); i++)
        {
            unsigned number = layer[i] & TILE_NUMBER;
            if (number == 0 || number >= level.pictures.size())
                continue;
            const TilePicture &picture = level.pictures[number];
            auto texture = g.pictures.find(picture.file);
            if (texture == g.pictures.end() || !texture->second)
                continue;
            SDL_Rect part{picture.x, picture.y, picture.w, picture.h};
            SDL_Rect cell{(int)i % level.cols * CELL, (int)i / level.cols * CELL, CELL, CELL};
            int flip = (layer[i] & FLIPPED_HORIZONTALLY ? SDL_FLIP_HORIZONTAL : 0) | (layer[i] & FLIPPED_VERTICALLY ? SDL_FLIP_VERTICAL : 0);
            SDL_RenderCopyEx(g.renderer, texture->second, picture.w > 0 ? &part : nullptr, &cell, 0, nullptr, (SDL_RendererFlip)flip);
        }
    }

    SDL_SetRenderDrawBlendMode(g.renderer, SDL_BLENDMODE_BLEND);
    for (int row = 0; row < level.rows; row++)
    {
        for (int col = 0; col < level.cols; col++)
        {
            if (level.wall(col, row))
                continue;
            if (level.wall(col, row - 1))
            {
                SDL_Rect shadow{col * CELL, row * CELL, CELL, 8};
                SDL_SetRenderDrawColor(g.renderer, 0, 0, 0, 110);
                SDL_RenderFillRect(g.renderer, &shadow);
            }
            if (level.wall(col - 1, row))
            {
                SDL_Rect shadow{col * CELL, row * CELL, 5, CELL};
                SDL_SetRenderDrawColor(g.renderer, 0, 0, 0, 60);
                SDL_RenderFillRect(g.renderer, &shadow);
            }
        }
    }
    SDL_SetRenderDrawBlendMode(g.renderer, SDL_BLENDMODE_NONE);
}

// The level only changes when the team goes through a door, so it's drawn into a texture once
// and each frame just copies that (one picture instead of hundreds of tiles).
void drawMap(Game &g, const Level &level, int map)
{
    if (!g.mapTexture && !g.noMapTexture)
    {
        g.mapTexture = SDL_CreateTexture(g.renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, W, H);
        g.noMapTexture = !g.mapTexture;
        g.mapDrawn = -1;
    }
    if (g.mapTexture && g.mapDrawn != map)
    {
        if (SDL_SetRenderTarget(g.renderer, g.mapTexture) == 0)
        {
            SDL_SetRenderDrawColor(g.renderer, 0, 0, 0, 255);
            SDL_RenderClear(g.renderer);
            drawTiles(g, level);
            SDL_SetRenderTarget(g.renderer, nullptr);
            g.mapDrawn = map;
        }
        else
        {
            SDL_DestroyTexture(g.mapTexture);
            g.mapTexture = nullptr;
            g.noMapTexture = true;
        }
    }
    if (g.mapTexture)
    {
        SDL_RenderCopy(g.renderer, g.mapTexture, nullptr, nullptr);
        return;
    }
    SDL_SetRenderDrawColor(g.renderer, 0, 0, 0, 255);
    SDL_RenderClear(g.renderer);
    drawTiles(g, level);
}

// A door: a shut wooden hatch, and once enough coins are in, a glowing way down
void drawDoor(Game &g, const Door &door, bool open)
{
    SDL_Renderer *renderer = g.renderer;
    SDL_FRect area{door.area.x, door.area.y, door.area.w, door.area.h};
    if (!open)
    {
        for (float y = area.y; y < area.y + area.h; y += CELL)
        {
            for (float x = area.x; x < area.x + area.w; x += CELL)
            {
                SDL_FRect plank{x, y, std::min((float)CELL, area.x + area.w - x), std::min((float)CELL, area.y + area.h - y)};
                SDL_SetRenderDrawColor(renderer, 150, 100, 70, 255);
                if (!g.wood || SDL_RenderCopyF(renderer, g.wood, nullptr, &plank) != 0)
                    SDL_RenderFillRectF(renderer, &plank);
            }
        }
        SDL_SetRenderDrawColor(renderer, 45, 30, 20, 255); // iron bands and a dark frame
        for (float band : {0.22f, 0.70f})
        {
            SDL_FRect strip{area.x, area.y + area.h * band, area.w, 5};
            SDL_RenderFillRectF(renderer, &strip);
        }
        for (float inset = 0; inset < 3; inset++)
        {
            SDL_FRect frame{area.x + inset, area.y + inset, area.w - 2 * inset, area.h - 2 * inset};
            SDL_RenderDrawRectF(renderer, &frame);
        }
        return;
    }

    // Steps going down into the dark
    SDL_SetRenderDrawColor(renderer, 6, 6, 8, 255);
    SDL_RenderFillRectF(renderer, &area);
    for (int i = 0; i < 4; i++)
    {
        Uint8 shade = (Uint8)(110 - i * 28);
        float h = (area.h - 8) / 4;
        SDL_FRect step{area.x + 4 + i * 4.0f, area.y + 4 + i * h, area.w - 8 - i * 8.0f, h * 0.55f};
        SDL_SetRenderDrawColor(renderer, shade, shade, (Uint8)(shade + 12), 255);
        SDL_RenderFillRectF(renderer, &step);
    }
    // ...with a glow around it, so nobody can miss it
    float pulse = 0.5f + 0.5f * std::sin((float)SDL_GetTicks() / 160.0f);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    for (float inset = 0; inset < 4; inset++)
    {
        SDL_SetRenderDrawColor(renderer, GOLD.r, GOLD.g, GOLD.b, (Uint8)((150 + 105 * pulse) * (1 - inset / 5)));
        SDL_FRect glow{area.x + inset, area.y + inset, area.w - 2 * inset, area.h - 2 * inset};
        SDL_RenderDrawRectF(renderer, &glow);
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

// "EXIT" over an open door (drawn after everything else, so no coin or knight hides it)
void drawExitSign(SDL_Renderer *renderer, const Door &door)
{
    std::string sign = "EXIT";
    float x = std::clamp(door.area.x + door.area.w / 2 - textWidth(sign, 2) / 2, 4.0f, W - 4 - textWidth(sign, 2));
    float y = door.area.y >= 24 ? door.area.y - 20 : door.area.y + door.area.h + 6;
    drawTextShadowed(renderer, sign, x, y, 2, GOLD);
}

// Big words in the middle of the screen that fade away: "LEVEL 2", "THE DOOR IS OPEN!"
void drawBanner(SDL_Renderer *renderer, const std::string &big, const std::string &small, float timeLeft, SDL_Color color)
{
    color.a = (Uint8)(255 * std::clamp(timeLeft / 0.6f, 0.0f, 1.0f));
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    drawCentered(renderer, big, 205, 6, color);
    drawCentered(renderer, small, 270, 3, {WHITE.r, WHITE.g, WHITE.b, color.a});
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

// 3. RENDERING
void render(Game &g)
{
    SDL_Renderer *renderer = g.renderer;
    const Snapshot &world = g.session->world();
    const Level &level = builtInLevel(world.map);
    int myId = g.session->myId();
    bool playing = g.state == GameState::PLAYING && !g.session->spectating();

    // The level: floor and walls, then its doors
    drawMap(g, level, world.map);
    for (const Door &door : level.doors)
        drawDoor(g, door, world.coinsLeft == 0);

    // Draw Coins (Yellow)
    SDL_SetRenderDrawColor(renderer, 255, 215, 0, 255);
    for (const auto &coin : world.coins)
    {
        SDL_FRect rect{coin.x, coin.y, COIN_SIZE, COIN_SIZE};
        SDL_RenderFillRectF(renderer, &rect);
    }

    // Draw Bullets (Cyan), moved on to where they must be by now (unless that's inside a wall)
    float age = g.session->worldAge();
    SDL_SetRenderDrawColor(renderer, 0, 255, 255, 255);
    for (const auto &bullet : world.bullets)
    {
        SDL_FRect rect{bullet.x + bullet.vx * age, bullet.y + bullet.vy * age, BULLET_SIZE, BULLET_SIZE};
        if (!level.wall((int)std::floor((rect.x + BULLET_SIZE / 2) / CELL), (int)std::floor((rect.y + BULLET_SIZE / 2) / CELL)))
            SDL_RenderFillRectF(renderer, &rect);
    }

    // Draw Enemies (Red Knights), each one facing the player it is chasing
    for (const auto &enemy : world.enemies)
    {
        float targetX = enemy.x, targetY = enemy.y + 1, best = 1e9f;
        auto consider = [&](float x, float y) {
            float d = getDistance(enemy.x, enemy.y, x, y);
            if (d < best)
            {
                best = d;
                targetX = x;
                targetY = y;
            }
        };
        for (auto &[id, r] : g.remotes)
            consider(r.x, r.y);
        if (playing)
            consider(g.player.x, g.player.y);
        SDL_FRect rect{enemy.x, enemy.y, ENEMY_SIZE, ENEMY_SIZE};
        drawKnight(renderer, g.red.look[facing(targetX - enemy.x, targetY - enemy.y, SOUTH)], rect, {255, 50, 50, 255});
    }

    // Draw the other players
    for (auto &[id, r] : g.remotes)
    {
        SDL_FRect rect{r.x, r.y, PLAYER_SIZE, PLAYER_SIZE};
        drawKnight(renderer, knightTexture(g, id, r.face), rect, {50, 150, 255, 255});
        drawLabel(renderer, "P" + std::to_string(id), rect);
    }

    // Draw Player (White Knight), blinking while spawn protection lasts
    if (playing)
    {
        if (g.protection <= 0 || (int)(g.protection * 10) % 2 == 0)
            drawKnight(renderer, knightTexture(g, myId, g.face), g.player, {50, 150, 255, 255});
        if (!g.remotes.empty())
            drawLabel(renderer, "YOU", g.player);
    }

    if (world.coinsLeft == 0)
    {
        for (const Door &door : level.doors)
            drawExitSign(renderer, door);
    }

    // Score in the top left, room and players in the top right
    bool team = world.players.size() > 1;
    drawTextShadowed(renderer, (team ? "TEAM SCORE " : "SCORE ") + std::to_string(world.score), 12, 12, 3, WHITE);
    std::string status = g.session->status();
    float y = 12;
    size_t start = 0;
    while (start <= status.size())
    {
        size_t end = status.find('\n', start);
        std::string line = status.substr(start, end == std::string::npos ? std::string::npos : end - start);
        drawTextShadowed(renderer, line, W - 12 - textWidth(line, 2), y, 2, LIGHT);
        y += 20;
        if (end == std::string::npos)
            break;
        start = end + 1;
    }

    // Along the bottom: which level this is, and how far its door is from opening
    drawTextShadowed(renderer, "LEVEL " + std::to_string(world.level) + "  " + level.name, 12, H - 21, 2, LIGHT);
    if (!level.doors.empty())
    {
        int left = world.coinsLeft;
        std::string door = left > 0 ? std::to_string(left) + (left == 1 ? " COIN" : " COINS") + " TO OPEN THE DOOR" : "THE DOOR IS OPEN";
        drawTextShadowed(renderer, door, W - 12 - textWidth(door, 2), H - 21, 2, left > 0 ? LIGHT : GOLD);
    }

    if (g.state == GameState::PLAYING && g.levelBanner > 0)
        drawBanner(renderer, "LEVEL " + std::to_string(world.level), level.name, g.levelBanner, GOLD);
    else if (g.state == GameState::PLAYING && g.doorBanner > 0)
        drawBanner(renderer, "THE DOOR IS OPEN!", "GO THROUGH IT TO THE NEXT LEVEL", g.doorBanner, GOLD);

    if (g.state == GameState::GAME_OVER)
    {
        // Game Over screen: the original red, now see-through so you can watch the others play on
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 255, 0, 0, 100);
        SDL_RenderFillRect(renderer, nullptr);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        drawCentered(renderer, "GAME OVER", 210, 8);
        drawCentered(renderer, "SCORE " + std::to_string(g.finalScore), 300, 3);
        if (g.deadTime > RESPAWN_DELAY)
            drawCentered(renderer, "PRESS ANY KEY OR TAP TO PLAY AGAIN", 360, 2);
    }

    SDL_RenderPresent(renderer);
}

void frame(Game &g)
{
    Uint64 now = SDL_GetPerformanceCounter();
    float deltaTime = (float)(now - g.lastCounter) / (float)SDL_GetPerformanceFrequency();
    g.lastCounter = now;
    if (deltaTime > 1.0f && g.state == GameState::PLAYING)
        g.protection = SPAWN_PROTECTION; // back from a hidden tab: a moment to look around
    deltaTime = std::min(deltaTime, 0.1f); // don't jump ahead after a pause

    handleEvents(g);
    update(g, deltaTime);
    render(g);
}

#ifdef __EMSCRIPTEN__
void frameCallback(void *game)
{
    frame(*static_cast<Game *>(game));
}
#endif

int main(int argc, char *argv[])
{
#ifdef __EMSCRIPTEN__
    // Only take the keys typed into the game, so the rest of the web page keeps working
    SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#canvas");
#endif
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0"); // we handle touches ourselves

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        SDL_Log("SDL_Init Error: %s", SDL_GetError());
        return 1;
    }

    // static: in the browser, main() returns while the game keeps running
    static Game g;
    g.window = SDL_CreateWindow("Score: 0", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, 0);
    g.renderer = SDL_CreateRenderer(g.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (g.window && !g.renderer)
        g.renderer = SDL_CreateRenderer(g.window, -1, 0); // no graphics card? software works too

    if (!g.window || !g.renderer)
    {
        SDL_Log("Renderer/Window Error: %s", SDL_GetError());
        return 1;
    }

    // --- ASSET LOADING ---
    std::string assets = findAssets();
    for (int d = 0; d < 8; d++)
    {
        std::string dir = DIRECTIONS[d];
        g.white.look[d] = loadBMP(g.renderer, assets + "WhiteKnight/WhiteKnight_" + dir + ".bmp");
        g.blue.look[d] = loadBMP(g.renderer, assets + "BlueKnight/BlueKnight_" + dir + ".bmp");
        g.red.look[d] = loadBMP(g.renderer, assets + "RedKnight/RedKnight_" + dir + ".bmp");
    }

    // The tiles the levels are drawn with, and the wood the doors are made of
    for (const Level &level : builtInLevels().levels)
    {
        for (const TilePicture &picture : level.pictures)
        {
            if (!picture.file.empty() && !g.pictures.count(picture.file))
                g.pictures[picture.file] = loadBMP(g.renderer, assets + picture.file, false);
        }
    }
    g.wood = loadBMP(g.renderer, assets + "Tiles/WoodTile.bmp", false);

    std::srand(static_cast<unsigned int>(SDL_GetPerformanceCounter()));
    g.session = createSession(argc, argv);
    SDL_FPoint start = safeSpot(g.session->world(), g.session->myId());
    g.player.x = start.x;
    g.player.y = start.y;
    g.lastCounter = SDL_GetPerformanceCounter();

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(frameCallback, &g, 0, true);
#else
    while (g.running)
        frame(g);

    g.session.reset();
    for (Knight *k : {&g.white, &g.blue, &g.red})
    {
        for (SDL_Texture *t : k->look)
            SDL_DestroyTexture(t);
    }
    for (SDL_Texture *t : {g.wood, g.mapTexture})
    {
        if (t)
            SDL_DestroyTexture(t);
    }
    for (auto &[file, texture] : g.pictures)
    {
        if (texture)
            SDL_DestroyTexture(texture);
    }
    SDL_DestroyRenderer(g.renderer);
    SDL_DestroyWindow(g.window);
    SDL_Quit();
#endif
    return 0;
}
