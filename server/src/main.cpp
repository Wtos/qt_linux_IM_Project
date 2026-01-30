#include "server.h"

#include <csignal>
#include <cstdlib>
#include <iostream>

static Server* g_server = nullptr;

void signalHandler(int signum) {
    std::cout << "\nSignal " << signum << " received, stopping server..." << std::endl;
    if (g_server) {
        g_server->stop();
    }
}

int main(int argc, char* argv[]) {
    std::string ip = "0.0.0.0";
    int port = 8888;

    if (argc >= 2) {
        port = std::atoi(argv[1]);
    }
    if (argc >= 3) {
        ip = argv[2];
    }

    std::cout << "========================================" << std::endl;
    std::cout << "  IM Server v1.0" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Listening on: " << ip << ":" << port << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    signal(SIGPIPE, SIG_IGN);

    Server server(ip, port);
    g_server = &server;

    if (!server.start()) {
        std::cerr << "Server start failed" << std::endl;
        return 1;
    }

    std::cout << "Server started, waiting for connections..." << std::endl;
    std::cout << "========================================" << std::endl;

    server.run();

    std::cout << "Server stopped" << std::endl;
    return 0;
}
