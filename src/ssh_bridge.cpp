#include "remote_gateway/ssh_bridge.hpp"

#include <libssh2.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <iomanip>
#include <sstream>
#include <thread>

namespace remote_gateway {
namespace {
int connect_tcp(const std::string& hostname, std::uint16_t port) {
    addrinfo hints{}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;
    if (getaddrinfo(hostname.c_str(), std::to_string(port).c_str(), &hints, &addresses) != 0)
        return -1;
    int socket_fd = -1;
    for (auto* address = addresses; address; address = address->ai_next) {
        socket_fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (socket_fd >= 0 && connect(socket_fd, address->ai_addr, address->ai_addrlen) == 0) break;
        if (socket_fd >= 0) close(socket_fd);
        socket_fd = -1;
    }
    freeaddrinfo(addresses);
    return socket_fd;
}

std::string session_error(LIBSSH2_SESSION* session, std::string fallback) {
    char* message = nullptr; int length = 0;
    libssh2_session_last_error(session, &message, &length, 0);
    return message && length > 0 ? std::string(message, static_cast<std::size_t>(length)) : fallback;
}
}

struct SshBridge::Impl {
    int upstream = -1;
    int listener = -1;
    LIBSSH2_SESSION* session = nullptr;
    LIBSSH2_CHANNEL* channel = nullptr;
    std::uint16_t listen_port = 0;
    std::string host_key_sha256;
    std::mutex channel_mutex;
    std::atomic_bool stopping = false;
    std::atomic_bool cleaned = false;
    std::jthread worker;

    ~Impl() { shutdown(); }

    void run() {
        sockaddr_storage address{}; socklen_t size = sizeof(address);
        const int client = accept(listener, reinterpret_cast<sockaddr*>(&address), &size);
        if (client < 0 || stopping) { if (client >= 0) close(client); return; }
        libssh2_session_set_blocking(session, 0);
        std::array<char, 32768> buffer{};
        while (!stopping) {
            pollfd descriptors[2]{{client, POLLIN, 0}, {upstream, POLLIN, 0}};
            poll(descriptors, 2, 50);
            if (descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL)) break;
            if (descriptors[0].revents & POLLIN) {
                const auto count = recv(client, buffer.data(), buffer.size(), 0);
                if (count <= 0) break;
                std::size_t offset = 0;
                while (offset < static_cast<std::size_t>(count) && !stopping) {
                    std::lock_guard channel_lock(channel_mutex);
                    const auto sent = libssh2_channel_write(channel, buffer.data() + offset,
                        static_cast<std::size_t>(count) - offset);
                    if (sent > 0) offset += static_cast<std::size_t>(sent);
                    else if (sent != LIBSSH2_ERROR_EAGAIN) { stopping = true; break; }
                    else poll(descriptors + 1, 1, 20);
                }
            }
            while (!stopping) {
                std::lock_guard channel_lock(channel_mutex);
                const auto count = libssh2_channel_read(channel, buffer.data(), buffer.size());
                if (count > 0) {
                    std::size_t offset = 0;
                    while (offset < static_cast<std::size_t>(count)) {
                        const auto sent = send(client, buffer.data() + offset,
                            static_cast<std::size_t>(count) - offset, MSG_NOSIGNAL);
                        if (sent <= 0) { stopping = true; break; }
                        offset += static_cast<std::size_t>(sent);
                    }
                } else break;
            }
            if (libssh2_channel_eof(channel)) break;
        }
        close(client);
    }

    void shutdown() {
        stopping = true;
        if (cleaned.exchange(true)) return;
        if (listener >= 0) { ::shutdown(listener, SHUT_RDWR); close(listener); listener = -1; }
        if (upstream >= 0) ::shutdown(upstream, SHUT_RDWR);
        if (worker.joinable() && worker.get_id() != std::this_thread::get_id()) worker.join();
        if (channel) { libssh2_channel_free(channel); channel = nullptr; }
        if (session) { libssh2_session_disconnect(session, "RemoteLink session closed");
            libssh2_session_free(session); session = nullptr; }
        if (upstream >= 0) { close(upstream); upstream = -1; }
    }
};

