#include "remote_gateway/frame_sink.hpp"
#include "remote_gateway/security_policy.hpp"
#include "remote_gateway/session.hpp"
#include "remote_gateway/test_pattern_source.hpp"
#ifdef REMOTE_GATEWAY_ENABLE_STREAMING
#include "remote_gateway/http_server.hpp"
#include "remote_gateway/webrtc_server.hpp"
#include "remote_gateway/webrtc_video_sink.hpp"
#include "remote_gateway/rdp_frame_source.hpp"
#include "remote_gateway/session_manager.hpp"
#include "remote_gateway/target_config.hpp"
#endif

#include <atomic>
#include <array>
#include <chrono>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <algorithm>
#include <memory>
#include <sstream>
#include <thread>
#include <mutex>
#include <nlohmann/json.hpp>
#include <openssl/rand.h>
#include <openssl/evp.h>

namespace {

std::atomic_bool running = true;

struct GatewayUser {
    std::string name;
    std::string token;
    bool enabled = true;
    std::string username;
    std::string password_salt;
    std::string password_hash;
    std::vector<std::string> allowed_targets;
};

std::string hex_encode(const unsigned char* bytes, std::size_t size) {
    constexpr char hex[] = "0123456789abcdef";
    std::string output(size * 2, '0');
    for (std::size_t i = 0; i < size; ++i) {
        output[i * 2] = hex[bytes[i] >> 4]; output[i * 2 + 1] = hex[bytes[i] & 0x0f];
    }
    return output;
}

std::pair<std::string, std::string> hash_password(std::string_view password) {
    std::array<unsigned char, 16> salt{};
    std::array<unsigned char, 32> hash{};
    if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1 ||
        PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), salt.data(),
            static_cast<int>(salt.size()), 210000, EVP_sha256(),
            static_cast<int>(hash.size()), hash.data()) != 1)
        throw std::runtime_error("unable to hash password");
    return {hex_encode(salt.data(), salt.size()), hex_encode(hash.data(), hash.size())};
}

bool password_matches(std::string_view password, std::string_view salt_hex,
                      std::string_view expected_hex) {
    if (salt_hex.size() != 32 || expected_hex.size() != 64) return false;
    std::array<unsigned char, 16> salt{}; std::array<unsigned char, 32> hash{};
    auto nibble = [](char c) -> int { if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10; if (c >= 'A' && c <= 'F') return c - 'A' + 10; return -1; };
    for (std::size_t i = 0; i < salt.size(); ++i) {
        const int high = nibble(salt_hex[i * 2]), low = nibble(salt_hex[i * 2 + 1]);
        if (high < 0 || low < 0) return false; salt[i] = static_cast<unsigned char>((high << 4) | low);
    }
    if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), salt.data(),
        static_cast<int>(salt.size()), 210000, EVP_sha256(), static_cast<int>(hash.size()), hash.data()) != 1) return false;
    return remote_gateway::constant_time_equal(hex_encode(hash.data(), hash.size()), expected_hex);
}

std::string generate_access_token() {
    std::array<unsigned char, 32> bytes{};
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1)
        throw std::runtime_error("unable to generate access token");
    return hex_encode(bytes.data(), bytes.size());
}

std::string generate_connection_id() {
    std::array<unsigned char, 8> bytes{};
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1)
        throw std::runtime_error("unable to generate connection id");
    return "connection-" + hex_encode(bytes.data(), bytes.size());
}

void save_users(const std::filesystem::path& path, const std::vector<GatewayUser>& users) {
    nlohmann::json data = nlohmann::json::array();
    for (const auto& user : users)
        data.push_back({{"name", user.name}, {"token", user.token}, {"enabled", user.enabled},
            {"username", user.username}, {"passwordSalt", user.password_salt},
            {"passwordHash", user.password_hash}, {"allowedTargets", user.allowed_targets}});
    const auto temporary = path.string() + ".tmp";
    { std::ofstream output(temporary, std::ios::trunc); output << data.dump(2) << '\n';
      if (!output) throw std::runtime_error("cannot save user registry"); }
    std::filesystem::permissions(temporary, std::filesystem::perms::owner_read |
        std::filesystem::perms::owner_write, std::filesystem::perm_options::replace);
    std::filesystem::rename(temporary, path);
}

std::vector<remote_gateway::TargetConfig> load_managed_connections(
    const std::filesystem::path& path, const std::string& allowed_hosts) {
    std::vector<remote_gateway::TargetConfig> result;
    if (!std::filesystem::is_regular_file(path)) return result;
    std::ifstream input(path);
    const auto data = nlohmann::json::parse(input, nullptr, false);
    if (!data.is_array()) throw std::runtime_error("invalid managed connection registry");
    for (const auto& item : data) {
        remote_gateway::TargetConfig target;
        target.id = item.value("id", ""); target.name = item.value("name", "");
        target.rdp.hostname = item.value("host", "");
        target.rdp.port = static_cast<std::uint16_t>(item.value("port", 3389));
        target.rdp.username = item.value("username", "");
        target.rdp.password = item.value("password", "");
        target.rdp.width = item.value("width", 1920U); target.rdp.height = item.value("height", 1080U);
        target.rdp.ignore_certificate = item.value("ignoreCertificate", true);
        if (target.id.empty() || target.name.empty() || target.rdp.hostname.empty() ||
            !remote_gateway::host_is_allowed(target.rdp.hostname, allowed_hosts))
            throw std::runtime_error("invalid managed connection: " + target.id);
        result.push_back(std::move(target));
    }
    return result;
}

