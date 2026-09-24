#pragma once

#include "remote_gateway/frame.hpp"

#include <functional>
#include <stop_token>

namespace remote_gateway {

class FrameSource {
public:
    using FrameHandler = std::function<void(Frame)>;

    virtual ~FrameSource() = default;
    virtual void run(std::stop_token stop_token, FrameHandler on_frame) = 0;
};

}  // namespace remote_gateway

