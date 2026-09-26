#include "remotelink/session.hpp"

#include <stdexcept>
#include <iostream>

namespace remotelink {

Session::Session(std::unique_ptr<FrameSource> source,
                 std::unique_ptr<FrameSink> sink,
                 std::size_t queue_capacity)
    : source_(std::move(source)),
      sink_(std::move(sink)),
      frames_(queue_capacity) {
    if (!source_ || !sink_) {
        throw std::invalid_argument("session source and sink are required");
    }
}

Session::~Session() {
    stop();
}

void Session::start() {
    SessionState expected = SessionState::idle;
    if (!state_.compare_exchange_strong(expected, SessionState::running)) {
        throw std::logic_error("session can only be started once");
    }

    source_thread_ = std::jthread([this](std::stop_token stop_token) {
        try {
            source_->run(stop_token, [this](Frame frame) {
                frames_.push(std::move(frame));
            });
        }
        catch (const std::exception& error) {
            std::cerr << "frame source stopped: " << error.what() << '\n';
            state_.store(SessionState::stopped);
        }
    });

    sink_thread_ = std::jthread([this](std::stop_token stop_token) {
        try {
            while (auto frame = frames_.pop(stop_token)) {
                sink_->consume(*frame);
            }
        }
        catch (const std::exception& error) {
            std::cerr << "frame sink stopped: " << error.what() << '\n';
            source_thread_.request_stop();
            state_.store(SessionState::stopped);
        }
    });
}

void Session::stop() {
    const SessionState previous = state_.exchange(SessionState::stopping);
    if (previous == SessionState::idle || previous == SessionState::stopped) {
        state_.store(SessionState::stopped);
        return;
    }

    source_thread_.request_stop();
    sink_thread_.request_stop();
    if (source_thread_.joinable()) {
        source_thread_.join();
    }
    if (sink_thread_.joinable()) {
        sink_thread_.join();
    }
    state_.store(SessionState::stopped);
}

SessionState Session::state() const noexcept {
    return state_.load();
}

std::uint64_t Session::dropped_frames() const {
    return frames_.dropped_frames();
}

}  // namespace remotelink
