#include "remote_gateway/webrtc_server.hpp"
#include "remote_gateway/security_policy.hpp"

#include <rtc/rtc.hpp>
#include <nlohmann/json.hpp>
#include <opus/opus.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace remote_gateway {
using json = nlohmann::json;

struct WebRtcServer::Peer {
    std::string id;
    std::shared_ptr<rtc::WebSocket> socket;
    std::shared_ptr<rtc::PeerConnection> connection;
    std::shared_ptr<rtc::Track> video;
    std::shared_ptr<rtc::Track> audio;
    std::shared_ptr<rtc::DataChannel> input;
    std::string pending_control;
    bool video_open = false;
    bool audio_open = false;
    std::mutex audio_mutex;
    OpusEncoder* opus = nullptr;
    std::vector<opus_int16> pcm;
    std::uint64_t audio_samples = 0;
    std::atomic_bool authenticated = false;
    std::atomic_bool started = false;
    std::atomic_bool connected = false;
    std::size_t user_identity = 0;
    std::atomic_uint64_t generation = 0;
    const std::chrono::steady_clock::time_point created_at = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point started_at;
    ~Peer() { if (opus) opus_encoder_destroy(opus); }
};

WebRtcServer::WebRtcServer(std::uint16_t signaling_port, std::vector<std::string> access_tokens,
                           std::string certificate_file, std::string key_file,
                           std::string bind_address)
    : access_tokens_(std::move(access_tokens)) {
    if (access_tokens_.empty()) {
        throw std::invalid_argument("at least one access token must be configured");
    }
    if (certificate_file.empty() != key_file.empty()) {
        throw std::invalid_argument("both TLS certificate and key must be configured");
    }
    rtc::InitLogger(rtc::LogLevel::Warning);
    rtc::WebSocketServer::Configuration config;
    config.port = signaling_port;
    config.enableTls = !certificate_file.empty();
    if (config.enableTls) {
        config.certificatePemFile = std::move(certificate_file);
        config.keyPemFile = std::move(key_file);
    }
    config.bindAddress = std::move(bind_address);
    config.maxMessageSize = 1024 * 1024;
    server_ = std::make_unique<rtc::WebSocketServer>(std::move(config));
    server_->onClient([this](std::shared_ptr<rtc::WebSocket> socket) {
        accept(socket);
    });
    maintenance_thread_ = std::jthread([this](std::stop_token stop_token) {
        while (!stop_token.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            const auto now = std::chrono::steady_clock::now();
            std::vector<std::shared_ptr<Peer>> peers;
            { std::lock_guard lock(mutex_); peers = peers_; }
            for (const auto& peer : peers) {
                const bool authentication_timeout =
                    !peer->authenticated.load() && now - peer->created_at > std::chrono::seconds(10);
                const bool negotiation_timeout = peer->started.load() && !peer->connected.load() &&
                    now - peer->started_at > std::chrono::seconds(20);
                if (authentication_timeout || negotiation_timeout) {
                    PeerHandler close;
                    bool removed = false;
                    {
                        std::lock_guard lock(mutex_);
                        const auto before = peers_.size();
                        std::erase(peers_, peer);
                        removed = peers_.size() != before;
                        close = close_handler_;
                    }
                    if (!removed) continue;
                    if (close) close(peer->id);
                    try {
                        peer->socket->send(json{{"type", "error"},
                            {"message", authentication_timeout ? "authentication timeout" : "WebRTC negotiation timeout"}}.dump());
                    } catch (...) {}
                    peer->socket->close();
                }
            }
        }
    });
}

WebRtcServer::~WebRtcServer() {
    maintenance_thread_.request_stop();
    if (maintenance_thread_.joinable()) maintenance_thread_.join();
    std::vector<std::shared_ptr<Peer>> peers;
    {
        std::lock_guard lock(mutex_);
        peers.swap(peers_);
        input_handler_ = {};
        start_handler_ = {};
        key_frame_handler_ = {};
        close_handler_ = {};
    }
    for (const auto& peer : peers) {
        if (peer->input) peer->input->close();
        if (peer->video) peer->video->close();
        if (peer->audio) peer->audio->close();
        if (peer->connection) peer->connection->close();
        if (peer->socket) peer->socket->close();
    }
    server_->stop();
}

