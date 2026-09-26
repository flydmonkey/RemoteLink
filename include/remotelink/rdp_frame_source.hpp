#pragma once

#include "remotelink/frame_source.hpp"

#include <cstdint>
#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

struct rdp_context;
typedef struct rdp_context rdpContext;

namespace remotelink {

struct RdpConnectionOptions {
    std::string hostname;
    std::uint16_t port = 3389;
    std::string username;
    std::string password;
    std::string domain;
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    bool ignore_certificate = false;
    bool audio_playback = true;
    bool redirect_printers = false;
    std::string shared_files_path;
};

class RdpFrameSource final : public FrameSource {
public:
    using StatusHandler = std::function<void(const std::string&)>;
    using CursorHandler = std::function<void(const std::string&)>;
    using AudioHandler = std::function<void(const std::uint8_t*, std::size_t,
                                            std::uint32_t, std::uint16_t)>;
    explicit RdpFrameSource(RdpConnectionOptions options);
    ~RdpFrameSource() override;

    void run(std::stop_token stop_token, FrameHandler on_frame) override;
    void enqueue_input(std::string json_event);
    void set_status_handler(StatusHandler handler);
    void set_cursor_handler(CursorHandler handler);
    void set_audio_handler(AudioHandler handler);
    void publish_audio(const std::uint8_t* data, std::size_t size,
                       std::uint32_t sample_rate, std::uint16_t channels);
    void publish_cursor(std::uint32_t width, std::uint32_t height,
                        std::uint32_t hotspot_x, std::uint32_t hotspot_y,
                        const std::vector<std::uint8_t>& rgba);
    void publish_system_cursor(bool hidden);
    void publish_cursor_message(const std::string& message);
    [[nodiscard]] std::uint64_t processed_input_events() const noexcept;
    [[nodiscard]] std::uint64_t published_frames() const noexcept;

    void publish_frame(rdpContext* context);

private:
    void drain_input(rdpContext* context);

    RdpConnectionOptions options_;
    FrameHandler on_frame_;
    std::mutex input_mutex_;
    std::deque<std::string> input_events_;
    std::uint64_t sequence_ = 0;
    std::atomic_uint64_t processed_input_events_ = 0;
    std::atomic_uint64_t published_frames_ = 0;
    StatusHandler status_handler_;
    CursorHandler cursor_handler_;
    AudioHandler audio_handler_;
};

}  // namespace remotelink
