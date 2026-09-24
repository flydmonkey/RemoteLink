#include "remote_gateway/session_manager.hpp"

#include "remote_gateway/rdp_frame_source.hpp"
#include "remote_gateway/security_policy.hpp"
#include "remote_gateway/session_identity.hpp"
#include "remote_gateway/session.hpp"
#include "remote_gateway/webrtc_server.hpp"
#include "remote_gateway/webrtc_video_sink.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>

namespace remote_gateway {

struct SessionManager::ManagedSession {
    std::string identity;
    std::string target_id;
    std::chrono::steady_clock::time_point started_at;
    std::string state = "connecting";
    RdpFrameSource* source = nullptr;
    WebRtcVideoSink* sink = nullptr;
    std::unique_ptr<Session> session;
};

SessionManager::SessionManager(WebRtcServer& server, std::vector<TargetConfig> targets,
                               std::string allowed_hosts)
    : server_(server), targets_(std::move(targets)),
      allowed_hosts_(std::move(allowed_hosts)) {}

SessionManager::~SessionManager() { stop_all(); }

bool SessionManager::start(const std::string& peer_id, const std::string& target_id,
                           const std::string& host, const std::string& username,
                           const std::string& password,
                           std::uint32_t width, std::uint32_t height,
                           std::uint32_t bitrate, bool audio_playback,
                           bool redirect_printers, std::size_t user_identity,
                           std::string& error) {
    auto target = std::find_if(targets_.begin(), targets_.end(),
        [&](const TargetConfig& item) { return item.id == target_id; });
    if (target == targets_.end()) {
        target = std::find_if(targets_.begin(), targets_.end(),
            [&](const TargetConfig& item) { return item.rdp.hostname == host; });
    }
    if (target == targets_.end() && host.empty()) { error = "unknown target"; return false; }
    if (!host.empty() && !host_is_allowed(host, allowed_hosts_)) {
        error = "host-not-allowed";
        return false;
    }
    const auto& template_target = target == targets_.end() ? targets_.front() : *target;
    auto rdp = template_target.rdp;
    if (!host.empty()) rdp.hostname = host;
    if (!username.empty()) rdp.username = username;
    if (!password.empty()) rdp.password = password;
    if (width >= 640 && width <= 7680 && height >= 480 && height <= 4320 &&
        width % 2 == 0 && height % 2 == 0) {
        rdp.width = width;
        rdp.height = height;
    }
    rdp.audio_playback = audio_playback;
    rdp.redirect_printers = redirect_printers;
    const char* state_root = std::getenv("RG_STATE_DIR");
    rdp.shared_files_path = (std::filesystem::path(state_root && *state_root
        ? state_root : "/var/lib/remote-gateway") / "users" /
        std::to_string(user_identity) / "files").string();
    std::filesystem::create_directories(rdp.shared_files_path);
    bitrate = std::clamp<std::uint32_t>(bitrate == 0 ? 4'000'000 : bitrate,
                                       500'000, 20'000'000);
    if (rdp.username.empty() || rdp.password.empty() ||
        rdp.username.size() > 256 || rdp.password.size() > 4096) {
        error = "invalid credentials";
        return false;
    }
    const std::string identity = session_identity(rdp.hostname, rdp.username);
    {
        std::lock_guard lock(mutex_);
        if (sessions_.contains(peer_id)) { error = "session already started"; return false; }
        if (!active_identities_.insert(identity).second) {
            error = "target-user-busy";
            add_event_locked("rejected", template_target.id, peer_id,
                             "相同主机和用户已有活动会话");
            return false;
        }
    }
    try {
        auto managed = std::make_unique<ManagedSession>();
        managed->identity = identity;
        managed->target_id = template_target.id;
        managed->started_at = std::chrono::steady_clock::now();
        auto source = std::make_unique<RdpFrameSource>(rdp);
        source->set_status_handler([this, peer_id](const std::string& state) {
            update_status(peer_id, state);
            server_.send_session_status(peer_id, state);
        });
        source->set_cursor_handler([this, peer_id](const std::string& message) {
            server_.send_control(peer_id, message);
        });
        source->set_audio_handler([this, peer_id](const std::uint8_t* data,
                                                  std::size_t size,
                                                  std::uint32_t sample_rate,
                                                  std::uint16_t channels) {
            server_.send_audio(peer_id, data, size, sample_rate, channels);
        });
        managed->source = source.get();
        auto sink = std::make_unique<WebRtcVideoSink>(
            server_, peer_id, rdp.width, rdp.height, 30, bitrate);
        managed->sink = sink.get();
        managed->session = std::make_unique<Session>(std::move(source), std::move(sink));
        std::unique_lock lock(mutex_);
        auto [position, inserted] = sessions_.emplace(peer_id, std::move(managed));
        if (!inserted) throw std::logic_error("reserved peer session disappeared");
        try {
            position->second->session->start();
            add_event_locked("started", template_target.id, peer_id, "远程会话已创建");
        }
        catch (...) {
            sessions_.erase(position);
            active_identities_.erase(identity);
            throw;
        }
    }
    catch (...) {
        std::lock_guard lock(mutex_);
        active_identities_.erase(identity);
        throw;
    }
    return true;
}

void SessionManager::input(const std::string& peer_id, const std::string& data) {
    std::lock_guard lock(mutex_);
    if (auto it = sessions_.find(peer_id); it != sessions_.end()) it->second->source->enqueue_input(data);
}

void SessionManager::request_key_frame(const std::string& peer_id) {
    std::lock_guard lock(mutex_);
    if (auto it = sessions_.find(peer_id); it != sessions_.end()) it->second->sink->request_key_frame();
}

void SessionManager::stop(const std::string& peer_id) {
    std::unique_ptr<ManagedSession> removed;
    {
        std::lock_guard lock(mutex_);
        if (auto it = sessions_.find(peer_id); it != sessions_.end()) {
            add_event_locked("stopped", it->second->target_id, peer_id,
                             "远程会话已结束");
            removed = std::move(it->second);
            sessions_.erase(it);
            active_identities_.erase(removed->identity);
        }
    }
    if (removed) removed->session->stop();
}

void SessionManager::stop_all() {
    decltype(sessions_) removed;
    {
        std::lock_guard lock(mutex_);
        removed.swap(sessions_);
        active_identities_.clear();
    }
    for (auto& [id, managed] : removed) { (void)id; managed->session->stop(); }
}

std::size_t SessionManager::session_count() const { std::lock_guard lock(mutex_); return sessions_.size(); }
std::uint64_t SessionManager::processed_input_events() const {
    std::lock_guard lock(mutex_);
    std::uint64_t total = 0;
    for (const auto& [id, managed] : sessions_) { (void)id; total += managed->source->processed_input_events(); }
    return total;
}
std::uint64_t SessionManager::captured_frames() const {
    std::lock_guard lock(mutex_); std::uint64_t total = 0;
    for (const auto& [id, managed] : sessions_) { (void)id; total += managed->source->published_frames(); }
    return total;
}
std::uint64_t SessionManager::encoded_frames() const {
    std::lock_guard lock(mutex_); std::uint64_t total = 0;
    for (const auto& [id, managed] : sessions_) { (void)id; total += managed->sink->encoded_frames(); }
    return total;
}
std::uint64_t SessionManager::dropped_frames() const {
    std::lock_guard lock(mutex_); std::uint64_t total = 0;
    for (const auto& [id, managed] : sessions_) { (void)id; total += managed->session->dropped_frames(); }
    return total;
}
std::uint64_t SessionManager::sent_bytes() const {
    std::lock_guard lock(mutex_); std::uint64_t total = 0;
    for (const auto& [id, managed] : sessions_) { (void)id; total += managed->sink->sent_bytes(); }
    return total;
}

void SessionManager::update_status(const std::string& peer_id,
                                   const std::string& state) {
    std::lock_guard lock(mutex_);
    if (const auto found = sessions_.find(peer_id); found != sessions_.end()) {
        if (found->second->state == state) return;
        found->second->state = state;
        add_event_locked("state", found->second->target_id, peer_id, state);
    }
}

void SessionManager::add_event_locked(std::string type, std::string target_id,
                                      std::string peer_id, std::string message) {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    events_.push_front(AdminEvent {
        .timestamp_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now).count()),
        .type = std::move(type),
        .target_id = std::move(target_id),
        .peer_id = std::move(peer_id),
        .message = std::move(message),
    });
    constexpr std::size_t maximum_events = 200;
    if (events_.size() > maximum_events) events_.resize(maximum_events);
}

