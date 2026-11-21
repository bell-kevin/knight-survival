// session_web.cpp - Online play in the browser, peer to peer.
//
// GitHub Pages can only serve files, so it can't run server/server.cpp. Instead the players'
// browsers connect straight to each other (WebRTC, see web/net.js) and one of them, the host,
// runs the same World that the server runs. Everybody else does exactly what the native client
// does with the native server: send the host their position and bullets, and draw the whole
// world the host sends back (30 times a second).
//
// Who hosts?
//   * Everyone starts out hosting a brand-new world, so you can play the moment you arrive.
//   * When two hosts meet, the older world wins and the other host joins it as a guest. That's
//     how newcomers end up in the game that's already going.
//   * When the host goes quiet (it left, or its tab is hidden and the browser paused it), the
//     remaining player with the lowest id takes over, continuing the world from the last
//     snapshot, so the score survives. Each hand-over bumps the world's "generation".
//   * Within the same world the newest generation wins, so a host that comes back from a hidden
//     tab sees that the game moved on without it and simply joins as a guest.
//
// Messages are text, like the original protocol:
//   guest -> host:  "x y" (my position), "B x y vx vy" (I fired), "H <epoch> <id>" (hello)
//   host -> guest:  "ID n" (your player id), "FULL" (no room for you), "W ..." (the world)
//
// Copyright (C) 2026 Kevin Bell. SPDX-License-Identifier: GPL-3.0-or-later
#include <emscripten.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "session.h"

// ---- The bridge to web/net.js: the only places this game touches JavaScript ----------------
EM_JS(void, js_join, (const char *room), { KnightNet.join(UTF8ToString(room)); });
EM_JS(char *, js_poll, (), {
    const event = KnightNet.poll();
    return event === undefined ? 0 : stringToNewUTF8(event);
});
EM_JS(void, js_send, (const char *peer, const char *message), {
    KnightNet.send(UTF8ToString(peer), UTF8ToString(message));
});
EM_JS(char *, js_self_id, (), { return stringToNewUTF8(KnightNet.selfId()); });
EM_JS(double, js_wall_clock, (), { return Date.now(); });

namespace
{
const float SEND_INTERVAL = 1.0f / 30;   // how often the host sends the world / guests their position
const double HOST_TIMEOUT_MS = 3000;     // no world from the host for this long: it's gone
const double SEARCH_TIMEOUT_MS = 2500;   // no new host showed up for this long: host it ourselves
const double RECLAIM_TIMEOUT_MS = 6000;  // players carried over to a new host must check in by then
const double AWAY_MS = 2000;             // a guest silent this long is away: take their knight out
const double GONE_MS = 60000;            // ...and this long: free their place in the world

double nowMs() { return emscripten_get_now(); }

std::string takeString(char *s) // copies a string made by stringToNewUTF8 and frees it
{
    std::string out = s ? s : "";
    std::free(s);
    return out;
}

void send(const std::string &peer, const std::string &message) { js_send(peer.c_str(), message.c_str()); }

// Who runs a world: when two hosts meet, the better one stays host.
struct Rank
{
    double epoch;
    int generation;
    int hostId;
    std::string peer;

    bool betterThan(const Rank &other) const
    {
        if (epoch != other.epoch)
            return epoch < other.epoch;           // the older world wins
        if (generation != other.generation)
            return generation > other.generation; // then its newest host
        if (hostId != other.hostId)
            return hostId < other.hostId;         // then the lower player id
        return peer < other.peer;                 // and as a last resort, any fixed order will do
    }
};

class P2PSession : public Session
{
public:
    explicit P2PSession(std::string roomName) : room(std::move(roomName))
    {
        js_join(room.c_str());
        hostNewWorld();
    }

    void update(float dt) override
    {
        pumpEvents();
        double now = nowMs();
        if (hosting())
        {
            accumulator += std::min(dt, 0.25f);
            while (accumulator >= TICK)
            {
                world_->step(TICK);
                accumulator -= TICK;
            }
            forgetQuietPlayers(now);
            snap = world_->snapshot(myPid);
            snap.generation = generation;
            snapTime = now;
            sendTimer += dt;
            if (sendTimer >= SEND_INTERVAL)
            {
                sendTimer = 0;
                if (!peers.empty())
                    send("", encodeSnapshot(snap)); // everyone in the room, guests or not
            }
            return;
        }

        if (!hostPeer.empty() && now - lastFromHost > HOST_TIMEOUT_MS)
            hostLost();
        if (hostPeer.empty())
        {
            if (!hosting() && now - searchingSince > SEARCH_TIMEOUT_MS)
                takeOver();
            return;
        }
        if (full)
        {
            if (now - lastHello > 3000)
                sayHello(); // maybe somebody left and there's room now
            return;
        }
        sendTimer += dt;
        if (sendTimer >= SEND_INTERVAL)
        {
            sendTimer = 0;
            char msg[64];
            std::snprintf(msg, sizeof(msg), "%.1f %.1f", myX, myY);
            send(hostPeer, msg);
        }
    }

