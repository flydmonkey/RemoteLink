#include "remotelink/session_manager.hpp"

#include "remotelink/rdp_frame_source.hpp"
#include "remotelink/security_policy.hpp"
#include "remotelink/session_identity.hpp"
#include "remotelink/session.hpp"
#include "remotelink/webrtc_server.hpp"
#include "remotelink/webrtc_video_sink.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace remotelink {

struct SessionManager::ManagedSession {
    std::string identity;
    std::string target_id;
    std::string account_username;
    std::chrono::steady_clock::time_point started_at;
    std::string state = "connecting";
    RdpFrameSource* source = nullptr;
    WebRtcVideoSink* sink = nullptr;
    std::unique_ptr<Session> session;
};

SessionManager::SessionManager(WebRtcServer& server, std::vector<TargetConfig> targets,
                               std::string allowed_hosts, std::filesystem::path audit_path)
    : server_(server), targets_(std::move(targets)),
      allowed_hosts_(std::move(allowed_hosts)), audit_path_(std::move(audit_path)) {
    if (audit_path_.empty()) return;
    std::ifstream input(audit_path_);
    const auto data = input ? nlohmann::json::parse(input, nullptr, false)
                            : nlohmann::json{};
    if (!data.is_array()) return;
    for (const auto& item : data) {
        events_.push_back({item.value("timestampMs", 0ULL), item.value("type", ""),
            item.value("targetId", ""), item.value("peerId", ""),
            item.value("message", ""), item.value("username", ""),
            item.value("reason", "")});
        if (events_.size() >= 200) break;
    }
}

SessionManager::~SessionManager() { stop_all(); }

bool SessionManager::start(const std::string& peer_id, const std::string& target_id,
                           const std::string& host, const std::string& username,
                           const std::string& password,
                           std::uint32_t width, std::uint32_t height,
                           std::uint32_t bitrate, bool audio_playback,
                           bool redirect_printers, bool redirect_files, std::size_t user_identity,
                           std::string account_username, std::string& error) {
    std::vector<TargetConfig> targets;
    { std::lock_guard lock(mutex_); targets = targets_; }
    auto target = std::find_if(targets.begin(), targets.end(),
        [&](const TargetConfig& item) { return item.id == target_id; });
    if (target == targets.end()) { error = "unknown target"; return false; }
    const auto& template_target = *target;
    auto rdp = template_target.rdp;
    (void)host;
    if (!username.empty()) rdp.username = username;
    if (!password.empty()) rdp.password = password;
    if (width >= 640 && width <= 7680 && height >= 480 && height <= 4320 &&
        width % 2 == 0 && height % 2 == 0) {
        rdp.width = width;
        rdp.height = height;
    }
    rdp.audio_playback = audio_playback;
    rdp.redirect_printers = redirect_printers;
    if (redirect_files) {
        const char* state_root = std::getenv("REMOTELINK_STATE_DIR");
        rdp.shared_files_path = (std::filesystem::path(state_root && *state_root
            ? state_root : "/var/lib/remotelink") / "users" /
            std::to_string(user_identity) / "files").string();
        std::filesystem::create_directories(rdp.shared_files_path);
    } else {
        rdp.shared_files_path.clear();
    }
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
        managed->account_username = std::move(account_username);
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
            add_event_locked("started", template_target.id, peer_id, "远程会话已创建",
                             position->second->account_username);
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

bool SessionManager::set_bitrate(const std::string& peer_id, std::uint32_t bitrate) {
    bitrate = std::clamp<std::uint32_t>(bitrate, 500'000, 20'000'000);
    std::lock_guard lock(mutex_);
    const auto found = sessions_.find(peer_id);
    return found != sessions_.end() && found->second->sink->set_bitrate(bitrate);
}

void SessionManager::stop(const std::string& peer_id) {
    std::unique_ptr<ManagedSession> removed;
    {
        std::lock_guard lock(mutex_);
        if (auto it = sessions_.find(peer_id); it != sessions_.end()) {
            add_event_locked("stopped", it->second->target_id, peer_id,
                             "远程会话已结束", it->second->account_username,
                             "客户端断开或网络中断");
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
        add_event_locked("state", found->second->target_id, peer_id, state,
                         found->second->account_username);
    }
}

void SessionManager::set_targets(std::vector<TargetConfig> targets) {
    std::lock_guard lock(mutex_);
    targets_ = std::move(targets);
}

void SessionManager::add_event_locked(std::string type, std::string target_id,
                                      std::string peer_id, std::string message,
                                      std::string username, std::string reason) {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    events_.push_front(AdminEvent {
        .timestamp_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now).count()),
        .type = std::move(type),
        .target_id = std::move(target_id),
        .peer_id = std::move(peer_id),
        .message = std::move(message),
        .username = std::move(username),
        .reason = std::move(reason),
    });
    constexpr std::size_t maximum_events = 200;
    if (events_.size() > maximum_events) events_.resize(maximum_events);
    save_events_locked();
}

void SessionManager::save_events_locked() const {
    if (audit_path_.empty()) return;
    nlohmann::json data = nlohmann::json::array();
    for (const auto& event : events_) data.push_back({
        {"timestampMs",event.timestamp_ms},{"type",event.type},
        {"targetId",event.target_id},{"peerId",event.peer_id},
        {"message",event.message},{"username",event.username},{"reason",event.reason}});
    std::filesystem::create_directories(audit_path_.parent_path());
    const auto temporary = audit_path_.string() + ".tmp";
    { std::ofstream output(temporary, std::ios::trunc); output << data.dump(2) << '\n';
      if (!output) return; }
    std::error_code error;
    std::filesystem::permissions(temporary, std::filesystem::perms::owner_read |
        std::filesystem::perms::owner_write, std::filesystem::perm_options::replace, error);
    std::filesystem::rename(temporary, audit_path_, error);
}

std::vector<SessionManager::AdminEvent> SessionManager::recent_events() const {
    std::lock_guard lock(mutex_);
    return {events_.begin(), events_.end()};
}

void SessionManager::record_admin_disconnect(const std::string& peer_id) {
    std::lock_guard lock(mutex_);
    if (const auto found = sessions_.find(peer_id); found != sessions_.end()) {
        add_event_locked("admin-disconnect", found->second->target_id, peer_id,
                         "管理员请求断开会话", found->second->account_username,
                         "管理员断开");
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
            .group = target.group,
            .host = target.rdp.hostname,
            .username = target.rdp.username,
            .port = target.rdp.port,
            .width = target.rdp.width,
            .height = target.rdp.height,
            .has_password = !target.rdp.password.empty(),
        };
        const auto active = std::find_if(sessions_.begin(), sessions_.end(),
            [&](const auto& entry) { return entry.second->target_id == target.id; });
        if (active != sessions_.end()) {
            const auto& managed = *active->second;
            snapshot.busy = true;
            snapshot.peer_id = active->first;
            snapshot.account_username = managed.account_username;
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
}  // namespace remotelink
