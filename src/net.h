#pragma once
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdio>
#include <string>

inline int netConnect(const char *ip)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9000);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1 || connect(sock, (sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(sock);
        return -1;
    }
    return sock;
}

// Sends our position as one line, like "412.5 300.0". Never waits.
inline void netSendPosition(int sock, float x, float y)
{
    if (sock < 0)
        return;
    char msg[64];
    int len = std::snprintf(msg, sizeof(msg), "%1f %.1f\n", x, y);
    send(sock, msg, len, MSG_DONTWAIT | MSG_NOSIGNAL);
}

// Tells the server we fired, like "B 120.0 120.0 600.0 0.0". Never waits.
inline void netSendBullet(int sock, float x, float y, float vx, float vy) {
    if (sock < 0) return;
    char msg[128];
    int len = std::snprintf(msg, sizeof(msg), "B %.1f %.1f %.1f %.1f\n", x, y, vx, vy);
    send(sock, msg, len, MSG_DONTWAIT | MSG_NOSIGNAL);
}

// Gets the next full line from the server. Never waits. 
// Returns false when no full line has arrived yet.
inline bool netReadLine(int sock, std::string& line) {
    if (sock < 0) return false;
    static std::string pending;
    char buf[4096];
    ssize_t n;
    while ((n = recv(sock, buf, sizeof(buf), MSG_DONTWAIT)) > 0 )
    {
        /* code */
        pending.append(buf, n);
    }
    size_t end = pending.find('\n');
    if (end == std::string::npos)
    {
        /* code */
        return false;
    }
    line = pending.substr(0, end);
    pending.erase(0, end + 1);
    return true;
    
    
}