    void sendPosition(float x, float y) override
    {
        myX = x;
        myY = y;
        if (hosting())
            world_->setPlayerPosition(myPid, x, y);
    }

    void sendBullet(float x, float y, float vx, float vy) override
    {
        shots++;
        if (hosting())
        {
            world_->addBullet(x, y, vx, vy);
        }
        else if (!hostPeer.empty() && !full)
        {
            char msg[128];
            std::snprintf(msg, sizeof(msg), "B %.1f %.1f %.1f %.1f", x, y, vx, vy);
            send(hostPeer, msg);
        }
    }

    const Snapshot &world() const override { return snap; }
    float worldAge() const override { return hosting() ? 0 : std::min((float)(nowMs() - snapTime) / 1000, 0.1f); }
    int myId() const override { return myPid; }
    bool spectating() const override { return full; }

    std::string status() const override
    {
        std::string lines = "ROOM " + room + "\n";
        if (!hosting() && hostPeer.empty())
            return lines + "HOST LEFT - RECONNECTING";
        if (full)
            return lines + "ROOM FULL - WATCHING";
        if (snap.players.size() <= 1)
            return lines + "WAITING FOR PLAYERS";
        return lines + std::to_string(snap.players.size()) + " PLAYERS";
    }

    // For tests and the curious: what this browser thinks is going on (see knight_debug_state in main.cpp).
    std::string debugJson() const override
    {
        auto points = [](const std::vector<Point> &list) {
            std::string out = "[";
            for (auto &p : list)
                out += (out.size() > 1 ? ",[" : "[") + std::to_string((int)p.x) + "," + std::to_string((int)p.y) + "]";
            return out + "]";
        };
        std::string enemies = "["; // [x, y, kind, health, size] each
        for (auto &e : snap.enemies)
        {
            enemies += (enemies.size() > 1 ? ",[" : "[") + std::to_string((int)e.x) + "," + std::to_string((int)e.y) + "," +
                       std::to_string(e.kind) + "," + std::to_string(e.health) + "," + std::to_string((int)enemyType(e.kind).size) + "]";
        }
        enemies += "]";
        int alive = (int)std::count_if(snap.players.begin(), snap.players.end(), [](const PlayerState &p) { return p.x >= 0; });
        char head[512];
        std::snprintf(head, sizeof(head),
                      "{\"role\":\"%s\",\"myId\":%d,\"epoch\":%.0f,\"gen\":%d,\"hostId\":%d,\"hostPeer\":\"%s\","
                      "\"peers\":%d,\"players\":%d,\"alive\":%d,\"score\":%d,\"bullets\":%d,\"shots\":%d,\"full\":%s,"
                      "\"me\":[%d,%d],\"level\":%d,\"map\":%d,\"coinsLeft\":%d,",
                      hosting() ? "host" : (hostPeer.empty() ? "searching" : "guest"), myPid, snap.epoch,
                      snap.generation, snap.hostId, hostPeer.c_str(), (int)peers.size(), (int)snap.players.size(),
                      alive, snap.score, (int)snap.bullets.size(), shots, full ? "true" : "false", (int)myX, (int)myY,
                      snap.level, snap.map, snap.coinsLeft);

        // The map, so a test can find its way: one string per row, '#' for wall, '.' for floor
        const Level &level = builtInLevel(snap.map);
        std::string walls = "[", doors = "[";
        for (int row = 0; row < level.rows; row++)
        {
            walls += row ? ",\"" : "\"";
            for (int col = 0; col < level.cols; col++)
                walls += level.wall(col, row) ? '#' : '.';
            walls += '"';
        }
        for (const Door &d : level.doors)
        {
            doors += doors.size() > 1 ? ",[" : "[";
            doors += std::to_string((int)d.area.x) + "," + std::to_string((int)d.area.y) + "," + std::to_string((int)d.area.w) +
                     "," + std::to_string((int)d.area.h) + "]";
        }
        return std::string(head) + "\"coins\":" + points(snap.coins) + ",\"enemies\":" + enemies +
               ",\"walls\":" + walls + "],\"doors\":" + doors + "]}";
    }

private:
    bool hosting() const { return world_ != nullptr; }

