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
    std::string password;
    std::string domain;
    std::uint32_t width = 1920;
    std::uint32_t height = 1080;
    std::uint64_t user_identity = 0;
    bool enable_printing = true;
    bool enable_drive = false;
    bool enable_audio = true;
    bool enable_clipboard = true;
    bool show_wallpaper = true;
    bool font_smoothing = true;
    bool full_window_drag = true;
    bool menu_animations = false;
    bool desktop_composition = true;
    std::uint32_t dpi = 96;
    std::string image_format = "webp";
    std::string resize_method = "display-update";
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