std::vector<SessionManager::AdminEvent> SessionManager::recent_events() const {
    std::lock_guard lock(mutex_);
    return {events_.begin(), events_.end()};
}

void SessionManager::record_admin_disconnect(const std::string& peer_id) {
    std::lock_guard lock(mutex_);
    if (const auto found = sessions_.find(peer_id); found != sessions_.end()) {
        add_event_locked("admin-disconnect", found->second->target_id, peer_id,
                         "管理员请求断开会话");
    }
}

std::vector<SessionManager::TargetSnapshot> SessionManager::target_snapshots() const {
    std::lock_guard lock(mutex_);
    std::vector<TargetSnapshot> result;
    result.reserve(targets_.size());
    const auto now = std::chrono::steady_clock::now();
    for (const auto& target : targets_) {
        TargetSnapshot snapshot {
            .id = target.id,
            .name = target.name,
            .host = target.rdp.hostname,
            .username = target.rdp.username,
            .width = target.rdp.width,
            .height = target.rdp.height,
        };
        const auto active = std::find_if(sessions_.begin(), sessions_.end(),
            [&](const auto& entry) { return entry.second->target_id == target.id; });
        if (active != sessions_.end()) {
            const auto& managed = *active->second;
            snapshot.busy = true;
            snapshot.peer_id = active->first;
            snapshot.state = managed.state;
            snapshot.connected_seconds = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(now - managed.started_at).count());
            snapshot.captured_frames = managed.source->published_frames();
            snapshot.encoded_frames = managed.sink->encoded_frames();
            snapshot.dropped_frames = managed.session->dropped_frames();
            snapshot.sent_bytes = managed.sink->sent_bytes();
        }
        result.push_back(std::move(snapshot));
    }
    return result;
}
}  // namespace remote_gateway
