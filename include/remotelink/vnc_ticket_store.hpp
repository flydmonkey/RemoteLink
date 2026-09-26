#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace remotelink {

struct VncDestination {
    std::string session_id;
    std::string target_id;
    std::string target_name;
    std::string username;
    std::string hostname;
    std::uint16_t port = 5900;
};

class VncTicketStore {
public:
    explicit VncTicketStore(std::chrono::milliseconds lifetime = std::chrono::seconds(30));
    std::string issue(VncDestination destination);
    std::optional<VncDestination> consume(const std::string& ticket);
    [[nodiscard]] std::size_t size() const;

private:
    struct Entry {
        VncDestination destination;
        std::chrono::steady_clock::time_point expires_at;
    };
    void remove_expired_locked(std::chrono::steady_clock::time_point now);
    std::chrono::milliseconds lifetime_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Entry> entries_;
};

}  // namespace remotelink
