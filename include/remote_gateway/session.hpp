#pragma once

#include "remote_gateway/frame_sink.hpp"
#include "remote_gateway/frame_source.hpp"
#include "remote_gateway/latest_frame_queue.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

namespace remote_gateway {

enum class SessionState {
    idle,
    running,
    stopping,
    stopped,
};

class Session {
public:
    Session(std::unique_ptr<FrameSource> source,
            std::unique_ptr<FrameSink> sink,
            std::size_t queue_capacity = 3);
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    void start();
    void stop();

    [[nodiscard]] SessionState state() const noexcept;
    [[nodiscard]] std::uint64_t dropped_frames() const;

private:
    std::unique_ptr<FrameSource> source_;
    std::unique_ptr<FrameSink> sink_;
    LatestFrameQueue frames_;
    std::atomic<SessionState> state_ = SessionState::idle;
    std::jthread source_thread_;
    std::jthread sink_thread_;
};

}  // namespace remote_gateway

