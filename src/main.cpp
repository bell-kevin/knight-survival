// main.cpp - SDL2 Survival Game (Shooting Edition)
// Objective: Collect gold coins & Shoot the bad guys with Left Click. Enough coins open the door
//            to the next level (the levels are maps made with Tiled: see maps/README.md). Every
//            kind of bad guy takes its own number of hits and hurts you its own amount (see
//            enemies.h); when your health runs out, you're out until you jump back in.
// Controls: WASD/Arrows to move. Mouse Left-Click to shoot. Esc to Quit.
//           Touch screens: the left thumb stick moves, the right one aims and fires (a quick tap on
//           it shoots the nearest bad guy). You can also drag your knight, and tap the field to
//           shoot there.
//
// The field is always 800 x 600 "units", drawn as big as the window or phone allows. On a phone
// or tablet the web page gives the game the whole screen, and the thumb sticks go in the space
// beside the field (sideways) or under it (upright).
//
// Multiplayer: everyone plays in one shared world, run by a host (see session.h).
//   * Native build: start ./knight-server, then ./knight-survival [server-ip] on each computer.
//   * Web build:    players who open the same room link play together (see session_web.cpp).
//
// Copyright (C) 2026 Kevin Bell. SPDX-License-Identifier: GPL-3.0-or-later

#include <SDL2/SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
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
const float HIT_PROTECTION = 1.0f;   // ...and after a bad guy hits it, to get away
const float KNOCKBACK_TIME = 0.2f;   // seconds a hit pushes you back for
const float KNOCKBACK_SPEED = 420.0f; // how fast that push starts out (it slows down to nothing)
const float HURT_FLASH = 0.35f;      // seconds the field flashes red when you're hit
const float RESPAWN_DELAY = 0.75f;   // so a click you meant as a shot doesn't respawn you
const float BANNER_TIME = 2.5f;      // seconds "LEVEL 2" or "THE DOOR IS OPEN" stays on screen
const float FIRE_INTERVAL = 0.18f;   // seconds between shots while the fire stick is pushed
const float DEAD_ZONE = 0.12f;       // a thumb stick pushed less than this (of its radius) does nothing

const SDL_Color WHITE{255, 255, 255, 255};
const SDL_Color LIGHT{220, 220, 220, 255};
const SDL_Color GOLD{255, 215, 0, 255};
const SDL_Color AROUND{11, 14, 18, 255}; // the screen around the field

#ifdef __EMSCRIPTEN__
// What the web page around the game says, and the menu it shows (see KnightUI in web/shell.html)
EM_JS(int, js_game_only, (), { return window.KnightUI && KnightUI.gameOnly() ? 1 : 0; });
EM_JS(int, js_touch_first, (), { return window.KnightUI && KnightUI.touchFirst() ? 1 : 0; });
EM_JS(int, js_covered, (), { return window.KnightUI && KnightUI.covered() ? 1 : 0; }); // the menu is over the game
EM_JS(void, js_open_menu, (), { if (window.KnightUI) KnightUI.openMenu(); });
#endif

// Player colors: 1 is the White Knight, 2 the Blue Knight, and from 3 on the White Knight tinted
const SDL_Color TINTS[MAX_PLAYERS + 1] = {
    {255, 255, 255, 255}, {255, 255, 255, 255}, {255, 255, 255, 255},
    {130, 235, 130, 255}, {255, 175, 80, 255}, {195, 140, 255, 255},
    {100, 225, 255, 255}, {255, 140, 200, 255}, {245, 230, 100, 255},
};

// The bad guys are all the red knight, recolored: the red of its armor becomes `body`, and for
// the warlord, the darker reds (its shading and shoulders) become gold `trim`. Alpha 0: keep it.
struct Armor
{
    SDL_Color body, trim;
};
const Armor ARMOR[ENEMY_KINDS] = {
    {{0, 0, 0, 0}, {0, 0, 0, 0}},             // red knight: just as it's drawn
    {{70, 190, 60, 255}, {0, 0, 0, 0}},       // scout: green
    {{150, 70, 215, 255}, {0, 0, 0, 0}},      // champion: purple
    {{215, 120, 30, 255}, {0, 0, 0, 0}},      // brute: bronze
    {{78, 78, 96, 255}, {225, 170, 30, 255}}, // warlord: black steel and gold
};
// What the "LEVEL n" banner calls them
const char *const CROWD_NAMES[ENEMY_KINDS] = {"RED KNIGHTS", "SCOUTS", "CHAMPIONS", "BRUTES", "THE WARLORD"};

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

// Where everything goes on the screen, in units (the field is 800 x 600 of them)
struct Screen
{
    float scaleX = 1, scaleY = 1;      // pixels per unit
    float w = W, h = H;                // the whole screen
    SDL_Rect field{0, 0, W, H};        // where the field is on it
    bool sticks = false;               // show the thumb sticks
    bool beside = false;               // ...in a space of their own, beside or under the field
    float radius = 62;                 // of a thumb stick
    SDL_FPoint moveHome{}, fireHome{}; // where the sticks wait when no thumb is on them
    SDL_FRect moveZone{}, fireZone{};  // where a thumb picks them up, when they're beside the field
    SDL_FRect menu{};                  // the menu button (w == 0: there is none)
};

// A thumb stick: it comes to where your thumb lands, and points where you push it
struct Stick
{
    bool held = false;
    SDL_FingerID finger = 0;
    SDL_FPoint base{}, knob{}; // in screen units
    float heldFor = 0;         // seconds
    float travel = 0;          // the farthest it was pushed, as a share of its radius
};

struct Game
{
    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    Knight white, blue;
    std::array<Knight, ENEMY_KINDS> enemies; // the bad guys, by kind
    std::map<std::string, SDL_Texture *> pictures; // the tile pictures, by file name (in assets/)
    SDL_Texture *wood = nullptr;                   // what a shut door is made of
    SDL_Texture *mapTexture = nullptr;             // the level, drawn once (see drawMap)
    int mapDrawn = -1;                             // which map is in mapTexture (-1: none yet)
    bool noMapTexture = false;                     // can't draw into a texture: draw the map every frame
    std::unique_ptr<Session> session;

