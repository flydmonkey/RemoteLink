#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <thread>

#include "remotelink/vnc_ticket_store.hpp"

typedef struct ssl_ctx_st SSL_CTX;

namespace remotelink {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string authorization;
    std::string target_id;
    std::string file_name;
    std::string destination;
    std::string upload_offset;
    std::string upload_total;
    std::string body;
};

struct HttpResponse {
    int status = 200;
    std::string content_type = "application/json";
    std::string body;
};

class HttpServer {
public:
    HttpServer(std::string bind_address, std::uint16_t port,
               std::string certificate_file = {}, std::string key_file = {});
    ~HttpServer();

    void start();
    void stop();
    void set_health_handler(std::function<std::string()> handler);
    void set_api_handler(std::function<HttpResponse(const HttpRequest&)> handler);
    void set_vnc_ticket_handler(
        std::function<std::optional<VncDestination>(const std::string&)> handler);
    void set_vnc_session_observer(std::function<void(const VncDestination&, bool)> observer);
    void set_ssh_ticket_handler(
        std::function<std::optional<VncDestination>(const std::string&)> handler);
    void set_ssh_session_observer(std::function<void(const VncDestination&, bool)> observer);
    void set_ssh_control_handler(
        std::function<void(const VncDestination&, const std::string&)> handler);

private:
    void run();

    std::string bind_address_;
    std::uint16_t port_;
    std::atomic_bool stopping_ = false;
    int listen_fd_ = -1;
    std::string certificate_file_;
    std::string key_file_;
    SSL_CTX* tls_context_ = nullptr;
    std::function<std::string()> health_handler_;
    std::function<HttpResponse(const HttpRequest&)> api_handler_;
    std::function<std::optional<VncDestination>(const std::string&)> vnc_ticket_handler_;
    std::function<void(const VncDestination&, bool)> vnc_session_observer_;
    std::function<std::optional<VncDestination>(const std::string&)> ssh_ticket_handler_;
    std::function<void(const VncDestination&, bool)> ssh_session_observer_;
    std::function<void(const VncDestination&, const std::string&)> ssh_control_handler_;
    std::jthread thread_;
};

}  // namespace remotelink
