#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace remotelink {

struct SshBridgeOptions {
    std::string hostname;
    std::uint16_t port = 22;
    std::string username;
    std::string password;
    std::string private_key;
    std::string passphrase;
    std::string host_key_sha256;
};

class SshBridge {
public:
    static std::shared_ptr<SshBridge> create(SshBridgeOptions options, std::string& error);
    ~SshBridge();
    SshBridge(const SshBridge&) = delete;
    SshBridge& operator=(const SshBridge&) = delete;
    [[nodiscard]] std::uint16_t port() const;
    [[nodiscard]] std::string host_key_sha256() const;
    bool resize(std::uint32_t columns, std::uint32_t rows);
    void stop();

private:
    struct Impl;
    explicit SshBridge(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace remotelink