    SDL_FRect player{100, 100, PLAYER_SIZE, PLAYER_SIZE};
    int face = SOUTH;
    GameState state = GameState::PLAYING;
    int health = PLAYER_HEALTH;
    float protection = SPAWN_PROTECTION;
    float hurtFlash = 0;          // seconds the red flash of a hit still shows
    float knockTime = 0;          // seconds a hit still pushes us back
    SDL_FPoint knock{};           // ...this fast at first
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

    // The screen, and the thumb sticks (on phones and tablets, or once the screen is touched)
    Screen screen;
    bool touchControls = false;
    Stick move, fire;
    float fireCooldown = 0; // seconds until the fire stick shoots again
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

// Works out where everything goes on the screen this frame
void layOut(Game &g)
{
    bool menu = false;
#ifdef __EMSCRIPTEN__
    // The web page decides how big the canvas is (the whole screen while playing on a phone). Draw
    // at the screen's own resolution, but at most 2 pixels per CSS pixel: sharp, and easy on phones.
    double cssW = 0, cssH = 0;
    emscripten_get_element_css_size("#canvas", &cssW, &cssH);
    if (cssW >= 1 && cssH >= 1)
    {
        double ratio = std::clamp(emscripten_get_device_pixel_ratio(), 1.0, 2.0);
        int wantW = (int)std::lround(cssW * ratio), wantH = (int)std::lround(cssH * ratio), haveW = 0, haveH = 0;
        SDL_GetWindowSize(g.window, &haveW, &haveH);
        if (wantW != haveW || wantH != haveH)
            SDL_SetWindowSize(g.window, wantW, wantH);
    }
    menu = js_game_only();
#endif
    int pw = W, ph = H;
    SDL_GetRendererOutputSize(g.renderer, &pw, &ph);
    pw = std::max(pw, 1);
    ph = std::max(ph, 1);

    Screen s;
    s.sticks = g.touchControls;
    float aspect = (float)pw / (float)ph, fieldAspect = (float)W / H;
    bool wide = aspect >= fieldAspect;
    if (!s.sticks && std::fabs(aspect / fieldAspect - 1) < 0.03f)
    {
        // The field's own shape (the game in the web page on a computer): it fills the screen
        s.scaleX = (float)pw / W;
        s.scaleY = (float)ph / H;
    }
    else
    {
        s.scaleX = s.scaleY = wide ? (float)ph / H : (float)pw / W;
        s.w = (float)pw / s.scaleX;
        s.h = (float)ph / s.scaleY;
        if (wide) // room left and right of the field
        {
            float side = (s.w - W) / 2;
            s.field.x = (int)std::floor(side);
            if (s.sticks && side >= 150) // a phone held sideways: a thumb stick on each side
            {
                s.beside = true;
                s.radius = std::min(side * 0.4f, 100.0f);
                s.moveZone = {0, 0, (float)s.field.x, s.h};
                s.fireZone = {(float)(s.field.x + W), 0, s.w - (float)(s.field.x + W), s.h};
                s.moveHome = {s.moveZone.w / 2, H * 0.66f};
                s.fireHome = {s.fireZone.x + s.fireZone.w / 2, H * 0.66f};
            }
        }
        else // room above and below
        {
            float below = s.h - H;
            if (s.sticks && below >= 260) // a phone held upright: the field on top, the sticks under it
            {
                s.beside = true;
                s.radius = std::min(below * 0.2f, 120.0f);
                s.moveZone = {0, (float)H, W / 2.0f, below};
                s.fireZone = {W / 2.0f, (float)H, W / 2.0f, below};
                s.moveHome = {W * 0.25f, H + below * 0.64f};
                s.fireHome = {W * 0.75f, H + below * 0.64f};
            }
            else
            {
                s.field.y = (int)std::floor(below / 2);
            }
        }
    }
    if (s.sticks && !s.beside) // no room of their own: on the field, in its bottom corners
    {
        s.moveHome = {s.field.x + 96.0f, s.field.y + H - 104.0f};
        s.fireHome = {s.field.x + W - 96.0f, s.field.y + H - 104.0f};
    }
    if (menu) // the game has the whole screen: the room, invite link and so on are in a menu
    {
        if (s.field.x >= 100)
            s.menu = {s.field.x / 2.0f - 40, 14, 80, 56}; // top left, beside the field
        else if (s.beside)
            s.menu = {W / 2.0f - 40, H + 14.0f, 80, 56}; // under the field
        else
            s.menu = {s.field.x + W / 2.0f - 34, s.field.y + 2.0f, 68, 30}; // on the top wall, between score and room
    }
    g.screen = s;
}

// Screen units for a touch (SDL gives finger positions from 0 to 1 across the window)...
SDL_FPoint touchPoint(const Game &g, float x, float y)
{
    return {x * g.screen.w, y * g.screen.h};
}

// ...and for the mouse (in window coordinates)
SDL_FPoint mousePoint(Game &g, int x, int y)
{
    int ww = 1, wh = 1, pw = 1, ph = 1;
    SDL_GetWindowSize(g.window, &ww, &wh);
    SDL_GetRendererOutputSize(g.renderer, &pw, &ph);
    return {(float)x * (float)pw / (float)std::max(ww, 1) / g.screen.scaleX, (float)y * (float)ph / (float)std::max(wh, 1) / g.screen.scaleY};
}

// Where a point on the screen is on the field
SDL_FPoint onField(const Game &g, SDL_FPoint p)
{
    return {p.x - (float)g.screen.field.x, p.y - (float)g.screen.field.y};
}

bool inside(const SDL_FRect &r, SDL_FPoint p)
{
    return r.w > 0 && p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h;
}

// The thumb stick (if any) that a thumb coming down at p picks up
Stick *stickAt(Game &g, SDL_FPoint p)
{
    const Screen &s = g.screen;
    if (!s.sticks)
        return nullptr;
    if (s.beside)
        return inside(s.moveZone, p) ? &g.move : inside(s.fireZone, p) ? &g.fire : nullptr;
    if (getDistance(p.x, p.y, s.moveHome.x, s.moveHome.y) < s.radius * 1.6f)
        return &g.move;
    if (getDistance(p.x, p.y, s.fireHome.x, s.fireHome.y) < s.radius * 1.6f)
        return &g.fire;
    return nullptr;
}

void pushStick(Game &g, Stick &stick, SDL_FPoint p)
{
    float r = g.screen.radius, dx = p.x - stick.base.x, dy = p.y - stick.base.y, d = std::hypot(dx, dy);
    if (d > r)
    {
        dx *= r / d;
        dy *= r / d;
    }
    stick.knob = {stick.base.x + dx, stick.base.y + dy};
    stick.travel = std::max(stick.travel, std::min(d, r) / r);
}

void grabStick(Game &g, Stick &stick, SDL_FingerID finger, SDL_FPoint p)
{
    const Screen &s = g.screen;
    stick.held = true;
    stick.finger = finger;
    stick.heldFor = 0;
    stick.travel = 0;
    stick.base = &stick == &g.move ? s.moveHome : s.fireHome;
    if (s.beside) // it comes to the thumb, as long as it still fits in its space
    {
        const SDL_FRect &zone = &stick == &g.move ? s.moveZone : s.fireZone;
        auto fit = [&](float v, float start, float length) {
            return length > 2 * s.radius ? std::clamp(v, start + s.radius, start + length - s.radius) : start + length / 2;
        };
        stick.base = {fit(p.x, zone.x, zone.w), fit(p.y, zone.y, zone.h)};
    }
    pushStick(g, stick, p);
}

void letGoOfEverything(Game &g)
{
    g.move.held = g.fire.held = g.dragging = false;
    g.fireCooldown = 0;
}

void openMenu(Game &g)
{
    letGoOfEverything(g); // the menu covers the game: fingers lifted there never reach it
#ifdef __EMSCRIPTEN__
    js_open_menu();
#endif
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

// Helper: Load the red knight in another suit of armor (see ARMOR)
SDL_Texture *loadEnemy(SDL_Renderer *renderer, const std::string &path, const Armor &armor)
{
    SDL_Surface *loaded = SDL_LoadBMP(path.c_str());
    if (!loaded)
    {
        SDL_Log("Failed to load BMP: %s", SDL_GetError());
        return nullptr;
    }
    SDL_Surface *surface = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(loaded);
    if (!surface)
        return nullptr;
    if (armor.body.a > 0 && SDL_LockSurface(surface) == 0)
    {
        for (int y = 0; y < surface->h; y++)
        {
            Uint32 *row = (Uint32 *)((Uint8 *)surface->pixels + (size_t)y * (size_t)surface->pitch);
            for (int x = 0; x < surface->w; x++)
            {
                Uint8 r, g, b, a;
                SDL_GetRGBA(row[x], surface->format, &r, &g, &b, &a);
                if (r <= 25 || r <= 2 * g || r <= 2 * b)
                    continue; // not the red armor: the sword, the boots, the outline
                float light = r / 191.0f; // 1 for the armor's own red, less for its shading
                bool trim = armor.trim.a > 0 && light < 0.9f;
                SDL_Color color = trim ? armor.trim : armor.body;
                float k = trim ? std::min(1.25f, light / 0.71f) : light;
                auto channel = [k](Uint8 c) { return (Uint8)std::min(255.0f, (float)c * k); };
                row[x] = SDL_MapRGBA(surface->format, channel(color.r), channel(color.g), channel(color.b), a);
            }
        }
        SDL_UnlockSurface(surface);
    }
    SDL_SetColorKey(surface, SDL_TRUE, SDL_MapRGBA(surface->format, 255, 255, 255, 255)); // white is see-through
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
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

// Shapes made of triangles: the thumb sticks, the coins, the warlord's crown
void fillTriangles(SDL_Renderer *renderer, const std::vector<SDL_FPoint> &corners, SDL_Color color)
{
    std::vector<SDL_Vertex> vertices;
    for (SDL_FPoint p : corners)
        vertices.push_back({p, color, {0, 0}});
    SDL_RenderGeometry(renderer, nullptr, vertices.data(), (int)vertices.size(), nullptr, 0);
}

// A disc from radius `inner` (0: a full disc) out to `outer`, squeezed to `wide` of its width
// (a coin seen as it turns)
void fillRing(SDL_Renderer *renderer, SDL_FPoint c, float inner, float outer, SDL_Color color, float wide = 1)
{
    const int sides = std::clamp((int)(outer * 2), 16, 48); // small ones look round with fewer
    const float pi = std::numbers::pi_v<float>;
    std::vector<SDL_FPoint> corners;
    for (int i = 0; i < sides; i++)
    {
        float a = 2 * pi * (float)i / (float)sides, b = 2 * pi * (float)(i + 1) / (float)sides;
        SDL_FPoint outA{c.x + outer * wide * std::cos(a), c.y + outer * std::sin(a)}, outB{c.x + outer * wide * std::cos(b), c.y + outer * std::sin(b)};
        SDL_FPoint inA{c.x + inner * wide * std::cos(a), c.y + inner * std::sin(a)}, inB{c.x + inner * wide * std::cos(b), c.y + inner * std::sin(b)};
        corners.insert(corners.end(), {outA, outB, inA});
        if (inner > 0)
            corners.insert(corners.end(), {inA, outB, inB});
    }
    fillTriangles(renderer, corners, color);
}

// A gold coin, slowly turning. Each one turns at its own moment, so they don't all wink together.
void drawCoin(SDL_Renderer *renderer, Point coin, float seconds)
{
    float r = COIN_SIZE / 2 + 1.5f;
    SDL_FPoint middle{coin.x + COIN_SIZE / 2, coin.y + COIN_SIZE / 2};
    float wide = 0.3f + 0.7f * std::fabs(std::cos(seconds * 2.2f + coin.x * 0.031f + coin.y * 0.017f));
    fillRing(renderer, {middle.x + 1.5f, middle.y + 2}, 0, r, {0, 0, 0, 95}, wide); // its shadow on the floor
    fillRing(renderer, middle, 0, r, {176, 118, 8, 255}, wide);                      // the rim
    fillRing(renderer, middle, 0, r * 0.78f, {255, 208, 40, 255}, wide);             // the face
    fillRing(renderer, middle, r * 0.42f, r * 0.56f, {214, 156, 18, 255}, wide);    // a ring stamped in it
    if (wide > 0.55f)                                                                // and a glint
        fillRing(renderer, {middle.x - r * 0.38f * wide, middle.y - r * 0.38f}, 0, r * 0.2f, {255, 250, 215, 255}, wide);
}

// Your health: a heart and a bar that goes from green to yellow to red as it runs out. `unit` is
// the size of one pixel of the heart.
void drawHealth(SDL_Renderer *renderer, int health, float x, float y, float unit)
{
    static const char *const HEART[6] = {".##.##.", "#######", "#######", ".#####.", "..###..", "...#..."};
    float share = std::clamp((float)health / (float)PLAYER_HEALTH, 0.0f, 1.0f);
    SDL_Color fill = share > 0.6f ? SDL_Color{70, 200, 90, 255} : share > 0.3f ? SDL_Color{240, 200, 40, 255} : SDL_Color{235, 60, 50, 255};
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    for (int shadow = 1; shadow >= 0; shadow--) // its shadow first, like the letters have
    {
        float shift = shadow ? std::max(1.0f, unit / 2) : 0;
        if (shadow)
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 150);
        else
            SDL_SetRenderDrawColor(renderer, 235, 60, 70, 255);
        for (int row = 0; row < 6; row++)
        {
            for (int col = 0; col < 7; col++)
            {
                SDL_FRect dot{x + (float)col * unit + shift, y + (float)row * unit + shift, unit, unit};
                if (HEART[row][col] == '#')
                    SDL_RenderFillRectF(renderer, &dot);
            }
        }
    }
    SDL_FRect frame{x + 9 * unit, y, 50 * unit, 6 * unit};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
    SDL_RenderFillRectF(renderer, &frame);
    SDL_FRect bar{frame.x + unit / 2, frame.y + unit / 2, (frame.w - unit) * share, frame.h - unit};
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, 255);
    SDL_RenderFillRectF(renderer, &bar);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

// Over a bad guy that takes more than one hit: how many it has left. The warlord also wears a crown.
void drawEnemyHealth(SDL_Renderer *renderer, const EnemyState &enemy)
{
    const EnemyType &type = enemyType(enemy.kind);
    if (type.health <= 1)
        return;
    float w = std::max(30.0f, type.size * 0.9f), h = enemy.kind == WARLORD ? 6.0f : 4.0f, middleX = enemy.x + type.size / 2;
    SDL_FRect frame{middleX - w / 2 - 1, enemy.y - h - 7, w + 2, h + 2};
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
    SDL_RenderFillRectF(renderer, &frame);
    SDL_FRect bar{frame.x + 1, frame.y + 1, w * std::clamp((float)enemy.health / (float)type.health, 0.0f, 1.0f), h};
    SDL_SetRenderDrawColor(renderer, 235, 65, 50, 255);
    SDL_RenderFillRectF(renderer, &bar);
    if (enemy.kind == WARLORD)
    {
        float bottom = frame.y - 3, top = bottom - 12, left = middleX - 12;
        fillTriangles(renderer, {{left, bottom}, {left + 2, top + 2}, {left + 9, bottom},              // three points
                                 {middleX - 6, bottom}, {middleX, top - 1}, {middleX + 6, bottom},
                                 {left + 15, bottom}, {left + 22, top + 2}, {left + 24, bottom}},
                      GOLD);
        SDL_FRect band{left, bottom - 4, 24, 5};
        SDL_SetRenderDrawColor(renderer, 205, 150, 0, 255);
        SDL_RenderFillRectF(renderer, &band);
        fillRing(renderer, {middleX, bottom - 1.5f}, 0, 2, {200, 30, 40, 255}); // a ruby
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

// The bad guys a level has, for its banner: "SCOUTS, RED KNIGHTS AND THE WARLORD"
std::string crowdOf(const Level &level)
{
    bool has[ENEMY_KINDS] = {};
    for (const auto *spots : {&level.enemyStarts, &level.enemySpawns})
    {
        for (const EnemySpot &e : *spots)
            has[std::clamp(e.kind, 0, ENEMY_KINDS - 1)] = true;
    }
    if (level.enemySpawns.empty())
        has[RED_KNIGHT] = true; // newcomers come from anywhere, as red knights
    std::vector<std::string> names;
    for (int kind : {SCOUT, RED_KNIGHT, CHAMPION, BRUTE, WARLORD}) // weakest first, the boss last
    {
        if (has[kind])
            names.push_back(CROWD_NAMES[kind]);
    }
    std::string list;
    for (size_t i = 0; i < names.size(); i++)
        list += (i == 0 ? "" : i + 1 == names.size() ? " AND " : ", ") + names[i];
    return list;
}

// Which level a snapshot is in, as a key: "<world>:<level number>:<map>"
std::string levelKey(const Snapshot &world)
{
    return std::to_string((long long)world.epoch) + ":" + std::to_string(world.level) + ":" + std::to_string(world.map);
}

// Somewhere safe to (re)appear: one of the level's entrances, as long as it's away from the bad
// guys and not on top of another player (with everyone starting at the same spot, knights and
// their names would pile up). Otherwise the safest place we can find that's reachable.
//
// When a new level starts, everybody moves at the same moment, and the others are still where
// they were on the old map as far as we know. So then each player takes the entrance that goes
// with their player id instead (and the next one if a bad guy is too close to it).
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
        for (auto &e : world.enemies) // middle to middle
        {
            float half = enemyType(e.kind).size / 2;
            enemyRoom = std::min(enemyRoom, getDistance(spot.x + PLAYER_SIZE / 2, spot.y + PLAYER_SIZE / 2, e.x + half, e.y + half));
        }
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
    g.health = PLAYER_HEALTH; // a fresh start, and every new level, puts you back to full health
    g.protection = SPAWN_PROTECTION;
    g.knockTime = g.hurtFlash = 0;
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

// Would a bullet get from (x0, y0) to (x1, y1) without hitting a wall?
bool clearShot(const Level &level, float x0, float y0, float x1, float y1)
{
    int steps = (int)(getDistance(x0, y0, x1, y1) / 8) + 1;
    for (int i = 1; i <= steps; i++)
    {
        float t = (float)i / (float)steps;
        if (level.wall((int)std::floor((x0 + (x1 - x0) * t) / CELL), (int)std::floor((y0 + (y1 - y0) * t) / CELL)))
            return false;
    }
    return true;
}

// A quick tap on the fire stick: shoot the nearest bad guy (one that no wall is hiding, if
// there is one), or else straight ahead
void shootNearest(Game &g)
{
    const Snapshot &world = g.session->world();
    const Level &level = builtInLevel(world.map);
    float centerX = g.player.x + g.player.w / 2, centerY = g.player.y + g.player.h / 2;
    SDL_FPoint target{};
    float best = -1;
    bool bestInSight = false;
    for (auto &e : world.enemies)
    {
        float half = enemyType(e.kind).size / 2, x = e.x + half, y = e.y + half;
        float d = getDistance(centerX, centerY, x, y);
        bool inSight = clearShot(level, centerX, centerY, x, y);
        if (best < 0 || (inSight && !bestInSight) || (inSight == bestInSight && d < best))
        {
            target = {x, y};
            best = d;
            bestInSight = inSight;
        }
    }
    if (best >= 0)
    {
        shootAt(g, target.x, target.y);
        return;
    }
    float angle = (float)g.face * std::numbers::pi_v<float> / 4; // 0 = north, clockwise
    shootAt(g, centerX + std::sin(angle) * 100, centerY - std::cos(angle) * 100);
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
#else
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE && g.screen.menu.w > 0)
            openMenu(g);
#endif
        bool playing = g.state == GameState::PLAYING && !g.session->spectating();
        bool canRespawn = g.state == GameState::GAME_OVER && g.deadTime > RESPAWN_DELAY;

        // SHOOTING LOGIC: Detect Left Mouse Click
        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT)
        {
            SDL_FPoint p = mousePoint(g, event.button.x, event.button.y), f = onField(g, p);
            if (inside(g.screen.menu, p))
                openMenu(g);
            else if (playing)
                shootAt(g, f.x, f.y);
            else if (canRespawn)
                respawn(g);
        }

        if (event.type == SDL_KEYDOWN && canRespawn && !event.key.repeat && event.key.keysym.sym != SDLK_ESCAPE)
            respawn(g);

        // TOUCH: the thumb sticks; or drag your knight to move it, and tap the field to shoot there
        if (event.type == SDL_FINGERDOWN)
        {
            g.touchControls = true; // a touch screen: show the thumb sticks from now on
            SDL_FPoint p = touchPoint(g, event.tfinger.x, event.tfinger.y), f = onField(g, p);
            float centerX = g.player.x + g.player.w / 2, centerY = g.player.y + g.player.h / 2;
            Stick *stick = playing ? stickAt(g, p) : nullptr;
            if (inside(g.screen.menu, p))
            {
                openMenu(g);
            }
            else if (canRespawn)
            {
                respawn(g);
            }
            else if (stick)
            {
                if (!stick->held)
                    grabStick(g, *stick, event.tfinger.fingerId, p);
            }
            else if (playing && !g.dragging && getDistance(f.x, f.y, centerX, centerY) < 90)
            {
                g.dragging = true;
                g.dragFinger = event.tfinger.fingerId;
                g.dragX = f.x;
                g.dragY = f.y;
            }
            else if (playing)
            {
                shootAt(g, f.x, f.y);
            }
        }
        if (event.type == SDL_FINGERMOTION)
        {
            SDL_FPoint p = touchPoint(g, event.tfinger.x, event.tfinger.y), f = onField(g, p);
            for (Stick *stick : {&g.move, &g.fire})
            {
                if (stick->held && stick->finger == event.tfinger.fingerId)
                    pushStick(g, *stick, p);
            }
            if (g.dragging && event.tfinger.fingerId == g.dragFinger)
            {
                g.dragX = f.x;
                g.dragY = f.y;
            }
        }
        if (event.type == SDL_FINGERUP)
        {
            if (g.fire.held && g.fire.finger == event.tfinger.fingerId)
            {
                if (playing && g.fire.heldFor < 0.3f && g.fire.travel < 0.35f) // a quick tap, not a push
                    shootNearest(g);
                g.fire.held = false;
                g.fireCooldown = 0;
            }
            if (g.move.held && g.move.finger == event.tfinger.fingerId)
                g.move.held = false;
            if (g.dragging && event.tfinger.fingerId == g.dragFinger)
                g.dragging = false;
        }
    }
}

// 2. UPDATE LOGIC
void update(Game &g, float deltaTime)
{
    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
    bool playing = g.state == GameState::PLAYING && !g.session->spectating();
#ifdef __EMSCRIPTEN__
    if (playing && js_covered()) // the page's menu (or its "swipe up" layer) is over the game: you can't play,
        g.protection = std::max(g.protection, 0.5f); // so nothing can hurt you either
#endif

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

        if (g.move.held) // the left thumb stick: the further you push it, the faster you go
        {
            float dx = (g.move.knob.x - g.move.base.x) / g.screen.radius, dy = (g.move.knob.y - g.move.base.y) / g.screen.radius;
            float push = std::hypot(dx, dy);
            if (push > DEAD_ZONE)
            {
                float speed = std::min(1.0f, (push - DEAD_ZONE) / (0.75f - DEAD_ZONE));
                moveX += dx / push * speed;
                moveY += dy / push * speed;
            }
        }

        if (moveX != 0 || moveY != 0)
        {
            // Walk, stopping at walls (and sliding along them). Keys always walk at full speed.
            float length = std::sqrt(moveX * moveX + moveY * moveY);
            float step = PLAYER_SPEED * deltaTime * std::min(1.0f, length);
            Rect me{g.player.x, g.player.y, g.player.w, g.player.h};
            builtInLevel(g.session->world().map).slide(me, moveX / length * step, moveY / length * step);
            g.player.x = me.x;
            g.player.y = me.y;
            g.face = facing(moveX, moveY, g.face);
        }

        if (g.knockTime > 0) // a bad guy's hit pushes us back, slower and slower
        {
            float share = g.knockTime / KNOCKBACK_TIME;
            Rect me{g.player.x, g.player.y, g.player.w, g.player.h};
            builtInLevel(g.session->world().map).slide(me, g.knock.x * share * deltaTime, g.knock.y * share * deltaTime);
            g.player.x = me.x;
            g.player.y = me.y;
            g.knockTime = std::max(0.0f, g.knockTime - deltaTime);
        }

        // The right thumb stick: aim where it points, and keep firing
        g.fireCooldown = std::max(0.0f, g.fireCooldown - deltaTime);
        if (g.fire.held)
        {
            g.fire.heldFor += deltaTime;
            float dx = g.fire.knob.x - g.fire.base.x, dy = g.fire.knob.y - g.fire.base.y, push = std::hypot(dx, dy);
            if (push > g.screen.radius * 0.35f && g.fireCooldown <= 0)
            {
                shootAt(g, g.player.x + g.player.w / 2 + dx / push * 100, g.player.y + g.player.h / 2 + dy / push * 100);
                g.fireCooldown = FIRE_INTERVAL;
            }
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
    g.hurtFlash = std::max(0.0f, g.hurtFlash - deltaTime);

    // Enemy Collision: every player checks their own knight, like the original game did. A bad
    // guy that touches you takes its strength off your health and pushes you back, and you get a
    // moment to get away. When your health runs out, you're out.
    if (playing && g.protection <= 0)
    {
        Rect me{g.player.x, g.player.y, g.player.w, g.player.h};
        for (auto &e : world.enemies)
        {
            const EnemyType &type = enemyType(e.kind);
            if (!checkCollision(me, {e.x, e.y, type.size, type.size}))
                continue;
            g.health = std::max(0, g.health - type.damage);
            g.hurtFlash = HURT_FLASH;
            if (g.health == 0)
            {
                g.state = GameState::GAME_OVER;
                g.deadTime = 0;
                g.finalScore = world.score;
                g.knockTime = 0;
                letGoOfEverything(g);
            }
            else
            {
                g.protection = HIT_PROTECTION;
                float dx = me.x + me.w / 2 - (e.x + type.size / 2), dy = me.y + me.h / 2 - (e.y + type.size / 2);
                float d = std::hypot(dx, dy);
                if (d < 0.01f) // right on top of us: back off the way we came
                {
                    float angle = (float)g.face * std::numbers::pi_v<float> / 4;
                    dx = -std::sin(angle);
                    dy = std::cos(angle);
                    d = 1;
                }
                g.knock = {dx / d * KNOCKBACK_SPEED, dy / d * KNOCKBACK_SPEED};
                g.knockTime = KNOCKBACK_TIME;
            }
            break;
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
    SDL_RenderFillRect(g.renderer, nullptr); // the field (the viewport), not the whole screen
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
void drawBanner(SDL_Renderer *renderer, const std::string &big, const std::string &small, float timeLeft, SDL_Color color,
                const std::string &smaller = "")
{
    color.a = (Uint8)(255 * std::clamp(timeLeft / 0.6f, 0.0f, 1.0f));
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    drawCentered(renderer, big, 205, 6, color);
    drawCentered(renderer, small, 270, 3, {WHITE.r, WHITE.g, WHITE.b, color.a});
    if (!smaller.empty())
        drawCentered(renderer, smaller, 310, 2, {LIGHT.r, LIGHT.g, LIGHT.b, color.a});
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

// 3. RENDERING
// The field, in its own 800 x 600 units (render() has pointed the renderer at it)
void drawField(Game &g)
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

    // Draw Coins (gold, round, turning)
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    float seconds = (float)SDL_GetTicks() / 1000.0f;
    for (const auto &coin : world.coins)
        drawCoin(renderer, coin, seconds);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    // Draw Bullets (Cyan), moved on to where they must be by now (unless that's inside a wall)
    float age = g.session->worldAge();
    SDL_SetRenderDrawColor(renderer, 0, 255, 255, 255);
    for (const auto &bullet : world.bullets)
    {
        SDL_FRect rect{bullet.x + bullet.vx * age, bullet.y + bullet.vy * age, BULLET_SIZE, BULLET_SIZE};
        if (!level.wall((int)std::floor((rect.x + BULLET_SIZE / 2) / CELL), (int)std::floor((rect.y + BULLET_SIZE / 2) / CELL)))
            SDL_RenderFillRectF(renderer, &rect);
    }

    // Draw the bad guys, each one facing the player it is chasing, and flashing when a bullet hits it
    for (const auto &enemy : world.enemies)
    {
        float size = enemyType(enemy.kind).size, middleX = enemy.x + size / 2, middleY = enemy.y + size / 2;
        float targetX = middleX, targetY = middleY + 1, best = 1e9f;
        auto consider = [&](float x, float y) { // a knight's top-left corner
            float d = getDistance(middleX, middleY, x + PLAYER_SIZE / 2, y + PLAYER_SIZE / 2);
            if (d < best)
            {
                best = d;
                targetX = x + PLAYER_SIZE / 2;
                targetY = y + PLAYER_SIZE / 2;
            }
        };
        for (auto &[id, r] : g.remotes)
            consider(r.x, r.y);
        if (playing)
            consider(g.player.x, g.player.y);
        SDL_FRect rect{enemy.x, enemy.y, size, size};
        SDL_Texture *look = g.enemies[(size_t)std::clamp(enemy.kind, 0, ENEMY_KINDS - 1)].look[facing(targetX - middleX, targetY - middleY, SOUTH)];
        drawKnight(renderer, look, rect, {255, 50, 50, 255});
        if (enemy.hurt && look) // drawn again on top, added: it lights up
        {
            SDL_SetTextureBlendMode(look, SDL_BLENDMODE_ADD);
            SDL_RenderCopyF(renderer, look, nullptr, &rect);
            SDL_SetTextureBlendMode(look, SDL_BLENDMODE_BLEND);
        }
    }
    for (const auto &enemy : world.enemies) // their health bars go over everything nearby
        drawEnemyHealth(renderer, enemy);

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

    if (g.hurtFlash > 0) // a bad guy just hit us
    {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 255, 0, 0, (Uint8)(100 * g.hurtFlash / HURT_FLASH));
        SDL_RenderFillRect(renderer, nullptr);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    // Score and health in the top left, room and players in the top right
    bool team = world.players.size() > 1;
    drawTextShadowed(renderer, (team ? "TEAM SCORE " : "SCORE ") + std::to_string(world.score), 12, 12, 3, WHITE);
    if (!g.session->spectating())
        drawHealth(renderer, g.health, 12, 42, 2);
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
    {
        std::string crowd = crowdOf(level), warning = "WATCH OUT FOR " + crowd;
        drawBanner(renderer, "LEVEL " + std::to_string(world.level), level.name, g.levelBanner, GOLD,
                   textWidth(warning, 2) <= W - 24 ? warning : crowd);
    }
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
}

// A thumb stick (fainter while no thumb is on it). The fire stick has a cross-hair on its knob,
// the move stick little arrows round its edge.
void drawStick(Game &g, const Stick &stick, SDL_FPoint home, bool fire)
{
    SDL_Renderer *renderer = g.renderer;
    const Screen &s = g.screen;
    SDL_FPoint base = stick.held ? stick.base : home, knob = stick.held ? stick.knob : home;
    float strength = stick.held ? 1.0f : s.beside ? 0.75f : 0.45f;
    auto shade = [&](SDL_Color c, float alpha) { return SDL_Color{c.r, c.g, c.b, (Uint8)(alpha * strength)}; };
    SDL_Color accent = fire ? GOLD : SDL_Color{230, 235, 240, 255}, dark{30, 22, 4, 255};
    float r = s.radius, knobR = r * 0.42f;

    fillRing(renderer, base, 0, r, shade({0, 0, 0, 0}, 120));
    fillRing(renderer, base, r - 3, r, shade(accent, 170));
    if (!fire)
    {
        for (int i = 0; i < 4; i++)
        {
            float a = (float)i * std::numbers::pi_v<float> / 2, tip = r * 0.86f, back = r * 0.7f, half = r * 0.11f;
            float cx = std::cos(a), sy = std::sin(a);
            fillTriangles(renderer, {{base.x + cx * tip, base.y + sy * tip}, {base.x + cx * back - sy * half, base.y + sy * back + cx * half},
                                     {base.x + cx * back + sy * half, base.y + sy * back - cx * half}},
                          shade(accent, 150));
        }
    }
    fillRing(renderer, knob, 0, knobR, shade(accent, 205));
    if (fire)
    {
        fillRing(renderer, knob, knobR * 0.5f, knobR * 0.62f, shade(dark, 230));
        SDL_SetRenderDrawColor(renderer, dark.r, dark.g, dark.b, (Uint8)(230 * strength));
        SDL_FRect across{knob.x - knobR * 0.85f, knob.y - 1.5f, knobR * 1.7f, 3}, down{knob.x - 1.5f, knob.y - knobR * 0.85f, 3, knobR * 1.7f};
        SDL_RenderFillRectF(renderer, &across);
        SDL_RenderFillRectF(renderer, &down);
    }
}

// The menu button: three bars, and the word if there's room for it
void drawMenuButton(Game &g)
{
    SDL_Renderer *renderer = g.renderer;
    const SDL_FRect &b = g.screen.menu;
    SDL_SetRenderDrawColor(renderer, 20, 26, 33, 215);
    SDL_RenderFillRectF(renderer, &b);
    SDL_SetRenderDrawColor(renderer, 120, 132, 145, 230);
    SDL_RenderDrawRectF(renderer, &b);
    bool roomy = b.h >= 50;
    float barW = roomy ? 34 : 26, barH = roomy ? 5 : 3, gap = roomy ? 9 : 7;
    float top = roomy ? b.y + 9 : b.y + (b.h - (gap * 2 + barH)) / 2;
    SDL_SetRenderDrawColor(renderer, 235, 235, 235, 255);
    for (int i = 0; i < 3; i++)
    {
        SDL_FRect bar{b.x + (b.w - barW) / 2, top + (float)i * gap, barW, barH};
        SDL_RenderFillRectF(renderer, &bar);
    }
    if (roomy)
        drawText(renderer, "MENU", b.x + b.w / 2 - textWidth("MENU", 2) / 2, b.y + b.h - 18, 2, LIGHT);
}

// Everything outside the field, in screen units: the thumb sticks, the menu button, and on a
// phone held upright the score and level in big letters under the field
void drawScreen(Game &g)
{
    SDL_Renderer *renderer = g.renderer;
    const Screen &s = g.screen;
    const Snapshot &world = g.session->world();
    const Level &level = builtInLevel(world.map);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    float below = s.h - (float)(s.field.y + H);
    if (s.beside && below >= 600)
    {
        auto centered = [&](const std::string &text, float y, int scale, SDL_Color color) {
            drawTextShadowed(renderer, text, W / 2.0f - textWidth(text, scale) / 2, y, scale, color);
        };
        centered((world.players.size() > 1 ? "TEAM SCORE " : "SCORE ") + std::to_string(world.score), H + 100.0f, 4, WHITE);
        centered("LEVEL " + std::to_string(world.level) + "  " + level.name, H + 156.0f, 3, LIGHT);
        if (!level.doors.empty())
        {
            int left = world.coinsLeft;
            centered(left > 0 ? std::to_string(left) + (left == 1 ? " COIN" : " COINS") + " TO OPEN THE DOOR" : "THE DOOR IS OPEN", H + 194.0f, 3,
                     left > 0 ? LIGHT : GOLD);
        }
        if (!g.session->spectating())
            drawHealth(renderer, g.health, W / 2.0f - 59 * 4 / 2.0f, H + 236.0f, 4); // heart and bar: 59 heart pixels wide
    }
    if (s.beside && below >= 500)
        drawTextShadowed(renderer, "TURN SIDEWAYS FOR A BIGGER VIEW", W / 2.0f - textWidth("TURN SIDEWAYS FOR A BIGGER VIEW", 2) / 2, s.h - 44, 2,
                         {150, 160, 172, 255});

    if (s.sticks && g.state == GameState::PLAYING && !g.session->spectating())
    {
        drawStick(g, g.move, s.moveHome, false);
        drawStick(g, g.fire, s.fireHome, true);
        if (s.beside)
        {
            drawTextShadowed(renderer, "MOVE", s.moveHome.x - textWidth("MOVE", 2) / 2, s.moveHome.y + s.radius + 14, 2, LIGHT);
            drawTextShadowed(renderer, "FIRE", s.fireHome.x - textWidth("FIRE", 2) / 2, s.fireHome.y + s.radius + 14, 2, LIGHT);
        }
    }
    if (s.menu.w > 0)
        drawMenuButton(g);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

void render(Game &g)
{
    SDL_Renderer *renderer = g.renderer;
    const Screen &s = g.screen;
    SDL_SetRenderDrawColor(renderer, AROUND.r, AROUND.g, AROUND.b, 255);
    SDL_RenderClear(renderer); // all of it: clearing ignores the viewport
    SDL_RenderSetScale(renderer, s.scaleX, s.scaleY);
    SDL_RenderSetViewport(renderer, &s.field);
    drawField(g);
    SDL_RenderSetViewport(renderer, nullptr);
    drawScreen(g);
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

    layOut(g);
    handleEvents(g);
    update(g, deltaTime);
    render(g);
}

#ifdef __EMSCRIPTEN__
void frameCallback(void *game)
{
    frame(*static_cast<Game *>(game));
}

Game *activeGame = nullptr; // for knight_debug_state

// Lets the page (and the automated tests) peek at the game: Module.ccall('knight_debug_state', 'string').
// What the session knows (see debugJson in session_web.cpp), and where things are on the screen:
// "screen" is [field x, field y, screen width, screen height], all in units.
extern "C" EMSCRIPTEN_KEEPALIVE const char *knight_debug_state()
{
    static std::string state;
    if (!activeGame || !activeGame->session)
        return "{}";
    const Screen &s = activeGame->screen;
    char screen[512];
    std::snprintf(screen, sizeof(screen),
                  "\"screen\":[%d,%d,%.2f,%.2f],\"sticks\":%s,\"beside\":%s,\"radius\":%.1f,\"move\":[%.1f,%.1f],"
                  "\"fire\":[%.1f,%.1f],\"menu\":[%.1f,%.1f,%.1f,%.1f],\"health\":%d",
                  s.field.x, s.field.y, s.w, s.h, s.sticks ? "true" : "false", s.beside ? "true" : "false", s.radius,
                  s.moveHome.x, s.moveHome.y, s.fireHome.x, s.fireHome.y, s.menu.x, s.menu.y, s.menu.w, s.menu.h,
                  activeGame->health);
    state = activeGame->session->debugJson();
    state.pop_back(); // its closing brace
    state += std::string(state.size() > 1 ? "," : "") + screen + "}";
    return state.c_str();
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
    Uint32 windowFlags = 0;
#ifndef __EMSCRIPTEN__
    windowFlags = SDL_WINDOW_RESIZABLE; // the field grows with the window (in the browser, the page sizes it)
#endif
    g.window = SDL_CreateWindow("Score: 0", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, windowFlags);
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
        for (int kind = 0; kind < ENEMY_KINDS; kind++)
            g.enemies[(size_t)kind].look[d] = loadEnemy(g.renderer, assets + "RedKnight/RedKnight_" + dir + ".bmp", ARMOR[kind]);
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
#ifdef __EMSCRIPTEN__
    g.touchControls = js_touch_first(); // a phone or tablet: thumb sticks from the start
    activeGame = &g;
#endif
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
    std::vector<Knight *> knights = {&g.white, &g.blue};
    for (Knight &k : g.enemies)
        knights.push_back(&k);
    for (Knight *k : knights)
    {
        for (SDL_Texture *t : k->look)
        {
            if (t)
                SDL_DestroyTexture(t);
        }
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
