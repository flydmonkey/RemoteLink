#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace remote_gateway {

struct VncBridgeOptions {
    std::string hostname;
    std::uint16_t port = 5900;
    std::string username;
    std::string password;
    std::string ca_file;
};

class VncBridge {
public:
    static std::shared_ptr<VncBridge> create(VncBridgeOptions options, std::string& error);
    ~VncBridge();
    VncBridge(const VncBridge&) = delete;
    VncBridge& operator=(const VncBridge&) = delete;
    [[nodiscard]] std::uint16_t port() const;
    void stop();

private:
    struct Impl;
    explicit VncBridge(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace remote_gateway