    void pumpEvents()
    {
        while (char *raw = js_poll())
        {
            std::string event = takeString(raw);
            if (event.size() < 3)
                continue;
            size_t space = event.find(' ', 2);
            std::string peer = event.substr(2, space == std::string::npos ? std::string::npos : space - 2);
            if (peer.empty())
                continue;
            if (event[0] == 'J')
                peers.insert(peer);
            else if (event[0] == 'L')
                peerLeft(peer);
            else if (event[0] == 'M' && space != std::string::npos)
                handleMessage(peer, event.substr(space + 1));
        }
    }

    void handleMessage(const std::string &peer, const std::string &msg)
    {
        if (msg.empty())
            return;
        peers.insert(peer); // a message can beat the "connected" event
        if (msg[0] == 'W')
        {
            Snapshot s;
            if (decodeSnapshot(msg, s))
                worldArrived(peer, s);
            return;
        }

        if (!hosting())
        {
            if (peer != hostPeer)
                return;
            if (msg.rfind("ID ", 0) == 0)
            {
                myPid = std::atoi(msg.c_str() + 3);
                full = false;
            }
            else if (msg == "FULL")
            {
                full = true;
                myPid = 0;
            }
            return;
        }

        // We're the host: hellos, positions and bullets from our guests
        lastHeard[peer] = nowMs();
        if (msg[0] == 'H')
        {
            double epoch = 0;
            int pid = 0;
            if (std::sscanf(msg.c_str(), "H %lf %d", &epoch, &pid) == 2)
                welcome(peer, epoch, pid);
            return;
        }
        auto guest = guests.find(peer);
        if (guest == guests.end())
        {
            welcome(peer, 0, 0); // they said hello to a previous host, or were away a long time
            guest = guests.find(peer);
            if (guest == guests.end())
                return; // no room for them
        }
        float a, b, c, d;
        if (msg[0] == 'B')
        {
            if (std::sscanf(msg.c_str(), "B %f %f %f %f", &a, &b, &c, &d) == 4)
                world_->addBullet(a, b, c, d);
        }
        else if (std::sscanf(msg.c_str(), "%f %f", &a, &b) == 2)
        {
            world_->setPlayerPosition(guest->second, a, b);
        }
    }

    Rank myRank() const { return {world_->epoch(), generation, myPid, takeString(js_self_id())}; }
    Rank hostRank() const { return {hostEpoch, snap.generation, snap.hostId, hostPeer}; }

    // A peer sent us its world. Is it a better one than ours?
    void worldArrived(const std::string &peer, const Snapshot &s)
    {
        Rank theirs{s.epoch, s.generation, s.hostId, peer};
        if (hosting())
        {
            if (theirs.betterThan(myRank()))
                joinHost(peer, s);
            return;
        }
        if (peer == hostPeer)
        {
            if (s.epoch == hostEpoch)
            {
                accept(s);
                return;
            }
            hostLost(); // our host switched to some other world; we carry on with ours
            worldArrived(peer, s);
            return;
        }
        bool better = hostPeer.empty()
                          // looking for our world's next host: anyone running it (or an older world)
                          ? s.epoch < hostEpoch || (s.epoch == hostEpoch && s.generation >= snap.generation)
                          : theirs.betterThan(hostRank());
        if (better)
            joinHost(peer, s);
    }

    void joinHost(const std::string &peer, const Snapshot &s)
    {
        double currentEpoch = hosting() ? world_->epoch() : hostEpoch;
        if (s.epoch != currentEpoch)
            myPid = 0; // a different world: we'll get a new player id
        world_.reset();
        guests.clear();
        unclaimed.clear();
        lastHeard.clear();
        hostPeer = peer;
        hostEpoch = s.epoch;
        full = false;
        accept(s);
        sayHello();
    }

    void sayHello()
    {
        char msg[64];
        std::snprintf(msg, sizeof(msg), "H %.0f %d", hostEpoch, myPid);
        send(hostPeer, msg);
        lastHello = nowMs();
    }

    void accept(const Snapshot &s)
    {
        snap = s;
        snapTime = lastFromHost = nowMs();
    }

    // The host gives a guest a player id (their old one, if they're coming back after a host change).
    void welcome(const std::string &peer, double epoch, int pid)
    {
        int id = 0;
        auto known = guests.find(peer);
        if (known != guests.end())
        {
            id = known->second;
        }
        else
        {
            if (epoch == world_->epoch() && unclaimed.count(pid))
            {
                id = pid;
                unclaimed.erase(pid);
            }
            else
            {
                id = world_->addPlayer();
            }
            if (id == 0)
            {
                send(peer, "FULL");
                return;
            }
            guests[peer] = id;
        }
        lastHeard[peer] = nowMs();
        send(peer, "ID " + std::to_string(id));
    }

