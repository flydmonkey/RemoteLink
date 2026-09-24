#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace remote_gateway {

struct EncodedFrame {
    std::uint64_t sequence = 0;
    bool key_frame = false;
    std::vector<std::byte> annex_b;
};

}  // namespace remote_gateway