SshBridge::SshBridge(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
SshBridge::~SshBridge() { stop(); }

std::shared_ptr<SshBridge> SshBridge::create(SshBridgeOptions options, std::string& error) {
    static std::once_flag initialized;
    std::call_once(initialized, [] { libssh2_init(0); });
    auto impl = std::make_unique<Impl>();
    impl->upstream = connect_tcp(options.hostname, options.port);
    if (impl->upstream < 0) { error = "无法连接 SSH 目标"; return {}; }
    impl->session = libssh2_session_init();
    if (!impl->session || libssh2_session_handshake(impl->session, impl->upstream) != 0) {
        error = impl->session ? session_error(impl->session, "SSH 握手失败") : "无法创建 SSH 会话";
        return {};
    }
    const auto* fingerprint = reinterpret_cast<const unsigned char*>(
        libssh2_hostkey_hash(impl->session, LIBSSH2_HOSTKEY_HASH_SHA256));
    if (!fingerprint) { error = "无法读取 SSH 主机指纹"; return {}; }
    std::ostringstream fingerprint_stream;
    fingerprint_stream << "SHA256:" << std::hex << std::setfill('0');
    for (int index = 0; index < 32; ++index) fingerprint_stream << std::setw(2) << static_cast<int>(fingerprint[index]);
    impl->host_key_sha256 = fingerprint_stream.str();
    if (!options.host_key_sha256.empty() && options.host_key_sha256 != impl->host_key_sha256) {
        error = "SSH 主机指纹已变更，已拒绝连接"; return {};
    }
    int authenticated = -1;
    if (!options.private_key.empty()) {
        authenticated = libssh2_userauth_publickey_frommemory(impl->session,
            options.username.c_str(), options.username.size(), nullptr, 0,
            options.private_key.c_str(), options.private_key.size(),
            options.passphrase.empty() ? nullptr : options.passphrase.c_str());
    } else {
        authenticated = libssh2_userauth_password_ex(impl->session, options.username.c_str(),
            options.username.size(), options.password.c_str(), options.password.size(), nullptr);
    }
    if (authenticated != 0) { error = session_error(impl->session, "SSH 身份验证失败"); return {}; }
    impl->channel = libssh2_channel_open_session(impl->session);
    if (!impl->channel || libssh2_channel_request_pty_ex(impl->channel, "xterm-256color", 14,
            nullptr, 0, 120, 36, 0, 0) != 0 || libssh2_channel_shell(impl->channel) != 0) {
        error = session_error(impl->session, "无法启动 SSH 终端"); return {};
    }
    impl->listener = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (impl->listener < 0 || bind(impl->listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(impl->listener, 1) != 0) { error = "无法创建 SSH 本地代理"; return {}; }
    socklen_t address_size = sizeof(address);
    getsockname(impl->listener, reinterpret_cast<sockaddr*>(&address), &address_size);
    impl->listen_port = ntohs(address.sin_port);
    auto bridge = std::shared_ptr<SshBridge>(new SshBridge(std::move(impl)));
    bridge->impl_->worker = std::jthread([state=bridge->impl_.get()] { state->run(); });
    return bridge;
}

std::uint16_t SshBridge::port() const { return impl_->listen_port; }
std::string SshBridge::host_key_sha256() const { return impl_->host_key_sha256; }
bool SshBridge::resize(std::uint32_t columns, std::uint32_t rows) {
    if (!impl_ || !impl_->channel || columns < 1 || rows < 1 || columns > 1000 || rows > 1000) return false;
    std::lock_guard lock(impl_->channel_mutex);
    return libssh2_channel_request_pty_size(impl_->channel, static_cast<int>(columns),
        static_cast<int>(rows)) == 0;
}
void SshBridge::stop() { if (impl_) impl_->shutdown(); }

}  // namespace remote_gateway
