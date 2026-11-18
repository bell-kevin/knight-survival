#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>
#include <string>
#include <poll.h>

int main(int argc, char* argv[]) {
    const char* ip = (argc > 1) ? argv[1] : "127.0.0.1";

    int sock = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9000);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        std::cerr << "not a valid IP address: " << ip << std::endl;
        return 1;
    }

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0 )
    {
        std::cerr << "connect failed: is the server running?" << std::endl;
        return 1;
    }
    std::cout << "Connected to " << ip << ". Type a message and press Enter." << std::endl;

    pollfd fds[2] = {{0, POLLIN, 0}, {sock, POLLIN, 0}};
    char buf[512];

    while (true)
    {
        poll(fds, 2, -1);
        if (fds[0].revents)
        {
            ssize_t n = read(0, buf, sizeof(buf));
            if (n <= 0)
            {
                break;
            }
            if (send(sock, buf, n, MSG_NOSIGNAL) < 0 )
            {
                std::cerr << "lost connection to server" << std::endl;
                break;
            } 
        }
        if (fds[1].revents)
        {
            ssize_t n = read(sock, buf, sizeof(buf));
            if (n <= 0)
            {
                std::cerr << "server closed the connection" << std::endl;
                break;
            }
            std::cout << "Other player: " << std::string(buf, n) << std::flush;
        } 
    }
    close(sock);
}