#include "packet.h"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstdlib>
#include <ctime>
#include <cstring>

uint32_t get_dword_time() {
    return static_cast<uint32_t>(time(nullptr));
}

int main() {
    srand(static_cast<unsigned int>(time(nullptr)));

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(13000);

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1) {
        perror("bind");
        return 1;
    }

    if (listen(server_fd, 5) == -1) {
        perror("listen");
        return 1;
    }

    std::cout << "Simple server started on port 13000\n";

    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd == -1) {
            perror("accept");
            continue;
        }

        std::cout << "Client connected: " << inet_ntoa(client_addr.sin_addr) << std::endl;

        uint32_t handshake = static_cast<uint32_t>(rand());
        TPacketGCHandshake out_packet{};
        out_packet.bHeader = HEADER_GC_HANDSHAKE;
        out_packet.dwHandshake = handshake;
        out_packet.dwTime = get_dword_time();
        out_packet.lDelta = 0;

        if (send(client_fd, &out_packet, sizeof(out_packet), 0) != sizeof(out_packet)) {
            std::cerr << "Failed to send handshake" << std::endl;
            close(client_fd);
            continue;
        }

        TPacketCGHandshake in_packet{};
        ssize_t received = recv(client_fd, &in_packet, sizeof(in_packet), 0);
        if (received != sizeof(in_packet)) {
            std::cerr << "Invalid handshake size" << std::endl;
            close(client_fd);
            continue;
        }

        if (in_packet.bHeader != HEADER_CG_HANDSHAKE || in_packet.dwHandshake != handshake) {
            std::cerr << "Handshake mismatch" << std::endl;
            close(client_fd);
            continue;
        }

        std::cout << "Handshake successful" << std::endl;

        const char msg[] = "Welcome to the Metin2-like server!\n";
        send(client_fd, msg, sizeof(msg) - 1, 0);

        close(client_fd);
    }

    close(server_fd);
    return 0;
}