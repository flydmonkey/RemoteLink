#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace remotelink {

struct TelnetBridgeOptions {
    std::string hostname;
    std::uint16_t port = 23;
    std::string username;
    std::string password;
};

class TelnetBridge {
public:
    static std::shared_ptr<TelnetBridge> create(TelnetBridgeOptions options, std::string& error);
    ~TelnetBridge();
    TelnetBridge(const TelnetBridge&) = delete;
    TelnetBridge& operator=(const TelnetBridge&) = delete;
    [[nodiscard]] std::uint16_t port() const;
    void stop();

private:
    struct Impl;
    explicit TelnetBridge(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace remotelink
