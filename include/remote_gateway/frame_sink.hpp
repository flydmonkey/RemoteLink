#pragma once

#include "remote_gateway/frame.hpp"

namespace remote_gateway {

class FrameSink {
public:
    virtual ~FrameSink() = default;
    virtual void consume(const Frame& frame) = 0;
};

}  // namespace remote_gateway

