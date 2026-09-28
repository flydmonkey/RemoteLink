#include "remotelink/telnet_bridge.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <thread>

namespace remotelink {
namespace {
int connect_tcp(const std::string& hostname, std::uint16_t port) {
    addrinfo hints{}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;
    if (getaddrinfo(hostname.c_str(), std::to_string(port).c_str(), &hints, &addresses) != 0)
        return -1;
    int fd = -1;
    for (auto* address = addresses; address; address = address->ai_next) {
        fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd >= 0 && connect(fd, address->ai_addr, address->ai_addrlen) == 0) break;
        if (fd >= 0) close(fd);
        fd = -1;
    }
    freeaddrinfo(addresses);
    return fd;
}
}

struct TelnetBridge::Impl {
    int upstream = -1;
    int listener = -1;
    std::uint16_t listen_port = 0;
    std::atomic_bool stopping = false;
    std::atomic_bool cleaned = false;
    std::jthread worker;
    std::string username;
    std::string password;

    ~Impl() { shutdown(); }

    void run() {
        sockaddr_storage address{}; socklen_t size = sizeof(address);
        const int client = accept(listener, reinterpret_cast<sockaddr*>(&address), &size);
        if (client < 0 || stopping) { if (client >= 0) close(client); return; }
        std::array<char, 32768> buffer{};
        std::string prompt;
        bool username_sent = false;
        bool password_sent = false;
        enum class TelnetState { data, command, option, subnegotiation, subnegotiation_iac };
        TelnetState telnet_state = TelnetState::data;
        unsigned char telnet_command = 0;
        while (!stopping) {
            pollfd descriptors[2]{{client, POLLIN, 0}, {upstream, POLLIN, 0}};
            if (poll(descriptors, 2, 100) < 0 && errno != EINTR) break;
            bool finished = false;
            for (int index = 0; index < 2 && !finished; ++index) {
                if (descriptors[index].revents & (POLLERR | POLLHUP | POLLNVAL)) { finished = true; break; }
                if (!(descriptors[index].revents & POLLIN)) continue;
                const int source = index == 0 ? client : upstream;
                const int destination = index == 0 ? upstream : client;
                const auto count = recv(source, buffer.data(), buffer.size(), 0);
                if (count <= 0) { finished = true; break; }
                std::string output;
                if (index == 1) {
                    output.reserve(static_cast<std::size_t>(count));
                    for (std::ptrdiff_t position = 0; position < count; ++position) {
                        const auto value = static_cast<unsigned char>(buffer[static_cast<std::size_t>(position)]);
                        if (telnet_state == TelnetState::data) {
                            if (value == 255) telnet_state = TelnetState::command;
                            else output.push_back(static_cast<char>(value));
                        } else if (telnet_state == TelnetState::command) {
                            if (value == 255) { output.push_back(static_cast<char>(255)); telnet_state = TelnetState::data; }
                            else if (value == 250) telnet_state = TelnetState::subnegotiation;
                            else if (value >= 251 && value <= 254) { telnet_command = value; telnet_state = TelnetState::option; }
                            else telnet_state = TelnetState::data;
                        } else if (telnet_state == TelnetState::option) {
                            const unsigned char response_command =
                                (telnet_command == 251 && (value == 1 || value == 3)) ? 253 :
                                (telnet_command == 251 || telnet_command == 252) ? 254 : 252;
                            const unsigned char response[3]{255, response_command, value};
                            send(upstream, response, sizeof(response), MSG_NOSIGNAL);
                            telnet_state = TelnetState::data;
                        } else if (telnet_state == TelnetState::subnegotiation) {
                            if (value == 255) telnet_state = TelnetState::subnegotiation_iac;
                        } else {
                            telnet_state = value == 240 ? TelnetState::data : TelnetState::subnegotiation;
                        }
                    }
                } else {
                    output.assign(buffer.data(), static_cast<std::size_t>(count));
                }
                if (index == 1 && (!username_sent || !password_sent)) {
                    prompt.append(output);
                    if (prompt.size() > 1024) prompt.erase(0, prompt.size() - 1024);
                    std::string lower_prompt = prompt;
                    for (char& value : lower_prompt) value = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
                    if (!username_sent && !username.empty() &&
                        (lower_prompt.find("login:") != std::string::npos || lower_prompt.find("username:") != std::string::npos)) {
                        const auto value = username + "\r\n";
                        send(upstream, value.data(), value.size(), MSG_NOSIGNAL);
                        username_sent = true; prompt.clear();
                    } else if (!password_sent && username_sent && !password.empty() &&
                               lower_prompt.find("password:") != std::string::npos) {
                        const auto value = password + "\r\n";
                        send(upstream, value.data(), value.size(), MSG_NOSIGNAL);
                        password_sent = true; prompt.clear();
                    }
                }
                std::size_t offset = 0;
                while (offset < output.size()) {
                    const auto sent = send(destination, output.data() + offset,
                        output.size() - offset, MSG_NOSIGNAL);
                    if (sent <= 0) { finished = true; break; }
                    offset += static_cast<std::size_t>(sent);
                }
            }
            if (finished) break;
        }
        close(client);
    }

    void shutdown() {
        stopping = true;
        if (cleaned.exchange(true)) return;
        if (listener >= 0) { ::shutdown(listener, SHUT_RDWR); close(listener); listener = -1; }
        if (upstream >= 0) ::shutdown(upstream, SHUT_RDWR);
        if (worker.joinable() && worker.get_id() != std::this_thread::get_id()) worker.join();
        if (upstream >= 0) { close(upstream); upstream = -1; }
    }
};

TelnetBridge::TelnetBridge(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
TelnetBridge::~TelnetBridge() { stop(); }

std::shared_ptr<TelnetBridge> TelnetBridge::create(TelnetBridgeOptions options, std::string& error) {
    auto impl = std::make_unique<Impl>();
    impl->username = std::move(options.username);
    impl->password = std::move(options.password);
    impl->upstream = connect_tcp(options.hostname, options.port);
    if (impl->upstream < 0) { error = "无法连接 Telnet 目标"; return {}; }
    impl->listener = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (impl->listener < 0 || bind(impl->listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(impl->listener, 1) != 0) { error = "无法创建 Telnet 本地代理"; return {}; }
    socklen_t address_size = sizeof(address);
    getsockname(impl->listener, reinterpret_cast<sockaddr*>(&address), &address_size);
    impl->listen_port = ntohs(address.sin_port);
    auto bridge = std::shared_ptr<TelnetBridge>(new TelnetBridge(std::move(impl)));
    bridge->impl_->worker = std::jthread([state=bridge->impl_.get()] { state->run(); });
    return bridge;
}

std::uint16_t TelnetBridge::port() const { return impl_->listen_port; }
void TelnetBridge::stop() { if (impl_) impl_->shutdown(); }

}  // namespace remotelink
