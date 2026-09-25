#pragma once

#include "remote_gateway/encoded_frame.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace rtc {
class PeerConnection;
class Track;
class WebSocket;
class WebSocketServer;
}

namespace remote_gateway {

class WebRtcServer {
public:
    struct PublicTarget {
        std::string id;
        std::string name;
        std::string host;
        std::string username;
        std::uint32_t width;
        std::uint32_t height;
    };
    using StartHandler = std::function<bool(const std::string&, const std::string&,
                                            const std::string&, const std::string&,
                                            const std::string&, std::uint32_t,
                                            std::uint32_t, std::uint32_t, bool, bool, bool, std::size_t,
                                            std::string&)>;
    using PeerHandler = std::function<void(const std::string&)>;
    using InputHandler = std::function<void(const std::string&, const std::string&)>;

    WebRtcServer(std::uint16_t signaling_port, std::vector<std::string> access_tokens,
                 std::string certificate_file = {}, std::string key_file = {},
                 std::string bind_address = "127.0.0.1");
    ~WebRtcServer();

    void send(const std::string& peer_id, const EncodedFrame& frame);
    void send_audio(const std::string& peer_id, const std::uint8_t* pcm,
                    std::size_t size, std::uint32_t sample_rate,
                    std::uint16_t channels);
    void send_session_status(const std::string& peer_id, const std::string& state);
    void send_control(const std::string& peer_id, const std::string& message);
    void set_targets(std::vector<PublicTarget> targets);
    void set_user_target_permissions(std::vector<std::vector<std::string>> permissions);
    void set_access_tokens(std::vector<std::string> access_tokens);
    void set_start_handler(StartHandler handler);
    void set_input_handler(InputHandler handler);
    void set_key_frame_handler(PeerHandler handler);
    void set_close_handler(PeerHandler handler);
    [[nodiscard]] std::size_t peer_count() const;
    bool disconnect_peer(const std::string& peer_id);

private:
    struct Peer;
    void accept(const std::shared_ptr<rtc::WebSocket>& socket);
    void handle_message(const std::shared_ptr<Peer>& peer, const std::string& message);

    std::unique_ptr<rtc::WebSocketServer> server_;
    std::jthread maintenance_thread_;
    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<Peer>> peers_;
    std::vector<PublicTarget> targets_;
    std::vector<std::vector<std::string>> user_target_permissions_;
    StartHandler start_handler_;
    InputHandler input_handler_;
    PeerHandler key_frame_handler_;
    PeerHandler close_handler_;
    std::vector<std::string> access_tokens_;
    std::uint64_t next_peer_id_ = 1;
};

}  // namespace remote_gateway