void WebRtcServer::set_targets(std::vector<PublicTarget> targets) {
    std::lock_guard lock(mutex_); targets_ = std::move(targets);
}
void WebRtcServer::set_user_target_permissions(
    std::vector<std::vector<std::string>> permissions) {
    std::lock_guard lock(mutex_); user_target_permissions_ = std::move(permissions);
}
void WebRtcServer::set_access_tokens(std::vector<std::string> access_tokens) {
    std::lock_guard lock(mutex_); access_tokens_ = std::move(access_tokens);
}
void WebRtcServer::set_start_handler(StartHandler handler) {
    std::lock_guard lock(mutex_); start_handler_ = std::move(handler);
}
void WebRtcServer::set_input_handler(InputHandler handler) {
    std::lock_guard lock(mutex_);
    input_handler_ = std::move(handler);
}
void WebRtcServer::set_bitrate_handler(BitrateHandler handler) {
    std::lock_guard lock(mutex_); bitrate_handler_ = std::move(handler);
}

void WebRtcServer::set_key_frame_handler(PeerHandler handler) {
    std::lock_guard lock(mutex_);
    key_frame_handler_ = std::move(handler);
}
void WebRtcServer::set_close_handler(PeerHandler handler) {
    std::lock_guard lock(mutex_); close_handler_ = std::move(handler);
}

void WebRtcServer::accept(const std::shared_ptr<rtc::WebSocket>& socket) {
    auto peer = std::make_shared<Peer>();
    peer->socket = socket;
    {
        std::lock_guard lock(mutex_);
        if (peers_.size() >= 8) {
            socket->send(json{{"type", "error"}, {"message", "too many clients"}}.dump());
            socket->close();
            return;
        }
        peer->id = std::to_string(next_peer_id_++);
        peers_.push_back(peer);
    }

    socket->onMessage([this, weak = std::weak_ptr<Peer>(peer)](rtc::message_variant data) {
        auto locked = weak.lock();
        if (!locked || !std::holds_alternative<std::string>(data)) return;
        try {
            handle_message(locked, std::get<std::string>(data));
        }
        catch (const std::exception& error) {
            std::cerr << "signaling error: " << error.what() << '\n';
        }
    });
    socket->onClosed([this, weak = std::weak_ptr<Peer>(peer)] {
        auto locked = weak.lock();
        if (!locked) return;
        PeerHandler close;
        bool removed = false;
        {
            std::lock_guard lock(mutex_);
            const auto before = peers_.size();
            std::erase(peers_, locked);
            removed = peers_.size() != before;
            close = close_handler_;
        }
        if (removed && close) close(locked->id);
    });
}

