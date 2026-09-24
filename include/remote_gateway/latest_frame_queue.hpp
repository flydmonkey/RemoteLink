#pragma once

#include "remote_gateway/frame.hpp"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <stop_token>

namespace remote_gateway {

/**
 * A bounded frame queue optimized for interactive streaming. When the queue
 * is full, the oldest frame is discarded so latency cannot grow without
 * bound. This is intentionally different from a recording queue, where every
 * frame would need to be retained.
 */
class LatestFrameQueue {
public:
    explicit LatestFrameQueue(std::size_t capacity) : capacity_(capacity) {
        if (capacity_ == 0) {
            throw std::invalid_argument("frame queue capacity must be positive");
        }
    }

    void push(Frame frame) {
        {
            std::lock_guard lock(mutex_);
            if (frames_.size() == capacity_) {
                frames_.pop_front();
                ++dropped_frames_;
            }
            frames_.push_back(std::move(frame));
        }
        changed_.notify_one();
    }

    std::optional<Frame> pop(std::stop_token stop_token) {
        std::unique_lock lock(mutex_);
        if (!changed_.wait(lock, stop_token, [this] { return !frames_.empty(); })) {
            return std::nullopt;
        }

        Frame frame = std::move(frames_.front());
        frames_.pop_front();
        return frame;
    }

    [[nodiscard]] std::size_t size() const {
        std::lock_guard lock(mutex_);
        return frames_.size();
    }

    [[nodiscard]] std::uint64_t dropped_frames() const {
        std::lock_guard lock(mutex_);
        return dropped_frames_;
    }

private:
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable_any changed_;
    std::deque<Frame> frames_;
    std::uint64_t dropped_frames_ = 0;
};

}  // namespace remote_gateway