    // Guests we haven't heard from in a while (a hidden tab, a lost connection): take their knight
    // out of the world so it isn't a frozen target, and after a long time give their place away.
    // Players carried over from the previous host who never checked in are dropped too.
    void forgetQuietPlayers(double now)
    {
        for (auto it = guests.begin(); it != guests.end();)
        {
            double quiet = now - lastHeard[it->first];
            if (quiet > GONE_MS)
            {
                world_->removePlayer(it->second);
                lastHeard.erase(it->first);
                it = guests.erase(it);
                continue;
            }
            if (quiet > AWAY_MS)
                world_->setPlayerPosition(it->second, DEAD, DEAD);
            ++it;
        }
        for (auto it = unclaimed.begin(); it != unclaimed.end();)
        {
            if (now > it->second)
            {
                world_->removePlayer(it->first);
                it = unclaimed.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void peerLeft(const std::string &peer)
    {
        peers.erase(peer);
        if (hosting())
        {
            auto guest = guests.find(peer);
            if (guest != guests.end())
            {
                world_->removePlayer(guest->second);
                guests.erase(guest);
            }
            lastHeard.erase(peer);
        }
        else if (peer == hostPeer)
        {
            hostLost();
        }
    }

    void hostLost()
    {
        hostPeer.clear();
        searchingSince = nowMs();
        // Everyone who saw the same last snapshot picks the same successor: the remaining player
        // with the lowest id. Everybody else waits for the successor's world to show up.
        int next = INT_MAX;
        for (auto &p : snap.players)
        {
            if (p.id != snap.hostId)
                next = std::min(next, p.id);
        }
        if (myPid > 0 && myPid == next)
            takeOver();
    }

    // Become the host, continuing the world from the last snapshot we got.
    void takeOver()
    {
        world_ = std::make_unique<World>(snap.epoch);
        world_->seed(snap);
        world_->removePlayer(snap.hostId); // the old host is gone
        generation = snap.generation + 1;
        hostPeer.clear();
        guests.clear();
        unclaimed.clear();
        lastHeard.clear();
        full = false;
        if (myPid == 0 || !world_->hasPlayer(myPid))
            myPid = world_->addPlayer();
        double deadline = nowMs() + RECLAIM_TIMEOUT_MS;
        for (auto &p : snap.players)
        {
            if (p.id != myPid && world_->hasPlayer(p.id))
                unclaimed[p.id] = deadline;
        }
        accumulator = 0;
        sendTimer = SEND_INTERVAL; // tell everyone right away
    }

    void hostNewWorld()
    {
        world_ = std::make_unique<World>(js_wall_clock());
        generation = 0;
        hostPeer.clear();
        guests.clear();
        unclaimed.clear();
        lastHeard.clear();
        full = false;
        myPid = world_->addPlayer();
        accumulator = 0;
        sendTimer = SEND_INTERVAL;
        snap = world_->snapshot(myPid);
        snapTime = nowMs();
    }

    std::string room;
    std::set<std::string> peers; // everyone we're connected to
    Snapshot snap;               // the world as we last saw it
    double snapTime = 0;
    int myPid = 0;
    float myX = DEAD, myY = DEAD;
    float sendTimer = 0;
    int shots = 0; // how many times we fired (for the tests)

    // While hosting
    std::unique_ptr<World> world_;
    int generation = 0;
    float accumulator = 0;
    std::map<std::string, int> guests;       // peer -> player id
    std::map<std::string, double> lastHeard; // peer -> when they last sent us anything
    std::map<int, double> unclaimed;         // player id -> when we stop waiting for them

    // While a guest
    std::string hostPeer; // "" while looking for a new host
    double hostEpoch = 0;
    double lastFromHost = 0, searchingSince = 0, lastHello = 0;
    bool full = false;
};

} // namespace

std::unique_ptr<Session> createSession(int argc, char *argv[])
{
    std::string room = "lobby";
    for (int i = 1; i + 1 < argc; i++)
    {
        if (std::string(argv[i]) == "--room")
            room = argv[i + 1];
    }
    // Room names travel in links; keep them to letters, digits and dashes.
    std::erase_if(room, [](char c) { return !std::isalnum((unsigned char)c) && c != '-'; });
    std::transform(room.begin(), room.end(), room.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    if (room.empty() || room.size() > 32)
        room = "lobby";
    return std::make_unique<P2PSession>(room);
}