void WebRtcServer::handle_message(const std::shared_ptr<Peer>& peer,
                                  const std::string& message) {
    const json payload = json::parse(message);
    const std::string type = payload.value("type", "");

    if (!peer->authenticated) {
        if (type != "authenticate") {
            peer->socket->send(json{{"type", "auth-error"}, {"message", "authentication required"}}.dump());
            peer->socket->close();
            return;
        }
        const std::string supplied = payload.value("token", "");
        std::vector<std::string> access_tokens;
        { std::lock_guard lock(mutex_); access_tokens = access_tokens_; }
        const auto identity = access_token_identity(supplied, access_tokens);
        if (!identity) {
            peer->socket->send(json{{"type", "auth-error"}, {"message", "invalid access token"}}.dump());
            peer->socket->close();
            return;
        }
        peer->user_identity = *identity;
        peer->authenticated = true;
        json targets = json::array();
        {
            std::lock_guard lock(mutex_);
            const auto permitted = [&](const std::string& target_id) {
                if (*identity == 0) return true;
                if (*identity >= user_target_permissions_.size()) return false;
                const auto& ids = user_target_permissions_[*identity];
                return std::find(ids.begin(), ids.end(), target_id) != ids.end();
            };
            for (const auto& target : targets_) if (permitted(target.id)) targets.push_back({
                {"id", target.id}, {"name", target.name},
                {"host", target.host}, {"username", target.username},
                {"width", target.width}, {"height", target.height}});
        }
        peer->socket->send(json{{"type", "authenticated"}, {"targets", targets}}.dump());
        return;
    }

    if (type == "start") {
        if (peer->started) {
            peer->socket->send(json{{"type", "error"}, {"message", "session already started"}}.dump());
        return;
    }
    if (type == "set-bitrate") {
        BitrateHandler handler;
        { std::lock_guard lock(mutex_); handler = bitrate_handler_; }
        const auto bitrate = payload.value("bitrate", 0U);
        if (handler && bitrate >= 500'000U && bitrate <= 20'000'000U)
            handler(peer->id, bitrate);
        return;
    }
        const std::string target = payload.value("target", "");
        {
            std::lock_guard lock(mutex_);
            const bool permitted = peer->user_identity == 0 ||
                (peer->user_identity < user_target_permissions_.size() &&
                 std::find(user_target_permissions_[peer->user_identity].begin(),
                           user_target_permissions_[peer->user_identity].end(), target) !=
                     user_target_permissions_[peer->user_identity].end());
            if (!permitted) {
                peer->socket->send(json{{"type", "error"}, {"message", "target-not-authorized"}}.dump());
                return;
            }
        }
        const std::string host = payload.value("host", "");
        const std::string username = payload.value("username", "");
        const std::string password = payload.value("password", "");
        const std::uint32_t width = payload.value("width", 0U);
        const std::uint32_t height = payload.value("height", 0U);
        const std::uint32_t bitrate = payload.value("bitrate", 4'000'000U);
        const bool audio_playback = payload.value("sound", true);
        const bool redirect_printers = payload.value("printer", false);
        const bool redirect_files = payload.value("files", false);
        StartHandler start;
        { std::lock_guard lock(mutex_); start = start_handler_; }
        std::string error;
        if (!start || !start(peer->id, target, host, username, password, width, height,
                             bitrate, audio_playback, redirect_printers, redirect_files,
                             peer->user_identity, error)) {
            peer->socket->send(json{{"type", "error"}, {"message", error.empty() ? "unable to start session" : error}}.dump());
            return;
        }
        peer->started_at = std::chrono::steady_clock::now();
        peer->connected = false;
        peer->started = true;
        const std::uint64_t generation = ++peer->generation;
        rtc::Configuration config;
        config.disableAutoNegotiation = true;
        peer->connection = std::make_shared<rtc::PeerConnection>(config);

        peer->connection->onLocalDescription([weak = std::weak_ptr<Peer>(peer), generation](rtc::Description description) {
            if (auto locked = weak.lock(); locked && locked->generation.load() == generation) {
                locked->socket->send(json {
                    {"type", description.typeString()},
                    {"sdp", std::string(description)}
                }.dump());
            }
        });
        peer->connection->onLocalCandidate([weak = std::weak_ptr<Peer>(peer), generation](rtc::Candidate candidate) {
            if (auto locked = weak.lock(); locked && locked->generation.load() == generation) {
                locked->socket->send(json {
                    {"type", "candidate"},
                    {"candidate", std::string(candidate)},
                    {"mid", candidate.mid()}
                }.dump());
            }
        });
        peer->connection->onStateChange([this, weak = std::weak_ptr<Peer>(peer), generation](
                                             rtc::PeerConnection::State state) {
            auto locked = weak.lock();
            if (!locked || locked->generation.load() != generation) return;
            if (state == rtc::PeerConnection::State::Failed ||
                state == rtc::PeerConnection::State::Closed) {
                locked->socket->close();
                return;
            }
            if (state != rtc::PeerConnection::State::Connected) return;
            locked->connected = true;
            locked->video_open = true;
            PeerHandler request;
            {
                std::lock_guard lock(mutex_);
                request = key_frame_handler_;
            }
            if (request) request(locked->id);
            std::cout << "peer connected; H.264 stream enabled\n";
        });

        rtc::Description::Video video("video", rtc::Description::Direction::SendOnly);
        video.addH264Codec(102);
        video.addSSRC(1, "remote-gateway", "desktop", "desktop");
        peer->video = peer->connection->addTrack(video);

        auto rtp = std::make_shared<rtc::RtpPacketizationConfig>(
            1, "remote-gateway", 102, rtc::H264RtpPacketizer::ClockRate);
        auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(
            rtc::NalUnit::Separator::LongStartSequence, rtp);
        auto sender_report = std::make_shared<rtc::RtcpSrReporter>(rtp);
        packetizer->addToChain(sender_report);
        packetizer->addToChain(std::make_shared<rtc::RtcpNackResponder>());
        peer->video->setMediaHandler(packetizer);
        peer->video->onOpen([this, weak = std::weak_ptr<Peer>(peer), generation] {
            auto locked = weak.lock();
            if (!locked || locked->generation.load() != generation) return;
            locked->video_open = true;
            std::cout << "video track opened; requesting H.264 key frame\n";
            PeerHandler request;
            {
                std::lock_guard lock(mutex_);
                request = key_frame_handler_;
            }
            if (request) request(locked->id);
        });

        rtc::Description::Audio audio("audio", rtc::Description::Direction::SendOnly);
        audio.addOpusCodec(111);
        audio.addSSRC(2, "remote-gateway", "desktop-audio", "desktop-audio");
        peer->audio = peer->connection->addTrack(audio);
        auto audio_rtp = std::make_shared<rtc::RtpPacketizationConfig>(
            2, "remote-gateway", 111, rtc::OpusRtpPacketizer::DefaultClockRate);
        auto audio_packetizer = std::make_shared<rtc::OpusRtpPacketizer>(audio_rtp);
        audio_packetizer->addToChain(std::make_shared<rtc::RtcpSrReporter>(audio_rtp));
        peer->audio->setMediaHandler(audio_packetizer);
        peer->audio->onOpen([weak = std::weak_ptr<Peer>(peer), generation] {
            if (auto locked = weak.lock(); locked && locked->generation.load() == generation) {
                locked->audio_open = true;
                std::cout << "audio track opened; Opus stream enabled\n";
            }
        });

        peer->input = peer->connection->createDataChannel("input");
        peer->input->onOpen([this, weak = std::weak_ptr<Peer>(peer), generation] {
            auto locked = weak.lock();
            if (!locked || locked->generation.load() != generation) return;
            std::string pending;
            {
                std::lock_guard lock(mutex_);
                pending.swap(locked->pending_control);
            }
            if (!pending.empty()) {
                try { locked->input->send(pending); }
                catch (const std::exception& error) {
                    std::cerr << "initial control send failed: " << error.what() << '\n';
                }
            }
        });
        peer->input->onMessage([this, weak = std::weak_ptr<Peer>(peer), generation](rtc::message_variant data) {
            if (!std::holds_alternative<std::string>(data)) return;
            auto locked = weak.lock();
            if (!locked || locked->generation.load() != generation) return;
            InputHandler handler;
            {
                std::lock_guard lock(mutex_);
                handler = input_handler_;
            }
            if (handler) handler(locked->id, std::get<std::string>(data));
        });

        peer->connection->setLocalDescription(rtc::Description::Type::Offer);
    }
    else if (type == "stop" && peer->started) {
        ++peer->generation;
        PeerHandler close;
        { std::lock_guard lock(mutex_); close = close_handler_; }
        if (close) close(peer->id);
        if (peer->input) peer->input->close();
        if (peer->video) peer->video->close();
        if (peer->audio) peer->audio->close();
        if (peer->connection) peer->connection->close();
        peer->input.reset();
        peer->video.reset();
        peer->audio.reset();
        peer->connection.reset();
        peer->video_open = false;
        peer->audio_open = false;
        peer->started = false;
        peer->connected = false;
        peer->socket->send(json{{"type", "stopped"}}.dump());
    }
    else if (type == "answer" && peer->connection) {
        peer->connection->setRemoteDescription(
            rtc::Description(payload.at("sdp").get<std::string>(), "answer"));
    }
    else if (type == "candidate" && peer->connection) {
        peer->connection->addRemoteCandidate(rtc::Candidate(
            payload.at("candidate").get<std::string>(),
            payload.value("mid", "0")));
    }
}

void WebRtcServer::send(const std::string& peer_id, const EncodedFrame& frame) {
    std::shared_ptr<Peer> peer;
    {
        std::lock_guard lock(mutex_);
        const auto it = std::find_if(peers_.begin(), peers_.end(),
            [&](const auto& item) { return item->id == peer_id; });
        if (it != peers_.end()) peer = *it;
    }
    if (!peer || !peer->video_open || !peer->video) return;
    const rtc::FrameInfo timestamp(std::chrono::duration<double>(
        static_cast<double>(frame.sequence) / 30.0));
    try { peer->video->sendFrame(frame.annex_b.data(), frame.annex_b.size(), timestamp); }
    catch (const std::exception& error) {
        std::cerr << "video send failed: " << error.what() << '\n';
    }
}

void WebRtcServer::send_audio(const std::string& peer_id, const std::uint8_t* pcm,
                              std::size_t size, std::uint32_t sample_rate,
                              std::uint16_t channels) {
    if (pcm == nullptr || size == 0 || sample_rate < 8000 || sample_rate > 48000 ||
        (channels != 1 && channels != 2) ||
        size % sizeof(opus_int16) != 0) return;
    std::shared_ptr<Peer> peer;
    {
        std::lock_guard lock(mutex_);
        const auto it = std::find_if(peers_.begin(), peers_.end(),
            [&](const auto& item) { return item->id == peer_id; });
        if (it != peers_.end()) peer = *it;
    }
    if (!peer || !peer->audio_open || !peer->audio) return;
    std::lock_guard audio_lock(peer->audio_mutex);
    if (!peer->opus) {
        int error = OPUS_OK;
        peer->opus = opus_encoder_create(48000, 2, OPUS_APPLICATION_AUDIO, &error);
        if (!peer->opus || error != OPUS_OK) {
            std::cerr << "unable to create Opus encoder: " << opus_strerror(error) << '\n';
            return;
        }
        opus_encoder_ctl(peer->opus, OPUS_SET_BITRATE(128000));
        opus_encoder_ctl(peer->opus, OPUS_SET_COMPLEXITY(5));
    }
    const auto* samples = reinterpret_cast<const opus_int16*>(pcm);
    const std::size_t input_frames = size / sizeof(opus_int16) / channels;
    const std::size_t output_frames = input_frames * static_cast<std::size_t>(48000) / sample_rate;
    peer->pcm.reserve(peer->pcm.size() + output_frames * 2);
    for (std::size_t frame = 0; frame < output_frames; ++frame) {
        const std::size_t source = std::min(input_frames - 1,
            frame * static_cast<std::size_t>(sample_rate) / static_cast<std::size_t>(48000));
        const opus_int16 left = samples[source * channels];
        const opus_int16 right = channels == 2 ? samples[source * channels + 1] : left;
        peer->pcm.push_back(left);
        peer->pcm.push_back(right);
    }
    constexpr std::size_t frame_samples = 960;
    constexpr std::size_t stereo_samples = frame_samples * 2;
    while (peer->pcm.size() >= stereo_samples) {
        std::array<unsigned char, 4000> packet {};
        const int encoded = opus_encode(peer->opus, peer->pcm.data(), frame_samples,
                                        packet.data(), static_cast<opus_int32>(packet.size()));
        if (encoded < 0) {
            std::cerr << "Opus encode failed: " << opus_strerror(encoded) << '\n';
            peer->pcm.clear();
            return;
        }
        const rtc::FrameInfo timestamp(std::chrono::duration<double>(
            static_cast<double>(peer->audio_samples) / 48000.0));
        try { peer->audio->sendFrame(reinterpret_cast<const std::byte*>(packet.data()),
                                     static_cast<std::size_t>(encoded), timestamp); }
        catch (const std::exception& error) {
            std::cerr << "audio send failed: " << error.what() << '\n';
            return;
        }
        peer->audio_samples += frame_samples;
        peer->pcm.erase(peer->pcm.begin(), peer->pcm.begin() + stereo_samples);
    }
}

void WebRtcServer::send_session_status(const std::string& peer_id,
                                       const std::string& state) {
    std::shared_ptr<Peer> peer;
    {
        std::lock_guard lock(mutex_);
        const auto it = std::find_if(peers_.begin(), peers_.end(),
            [&](const auto& item) { return item->id == peer_id; });
        if (it != peers_.end()) peer = *it;
    }
    if (!peer || !peer->authenticated.load()) return;
    try { peer->socket->send(json{{"type", "session-status"}, {"state", state}}.dump()); }
    catch (const std::exception& error) {
        std::cerr << "session status send failed: " << error.what() << '\n';
    }
}

void WebRtcServer::send_control(const std::string& peer_id,
                                const std::string& message) {
    std::shared_ptr<Peer> peer;
    {
        std::lock_guard lock(mutex_);
        const auto it = std::find_if(peers_.begin(), peers_.end(),
            [&](const auto& item) { return item->id == peer_id; });
        if (it != peers_.end()) {
            peer = *it;
            if (!peer->input || !peer->input->isOpen()) {
                peer->pending_control = message;
                return;
            }
        }
    }
    if (!peer) return;
    try { peer->input->send(message); }
    catch (const std::exception& error) {
        std::cerr << "control send failed: " << error.what() << '\n';
    }
}

std::size_t WebRtcServer::peer_count() const {
    std::lock_guard lock(mutex_);
    return peers_.size();
}

bool WebRtcServer::disconnect_peer(const std::string& peer_id) {
    std::shared_ptr<Peer> peer;
    {
        std::lock_guard lock(mutex_);
        const auto found = std::find_if(peers_.begin(), peers_.end(),
            [&](const auto& item) { return item->id == peer_id; });
        if (found == peers_.end()) return false;
        peer = *found;
    }
    peer->socket->close();
    return true;
}

}  // namespace remote_gateway
