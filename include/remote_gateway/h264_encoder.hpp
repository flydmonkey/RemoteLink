#pragma once

#include "remote_gateway/encoded_frame.hpp"
#include "remote_gateway/frame.hpp"

#include <cstdint>
#include <atomic>
#include <memory>
#include <vector>

struct ISVCEncoder;

namespace remote_gateway {

class H264Encoder {
public:
    H264Encoder(std::uint32_t width, std::uint32_t height,
                std::uint32_t fps, std::uint32_t bitrate);
    ~H264Encoder();

    H264Encoder(const H264Encoder&) = delete;
    H264Encoder& operator=(const H264Encoder&) = delete;

    EncodedFrame encode(const Frame& frame);
    void request_key_frame();

private:
    void convert_bgra_to_i420(const Frame& frame);

    ISVCEncoder* encoder_ = nullptr;
    std::uint32_t width_;
    std::uint32_t height_;
    std::uint32_t fps_;
    std::vector<std::uint8_t> i420_;
    std::atomic_bool key_frame_requested_ = false;
};

}  // namespace remote_gateway