void save_managed_connections(const std::filesystem::path& path,
                              const std::vector<remote_gateway::TargetConfig>& targets) {
    nlohmann::json data = nlohmann::json::array();
    for (const auto& target : targets) data.push_back({
        {"id", target.id}, {"name", target.name}, {"host", target.rdp.hostname},
        {"port", target.rdp.port}, {"username", target.rdp.username},
        {"password", target.rdp.password}, {"width", target.rdp.width},
        {"height", target.rdp.height}, {"ignoreCertificate", target.rdp.ignore_certificate}});
    const auto temporary = path.string() + ".tmp";
    { std::ofstream output(temporary, std::ios::trunc); output << data.dump(2) << '\n';
      if (!output) throw std::runtime_error("cannot save managed connections"); }
    std::filesystem::permissions(temporary, std::filesystem::perms::owner_read |
        std::filesystem::perms::owner_write, std::filesystem::perm_options::replace);
    std::filesystem::rename(temporary, path);
}

void request_stop(int) {
    running = false;
}

std::string percent_decode(std::string_view value) {
    std::string output;
    output.reserve(value.size());
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const int high = hex(value[i + 1]), low = hex(value[i + 2]);
            if (high >= 0 && low >= 0) { output.push_back(static_cast<char>((high << 4) | low)); i += 2; continue; }
        }
        output.push_back(value[i]);
    }
    return output;
}

std::string required_secret(const char* environment_name, const char* file_environment_name) {
    if (const char* value = std::getenv(environment_name); value != nullptr && *value != '\0') return value;
    const char* path = std::getenv(file_environment_name);
    if (path == nullptr || *path == '\0') throw std::runtime_error(std::string(environment_name) + " or " + file_environment_name + " is required");
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error(std::string("cannot read secret file: ") + file_environment_name);
    std::string value(std::istreambuf_iterator<char>(stream), {});
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) value.pop_back();
    if (value.empty()) throw std::runtime_error(std::string("secret file is empty: ") + file_environment_name);
    return value;
}

class MetricsSink final : public remote_gateway::FrameSink {
public:
    void consume(const remote_gateway::Frame& frame) override {
        ++frames_;
        const auto now = std::chrono::steady_clock::now();
        if (now - last_report_ >= std::chrono::seconds(1)) {
            std::cout << "processed=" << frames_.exchange(0)
                      << " fps latest_sequence=" << frame.sequence << '\n';
            last_report_ = now;
        }
    }

private:
    std::atomic_uint64_t frames_ = 0;
    std::chrono::steady_clock::time_point last_report_ = std::chrono::steady_clock::now();
};

}  // namespace

