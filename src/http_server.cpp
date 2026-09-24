#include "remote_gateway/http_server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

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
        const bool root_request = parsed.method == "GET" &&
            (parsed.path == "/" || parsed.path == "/index.html");
        const bool admin_request = parsed.method == "GET" &&
            (parsed.path == "/admin" || parsed.path == "/admin.html");
        const bool session_request = parsed.method == "GET" &&
            (parsed.path == "/session" || parsed.path == "/session.html");
        const bool settings_request = parsed.method == "GET" &&
            (parsed.path == "/settings" || parsed.path == "/settings.html");
        const bool icon_request = parsed.method == "GET" && parsed.path == "/remotelink-icon.png";
        const bool i18n_request = parsed.method == "GET" && parsed.path == "/i18n.js";
        const bool health_request = parsed.method == "GET" && parsed.path == "/healthz";
        const bool api_request = parsed.path.starts_with("/api/admin/") ||
                                 parsed.path.starts_with("/api/auth/");

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
        else if (root_request || admin_request || session_request || settings_request || icon_request || i18n_request) {
            const char* configured_web_root = std::getenv("RG_WEB_ROOT");
            const std::string web_root = configured_web_root && *configured_web_root
                ? configured_web_root : REMOTE_GATEWAY_WEB_ROOT;
            const std::string page = icon_request ? "/remotelink-icon.png" :
                                     i18n_request ? "/i18n.js" :
                                     admin_request ? "/admin.html" :
                                     session_request ? "/index.html" :
                                     settings_request ? "/settings.html" : "/connect.html";
            std::ifstream input(web_root + page,
                                std::ios::binary);
            std::ostringstream contents;
            contents << input.rdbuf();
            body = contents.str();
            status_code = input ? 200 : 500;
            content_type = icon_request ? "image/png" :
                           i18n_request ? "application/javascript; charset=utf-8" :
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

        std::ostringstream response;
        response << "HTTP/1.1 " << status_text(status_code) << "\r\n"
                 << "Content-Type: " << content_type << "\r\n"
                 << "Content-Length: " << body.size() << "\r\n"
                 << "Cache-Control: no-store\r\n"
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
