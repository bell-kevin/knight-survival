// main.cpp - SDL2 Survival Game (Shooting Edition)
// Objective: Collect yellow coins & Shoot red enemies with Left Click.
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
    std::unique_ptr<Session> session;

    SDL_FRect player{100, 100, PLAYER_SIZE, PLAYER_SIZE};
    int face = SOUTH;
    GameState state = GameState::PLAYING;
    float protection = SPAWN_PROTECTION;
    float deadTime = 0;
    int finalScore = 0;
    int shownScore = -1;
    Uint64 lastCounter = 0;
    bool running = true;
    std::map<int, Remote> remotes;

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
SDL_Texture *loadBMP(SDL_Renderer *renderer, const std::string &path)
{
    SDL_Surface *tempSurface = SDL_LoadBMP(path.c_str());
    if (!tempSurface)
    {
        SDL_Log("Failed to load BMP: %s", SDL_GetError());
        return nullptr;
    }
    // The sprites are drawn on white. Make the white see-through so knights can overlap and be tinted.
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
    drawText(renderer, text, x, over.y - 18, 2, {90, 90, 90, 255});
}

void drawCentered(SDL_Renderer *renderer, const std::string &text, float y, int scale)
{
    drawTextShadowed(renderer, text, W / 2.0f - textWidth(text, scale) / 2, y, scale, {255, 255, 255, 255});
}

// Somewhere safe to (re)appear: away from the red knights, and not on top of another player
// (with everyone starting at the same spot, knights and their names would pile up).
SDL_FPoint safeSpot(const Snapshot &world, int myId)
{
    SDL_FPoint best{100, 100};
    float bestScore = -1;
    for (int i = 0; i < 40; i++)
    {
        float x = 20 + (float)(std::rand() % (int)(W - PLAYER_SIZE - 40));
        float y = 60 + (float)(std::rand() % (int)(H - PLAYER_SIZE - 80)); // below the score
        float enemyRoom = 1e9f, playerRoom = 1e9f;
        for (auto &e : world.enemies)
            enemyRoom = std::min(enemyRoom, getDistance(x, y, e.x, e.y));
        for (auto &p : world.players)
        {
            if (p.id != myId && p.x >= 0)
                playerRoom = std::min(playerRoom, getDistance(x, y, p.x, p.y));
        }
        if (enemyRoom >= 200 && playerRoom >= 90)
            return {x, y};
        float score = std::min(enemyRoom, playerRoom * 2);
        if (score > bestScore)
        {
            best = {x, y};
            bestScore = score;
        }
    }
    return best;
}

void respawn(Game &g)
{
    SDL_FPoint spot = safeSpot(g.session->world(), g.session->myId());
    g.player.x = spot.x;
    g.player.y = spot.y;
    g.state = GameState::PLAYING;
    g.protection = SPAWN_PROTECTION;
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
            float length = std::sqrt(moveX * moveX + moveY * moveY);
            g.player.x += (moveX / length) * PLAYER_SPEED * deltaTime;
            g.player.y += (moveY / length) * PLAYER_SPEED * deltaTime;
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
    if (world.score != g.shownScore)
    {
        g.shownScore = world.score;
        SDL_SetWindowTitle(g.window, ("Score: " + std::to_string(world.score)).c_str());
    }
#endif
}

// 3. RENDERING
void render(Game &g)
{
    SDL_Renderer *renderer = g.renderer;
    const Snapshot &world = g.session->world();
    int myId = g.session->myId();
    bool playing = g.state == GameState::PLAYING && !g.session->spectating();

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderClear(renderer);

    // Draw Coins (Yellow)
    SDL_SetRenderDrawColor(renderer, 255, 215, 0, 255);
    for (const auto &coin : world.coins)
    {
        SDL_FRect rect{coin.x, coin.y, COIN_SIZE, COIN_SIZE};
        SDL_RenderFillRectF(renderer, &rect);
    }

    // Draw Bullets (Cyan), moved on to where they must be by now
    float age = g.session->worldAge();
    SDL_SetRenderDrawColor(renderer, 0, 255, 255, 255);
    for (const auto &bullet : world.bullets)
    {
        SDL_FRect rect{bullet.x + bullet.vx * age, bullet.y + bullet.vy * age, BULLET_SIZE, BULLET_SIZE};
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

    // Score in the top left, room and players in the top right
    bool team = world.players.size() > 1;
    drawText(renderer, (team ? "TEAM SCORE " : "SCORE ") + std::to_string(world.score), 12, 12, 3, {40, 40, 40, 255});
    std::string status = g.session->status();
    float y = 12;
    size_t start = 0;
    while (start <= status.size())
    {
        size_t end = status.find('\n', start);
        std::string line = status.substr(start, end == std::string::npos ? std::string::npos : end - start);
        drawText(renderer, line, W - 12 - textWidth(line, 2), y, 2, {110, 110, 110, 255});
        y += 20;
        if (end == std::string::npos)
            break;
        start = end + 1;
    }

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
    SDL_DestroyRenderer(g.renderer);
    SDL_DestroyWindow(g.window);
    SDL_Quit();
#endif
    return 0;
}
