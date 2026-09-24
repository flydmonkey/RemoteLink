#include "remote_gateway/test_pattern_source.hpp"

#include <stdexcept>
#include <thread>

namespace remote_gateway {

TestPatternSource::TestPatternSource(std::uint32_t width,
                                     std::uint32_t height,
                                     std::uint32_t frames_per_second)
    : width_(width),
      height_(height),
      frame_interval_(std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::seconds(1)) /
                      frames_per_second) {
    if (width == 0 || height == 0 || frames_per_second == 0) {
        throw std::invalid_argument("test pattern dimensions and FPS must be positive");
    }
}

Frame TestPatternSource::render(std::uint64_t sequence) const {
    Frame frame;
    frame.width = width_;
    frame.height = height_;
    frame.stride = width_ * 4;
    frame.sequence = sequence;
    frame.captured_at = std::chrono::steady_clock::now();
    frame.pixels.resize(static_cast<std::size_t>(frame.stride) * height_);

    const auto moving_bar = static_cast<std::uint32_t>((sequence * 8) % width_);
    for (std::uint32_t y = 0; y < height_; ++y) {
        for (std::uint32_t x = 0; x < width_; ++x) {
            const auto offset = static_cast<std::size_t>(y) * frame.stride + x * 4;
            const bool in_bar = x >= moving_bar && x < moving_bar + 64;
            frame.pixels[offset + 0] = static_cast<std::byte>((x + sequence) & 0xFF); // B
            frame.pixels[offset + 1] = static_cast<std::byte>((y * 255) / height_);   // G
            frame.pixels[offset + 2] = static_cast<std::byte>(in_bar ? 255 : 32);     // R
            frame.pixels[offset + 3] = static_cast<std::byte>(255);                    // A
        }
    }

    return frame;
}

void TestPatternSource::run(std::stop_token stop_token, FrameHandler on_frame) {
    auto next_frame_at = std::chrono::steady_clock::now();
    std::uint64_t sequence = 0;

    while (!stop_token.stop_requested()) {
        on_frame(render(sequence++));
        next_frame_at += frame_interval_;
        std::this_thread::sleep_until(next_frame_at);
    }
}

}  // namespace remote_gateway
