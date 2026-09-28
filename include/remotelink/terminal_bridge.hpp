#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace remotelink {

enum class TerminalProtocol {
    telnet,
    raw_tcp,
};

struct TerminalBridgeOptions {
    std::string hostname;
    std::uint16_t port = 23;
    TerminalProtocol protocol = TerminalProtocol::telnet;
    std::string username;
    std::string password;
};

class TerminalBridge {
public:
    static std::shared_ptr<TerminalBridge> create(TerminalBridgeOptions options,
                                                  std::string& error);
    ~TerminalBridge();
    TerminalBridge(const TerminalBridge&) = delete;
    TerminalBridge& operator=(const TerminalBridge&) = delete;
    [[nodiscard]] std::uint16_t port() const;
    void stop();

private:
    struct Impl;
    explicit TerminalBridge(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace remotelink
