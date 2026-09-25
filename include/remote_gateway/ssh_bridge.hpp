#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace remote_gateway {

struct SshBridgeOptions {
    std::string hostname;
    std::uint16_t port = 22;
    std::string username;
    std::string password;
    std::string private_key;
    std::string passphrase;
};

class SshBridge {
public:
    static std::shared_ptr<SshBridge> create(SshBridgeOptions options, std::string& error);
    ~SshBridge();
    SshBridge(const SshBridge&) = delete;
    SshBridge& operator=(const SshBridge&) = delete;
    [[nodiscard]] std::uint16_t port() const;
    void stop();

private:
    struct Impl;
    explicit SshBridge(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace remote_gateway
