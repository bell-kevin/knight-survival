// server.cpp - Dedicated game server for playing over a LAN with the native build.
//
// It runs the shared World (src/world.cpp) and talks to every client over TCP port 9000,
// one line of text per message:
//   client -> server: "x y" (my position) or "B x y vx vy" (I fired)
//   server -> client: "ID n" (your player id), then 60 times a second "W ..." (the whole world)
//
// (The web version doesn't need this: there, one of the players' browsers runs the World.)
//
// Copyright (C) 2026 Kevin Bell. SPDX-License-Identifier: GPL-3.0-or-later
#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "world.h"

struct Client
{
    int fd;
    int id;
    std::string pending; // what they sent that isn't a whole line yet
    std::string outbox;  // what we still have to send them
};

double wallClockMs()
{
    using namespace std::chrono;
    return (double)duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// Sends as much of the outbox as the connection takes right now. Never waits.
void flush(Client &c)
{
    while (!c.outbox.empty())
    {
        ssize_t n = send(c.fd, c.outbox.data(), c.outbox.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
        if (n <= 0)
            return;
        c.outbox.erase(0, (size_t)n);
    }
}

void queueLine(Client &c, const std::string &line)
{
    if (c.outbox.size() > 64 * 1024)
        return; // they're not keeping up; skip a frame rather than cut a line in half
    c.outbox += line;
    c.outbox += '\n';
    flush(c);
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
    std::vector<Client> clients; // clients[i] goes with fds[i + 1]

    // The game world, shared by everyone
    World world(wallClockMs());
    auto lastTick = std::chrono::steady_clock::now();

    while (true)
    {
        poll(fds.data(), fds.size(), 5);
        if (fds[0].revents & POLLIN)
        {
            int fd = accept(server, nullptr, nullptr);
            if (fd >= 0)
            {
                int id = world.addPlayer();
                if (id == 0)
                {
                    send(fd, "FULL\n", 5, MSG_NOSIGNAL);
                    close(fd);
                }
                else
                {
                    fds.push_back({fd, POLLIN, 0});
                    clients.push_back({fd, id, "", ""});
                    queueLine(clients.back(), "ID " + std::to_string(id));
                    std::cout << "Player " << id << " joined (" << clients.size() << " online)" << std::endl;
                }
            }
        }

        for (size_t i = 0; i < clients.size(); i++)
        {
            if (!fds[i + 1].revents)
                continue;

            char buf[512];
            ssize_t n = read(clients[i].fd, buf, sizeof(buf));
            if (n <= 0)
            {
                close(clients[i].fd);
                world.removePlayer(clients[i].id);
                std::cout << "Player " << clients[i].id << " left (" << clients.size() - 1 << " online)" << std::endl;
                fds.erase(fds.begin() + (long)i + 1);
                clients.erase(clients.begin() + (long)i);
                i--;
                continue;
            }

            // Each line is "x y" (my position) or "B x y vx vy" (I fired)
            std::string &pending = clients[i].pending;
            pending.append(buf, (size_t)n);
            size_t end;
            while ((end = pending.find('\n')) != std::string::npos)
            {
                std::string line = pending.substr(0, end);
                pending.erase(0, end + 1);
                float x, y, vx, vy;
                if (std::sscanf(line.c_str(), "B %f %f %f %f", &x, &y, &vx, &vy) == 4)
                    world.addBullet(x, y, vx, vy);
                else if (std::sscanf(line.c_str(), "%f %f", &x, &y) == 2)
                    world.setPlayerPosition(clients[i].id, x, y);
            }
            if (pending.size() > 4096)
                pending.clear(); // nobody sends lines that long
        }

        // Run the world about 60 times a second
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - lastTick).count();
        if (dt < TICK)
            continue;
        lastTick = now;
        world.step(std::min(dt, 0.1f));

        // Send the whole world to everyone as one line
        std::string msg = encodeSnapshot(world.snapshot(0));
        for (auto &c : clients)
            queueLine(c, msg);
    }
}
