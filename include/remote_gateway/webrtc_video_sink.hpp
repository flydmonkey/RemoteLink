#pragma once

#include "remote_gateway/frame_sink.hpp"
#include "remote_gateway/h264_encoder.hpp"
#include "remote_gateway/webrtc_server.hpp"

#include <cstdint>
#include <atomic>

namespace remote_gateway {

class WebRtcVideoSink final : public FrameSink {
public:
    WebRtcVideoSink(WebRtcServer& server, std::string peer_id, std::uint32_t width,
                    std::uint32_t height, std::uint32_t fps,
                    std::uint32_t bitrate);
    void consume(const Frame& frame) override;
    void request_key_frame();
    [[nodiscard]] std::uint64_t encoded_frames() const noexcept;
    [[nodiscard]] std::uint64_t sent_bytes() const noexcept;

private:
    WebRtcServer& server_;
    std::string peer_id_;
    H264Encoder encoder_;
    std::atomic_uint64_t encoded_frames_ = 0;
    std::atomic_uint64_t sent_bytes_ = 0;
};

}  // namespace remote_gateway