int main() {
    try {
    std::signal(SIGINT, request_stop);
    std::signal(SIGTERM, request_stop);

    std::cout << "RemoteLink 0.1.0\n";

#ifdef REMOTE_GATEWAY_ENABLE_STREAMING
    const std::string access_token = required_secret("RG_ACCESS_TOKEN", "RG_ACCESS_TOKEN_FILE");
    std::vector<std::string> access_tokens {access_token};
    if (const char* additional = std::getenv("RG_ACCESS_TOKENS"); additional && *additional) {
        auto parsed = remote_gateway::parse_access_tokens(additional);
        access_tokens.insert(access_tokens.end(), parsed.begin(), parsed.end());
    }
    const char* certificate = std::getenv("RG_TLS_CERTIFICATE");
    const char* private_key = std::getenv("RG_TLS_PRIVATE_KEY");
    const bool tls_enabled = certificate != nullptr && *certificate != '\0' &&
                             private_key != nullptr && *private_key != '\0';
    const char* tls_proxy_environment = std::getenv("RG_BEHIND_TLS_PROXY");
    const bool tls_proxy = tls_proxy_environment != nullptr && *tls_proxy_environment != '\0';
    if (!tls_enabled && !tls_proxy && std::getenv("RG_ALLOW_INSECURE_HTTP") == nullptr) {
        throw std::runtime_error(
            "RG_TLS_CERTIFICATE and RG_TLS_PRIVATE_KEY are required "
            "(set RG_BEHIND_TLS_PROXY=1 behind a TLS reverse proxy, or "
            "RG_ALLOW_INSECURE_HTTP=1 only for localhost development)");
    }
    const char* targets_file = std::getenv("RG_TARGETS_FILE");
    const char* allowed_hosts_environment = std::getenv("RG_ALLOWED_HOSTS");
    const std::string allowed_hosts = allowed_hosts_environment && *allowed_hosts_environment
        ? allowed_hosts_environment : "*";
    if (targets_file == nullptr || *targets_file == '\0') throw std::runtime_error("RG_TARGETS_FILE is required");
    const char* state_directory = std::getenv("RG_STATE_DIR");
    const std::filesystem::path state_root = state_directory && *state_directory
        ? state_directory : "/var/lib/remote-gateway";
    std::filesystem::create_directories(state_root / "files");
    std::filesystem::create_directories(state_root / "print-jobs");
    const auto users_path = state_root / "users.json";
    std::vector<GatewayUser> users;
    if (std::filesystem::is_regular_file(users_path)) {
        std::ifstream input(users_path);
        const auto data = nlohmann::json::parse(input, nullptr, false);
        if (!data.is_array()) throw std::runtime_error("invalid user registry");
        for (const auto& item : data) users.push_back({item.value("name", ""),
            item.value("token", ""), item.value("enabled", true),
            item.value("username", ""), item.value("passwordSalt", ""),
            item.value("passwordHash", ""), item.value("allowedTargets", std::vector<std::string>{})});
    }
    if (users.empty()) {
        for (std::size_t index = 0; index < access_tokens.size(); ++index)
            users.push_back({index == 0 ? "主管理员" : "用户 " + std::to_string(index),
                             access_tokens[index], true, index == 0 ? "admin" : "", "", "", {}});
    }
    // The primary administrator credential remains controlled by the protected
    // secret file so a lost registry can always be recovered after a restart.
    users[0].name = users[0].name.empty() ? "主管理员" : users[0].name;
    users[0].token = access_token;
    users[0].enabled = true;
    users[0].username = "admin";
    if (users[0].password_hash.empty()) {
        std::string initial_admin_password = "admin";
        if (const char* password_file = std::getenv("RG_INITIAL_ADMIN_PASSWORD_FILE");
            password_file && *password_file) {
            std::ifstream input(password_file, std::ios::binary);
            if (!input) throw std::runtime_error("cannot read initial administrator password");
            initial_admin_password.assign(std::istreambuf_iterator<char>(input), {});
            while (!initial_admin_password.empty() &&
                   (initial_admin_password.back() == '\n' || initial_admin_password.back() == '\r'))
                initial_admin_password.pop_back();
            if (initial_admin_password.size() < 4)
                throw std::runtime_error("initial administrator password is too short");
        }
        const auto [salt, hash] = hash_password(initial_admin_password);
        users[0].password_salt = salt; users[0].password_hash = hash;
    }
    access_tokens.clear();
    for (const auto& user : users) access_tokens.push_back(user.enabled ? user.token : "");
    save_users(users_path, users);
    std::mutex users_mutex;
    const auto connections_path = state_root / "connections.json";
    const auto connections_migration_marker = state_root / "connections.migrated";
    const auto configured_targets = remote_gateway::load_targets(targets_file, allowed_hosts);
    auto managed_targets = load_managed_connections(connections_path, allowed_hosts);
    if (!std::filesystem::exists(connections_migration_marker)) {
        for (const auto& target : configured_targets) {
            const bool duplicate = std::any_of(managed_targets.begin(), managed_targets.end(),
                [&](const auto& existing) { return existing.id == target.id; });
            if (!duplicate) managed_targets.push_back(target);
        }
        save_managed_connections(connections_path, managed_targets);
        std::ofstream marker(connections_migration_marker, std::ios::trunc);
        marker << "Legacy target configuration migrated to managed connections.\n";
        if (!marker) throw std::runtime_error("cannot save connection migration marker");
        std::filesystem::permissions(connections_migration_marker,
            std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
            std::filesystem::perm_options::replace);
    }
    auto target_catalog = managed_targets;
    remote_gateway::HttpServer http(
        "0.0.0.0", 18080, certificate ? certificate : "", private_key ? private_key : "");
    // Signaling remains an in-process backend on loopback. HttpServer exposes
    // it publicly as /ws on the same HTTPS port as the UI and REST API.
    remote_gateway::WebRtcServer webrtc(18081, access_tokens);
    std::vector<remote_gateway::WebRtcServer::PublicTarget> public_targets;
    for (const auto& target : target_catalog) public_targets.push_back({
        target.id, target.name, target.rdp.hostname, target.rdp.username,
        target.rdp.width, target.rdp.height});
    webrtc.set_targets(std::move(public_targets));
    {
        std::vector<std::vector<std::string>> permissions;
        for (const auto& user : users) permissions.push_back(user.allowed_targets);
        webrtc.set_user_target_permissions(std::move(permissions));
    }
    remote_gateway::SessionManager sessions(webrtc, target_catalog, allowed_hosts);
    webrtc.set_start_handler([&sessions](const std::string& peer, const std::string& target,
                                         const std::string& host, const std::string& username,
                                         const std::string& password,
                                         std::uint32_t width, std::uint32_t height,
                                         std::uint32_t bitrate, bool audio_playback,
                                         bool redirect_printers, bool redirect_files,
                                         std::size_t user_identity,
                                         std::string& error) {
        return sessions.start(peer, target, host, username, password, width, height,
                              bitrate, audio_playback, redirect_printers, redirect_files,
                              user_identity, error);
    });
    webrtc.set_input_handler([&sessions](const std::string& peer, const std::string& input) { sessions.input(peer, input); });
    webrtc.set_key_frame_handler([&sessions](const std::string& peer) { sessions.request_key_frame(peer); });
    webrtc.set_close_handler([&sessions](const std::string& peer) { sessions.stop(peer); });
    // Keep the unauthenticated liveness endpoint intentionally minimal.
    // Detailed session and performance data is available through the
    // authenticated admin API below.
    http.set_health_handler([] { return std::string("{\"status\":\"ok\"}\n"); });
    http.set_api_handler([&sessions, &webrtc, &access_tokens, &users, &users_mutex,
                          &users_path, &target_catalog, &managed_targets, &connections_path,
                          &allowed_hosts, tls_enabled](
                             const remote_gateway::HttpRequest& request) {
        using json = nlohmann::json;
        remote_gateway::HttpResponse response;
        if (request.method == "POST" && request.path == "/api/auth/login") {
            const auto payload = json::parse(request.body, nullptr, false);
            const std::string username = payload.is_object() ? payload.value("username", "") : "";
            const std::string password = payload.is_object() ? payload.value("password", "") : "";
            std::lock_guard lock(users_mutex);
            for (std::size_t id = 0; id < users.size(); ++id) {
                const auto& user = users[id];
                if (user.enabled && remote_gateway::constant_time_equal(username, user.username) &&
                    password_matches(password, user.password_salt, user.password_hash)) {
                    response.body = json{{"token", user.token}, {"name", user.name},
                        {"admin", id == 0}}.dump(); return response;
                }
            }
            response.status = 401; response.body = json{{"error", "invalid username or password"}}.dump();
            return response;
        }
        const std::string prefix = "Bearer ";
        const std::string supplied = request.authorization.starts_with(prefix)
            ? request.authorization.substr(prefix.size()) : std::string();
        std::optional<std::size_t> user_identity;
        { std::lock_guard lock(users_mutex);
          user_identity = remote_gateway::access_token_identity(supplied, access_tokens); }
        if (!user_identity) {
            response.status = 401;
            response.body = json{{"error", "invalid access token"}}.dump();
            return response;
        }
        if (request.method == "GET" && request.path == "/api/auth/me") {
            std::lock_guard lock(users_mutex);
            response.body = json{{"id", *user_identity}, {"name", users[*user_identity].name},
                {"username", users[*user_identity].username}, {"admin", *user_identity == 0}}.dump();
            return response;
        }
        if (*user_identity != 0 &&
            (request.path == "/api/admin/state" || request.path == "/api/admin/disconnect")) {
            response.status = 403;
            response.body = json{{"error", "administrator required"}}.dump();
            return response;
        }
        if (request.path.starts_with("/api/admin/connections")) {
            if (*user_identity != 0) {
                response.status = 403; response.body = json{{"error", "primary administrator required"}}.dump(); return response;
            }
            if (request.method == "POST" && request.path == "/api/admin/connections/create") {
                const auto payload = json::parse(request.body, nullptr, false);
                std::string id;
                do { id = generate_connection_id(); }
                while (std::any_of(target_catalog.begin(), target_catalog.end(),
                    [&](const auto& target) { return target.id == id; }));
                const std::string name = payload.is_object() ? payload.value("name", "") : "";
                const std::string host = payload.is_object() ? payload.value("host", "") : "";
                const std::string username = payload.is_object() ? payload.value("username", "") : "";
                const std::string password = payload.is_object() ? payload.value("password", "") : "";
                const int port = payload.is_object() ? payload.value("port", 3389) : 0;
                if (name.empty() || name.size() > 128 || host.empty() || host.size() > 255 ||
                    port < 1 || port > 65535 || username.size() > 256 || password.size() > 4096 ||
                    !remote_gateway::host_is_allowed(host, allowed_hosts)) {
                    response.status = 400; response.body = json{{"error", "invalid connection"}}.dump(); return response;
                }
                remote_gateway::TargetConfig target;
                target.id = id; target.name = name; target.rdp.hostname = host;
                target.rdp.port = static_cast<std::uint16_t>(port);
                target.rdp.username = username; target.rdp.password = password;
                target.rdp.width = payload.value("width", 1920U);
                target.rdp.height = payload.value("height", 1080U);
                target.rdp.ignore_certificate = payload.value("ignoreCertificate", true);
                if (target.rdp.width < 640 || target.rdp.width > 7680 ||
                    target.rdp.height < 480 || target.rdp.height > 4320) {
                    response.status = 400; response.body = json{{"error", "invalid dimensions"}}.dump(); return response;
                }
                managed_targets.push_back(target); target_catalog.push_back(target);
                save_managed_connections(connections_path, managed_targets);
                sessions.set_targets(target_catalog);
                std::vector<remote_gateway::WebRtcServer::PublicTarget> published;
                for (const auto& item : target_catalog) published.push_back({item.id, item.name,
                    item.rdp.hostname, item.rdp.username, item.rdp.width, item.rdp.height});
                webrtc.set_targets(std::move(published));
                response.body = json{{"id", id}, {"name", name}}.dump(); return response;
            }
            if (request.method == "POST" && request.path == "/api/admin/connections/update") {
                const auto payload = json::parse(request.body, nullptr, false);
                const std::string id = payload.is_object() ? payload.value("id", "") : "";
                auto managed = std::find_if(managed_targets.begin(), managed_targets.end(),
                    [&](const auto& target) { return target.id == id; });
                auto catalog = std::find_if(target_catalog.begin(), target_catalog.end(),
                    [&](const auto& target) { return target.id == id; });
                if (managed == managed_targets.end() || catalog == target_catalog.end()) {
                    response.status = 404; response.body = json{{"error", "connection not found"}}.dump(); return response;
                }
                const std::string name = payload.value("name", "");
                const std::string host = payload.value("host", "");
                const std::string username = payload.value("username", "");
                const std::string password = payload.value("password", "");
                const int port = payload.value("port", 3389);
                if (name.empty() || name.size() > 128 || host.empty() || host.size() > 255 ||
                    port < 1 || port > 65535 || username.size() > 256 || password.size() > 4096 ||
                    !remote_gateway::host_is_allowed(host, allowed_hosts)) {
                    response.status = 400; response.body = json{{"error", "invalid connection"}}.dump(); return response;
                }
                auto updated = *managed;
                updated.name = name; updated.rdp.hostname = host;
                updated.rdp.port = static_cast<std::uint16_t>(port);
                updated.rdp.username = username;
                if (!password.empty()) updated.rdp.password = password;
                *managed = updated; *catalog = updated;
                save_managed_connections(connections_path, managed_targets);
                sessions.set_targets(target_catalog);
                std::vector<remote_gateway::WebRtcServer::PublicTarget> published;
                for (const auto& item : target_catalog) published.push_back({item.id, item.name,
                    item.rdp.hostname, item.rdp.username, item.rdp.width, item.rdp.height});
                webrtc.set_targets(std::move(published));
                response.body = json{{"id", id}, {"name", name}}.dump(); return response;
            }
            if (request.method == "POST" && request.path == "/api/admin/connections/delete") {
                const auto payload = json::parse(request.body, nullptr, false);
                const std::string id = payload.is_object() ? payload.value("id", "") : "";
                const auto managed = std::find_if(managed_targets.begin(), managed_targets.end(),
                    [&](const auto& target) { return target.id == id; });
                if (managed == managed_targets.end()) {
                    response.status = 404; response.body = json{{"error", "connection not found"}}.dump(); return response;
                }
                const auto snapshots = sessions.target_snapshots();
                const auto active = std::find_if(snapshots.begin(), snapshots.end(),
                    [&](const auto& target) { return target.id == id && target.busy; });
                if (active != snapshots.end()) {
                    response.status = 409; response.body = json{{"error", "connection has an active session"}}.dump(); return response;
                }
                managed_targets.erase(managed);
                std::erase_if(target_catalog, [&](const auto& target) { return target.id == id; });
                save_managed_connections(connections_path, managed_targets);
                sessions.set_targets(target_catalog);
                std::vector<remote_gateway::WebRtcServer::PublicTarget> published;
                for (const auto& item : target_catalog) published.push_back({item.id, item.name,
                    item.rdp.hostname, item.rdp.username, item.rdp.width, item.rdp.height});
                webrtc.set_targets(std::move(published));
                {
                    std::lock_guard lock(users_mutex);
                    std::vector<std::vector<std::string>> permissions;
                    for (auto& user : users) {
                        std::erase(user.allowed_targets, id);
                        permissions.push_back(user.allowed_targets);
                    }
                    save_users(users_path, users);
                    webrtc.set_user_target_permissions(std::move(permissions));
                }
                response.body = json{{"id", id}, {"status", "deleted"}}.dump(); return response;
            }
            response.status = 404; response.body = json{{"error", "not found"}}.dump(); return response;
        }
        if (request.path.starts_with("/api/admin/users")) {
            if (*user_identity != 0) {
                response.status = 403;
                response.body = json{{"error", "primary administrator required"}}.dump();
                return response;
            }
            auto publish_users = [&] {
                access_tokens.clear();
                std::vector<std::vector<std::string>> permissions;
                for (const auto& user : users)
                    { access_tokens.push_back(user.enabled ? user.token : "");
                      permissions.push_back(user.allowed_targets); }
                save_users(users_path, users);
                webrtc.set_access_tokens(access_tokens);
                webrtc.set_user_target_permissions(std::move(permissions));
            };
            std::lock_guard lock(users_mutex);
            if (request.method == "GET" && request.path == "/api/admin/users") {
                json items = json::array();
                for (std::size_t id = 0; id < users.size(); ++id)
                    items.push_back({{"id", id}, {"name", users[id].name},
                        {"username", users[id].username}, {"enabled", users[id].enabled},
                        {"admin", id == 0}, {"allowedTargets", users[id].allowed_targets}});
                response.body = json{{"items", std::move(items)}}.dump();
                return response;
            }
            const auto payload = json::parse(request.body, nullptr, false);
            if (!payload.is_object()) {
                response.status = 400; response.body = json{{"error", "invalid request"}}.dump(); return response;
            }
            if (request.method == "POST" && request.path == "/api/admin/users/create") {
                std::string name = payload.value("name", "");
                const std::string username = payload.value("username", "");
                const std::string password = payload.value("password", "");
                while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front()))) name.erase(name.begin());
                while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.pop_back();
                const bool duplicate = std::any_of(users.begin(), users.end(), [&](const auto& user) {
                    return user.username == username;
                });
                if (name.empty() || name.size() > 64 || username.size() < 2 || username.size() > 64 ||
                    password.size() < 4 || password.size() > 256 || duplicate) {
                    response.status = 400; response.body = json{{"error", "invalid or duplicate account details"}}.dump(); return response;
                }
                const std::string token = generate_access_token();
                const auto [salt, hash] = hash_password(password);
                users.push_back({name, token, true, username, salt, hash, {}});
                publish_users();
                response.body = json{{"id", users.size() - 1}, {"name", name}}.dump();
                return response;
            }
            const std::size_t id = payload.value("id", users.size());
            if (id >= users.size()) {
                response.status = 404; response.body = json{{"error", "user not found"}}.dump(); return response;
            }
            if (request.method == "POST" && request.path == "/api/admin/users/toggle") {
                const bool enabled = payload.value("enabled", true);
                if (id == 0 && !enabled) {
                    response.status = 400; response.body = json{{"error", "primary administrator cannot be disabled"}}.dump(); return response;
                }
                users[id].enabled = enabled;
                publish_users();
                response.body = json{{"status", enabled ? "enabled" : "disabled"}}.dump(); return response;
            }
            if (request.method == "POST" && request.path == "/api/admin/users/rotate") {
                const std::string password = payload.value("password", "");
                if (password.size() < 4 || password.size() > 256) {
                    response.status = 400; response.body = json{{"error", "password must be 4-256 characters"}}.dump(); return response;
                }
                const auto [salt, hash] = hash_password(password);
                users[id].password_salt = salt; users[id].password_hash = hash;
                users[id].token = id == 0 ? users[id].token : generate_access_token();
                users[id].enabled = true;
                publish_users();
                response.body = json{{"id", id}, {"name", users[id].name}}.dump(); return response;
            }
            if (request.method == "POST" && request.path == "/api/admin/users/permissions") {
                if (id == 0 || !payload.contains("targetIds") || !payload["targetIds"].is_array()) {
                    response.status = 400; response.body = json{{"error", "invalid permissions"}}.dump(); return response;
                }
                std::vector<std::string> allowed;
                const auto snapshots = sessions.target_snapshots();
                for (const auto& value : payload["targetIds"]) {
                    if (!value.is_string()) continue;
                    const auto target_id = value.get<std::string>();
                    if (std::any_of(snapshots.begin(), snapshots.end(), [&](const auto& target) {
                        return target.id == target_id;
                    }) && std::find(allowed.begin(), allowed.end(), target_id) == allowed.end())
                        allowed.push_back(target_id);
                }
                users[id].allowed_targets = std::move(allowed);
                publish_users();
                response.body = json{{"id", id}, {"allowedTargets", users[id].allowed_targets}}.dump(); return response;
            }
            response.status = 404; response.body = json{{"error", "not found"}}.dump(); return response;
        }
        const char* configured_state = std::getenv("RG_STATE_DIR");
        const std::filesystem::path file_directory =
            std::filesystem::path(configured_state && *configured_state
                ? configured_state : "/var/lib/remote-gateway") / "users" /
                std::to_string(*user_identity) / "files";
        const std::filesystem::path print_directory =
            std::filesystem::path(configured_state && *configured_state
                ? configured_state : "/var/lib/remote-gateway") / "print-jobs";
        const std::filesystem::path audit_path =
            std::filesystem::path(configured_state && *configured_state
                ? configured_state : "/var/lib/remote-gateway") / "file-audit.jsonl";
        auto audit_file_action = [&audit_path, user_identity](std::string_view action,
                                                               const std::filesystem::path& path,
                                                               std::uint64_t size = 0) {
            std::ofstream audit(audit_path, std::ios::app);
            audit << nlohmann::json{{"timestampMs", std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()},
                {"user", *user_identity}, {"action", action},
                {"path", path.generic_string()}, {"size", size}}.dump() << '\n';
        };
        auto managed_file_path = [&file_directory](const std::string& encoded_name) {
            const std::string name = percent_decode(encoded_name);
            const std::filesystem::path relative(name);
            if (name.size() > 1024 || relative.is_absolute()) return std::filesystem::path{};
            for (const auto& component : relative) {
                if (component == "..") return std::filesystem::path{};
            }
            const auto normalized = relative.lexically_normal();
            return normalized.empty() || normalized == "." ? file_directory : file_directory / normalized;
        };
        const std::uint64_t file_quota = [] {
            const char* value = std::getenv("RG_USER_FILE_QUOTA_BYTES");
            if (!value || !*value) return 5ULL * 1024 * 1024 * 1024;
            try { return std::stoull(value); } catch (...) { return 5ULL * 1024 * 1024 * 1024; }
        }();
        auto file_usage = [&file_directory] {
            std::uint64_t total = 0; std::error_code error;
            for (std::filesystem::recursive_directory_iterator it(file_directory, error), end;
                 !error && it != end; it.increment(error)) if (it->is_regular_file(error)) total += it->file_size(error);
            return total;
        };
        if (request.method == "GET" && request.path == "/api/admin/files") {
            json items = json::array();
            std::error_code error;
            std::filesystem::create_directories(file_directory, error);
            const auto directory = managed_file_path(request.file_name);
            if (directory.empty() || !std::filesystem::is_directory(directory)) {
                response.status = 404; response.body = json{{"error", "directory not found"}}.dump(); return response;
            }
            for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
                const auto modified = std::chrono::time_point_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now() +
                    (entry.last_write_time() - std::filesystem::file_time_type::clock::now()));
                const bool is_directory = entry.is_directory(error);
                items.push_back({{"name", entry.path().filename().string()}, {"directory", is_directory},
                                 {"size", is_directory ? 0 : entry.file_size()},
                                 {"modifiedMs", modified.time_since_epoch().count()}});
            }
            response.body = json{{"items", std::move(items)}, {"usage", file_usage()}, {"quota", file_quota}}.dump();
            return response;
        }
        if (request.method == "POST" && request.path == "/api/admin/files/upload") {
            const auto path = managed_file_path(request.file_name);
            if (path.empty() || request.body.size() > 64 * 1024 * 1024) {
                response.status = 400; response.body = json{{"error", "invalid file"}}.dump(); return response;
            }
            if (const char* blocked = std::getenv("RG_BLOCKED_FILE_EXTENSIONS"); blocked && *blocked) {
                std::string extension = path.extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                const auto blocked_extensions = remote_gateway::parse_access_tokens(blocked);
                if (std::find(blocked_extensions.begin(), blocked_extensions.end(), extension) != blocked_extensions.end()) {
                    response.status = 415; response.body = json{{"error", "file type blocked"}}.dump(); return response;
                }
            }
            std::error_code error; std::filesystem::create_directories(path.parent_path(), error);
            std::uint64_t offset = 0, total = request.body.size();
            try {
                if (!request.upload_offset.empty()) offset = std::stoull(request.upload_offset);
                if (!request.upload_total.empty()) total = std::stoull(request.upload_total);
            } catch (...) { response.status = 400; response.body = json{{"error", "invalid upload range"}}.dump(); return response; }
            if (total > 4ULL * 1024 * 1024 * 1024 || offset + request.body.size() > total) {
                response.status = 400; response.body = json{{"error", "invalid upload size"}}.dump(); return response;
            }
            const std::uint64_t existing = std::filesystem::is_regular_file(path, error)
                ? std::filesystem::file_size(path, error) : 0;
            if (offset == 0 && file_usage() - existing + total > file_quota) {
                response.status = 413; response.body = json{{"error", "quota exceeded"}}.dump(); return response;
            }
            std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out |
                                    (offset == 0 ? std::ios::trunc : std::ios::app));
            if (!file && offset > 0) { response.status = 409; response.body = json{{"error", "missing upload"}}.dump(); return response; }
            if (offset > 0 && std::filesystem::file_size(path, error) != offset) {
                response.status = 409; response.body = json{{"error", "upload offset mismatch"}}.dump(); return response;
            }
            file.write(request.body.data(), static_cast<std::streamsize>(request.body.size()));
            if (!file) { response.status = 500; response.body = json{{"error", "write failed"}}.dump(); return response; }
            if (offset + request.body.size() == total) audit_file_action("upload", path.lexically_relative(file_directory), total);
            response.body = json{{"status", offset + request.body.size() == total ? "uploaded" : "partial"},
                                 {"received", offset + request.body.size()}, {"name", path.filename().string()}}.dump(); return response;
        }
        if (request.method == "POST" && request.path == "/api/admin/files/download") {
            const auto path = managed_file_path(request.file_name);
            if (path.empty() || !std::filesystem::is_regular_file(path)) {
                response.status = 404; response.body = json{{"error", "file not found"}}.dump(); return response;
            }
            std::ifstream file(path, std::ios::binary);
            audit_file_action("download", path.lexically_relative(file_directory), std::filesystem::file_size(path));
            response.content_type = "application/octet-stream";
            response.body.assign(std::istreambuf_iterator<char>(file), {}); return response;
        }
        if (request.method == "POST" && request.path == "/api/admin/files/delete") {
            const auto path = managed_file_path(request.file_name); std::error_code error;
            if (path.empty() || path == file_directory || std::filesystem::remove_all(path, error) == 0) {
                response.status = 404; response.body = json{{"error", "file not found"}}.dump(); return response;
            }
            audit_file_action("delete", path.lexically_relative(file_directory));
            response.body = json{{"status", "deleted"}}.dump(); return response;
        }
        if (request.method == "POST" && request.path == "/api/admin/files/mkdir") {
            const auto path = managed_file_path(request.file_name); std::error_code error;
            if (path.empty() || path == file_directory || !std::filesystem::create_directories(path, error)) {
                response.status = 400; response.body = json{{"error", "unable to create directory"}}.dump(); return response;
            }
            audit_file_action("mkdir", path.lexically_relative(file_directory));
            response.body = json{{"status", "created"}}.dump(); return response;
        }
        if (request.method == "POST" && request.path == "/api/admin/files/move") {
            const auto source = managed_file_path(request.file_name);
            const auto destination = managed_file_path(request.destination);
            std::error_code error;
            if (source.empty() || destination.empty() || source == file_directory ||
                std::filesystem::exists(destination) || !std::filesystem::exists(source)) {
                response.status = 400; response.body = json{{"error", "invalid move"}}.dump(); return response;
            }
            std::filesystem::create_directories(destination.parent_path(), error);
            std::filesystem::rename(source, destination, error);
            if (error) { response.status = 500; response.body = json{{"error", "move failed"}}.dump(); return response; }
            audit_file_action("move", destination.lexically_relative(file_directory));
            response.body = json{{"status", "moved"}}.dump(); return response;
        }
        auto print_jobs = [&print_directory, user_identity] {
            std::vector<std::filesystem::directory_entry> jobs;
            std::error_code error;
            for (std::filesystem::directory_iterator it(print_directory, error), end;
                 !error && it != end; it.increment(error)) {
                if (it->is_regular_file(error) && it->path().extension() == ".pdf") {
                    auto owner_path = it->path(); owner_path += ".owner";
                    std::string owner;
                    { std::ifstream owner_file(owner_path); owner_file >> owner; }
                    const std::string expected = std::to_string(*user_identity);
                    if (owner.empty()) {
                        std::ofstream owner_file(owner_path, std::ios::trunc);
                        owner_file << expected;
                        owner = expected;
                    }
                    if (owner == expected) jobs.push_back(*it);
                }
            }
            std::sort(jobs.begin(), jobs.end(), [](const auto& left, const auto& right) {
                return left.last_write_time() > right.last_write_time();
            });
            const auto cutoff = std::filesystem::file_time_type::clock::now() -
                                std::chrono::hours(24);
            for (std::size_t index = 0; index < jobs.size(); ++index) {
                if (index >= 100 || jobs[index].last_write_time() < cutoff) {
                    std::error_code remove_error;
                    std::filesystem::remove(jobs[index].path(), remove_error);
                    auto owner_path = jobs[index].path(); owner_path += ".owner";
                    std::filesystem::remove(owner_path, remove_error);
                }
            }
            jobs.erase(std::remove_if(jobs.begin(), jobs.end(), [&](const auto& entry) {
                return !std::filesystem::exists(entry.path());
            }), jobs.end());
            return jobs;
        };
        auto print_path = [&print_directory, user_identity](const std::string& name) {
            if (name.empty() || name != std::filesystem::path(name).filename().string() ||
                std::filesystem::path(name).extension() != ".pdf") return std::filesystem::path{};
            const auto path = print_directory / name;
            auto owner_path = path; owner_path += ".owner";
            std::string owner;
            { std::ifstream owner_file(owner_path); owner_file >> owner; }
            return owner == std::to_string(*user_identity) ? path : std::filesystem::path{};
        };
        if (request.method == "GET" && request.path == "/api/admin/prints") {
            json items = json::array();
            for (const auto& entry : print_jobs()) {
                const auto modified = std::chrono::time_point_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now() +
                    (entry.last_write_time() - std::filesystem::file_time_type::clock::now()));
                items.push_back({{"name", entry.path().filename().string()},
                                 {"size", entry.file_size()},
                                 {"modifiedMs", modified.time_since_epoch().count()}});
            }
            response.body = json{{"items", std::move(items)}}.dump();
            return response;
        }
        if (request.method == "POST" && request.path == "/api/admin/prints/download") {
            const auto payload = json::parse(request.body, nullptr, false);
            const auto path = print_path(payload.is_object() ? payload.value("name", "") : "");
            if (path.empty() || !std::filesystem::is_regular_file(path)) {
                response.status = 404;
                response.body = json{{"error", "no print jobs"}}.dump();
                return response;
            }
            std::ifstream file(path, std::ios::binary);
            if (!file) {
                response.status = 500;
                response.body = json{{"error", "cannot read print job"}}.dump();
                return response;
            }
            response.content_type = "application/pdf";
            response.body.assign(std::istreambuf_iterator<char>(file), {});
            return response;
        }
        if (request.method == "POST" && request.path == "/api/admin/prints/delete") {
            const auto payload = json::parse(request.body, nullptr, false);
            const auto path = print_path(payload.is_object() ? payload.value("name", "") : "");
            std::error_code error;
            if (path.empty() || !std::filesystem::remove(path, error)) {
                response.status = 404;
                response.body = json{{"error", "print job not found"}}.dump();
                return response;
            }
            auto owner_path = path; owner_path += ".owner";
            std::filesystem::remove(owner_path, error);
            response.body = json{{"status", "deleted"}}.dump();
            return response;
        }
        if (request.method == "POST" && request.path == "/api/admin/prints/delete-all") {
            std::size_t removed = 0;
            for (const auto& entry : print_jobs()) {
                std::error_code error;
                if (std::filesystem::remove(entry.path(), error)) ++removed;
                auto owner_path = entry.path(); owner_path += ".owner";
                std::filesystem::remove(owner_path, error);
            }
            response.body = json{{"status", "deleted"}, {"count", removed}}.dump();
            return response;
        }
        if (request.method == "GET" && request.path == "/api/admin/state") {
            json targets_json = json::array();
            for (const auto& target : sessions.target_snapshots()) {
                targets_json.push_back({
                    {"id", target.id}, {"name", target.name},
                    {"host", target.host}, {"username", target.username}, {"port", target.port},
                    {"managed", std::any_of(managed_targets.begin(), managed_targets.end(),
                        [&](const auto& item) { return item.id == target.id; })},
                    {"width", target.width}, {"height", target.height},
                    {"busy", target.busy}, {"peerId", target.peer_id},
                    {"state", target.state},
                    {"connectedSeconds", target.connected_seconds},
                    {"capturedFrames", target.captured_frames},
                    {"encodedFrames", target.encoded_frames},
                    {"droppedFrames", target.dropped_frames},
                    {"sentBytes", target.sent_bytes}
                });
            }
            json events_json = json::array();
            for (const auto& event : sessions.recent_events()) {
                events_json.push_back({
                    {"timestampMs", event.timestamp_ms}, {"type", event.type},
                    {"targetId", event.target_id}, {"peerId", event.peer_id},
                    {"message", event.message}
                });
            }
            response.body = json{
                {"status", "ok"}, {"tls", tls_enabled},
                {"encoderBackend", "openh264"},
                {"hardwareEncodingAvailable", false},
                {"peers", webrtc.peer_count()},
                {"sessions", sessions.session_count()},
                {"capturedFrames", sessions.captured_frames()},
                {"encodedFrames", sessions.encoded_frames()},
                {"droppedFrames", sessions.dropped_frames()},
                {"sentBytes", sessions.sent_bytes()},
                {"targets", std::move(targets_json)},
                {"events", std::move(events_json)}
            }.dump();
            return response;
        }
        if (request.method == "POST" && request.path == "/api/admin/disconnect") {
            try {
                const auto payload = json::parse(request.body);
                const std::string peer_id = payload.value("peerId", "");
                if (peer_id.empty()) throw std::runtime_error("peerId is required");
                sessions.record_admin_disconnect(peer_id);
                if (!webrtc.disconnect_peer(peer_id)) {
                    response.status = 404;
                    response.body = json{{"error", "session not found"}}.dump();
                    return response;
                }
                response.body = json{{"status", "disconnecting"}}.dump();
                return response;
            }
            catch (const std::exception& error) {
                response.status = 400;
                response.body = json{{"error", error.what()}}.dump();
                return response;
            }
        }
        response.status = 404;
        response.body = json{{"error", "not found"}}.dump();
        return response;
    });
    http.start();
    std::cout << "open " << (tls_enabled ? "https" : "http")
              << "://localhost:18080 (signaling /ws on the same port)\n"
              << "streaming 1280x720 H.264 at 30 FPS; press Ctrl+C to stop\n";
#else
    std::cout << "streaming disabled; running frame-pipeline metrics\n";
    remote_gateway::Session session(
        std::make_unique<remote_gateway::TestPatternSource>(1280, 720, 30),
        std::make_unique<MetricsSink>());
#endif
#ifndef REMOTE_GATEWAY_ENABLE_STREAMING
    session.start();
#endif

    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

#ifdef REMOTE_GATEWAY_ENABLE_STREAMING
    sessions.stop_all();
    http.stop();
#else
    session.stop();
#endif
    std::cout << "stopped\n";
    return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "startup failed: " << error.what() << '\n';
        return 1;
    }
}
