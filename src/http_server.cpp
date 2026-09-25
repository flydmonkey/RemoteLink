#include "remote_gateway/http_server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/sha.h>
#include <openssl/evp.h>

#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <array>
#include <cctype>
#include <unordered_map>
#include <vector>

namespace remote_gateway {

HttpServer::HttpServer(std::string bind_address, std::uint16_t port,
                       std::string certificate_file, std::string key_file)
    : bind_address_(std::move(bind_address)), port_(port),
      certificate_file_(std::move(certificate_file)), key_file_(std::move(key_file)) {
    if (certificate_file_.empty() != key_file_.empty()) {
        throw std::invalid_argument("both TLS certificate and key must be configured");
    }
}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::set_health_handler(std::function<std::string()> handler) {
    if (thread_.joinable()) {
        throw std::logic_error("health handler must be set before HTTP server starts");
    }
    health_handler_ = std::move(handler);
}

void HttpServer::set_api_handler(
    std::function<HttpResponse(const HttpRequest&)> handler) {
    if (thread_.joinable()) {
        throw std::logic_error("API handler must be set before HTTP server starts");
    }
    api_handler_ = std::move(handler);
}

void HttpServer::set_vnc_ticket_handler(
    std::function<std::optional<VncDestination>(const std::string&)> handler) {
    if (thread_.joinable())
        throw std::logic_error("VNC ticket handler must be set before HTTP server starts");
    vnc_ticket_handler_ = std::move(handler);
}

void HttpServer::set_vnc_session_observer(
    std::function<void(const VncDestination&, bool)> observer) {
    if (thread_.joinable())
        throw std::logic_error("VNC session observer must be set before HTTP server starts");
    vnc_session_observer_ = std::move(observer);
}

void HttpServer::set_ssh_ticket_handler(
    std::function<std::optional<VncDestination>(const std::string&)> handler) {
    if (thread_.joinable())
        throw std::logic_error("SSH ticket handler must be set before HTTP server starts");
    ssh_ticket_handler_ = std::move(handler);
}
void HttpServer::set_ssh_session_observer(
    std::function<void(const VncDestination&, bool)> observer) {
    if (thread_.joinable()) throw std::logic_error("SSH observer must be set before HTTP server starts");
    ssh_session_observer_ = std::move(observer);
}
void HttpServer::set_ssh_control_handler(
    std::function<void(const VncDestination&, const std::string&)> handler) {
    if (thread_.joinable()) throw std::logic_error("SSH control handler must be set before HTTP server starts");
    ssh_control_handler_ = std::move(handler);
}

namespace {
std::string status_text(int status) {
    switch (status) {
        case 200: return "200 OK";
        case 400: return "400 Bad Request";
        case 401: return "401 Unauthorized";
        case 403: return "403 Forbidden";
        case 404: return "404 Not Found";
        case 405: return "405 Method Not Allowed";
        case 409: return "409 Conflict";
        case 502: return "502 Bad Gateway";
        default: return "500 Internal Server Error";
    }
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

bool write_socket(int socket, const char* data, std::size_t size) {
    std::size_t sent = 0;
    while (sent < size) {
        const auto result = ::send(socket, data + sent, size - sent, MSG_NOSIGNAL);
        if (result <= 0) return false;
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

bool write_client(int client, SSL* tls, const char* data, std::size_t size) {
    std::size_t sent = 0;
    while (sent < size) {
        const auto result = tls != nullptr
            ? SSL_write(tls, data + sent, static_cast<int>(size - sent))
            : ::send(client, data + sent, size - sent, MSG_NOSIGNAL);
        if (result <= 0) return false;
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

bool read_client_exact(int client, SSL* tls, char* data, std::size_t size) {
    std::size_t received = 0;
    while (received < size) {
        const auto result = tls != nullptr
            ? SSL_read(tls, data + received, static_cast<int>(size - received))
            : ::recv(client, data + received, size - received, 0);
        if (result <= 0) return false;
        received += static_cast<std::size_t>(result);
    }
    return true;
}

std::string header_value(const std::string& request, const std::string& expected_name) {
    std::istringstream stream(request);
    std::string line;
    std::getline(stream, line);
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto separator = line.find(':');
        if (separator == std::string::npos) continue;
        if (lower(line.substr(0, separator)) != expected_name) continue;
        auto value = line.substr(separator + 1);
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
            value.erase(value.begin());
        return value;
    }
    return {};
}

std::string websocket_accept(const std::string& key) {
    const auto source = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::array<unsigned char, SHA_DIGEST_LENGTH> digest {};
    SHA1(reinterpret_cast<const unsigned char*>(source.data()), source.size(), digest.data());
    std::array<unsigned char, 32> encoded {};
    const auto length = EVP_EncodeBlock(encoded.data(), digest.data(), digest.size());
    return std::string(reinterpret_cast<char*>(encoded.data()), static_cast<std::size_t>(length));
}

int connect_tcp(const std::string& hostname, std::uint16_t port) {
    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;
    const auto service = std::to_string(port);
    if (::getaddrinfo(hostname.c_str(), service.c_str(), &hints, &addresses) != 0) return -1;
    int socket_fd = -1;
    for (auto* address = addresses; address != nullptr; address = address->ai_next) {
        socket_fd = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (socket_fd >= 0 && ::connect(socket_fd, address->ai_addr, address->ai_addrlen) == 0) break;
        if (socket_fd >= 0) ::close(socket_fd);
        socket_fd = -1;
    }
    ::freeaddrinfo(addresses);
    return socket_fd;
}

bool write_websocket_frame(int client, SSL* tls, std::uint8_t opcode,
                           const char* data, std::size_t size) {
    std::array<char, 10> header {};
    std::size_t header_size = 2;
    header[0] = static_cast<char>(0x80U | opcode);
    if (size <= 125) header[1] = static_cast<char>(size);
    else if (size <= 0xffff) {
        header[1] = 126;
        header[2] = static_cast<char>((size >> 8) & 0xff);
        header[3] = static_cast<char>(size & 0xff);
        header_size = 4;
    } else {
        header[1] = 127;
        for (int index = 0; index < 8; ++index)
            header[2 + index] = static_cast<char>((size >> (56 - index * 8)) & 0xff);
        header_size = 10;
    }
    return write_client(client, tls, header.data(), header_size) &&
           write_client(client, tls, data, size);
}

bool relay_websocket_message_to_tcp(int client, SSL* tls, int backend,
    const std::function<void(const std::string&)>& control = {}) {
    std::array<unsigned char, 2> header {};
    if (!read_client_exact(client, tls, reinterpret_cast<char*>(header.data()), header.size()))
        return false;
    const bool final = (header[0] & 0x80U) != 0;
    const auto opcode = static_cast<std::uint8_t>(header[0] & 0x0fU);
    if (!final || (opcode != 0x1 && opcode != 0x2 && opcode != 0x8 && opcode != 0x9)) return false;
    if ((header[1] & 0x80U) == 0) return false;
    std::uint64_t length = header[1] & 0x7fU;
    if (length == 126) {
        std::array<unsigned char, 2> extended {};
        if (!read_client_exact(client, tls, reinterpret_cast<char*>(extended.data()), 2)) return false;
        length = static_cast<std::uint64_t>(extended[0]) << 8 | extended[1];
    } else if (length == 127) {
        std::array<unsigned char, 8> extended {};
        if (!read_client_exact(client, tls, reinterpret_cast<char*>(extended.data()), 8)) return false;
        length = 0;
        for (const auto byte : extended) length = (length << 8) | byte;
    }
    if (length > 16U * 1024U * 1024U) return false;
    std::array<unsigned char, 4> mask {};
    if (!read_client_exact(client, tls, reinterpret_cast<char*>(mask.data()), mask.size())) return false;
    std::vector<char> payload(static_cast<std::size_t>(length));
    if (length > 0 && !read_client_exact(client, tls, payload.data(), payload.size())) return false;
    for (std::size_t index = 0; index < payload.size(); ++index)
        payload[index] ^= static_cast<char>(mask[index % mask.size()]);
    if (opcode == 0x8) return false;
    if (opcode == 0x9) return write_websocket_frame(client, tls, 0xA, payload.data(), payload.size());
    if (opcode == 0x1) { if (control) control(std::string(payload.begin(), payload.end())); return true; }
    return write_socket(backend, payload.data(), payload.size());
}

void proxy_terminal_websocket(int client, SSL* tls, const std::string& request,
                         const VncDestination& destination,
                         const std::function<void(const VncDestination&, bool)>& observer,
                         const std::function<void(const VncDestination&, const std::string&)>& control = {}) {
    const auto key = header_value(request, "sec-websocket-key");
    const int backend = key.empty() ? -1 : connect_tcp(destination.hostname, destination.port);
    if (backend < 0) {
        static constexpr char unavailable[] =
            "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        write_client(client, tls, unavailable, sizeof(unavailable) - 1);
    } else {
        const auto response = "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " +
            websocket_accept(key) + "\r\n\r\n";
        if (write_client(client, tls, response.data(), response.size())) {
            if (observer) observer(destination, true);
            std::array<char, 64 * 1024> buffer {};
            while (true) {
                pollfd descriptors[2] {{client, POLLIN, 0}, {backend, POLLIN, 0}};
                const auto ready = ::poll(descriptors, 2, -1);
                if (ready < 0 && errno == EINTR) continue;
                if (ready <= 0 || descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL) ||
                    descriptors[1].revents & (POLLERR | POLLHUP | POLLNVAL)) break;
                if (descriptors[0].revents & POLLIN &&
                    !relay_websocket_message_to_tcp(client, tls, backend,
                        [&](const std::string& message) { if (control) control(destination, message); })) break;
                if (descriptors[1].revents & POLLIN) {
                    const auto received = ::recv(backend, buffer.data(), buffer.size(), 0);
                    if (received <= 0 || !write_websocket_frame(client, tls, 0x2,
                        buffer.data(), static_cast<std::size_t>(received))) break;
                }
            }
        }
        if (observer) observer(destination, false);
        ::shutdown(backend, SHUT_RDWR);
        ::close(backend);
    }
    if (tls != nullptr) { SSL_shutdown(tls); SSL_free(tls); }
    ::shutdown(client, SHUT_RDWR);
    ::close(client);
}

void proxy_websocket(int client, SSL* tls, std::string request) {
    const int backend = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(18081);
    ::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (backend < 0 || ::connect(backend, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        static constexpr char unavailable[] =
            "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        write_client(client, tls, unavailable, sizeof(unavailable) - 1);
        if (backend >= 0) ::close(backend);
    } else {
        const auto first_space = request.find(' ');
        const auto second_space = first_space == std::string::npos
            ? std::string::npos : request.find(' ', first_space + 1);
        if (first_space != std::string::npos && second_space != std::string::npos)
            request.replace(first_space + 1, second_space - first_space - 1, "/signal");

        if (write_socket(backend, request.data(), request.size())) {
            std::array<char, 16 * 1024> relay {};
            while (true) {
                pollfd descriptors[2] {{client, POLLIN, 0}, {backend, POLLIN, 0}};
                const auto ready = ::poll(descriptors, 2, -1);
                if (ready < 0 && errno == EINTR) continue;
                if (ready <= 0) break;
                if (descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL)) break;
                if (descriptors[1].revents & (POLLERR | POLLHUP | POLLNVAL)) break;
                if (descriptors[0].revents & POLLIN) {
                    const auto received = tls != nullptr
                        ? SSL_read(tls, relay.data(), static_cast<int>(relay.size()))
                        : ::recv(client, relay.data(), relay.size(), 0);
                    if (received <= 0 || !write_socket(backend, relay.data(), received)) break;
                }
                if (descriptors[1].revents & POLLIN) {
                    const auto received = ::recv(backend, relay.data(), relay.size(), 0);
                    if (received <= 0 || !write_client(client, tls, relay.data(), received)) break;
                }
            }
        }
        ::shutdown(backend, SHUT_RDWR);
        ::close(backend);
    }
    if (tls != nullptr) {
        SSL_shutdown(tls);
        SSL_free(tls);
    }
    ::shutdown(client, SHUT_RDWR);
    ::close(client);
}
}  // namespace

void HttpServer::start() {
    if (thread_.joinable()) {
        throw std::logic_error("HTTP server already started");
    }
    stopping_ = false;
    if (!certificate_file_.empty()) {
        tls_context_ = SSL_CTX_new(TLS_server_method());
        if (tls_context_ == nullptr ||
            SSL_CTX_use_certificate_chain_file(tls_context_, certificate_file_.c_str()) != 1 ||
            SSL_CTX_use_PrivateKey_file(tls_context_, key_file_.c_str(), SSL_FILETYPE_PEM) != 1 ||
            SSL_CTX_check_private_key(tls_context_) != 1) {
            const unsigned long code = ERR_get_error();
            const std::string message = code ? ERR_error_string(code, nullptr) : "invalid certificate or key";
            if (tls_context_ != nullptr) SSL_CTX_free(tls_context_);
            tls_context_ = nullptr;
            throw std::runtime_error("unable to configure HTTPS: " + message);
        }
        SSL_CTX_set_min_proto_version(tls_context_, TLS1_2_VERSION);
    }
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        if (tls_context_ != nullptr) SSL_CTX_free(tls_context_);
        tls_context_ = nullptr;
        throw std::runtime_error("unable to create HTTP socket");
    }

    int reuse = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(port_);
    if (::inet_pton(AF_INET, bind_address_.c_str(), &address.sin_addr) != 1 ||
        ::bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(listen_fd_, 16) != 0) {
        const std::string error = std::strerror(errno);
        ::close(listen_fd_);
        listen_fd_ = -1;
        if (tls_context_ != nullptr) SSL_CTX_free(tls_context_);
        tls_context_ = nullptr;
        throw std::runtime_error("unable to bind HTTP server: " + error);
    }
    thread_ = std::jthread([this] { run(); });
}

void HttpServer::stop() {
    stopping_ = true;
    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    if (tls_context_ != nullptr) {
        SSL_CTX_free(tls_context_);
        tls_context_ = nullptr;
    }
}

void HttpServer::run() {

    while (!stopping_) {
        const int client = ::accept(listen_fd_, nullptr, nullptr);
        if (client < 0) {
            if (stopping_) break;
            continue;
        }
        timeval timeout {};
        timeout.tv_sec = 5;
        ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        ::setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        SSL* tls = nullptr;
        if (tls_context_ != nullptr) {
            tls = SSL_new(tls_context_);
            if (tls == nullptr) {
                ::close(client);
                continue;
            }
            SSL_set_fd(tls, client);
            if (SSL_accept(tls) != 1) {
                SSL_free(tls);
                ::close(client);
                continue;
            }
        }

        const auto receive = [&](char* buffer, std::size_t size) {
            return tls != nullptr ? SSL_read(tls, buffer, static_cast<int>(size))
                                  : ::recv(client, buffer, size, 0);
        };
        std::string raw_request;
        raw_request.reserve(4096);
        char buffer[4096];
        std::size_t expected_size = 0;
        bool malformed_request = false;
        while (raw_request.size() < 64 * 1024) {
            const auto received = receive(buffer, sizeof(buffer));
            if (received <= 0) break;
            raw_request.append(buffer, static_cast<std::size_t>(received));
            const auto headers_end = raw_request.find("\r\n\r\n");
            if (headers_end == std::string::npos) continue;
            if (expected_size == 0) {
                expected_size = headers_end + 4;
                const auto content_length = lower(raw_request.substr(0, headers_end)).find("content-length:");
                if (content_length != std::string::npos) {
                    const auto value_start = content_length + std::strlen("content-length:");
                    try {
                        const auto body_size = static_cast<std::size_t>(std::stoul(
                            raw_request.substr(value_start, raw_request.find("\r\n", value_start) - value_start)));
                        if (body_size > 64 * 1024 * 1024) malformed_request = true;
                        else expected_size += body_size;
                    }
                    catch (...) { malformed_request = true; }
                }
            }
            if (malformed_request) break;
            if (raw_request.size() >= expected_size) break;
        }

        HttpRequest parsed;
        const auto headers_end = raw_request.find("\r\n\r\n");
        const bool incomplete_request = headers_end == std::string::npos ||
            (expected_size != 0 && raw_request.size() < expected_size);
        if (incomplete_request) {
            // Browsers commonly open speculative TLS connections before they
            // have a request to send. Closing an idle/incomplete connection is
            // correct; returning a 400 here can surface as an intermittent
            // "Bad request" page during navigation.
            if (tls != nullptr) {
                SSL_shutdown(tls);
                SSL_free(tls);
            }
            ::close(client);
            continue;
        }
        if (headers_end != std::string::npos) {
            std::istringstream headers(raw_request.substr(0, headers_end));
            std::string request_line;
            std::getline(headers, request_line);
            std::istringstream line(request_line);
            line >> parsed.method >> parsed.path;
            std::string header;
            while (std::getline(headers, header)) {
                if (!header.empty() && header.back() == '\r') header.pop_back();
                const auto separator = header.find(':');
                if (separator == std::string::npos) continue;
                auto name = lower(header.substr(0, separator));
                auto value = header.substr(separator + 1);
                while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
                if (name == "authorization") parsed.authorization = std::move(value);
                else if (name == "x-file-name") parsed.file_name = std::move(value);
                else if (name == "x-destination") parsed.destination = std::move(value);
                else if (name == "x-upload-offset") parsed.upload_offset = std::move(value);
                else if (name == "x-upload-total") parsed.upload_total = std::move(value);
            }
            parsed.body = raw_request.substr(headers_end + 4);
        }
        const bool websocket_request = parsed.method == "GET" && parsed.path == "/ws" &&
            lower(raw_request.substr(0, headers_end)).find("upgrade: websocket") != std::string::npos;
        if (websocket_request) {
            // The relay owns the accepted socket and SSL object from here. It
            // runs independently so long-lived sessions do not block HTTP.
            std::thread(proxy_websocket, client, tls, std::move(raw_request)).detach();
            continue;
        }
        const std::string vnc_prefix = "/vnc/ws?ticket=";
        const bool vnc_websocket_request = parsed.method == "GET" &&
            parsed.path.starts_with(vnc_prefix) &&
            lower(raw_request.substr(0, headers_end)).find("upgrade: websocket") != std::string::npos;
        if (vnc_websocket_request) {
            const auto ticket = parsed.path.substr(vnc_prefix.size());
            const auto destination = vnc_ticket_handler_ ? vnc_ticket_handler_(ticket) : std::nullopt;
            if (!destination) {
                static constexpr char forbidden[] =
                    "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                write_client(client, tls, forbidden, sizeof(forbidden) - 1);
                if (tls != nullptr) { SSL_shutdown(tls); SSL_free(tls); }
                ::close(client);
                continue;
            }
            std::thread(proxy_terminal_websocket, client, tls, std::move(raw_request),
                        std::move(*destination), vnc_session_observer_, nullptr).detach();
            continue;
        }
        const std::string ssh_prefix = "/ssh/ws?ticket=";
        const bool ssh_websocket_request = parsed.method == "GET" &&
            parsed.path.starts_with(ssh_prefix) &&
            lower(raw_request.substr(0, headers_end)).find("upgrade: websocket") != std::string::npos;
        if (ssh_websocket_request) {
            const auto ticket = parsed.path.substr(ssh_prefix.size());
            const auto destination = ssh_ticket_handler_ ? ssh_ticket_handler_(ticket) : std::nullopt;
            if (!destination) {
                static constexpr char forbidden[] =
                    "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                write_client(client, tls, forbidden, sizeof(forbidden) - 1);
                if (tls != nullptr) { SSL_shutdown(tls); SSL_free(tls); }
                ::close(client);
                continue;
            }
            std::thread(proxy_terminal_websocket, client, tls, std::move(raw_request),
                        std::move(*destination), ssh_session_observer_, ssh_control_handler_).detach();
            continue;
        }
        const auto query_position = parsed.path.find('?');
        const std::string resource_path = parsed.path.substr(0, query_position);
        const bool versioned_resource = query_position != std::string::npos &&
            parsed.path.find("v=", query_position + 1) != std::string::npos &&
            parsed.path.find("__REMOTELINK_VERSION__", query_position + 1) == std::string::npos;
        const bool root_request = parsed.method == "GET" &&
            (resource_path == "/" || resource_path == "/index.html");
        const bool admin_request = parsed.method == "GET" &&
            (resource_path == "/admin" || resource_path == "/admin.html");
        const bool session_request = parsed.method == "GET" &&
            (resource_path == "/session" || resource_path == "/session.html");
        const bool settings_request = parsed.method == "GET" &&
            (resource_path == "/settings" || resource_path == "/settings.html");
        const bool vnc_request = parsed.method == "GET" &&
            (resource_path == "/vnc" || resource_path == "/vnc.html");
        const bool vnc_session_request = parsed.method == "GET" &&
            (resource_path == "/vnc/session" || resource_path == "/vnc-session.html");
        const bool vnc_admin_request = parsed.method == "GET" &&
            (resource_path == "/vnc/admin" || resource_path == "/vnc-admin.html");
        const bool vnc_permissions_request = parsed.method == "GET" &&
            (resource_path == "/vnc/permissions" || resource_path == "/vnc-permissions.html");
        const bool users_request = parsed.method == "GET" &&
            (resource_path == "/users" || resource_path == "/users.html");
        const bool ssh_request = parsed.method == "GET" &&
            (resource_path == "/ssh" || resource_path == "/ssh.html" || resource_path == "/ssh/settings");
        const bool ssh_session_request = parsed.method == "GET" &&
            (resource_path == "/ssh/session" || resource_path == "/ssh-session.html");
        const bool icon_request = parsed.method == "GET" && resource_path == "/remotelink-icon.png";
        const bool i18n_request = parsed.method == "GET" && resource_path == "/i18n.js";
        const bool novnc_request = parsed.method == "GET" &&
            resource_path.starts_with("/vendor/novnc/") &&
            resource_path.find("..") == std::string::npos;
        const bool xterm_request = parsed.method == "GET" &&
            resource_path.starts_with("/vendor/xterm/") &&
            resource_path.find("..") == std::string::npos;
        const bool health_request = parsed.method == "GET" && resource_path == "/healthz";
        const bool api_request = resource_path == "/api/admin/vnc" ||
                                 resource_path.starts_with("/api/admin/") ||
                                 resource_path.starts_with("/api/auth/") ||
                                 resource_path.starts_with("/api/vnc/") ||
                                 resource_path.starts_with("/api/ssh/") ||
                                 resource_path.starts_with("/api/admin/ssh");

        std::string body;
        int status_code = 404;
        std::string content_type = "text/plain; charset=utf-8";
        if (malformed_request || parsed.method.empty() || parsed.path.empty()) {
            status_code = 400;
            body = "Bad request\n";
        }
        else if (health_request) {
            status_code = 200;
            content_type = "application/json";
            body = health_handler_ ? health_handler_() : "{\"status\":\"ok\"}\n";
        }
        else if (root_request || admin_request || session_request || settings_request ||
                 vnc_request || vnc_session_request || vnc_admin_request || vnc_permissions_request ||
                 users_request || ssh_request || ssh_session_request || icon_request || i18n_request || novnc_request || xterm_request) {
            const char* configured_web_root = std::getenv("RG_WEB_ROOT");
            const std::string web_root = configured_web_root && *configured_web_root
                ? configured_web_root : REMOTE_GATEWAY_WEB_ROOT;
            std::string page = icon_request ? "/remotelink-icon.png" :
                                     i18n_request ? "/i18n.js" :
                                     admin_request ? "/admin.html" :
                                     session_request ? "/index.html" :
                                     settings_request ? "/settings.html" : "/connect.html";
            if (vnc_request) page = "/vnc.html";
            else if (vnc_session_request) page = "/vnc-session.html";
            else if (vnc_admin_request) page = "/vnc.html";
            else if (vnc_permissions_request) page = "/users.html";
            else if (users_request) page = "/users.html";
            else if (ssh_request) page = "/ssh.html";
            else if (ssh_session_request) page = "/ssh-session.html";
            else if (novnc_request) page = resource_path;
            else if (xterm_request) page = resource_path;
            std::ifstream input(web_root + page,
                                std::ios::binary);
            std::ostringstream contents;
            contents << input.rdbuf();
            body = contents.str();
            status_code = input ? 200 : 500;
            content_type = icon_request ? "image/png" :
                           xterm_request && resource_path.ends_with(".css") ? "text/css; charset=utf-8" :
                           (i18n_request || novnc_request || xterm_request) ? "application/javascript; charset=utf-8" :
                           "text/html; charset=utf-8";
        }
        else if (api_request && api_handler_) {
            const auto api_response = api_handler_(parsed);
            status_code = api_response.status;
            content_type = api_response.content_type;
            body = api_response.body;
        }
        else {
            body = "Not found\n";
        }

        const bool static_asset = icon_request || i18n_request || novnc_request || xterm_request;
        const bool html_document = root_request || admin_request || session_request || settings_request ||
            vnc_request || vnc_session_request || vnc_admin_request || vnc_permissions_request ||
            users_request || ssh_request || ssh_session_request;
        const std::string cache_control = static_asset
            ? (versioned_resource ? "public, max-age=31536000, immutable"
                                  : "public, max-age=3600, must-revalidate")
            : (html_document ? "no-cache" : "no-store");
        std::ostringstream response;
        response << "HTTP/1.1 " << status_text(status_code) << "\r\n"
                 << "Content-Type: " << content_type << "\r\n"
                 << "Content-Length: " << body.size() << "\r\n"
                 << "Cache-Control: " << cache_control << "\r\n"
                 << "Content-Security-Policy: default-src 'self'; connect-src 'self' ws: wss:; "
                    "img-src 'self' data:; script-src 'self' 'unsafe-inline'; "
                    "style-src 'self' 'unsafe-inline'\r\n"
                 << "X-Content-Type-Options: nosniff\r\n"
                 << "X-Frame-Options: DENY\r\n"
                 << "Referrer-Policy: no-referrer\r\n"
                 << "Cross-Origin-Opener-Policy: same-origin\r\n"
                 << "Cross-Origin-Resource-Policy: same-origin\r\n"
                 << "Permissions-Policy: camera=(), microphone=(), geolocation=()\r\n";
        if (tls_context_ != nullptr) {
            response << "Strict-Transport-Security: max-age=31536000\r\n";
        }
        response
                 << "Connection: close\r\n\r\n"
                 << body;
        const std::string serialized = response.str();
        std::size_t sent = 0;
        while (sent < serialized.size()) {
            const auto result = tls != nullptr
                ? SSL_write(tls, serialized.data() + sent,
                            static_cast<int>(serialized.size() - sent))
                : ::send(client, serialized.data() + sent,
                         serialized.size() - sent, MSG_NOSIGNAL);
            if (result <= 0) break;
            sent += static_cast<std::size_t>(result);
        }
        if (tls != nullptr) {
            SSL_shutdown(tls);
            SSL_free(tls);
        }
        ::close(client);
    }

    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
}

}  // namespace remote_gateway
