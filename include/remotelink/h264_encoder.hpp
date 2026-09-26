#pragma once

#include "remotelink/encoded_frame.hpp"
#include "remotelink/frame.hpp"

#include <cstdint>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

struct ISVCEncoder;

namespace remotelink {

class H264Encoder {
public:
    H264Encoder(std::uint32_t width, std::uint32_t height,
                std::uint32_t fps, std::uint32_t bitrate);
    ~H264Encoder();

    H264Encoder(const H264Encoder&) = delete;
    H264Encoder& operator=(const H264Encoder&) = delete;

    EncodedFrame encode(const Frame& frame);
    void request_key_frame();
    bool set_bitrate(std::uint32_t bitrate);

private:
    void convert_bgra_to_i420(const Frame& frame);

    ISVCEncoder* encoder_ = nullptr;
    std::uint32_t width_;
    std::uint32_t height_;
    std::uint32_t fps_;
    std::vector<std::uint8_t> i420_;
    std::atomic_bool key_frame_requested_ = false;
    std::mutex mutex_;
};

}  // namespace remotelink
