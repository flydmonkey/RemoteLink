#pragma once

#include "remotelink/frame_source.hpp"

#include <chrono>
#include <cstdint>

namespace remotelink {

class TestPatternSource final : public FrameSource {
public:
    TestPatternSource(std::uint32_t width, std::uint32_t height,
                      std::uint32_t frames_per_second);

    void run(std::stop_token stop_token, FrameHandler on_frame) override;

private:
    Frame render(std::uint64_t sequence) const;

    std::uint32_t width_;
    std::uint32_t height_;
    std::chrono::nanoseconds frame_interval_;
};

}  // namespace remotelink

