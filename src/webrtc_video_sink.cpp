#include "remotelink/webrtc_video_sink.hpp"

#include <iostream>

namespace remotelink {

WebRtcVideoSink::WebRtcVideoSink(WebRtcServer& server, std::string peer_id, std::uint32_t width,
                                 std::uint32_t height, std::uint32_t fps,
                                 std::uint32_t bitrate)
    : server_(server), peer_id_(std::move(peer_id)), encoder_(width, height, fps, bitrate) {}

void WebRtcVideoSink::consume(const Frame& frame) {
    try {
        EncodedFrame encoded = encoder_.encode(frame);
        if (!encoded.annex_b.empty()) {
            ++encoded_frames_;
            sent_bytes_ += encoded.annex_b.size();
            server_.send(peer_id_, encoded);
        }
    }
    catch (const std::exception& error) {
        std::cerr << "frame encode skipped: " << error.what() << '\n';
    }
}

void WebRtcVideoSink::request_key_frame() { encoder_.request_key_frame(); }
bool WebRtcVideoSink::set_bitrate(std::uint32_t bitrate) { return encoder_.set_bitrate(bitrate); }
std::uint64_t WebRtcVideoSink::encoded_frames() const noexcept { return encoded_frames_.load(); }
std::uint64_t WebRtcVideoSink::sent_bytes() const noexcept { return sent_bytes_.load(); }

}  // namespace remotelink
