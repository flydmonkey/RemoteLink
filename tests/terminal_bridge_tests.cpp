#include "remotelink/terminal_bridge.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstdlib>
#include <string>
#include <thread>

namespace {

void require(bool condition) {
    if (!condition) std::abort();
}

int listen_loopback(std::uint16_t& port) {
    const int listener = socket(AF_INET, SOCK_STREAM, 0);
    require(listener >= 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    require(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    require(listen(listener, 1) == 0);
    socklen_t size = sizeof(address);
    require(getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size) == 0);
    port = ntohs(address.sin_port);
    return listener;
}

int connect_loopback(std::uint16_t port) {
    const int client = socket(AF_INET, SOCK_STREAM, 0);
    require(client >= 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    require(connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    return client;
}

}  // namespace

int main() {
    std::uint16_t upstream_port = 0;
    const int listener = listen_loopback(upstream_port);
    const std::array<unsigned char, 5> payload{'A', 255, 251, 1, 'B'};
    std::jthread upstream([&] {
        const int client = accept(listener, nullptr, nullptr);
        require(client >= 0);
        require(send(client, payload.data(), payload.size(), 0) ==
                static_cast<ssize_t>(payload.size()));
        close(client);
    });

    std::string error;
    auto bridge = remotelink::TerminalBridge::create({
        .hostname = "127.0.0.1",
        .port = upstream_port,
        .protocol = remotelink::TerminalProtocol::raw_tcp,
    }, error);
    require(bridge && error.empty());

    const int browser = connect_loopback(bridge->port());
    std::array<unsigned char, 5> received{};
    require(recv(browser, received.data(), received.size(), MSG_WAITALL) ==
            static_cast<ssize_t>(received.size()));
    require(received == payload);
    close(browser);
    bridge->stop();
    close(listener);
}
