#include "remote_gateway/target_config.hpp"

#include "remote_gateway/security_policy.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <stdexcept>

namespace remote_gateway {
using json = nlohmann::json;

std::vector<TargetConfig> load_targets(const std::string& path,
                                       const std::string& allowed_hosts) {
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("cannot open target configuration: " + path);
    const json root = json::parse(stream);
    if (!root.contains("targets") || !root.at("targets").is_array()) {
        throw std::runtime_error("target configuration must contain a targets array");
    }

    std::vector<TargetConfig> result;
    std::set<std::string> ids;
    const std::regex valid_id("^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$");
    for (const auto& item : root.at("targets")) {
        if (result.size() >= 64) throw std::runtime_error("target limit exceeded (64)");
        TargetConfig target;
        target.id = item.value("id", "");
        target.name = item.value("name", "");
        target.group = item.value("group", "默认分组");
        target.rdp.hostname = item.value("host", "");
        const int port = item.value("port", 3389);
        target.rdp.username = item.value("username", "");
        target.rdp.domain = item.value("domain", "");
        target.rdp.width = item.value("width", 1280);
        target.rdp.height = item.value("height", 720);
        target.rdp.ignore_certificate = item.value("ignoreCertificate", false);
        const std::string password_env = item.value("passwordEnv", "");
        const std::string password_file = item.value("passwordFile", "");
        const std::regex valid_env("^[A-Za-z_][A-Za-z0-9_]*$");
        if (!std::regex_match(target.id, valid_id) || target.name.empty() ||
            target.name.size() > 128 || target.rdp.hostname.empty() ||
            target.rdp.username.empty() ||
            (password_env.empty() == password_file.empty()) ||
            (!password_env.empty() && !std::regex_match(password_env, valid_env)) ||
            port < 1 || port > 65535) {
            throw std::runtime_error("invalid or incomplete target entry: " + target.id);
        }
        target.rdp.port = static_cast<std::uint16_t>(port);
        if (!ids.insert(target.id).second) {
            throw std::runtime_error("duplicate target id: " + target.id);
        }
        if (!host_is_allowed(target.rdp.hostname, allowed_hosts)) {
            throw std::runtime_error("target host is not present in RG_ALLOWED_HOSTS: " +
                                     target.rdp.hostname);
        }
        if (!password_env.empty()) {
            const char* password = std::getenv(password_env.c_str());
            if (password == nullptr || *password == '\0') {
                throw std::runtime_error("password environment variable is missing for target: " + target.id);
            }
            target.rdp.password = password;
        } else {
            std::ifstream password_stream(password_file, std::ios::binary);
            if (!password_stream) throw std::runtime_error("password file cannot be read for target: " + target.id);
            target.rdp.password.assign(std::istreambuf_iterator<char>(password_stream), {});
            while (!target.rdp.password.empty() &&
                   (target.rdp.password.back() == '\n' || target.rdp.password.back() == '\r'))
                target.rdp.password.pop_back();
            if (target.rdp.password.empty()) throw std::runtime_error("password file is empty for target: " + target.id);
        }
        if (target.rdp.width < 320 || target.rdp.width > 7680 ||
            target.rdp.height < 200 || target.rdp.height > 4320) {
            throw std::runtime_error("invalid dimensions for target: " + target.id);
        }
        result.push_back(std::move(target));
    }
    return result;
}

}  // namespace remote_gateway
