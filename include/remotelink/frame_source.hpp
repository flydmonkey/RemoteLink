#pragma once

#include "remotelink/frame.hpp"

#include <functional>
#include <stop_token>

namespace remotelink {

class FrameSource {
public:
    using FrameHandler = std::function<void(Frame)>;

    virtual ~FrameSource() = default;
    virtual void run(std::stop_token stop_token, FrameHandler on_frame) = 0;
};

}  // namespace remotelink

