#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>
#include <poll.h>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>

const int W = 800, H = 600;

struct Rect
{
    float x, y, w, h;
};
struct Bullet
{
    Rect rect;
    float vx, vy;
};
struct Player
{
    Rect rect;
    std::string pending;
};

bool checkCollision(const Rect &a, const Rect &b)
{
    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

// Adds a number to a message, rounded to a whole pixel
void add(std::string &msg, float value)
{
    msg += " " + std::to_string((int)value);
}

int main()
{
    int server = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(9000);

    if (bind(server, (sockaddr *)&addr, sizeof(addr)) < 0)
    {
        std::cerr << "bind failed: is port 9000 already in use?" << std::endl;
        return 1;
    }

    listen(server, 8);
    std::cout << "Server listening on port 9000" << std::endl;
    std::vector<pollfd> fds = {{server, POLLIN, 0}};
    std::vector<int> ids = {0};

    std::vector<Player> players(1); // players[i] goes with fds[i] and ids[i]

    // The game world, shared by everyone
    std::srand(std::time(nullptr));
    std::vector<Rect> coins, enemies;
    std::vector<Bullet> bullets;
    int score = 0;
    for (int i = 0; i < 5; i++)
    {
        coins.push_back({(float)(std::rand() % (W - 20)), (float)(std::rand() % (H - 20)), 15, 15});
    }
    auto lastTick = std::chrono::steady_clock::now();

    while (true)
    {
        poll(fds.data(), fds.size(), 5);
        if (fds[0].revents)
        {
            int client = accept(server, nullptr, nullptr);
            if (client >= 0)
            {
                int id = 1;
                while (std::find(ids.begin(), ids.end(), id) != ids.end())
                    id++;
                fds.push_back({client, POLLIN, 0});
                ids.push_back(id);
                players.push_back({{-1000, -1000, 40, 40}, ""});
                std::string hello = "ID " + std::to_string(id) + "\n";
                send(client, hello.c_str(), hello.size(), MSG_NOSIGNAL);
                std::cout << "Player " << id << " joined (" << fds.size() - 1 << " online)" << std::endl;
            }
        }

        for (size_t i = 1; i < fds.size(); i++)
        {
            if (!fds[i].revents)
            {
                continue;
            }

            char buf[512];
            ssize_t n = read(fds[i].fd, buf, sizeof(buf));
            if (n <= 0)
            {
                close(fds[i].fd);
                fds.erase(fds.begin() + i);
                ids.erase(ids.begin() + i);
                players.erase(players.begin() + i);
                i--;
                std::cout << "Player left (" << fds.size() - 1 << " online)" << std::endl;
                continue;
            }

            // Each line is "x y" (my position) or "B x y vx vy" (I fired)
            std::string &pending = players[i].pending;
            pending.append(buf, n);
            size_t end;
            while ((end = pending.find('\n')) != std::string::npos)
            {
                /* code */
                std::string line = pending.substr(0, end);
                pending.erase(0, end + 1);
                Bullet b{{0, 0, 8, 8}, 0, 0};
                if (std::sscanf(line.c_str(), "B %f %f %f %f", &b.rect.x, &b.rect.y, &b.vx, &b.vy) == 4)
                {
                    /* code */
                    bullets.push_back(b);
                }
                else
                {
                    /* code */
                    std::sscanf(line.c_str(), "%f %f", &players[i].rect.x, &players[i].rect.y);
                }
            }
        }
        // Run the world about 60 times a second
        auto now = std::chrono::steady_clock::now();
        std::chrono::duration<float>(now - lastTick).count();
        float dt = std::chrono::duration<float>(now - lastTick).count();
        if (dt < 1.0f / 60)
            continue;
        lastTick = now;

        // Bullets fly, and dissappear off screen
        for (auto it = bullets.begin(); it != bullets.end();)
        {
            it->rect.x += it->vx * dt;
            it->rect.y += it->vy * dt;
            if (it->rect.x < 0 || it->rect.x > W || it->rect.y < 0 || it->rect.y > H)
                it = bullets.erase(it);
            else
                ++it;
        }

        // A bullet that touches an enemy removes both: +10 points
        for (auto b = bullets.begin(); b != bullets.end();)
        {
            /* code */
            auto e = enemies.begin();
            while (e != enemies.end() && !checkCollision(b->rect, *e))
                ++e;
            if (e != enemies.end())
            {
                /* code */
                enemies.erase(e);
                b = bullets.erase(b);
                score += 10;
            }
            else
            {
                /* code */
                ++b;
            }
        }

        // A player who touches a coin gets a point. Every 5 points brings an enemy.
        for (size_t i = 1; i < players.size(); i++)
        {
            /* code */
            for (auto &coin : coins)
            {
                /* code */
                if (checkCollision(players[i].rect, coin))
                {
                    /* code */
                    score++;
                    coin.x = (float)(std::rand() % (W - 20));
                    coin.y = (float)(std::rand() % (H - 20));
                    if (score % 5 == 0)
                    {
                        /* code */
                        enemies.push_back({(float)(std::rand() % W), (float)(std::rand() % H), 40, 40});
                    }
                }
            }
        }

        // Enemies walk toward the nearest player who's alive
        for (auto &enemy : enemies)
        {
            Rect *target = nullptr;
            float best = 0;
            for (size_t i = 1; i < players.size(); i++)
            {
                Rect &p = players[i].rect;
                float d = std::hypot(p.x - enemy.x, p.y - enemy.y);
                if (p.x >= 0 && (!target || d < best))
                {
                    target = &p;
                    best = d;
                }
            }
            if (target && best > 0)
            {
                /* code */
                enemy.x += (target->x - enemy.x) / best * 80.0f * dt;
                enemy.y += (target->y - enemy.y) / best * 80.0f * dt;
            }
        }

        // If nobody is alive, start the world over
        bool anyoneAlive = false;
        for (size_t i = 1; i < players.size(); i++)
        {
            /* code */
            if (players[i].rect.x >= 0)
            {
                /* code */
                anyoneAlive = true;
            }
            if (!anyoneAlive)
            {
                /* code */
                score = 0;
                enemies.clear();
                bullets.clear();
            }
        }

        // Send the whole world to everyone as one line
        std::string msg = "W";
        add(msg, score);

        add(msg, players.size() - 1);
        for (size_t i = 1; i < players.size(); i++)
        {
            /* code */
            add(msg, ids[i]);
            add(msg, players[i].rect.x);
            add(msg, players[i].rect.y);
        }

        add(msg, coins.size());
        for (auto &c : coins)
        {
            /* code */
            add(msg, c.x);
            add(msg, c.y);
        }

        add(msg, enemies.size());
        for (auto &e : enemies)
        {
            /* code */
            add(msg, e.x);
            add(msg, e.y);
        }

        add(msg, bullets.size());
        for (auto &b : bullets)
        {
            /* code */
            add(msg, b.rect.x);
            add(msg, b.rect.y);
        }
        msg += "\n";
        for (size_t i = 1; i < fds.size(); i++)
        {
            /* code */
            send(fds[i].fd, msg.c_str(), msg.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
        }
    }
}