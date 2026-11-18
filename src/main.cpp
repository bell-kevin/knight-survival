// main.cpp - SDL2 Survival Game (Shooting Edition)
// Objective: Collect yellow coins & Shoot red enemies with Left Click.
// Controls: WASD/Arrows to move. Mouse Left-Click to shoot. Esc to Quit.

#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>
#include <cmath>
#include "net.h"
#include <sstream>

const int W = 800, H = 600;

enum class GameState
{
    PLAYING,
    GAME_OVER
};

struct Entity
{
    SDL_FRect rect;
    float speed;
    SDL_Texture *texture;
};

// New struct for Bullets
struct Bullet
{
    SDL_FRect rect;
    float vx, vy; // Velocity components
};

// Helper: Collision detection between two rectangles
bool checkCollision(const SDL_FRect &a, const SDL_FRect &b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

// Helper: Simple distance calculation
float getDistance(float x1, float y1, float x2, float y2)
{
    return std::sqrt(std::pow(x2 - x1, 2) + std::pow(y2 - y1, 2));
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
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, tempSurface);
    SDL_FreeSurface(tempSurface);
    return texture;
}

int main(int argc, char *argv[])
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        SDL_Log("SDL_Init Error: %MM", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("Score: 0", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, 0);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    if (!window || !renderer)
    {
        SDL_Log("Renderer/Window Error: %s", SDL_GetError());
        return 1;
    }

    // --- ASSET LOADING ---
    SDL_Texture *playerTex = loadBMP(renderer, "/workspace/assets/WhiteKnight/WhiteKnight_S.bmp");
    SDL_Texture *blueTex = loadBMP(renderer, "/workspace/assets/BlueKnight/BlueKnight_S.bmp");
    SDL_Texture *enemyTex = loadBMP(renderer, "/workspace/assets/RedKnight/RedKnight_S.bmp");

    std::srand(static_cast<unsigned int>(SDL_GetPerformanceCounter()));

    // Game Entities
    Entity player{{100, 100, 40, 40}, 250.0f, playerTex};
    std::vector<SDL_FRect> coins;
    std::vector<Entity> enemies;
    std::vector<Bullet> bullets; // List to hold active bullets

    for (int i = 0; i < 5; ++i)
    {
        coins.push_back({(float)(std::rand() % (W - 20)), (float)(std::rand() % (H - 20)), 15, 15});
    }

    GameState state = GameState::PLAYING;
    int score = 0;
    Uint32 lastTime = SDL_GetTicks();
    bool running = true;

    int sock = netConnect(argc > 1 ? argv[1] : "127.0.0.1");
    if (sock < 0)
        SDL_Log("No server found, playing solo.");
    float otherX = -100, otherY = -100;
    int myId = 0;

    while (running)
    {
        // 1. INPUT HANDLING
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
                running = false;
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
                running = false;

            // SHOOTING LOGIC: Detect Left Mouse Click
            if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT)
            {
                if (state == GameState::PLAYING)
                {
                    int mx, my;
                    SDL_GetMouseState(&mx, &my); // Get mouse position

                    // Calculate direction from player center to mouse
                    float centerX = player.rect.x + player.rect.w / 2.0f;
                    float centerY = player.rect.y + player.rect.h / 2.0f;
                    float dx = (float)mx - centerX;
                    float dy = (float)my - centerY;
                    float dist = std::sqrt(dx * dx + dy * dy);

                    if (dist > 0)
                    {
                        Bullet newBullet;
                        newBullet.rect = {centerX, centerY, 8, 8}; // Small bullet size
                        // Bullet speed (adjust 600.0f to change how fast bullets fly)
                        newBullet.vx = (dx / dist) * 600.0f;
                        newBullet.vy = (dy / dist) * 600.0f;
                        if (sock < 0)
                        {
                            /* code */
                            bullets.push_back(newBullet);
                        }
                        else
                        {
                            /* code */
                            netSendBullet(sock, newBullet.rect.x, newBullet.rect.y, newBullet.vx, newBullet.vy);
                        }
                    }
                }
            }

            if (event.type == SDL_KEYDOWN && state == GameState::GAME_OVER)
            {
                score = 0;
                state = GameState::PLAYING;
                player.rect.x = 100;
                player.rect.y = 100;
                enemies.clear();
                bullets.clear();
                SDL_SetWindowTitle(window, "Score: 0");
            }
        }

        const Uint8 *keys = SDL_GetKeyboardState(nullptr);

        // 2. UPDATE LOGIC
        Uint32 currentTime = SDL_GetTicks();
        float deltaTime = (currentTime - lastTime) / 1000.0f;
        lastTime = currentTime;

        if (state == GameState::PLAYING)
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

            if (moveX != 0 || moveY != 0)
            {
                float length = std::sqrt(moveX * moveX + moveY * moveY);
                player.rect.x += (moveX / length) * player.speed * deltaTime; // Fixed syntax errors here
                player.rect.y += (moveY / length) * player.speed * deltaTime;
            }

            // Keep player in bounds
            player.rect.x = std::clamp(player.rect.x, 0.0f, (float)W - player.rect.w);
            player.rect.y = std::clamp(player.rect.y, 0.0f, (float)H - player.rect.h);

            if (sock < 0)
            {
                // Bullet Movement & Bounds Check
                for (auto it = bullets.begin(); it != bullets.end();)
                {
                    it->rect.x += it->vx * deltaTime;
                    it->rect.y += it->vy * deltaTime;

                    if (it->rect.x < 0 || it->rect.x > W || it->rect.y < 0 || it->rect.y > H)
                    {
                        it = bullets.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }

                // Bullet vs Enemy Collision
                for (auto b_it = bullets.begin(); b_it != bullets.end();)
                {
                    bool bulletDestroyed = false;
                    for (auto e_it = enemies.begin(); e_it != enemies.end();)
                    {
                        if (checkCollision(b_it->rect, e_it->rect))
                        {
                            e_it = enemies.erase(e_it); // Kill enemy
                            bulletDestroyed = true;
                            score += 10; // Bonus for shooting!
                            SDL_SetWindowTitle(window, ("Score: " + std::to_string(score)).c_str());
                            break;
                        }
                        else
                        {
                            ++e_it;
                        }
                    }

                    if (bulletDestroyed)
                        b_it = bullets.erase(b_it);
                    else
                        ++b_it;
                }

                // Coin Collection
                for (auto it = coins.begin(); it != coins.end();)
                {
                    if (checkCollision(player.rect, *it))
                    {
                        score++;
                        SDL_SetWindowTitle(window, ("Score: " + std::to_string(score)).c_str());
                        it->x = (float)(std::rand() % (W - 20));
                        it->y = (float)(std::rand() % (H - 20));

                        if (score % 3 == 0)
                        {
                            enemies.push_back({{(float)(std::rand() % W), (float)(std::rand() % H), 40, 40}, 80.0f, enemyTex});
                        }
                    }
                    ++it;
                }
            }

            // Enemy AI & Player Collision
            for (auto &enemy : enemies)
            {
                float dist = getDistance(enemy.rect.x, enemy.rect.y, player.rect.x, player.rect.y);
                if (sock < 0 && dist > 0)
                {
                    float dx = (player.rect.x - enemy.rect.x) / dist;
                    float dy = (auto(player.rect.y - enemy.rect.y) / dist);
                    enemy.rect.x += dx * enemy.speed * deltaTime;
                    enemy.rect.y += dy * enemy.speed * deltaTime;
                }

                if (checkCollision(player.rect, enemy.rect))
                {
                    state = GameState::GAME_OVER;
                }
            }
        }

        if (state == GameState::PLAYING)
        {
            /* code */
            netSendPosition(sock, player.rect.x, player.rect.y);
        }
        else
        {
            /* code */
            netSendPosition(sock, -1000, -1000);
        }

        std::string line;
        while (netReadLine(sock, line))
        {
            /* code */
            std::istringstream in(line);
            std::string tag;
            int n = 0, id = 0, newScore = 0;
            float x = 0, y = 0;
            in >> tag;
            if (tag == "ID")
            {
                /* code */
                in >> myId;
            }
            if (tag != "W")
            {
                /* code */
                continue;
            }

            in >> newScore;
            if (newScore != score)
            {
                /* code */
                score = newScore;
                SDL_SetWindowTitle(window, ("Score: " + std::to_string(score)).c_str());
            }
            
            otherX = otherY = -100;
            in >> n;
            while (n-- > 0 && in >> id >> x >> y)
            {
                /* code */
                if (id != myId)
                {
                    /* code */
                    otherX = x;
                    otherY = y;
                }
                
            }

            coins.clear();
            in >> n;
            while (n-- > 0 && in >> x >> y)
            {
                /* code */
                coins.push_back({x, y, 15, 15});
            }
            
            enemies.clear();
            in >> n;
            while (n-- > 0 && in >> x >> y)
            {
                /* code */
                enemies.push_back({{x, y, 40, 40}, 80.0f, enemyTex});
            }

            bullets.clear();
            in >> n;
            while (n-- > 0 && in >> x >> y)
            {
                /* code */
                bullets.push_back({{x, y, 8, 8}, 0, 0});
            }
            
            
            
            
            
        }
        
        
        player.texture = (myId == 2) ? blueTex : playerTex;

        // 3. RENDERING
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderClear(renderer);

        if (state == GameState::PLAYING)
        {
            // Draw Coins (Yellow)
            SDL_SetRenderDrawColor(renderer, 255, 215, 0, 255);
            for (const auto &coin : coins)
            {
                SDL_RenderFillRectF(renderer, &coin);
            }

            // Draw Bullets (White/Cyan)
            SDL_SetRenderDrawColor(renderer, 0, 255, 255, 255);
            for (const auto &bullet : bullets)
            {
                SDL_RenderFillRectF(renderer, &bullet.rect);
            }

            // Draw Enemies (Red Knight)
            for (const auto &enemy : enemies)
            {
                if (enemy.texture)
                    SDL_RenderCopyF(renderer, enemy.texture, nullptr, &enemy.rect);
                else
                {
                    SDL_SetRenderDrawColor(renderer, 255, 50, 50, 255);
                    SDL_RenderFillRectF(renderer, &enemy.rect);
                }
            }

            // Draw Player (White Knight)
            if (player.texture)
                SDL_RenderCopyF(renderer, player.texture, nullptr, &player.rect);
            else
            {
                SDL_SetRenderDrawColor(renderer, 50, 150, 255, 255);
                SDL_RenderFillRectF(renderer, &player.rect);
            }
            SDL_FRect other = {otherX, otherY, player.rect.w, player.rect.h};
            // SDL_SetRenderDrawColor(renderer, 0, 200, 0, 255);
            // SDL_RenderFillRectF(renderer, &other);
            SDL_RenderCopyF(renderer, (myId == 2) ? playerTex : blueTex, nullptr, &other);
        }
        else
        {
            // Game Over screen
            SDL_SetRenderDrawColor(renderer, 255, 0, 0, 100);
            SDL_RenderClear(renderer);
        }

        SDL_RenderPresent(renderer);
    }

    SDL_DestroyTexture(playerTex);
    SDL_DestroyTexture(blueTex);
    SDL_DestroyTexture(enemyTex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}