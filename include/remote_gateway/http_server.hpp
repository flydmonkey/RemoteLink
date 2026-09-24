#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

typedef struct ssl_ctx_st SSL_CTX;

namespace remote_gateway {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string authorization;
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
    std::jthread thread_;
};

}  // namespace remote_gateway
