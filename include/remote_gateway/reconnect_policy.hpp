#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <stop_token>
#include <thread>

namespace remote_gateway {

inline std::chrono::seconds reconnect_delay(std::size_t consecutive_failures) {
    if (consecutive_failures == 0) return std::chrono::seconds(0);
    const auto shift = std::min<std::size_t>(consecutive_failures - 1, 4);
    return std::min(std::chrono::seconds(2U << shift), std::chrono::seconds(30));
}

inline bool interruptible_wait(std::stop_token stop_token,
                               std::chrono::steady_clock::duration duration) {
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (!stop_token.stop_requested()) {
        const auto remaining = deadline - std::chrono::steady_clock::now();
        if (remaining <= std::chrono::steady_clock::duration::zero()) return true;
        std::this_thread::sleep_for(std::min(remaining,
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::milliseconds(100))));
    }
    return false;
}

}  // namespace remote_gateway
