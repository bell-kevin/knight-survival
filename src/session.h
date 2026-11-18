// session.h - How the game talks to the world, wherever the world lives:
//   * native: a dedicated server over TCP (server/server.cpp), or a local world when playing solo
//     (session_native.cpp)
//   * browser: peer to peer with the other players in a room (session_web.cpp)
//
// Copyright (C) 2026 Kevin Bell. SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <string>

#include "world.h"

class Session
{
public:
    virtual ~Session() = default;

    virtual void update(float dt) = 0;               // call once per frame
    virtual void sendPosition(float x, float y) = 0; // where we are (x < 0 means dead)
    virtual void sendBullet(float x, float y, float vx, float vy) = 0;

    virtual const Snapshot &world() const = 0; // the latest state of the world we know of
    virtual float worldAge() const = 0;        // seconds since that state was current
    virtual int myId() const = 0;              // our player id, 0 until the host gives us one
    virtual bool spectating() const { return false; } // the room is full: we can only watch
    virtual std::string status() const = 0;    // a few words for the corner of the screen
};

std::unique_ptr<Session> createSession(int argc, char *argv[]);
