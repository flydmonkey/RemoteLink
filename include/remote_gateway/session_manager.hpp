#pragma once

#include "remote_gateway/target_config.hpp"

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace remote_gateway {
class RdpFrameSource;
class Session;
class WebRtcServer;
class WebRtcVideoSink;

class SessionManager {
public:
    struct TargetSnapshot {
        std::string id;
        std::string name;
        std::string group;
        std::string host;
        std::string username;
        std::uint16_t port = 3389;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        bool busy = false;
        std::string peer_id;
        std::string state;
        std::uint64_t connected_seconds = 0;
        std::uint64_t captured_frames = 0;
        std::uint64_t encoded_frames = 0;
        std::uint64_t dropped_frames = 0;
        std::uint64_t sent_bytes = 0;
    };
    struct AdminEvent {
        std::uint64_t timestamp_ms = 0;
        std::string type;
        std::string target_id;
        std::string peer_id;
        std::string message;
    };
    SessionManager(WebRtcServer& server, std::vector<TargetConfig> targets,
                   std::string allowed_hosts);
    ~SessionManager();
    bool start(const std::string& peer_id, const std::string& target_id,
               const std::string& host, const std::string& username,
               const std::string& password, std::uint32_t width,
               std::uint32_t height, std::uint32_t bitrate,
               bool audio_playback, bool redirect_printers, bool redirect_files,
               std::size_t user_identity,
               std::string& error);
    void input(const std::string& peer_id, const std::string& data);
    void request_key_frame(const std::string& peer_id);
    bool set_bitrate(const std::string& peer_id, std::uint32_t bitrate);
    void stop(const std::string& peer_id);
    void stop_all();
    [[nodiscard]] std::size_t session_count() const;
    [[nodiscard]] std::uint64_t processed_input_events() const;
    [[nodiscard]] std::uint64_t captured_frames() const;
    [[nodiscard]] std::uint64_t encoded_frames() const;
    [[nodiscard]] std::uint64_t dropped_frames() const;
    [[nodiscard]] std::uint64_t sent_bytes() const;
    [[nodiscard]] std::vector<TargetSnapshot> target_snapshots() const;
    [[nodiscard]] std::vector<AdminEvent> recent_events() const;
    void record_admin_disconnect(const std::string& peer_id);
    void set_targets(std::vector<TargetConfig> targets);

private:
    struct ManagedSession;
    WebRtcServer& server_;
    std::vector<TargetConfig> targets_;
    std::string allowed_hosts_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<ManagedSession>> sessions_;
    std::unordered_set<std::string> active_identities_;
    std::deque<AdminEvent> events_;
    void update_status(const std::string& peer_id, const std::string& state);
    void add_event_locked(std::string type, std::string target_id,
                          std::string peer_id, std::string message);
};
}  // namespace remote_gateway
