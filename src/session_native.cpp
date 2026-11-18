// session_native.cpp - Native builds: play on a LAN server (server/server.cpp) over TCP, or
// solo with the world running right here in the game.
//
// Copyright (C) 2026 Kevin Bell. SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "net.h"
#include "session.h"

namespace
{
double wallClockMs()
{
    using namespace std::chrono;
    return (double)duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// No server: the world lives right here, exactly as it would on the server.
class LocalSession : public Session
{
public:
    LocalSession() : world_(wallClockMs())
    {
        id = world_.addPlayer();
        snap = world_.snapshot(id);
    }

    void update(float dt) override
    {
        accumulator += std::min(dt, 0.25f);
        while (accumulator >= TICK)
        {
            world_.step(TICK);
            accumulator -= TICK;
        }
        snap = world_.snapshot(id);
    }
    void sendPosition(float x, float y) override { world_.setPlayerPosition(id, x, y); }
    void sendBullet(float x, float y, float vx, float vy) override { world_.addBullet(x, y, vx, vy); }

    const Snapshot &world() const override { return snap; }
    float worldAge() const override { return 0; }
    int myId() const override { return id; }
    std::string status() const override { return "SOLO"; }

private:
    World world_;
    Snapshot snap;
    int id = 0;
    float accumulator = 0;
};

// A dedicated server holds the world; we send it our moves and draw what it sends back.
class TcpSession : public Session
{
public:
    TcpSession(int sock, std::string ip) : sock(sock), ip(std::move(ip)) {}
    ~TcpSession() override { close(sock); }

    void update(float dt) override
    {
        age += dt;
        std::string line;
        while (netReadLine(sock, line))
        {
            if (line.rfind("ID ", 0) == 0)
                id = std::atoi(line.c_str() + 3);
            else if (line == "FULL")
                full = true;
            else if (decodeSnapshot(line, snap))
                age = 0;
        }
    }
    void sendPosition(float x, float y) override
    {
        if (!full)
            netSendPosition(sock, x, y);
    }
    void sendBullet(float x, float y, float vx, float vy) override
    {
        if (!full)
            netSendBullet(sock, x, y, vx, vy);
    }

    const Snapshot &world() const override { return snap; }
    float worldAge() const override { return std::min(age, 0.1f); }
    int myId() const override { return id; }
    bool spectating() const override { return full; }
    std::string status() const override
    {
        if (full)
            return "SERVER FULL - WATCHING";
        if (age > 3)
            return "NO SIGNAL FROM " + ip;
        return "SERVER " + ip + "\n" + std::to_string(snap.players.size()) + " PLAYERS";
    }

private:
    int sock;
    std::string ip;
    Snapshot snap;
    int id = 0;
    float age = 0;
    bool full = false;
};
} // namespace

std::unique_ptr<Session> createSession(int argc, char *argv[])
{
    const char *ip = argc > 1 ? argv[1] : "127.0.0.1";
    int sock = netConnect(ip);
    if (sock < 0)
    {
        std::fprintf(stderr, "No server found at %s, playing solo.\n", ip);
        return std::make_unique<LocalSession>();
    }
    return std::make_unique<TcpSession>(sock, ip);
}
