#pragma once

#include "remotelink/frame.hpp"

namespace remotelink {

class FrameSink {
public:
    virtual ~FrameSink() = default;
    virtual void consume(const Frame& frame) = 0;
};

}  // namespace remotelink

