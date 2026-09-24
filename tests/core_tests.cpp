#include "remote_gateway/latest_frame_queue.hpp"
#include "remote_gateway/input_text.hpp"
#include "remote_gateway/security_policy.hpp"
#include "remote_gateway/reconnect_policy.hpp"
#include "remote_gateway/session_identity.hpp"
#include "remote_gateway/test_pattern_source.hpp"

#include <cassert>
#include <chrono>
#include <stdexcept>

using namespace std::chrono_literals;

int main() {
    using remote_gateway::Frame;
    using remote_gateway::LatestFrameQueue;

    assert(remote_gateway::host_is_allowed("10.17.12.55", "10.17.12.55"));
    assert(remote_gateway::host_is_allowed("10.17.12.55", " 192.168.1.5, 10.17.12.55 "));
    assert(!remote_gateway::host_is_allowed("10.17.12.5", "10.17.12.55"));
    assert(!remote_gateway::host_is_allowed("10.17.12.55.evil", "10.17.12.55"));
    assert(!remote_gateway::host_is_allowed("", "10.17.12.55"));
    assert(remote_gateway::host_is_allowed("10.17.12.55", "10.0.0.0/8"));
    assert(remote_gateway::host_is_allowed("192.168.9.176", "192.168.0.0/16"));
    assert(remote_gateway::host_is_allowed("172.31.255.254", "172.16.0.0/12"));
    assert(!remote_gateway::host_is_allowed("172.32.0.1", "172.16.0.0/12"));
    assert(!remote_gateway::host_is_allowed("8.8.8.8", "10.0.0.0/8,192.168.0.0/16"));
    assert(remote_gateway::host_is_allowed("8.8.8.8", "*"));
    assert(remote_gateway::host_is_allowed("rdp.example.com", "*"));
    assert(remote_gateway::host_is_allowed("2001:db8::10", "*"));
    assert(!remote_gateway::host_is_allowed("", "*"));
    assert(remote_gateway::constant_time_equal("secret", "secret"));
    assert(!remote_gateway::constant_time_equal("secret", "Secret"));
    assert(!remote_gateway::constant_time_equal("short", "shorter"));
    const auto access_tokens = remote_gateway::parse_access_tokens(" first,second , third ");
    assert(access_tokens.size() == 3);
    assert(remote_gateway::access_token_matches("second", access_tokens));
    assert(!remote_gateway::access_token_matches("unknown", access_tokens));
    assert(remote_gateway::access_token_identity("second", access_tokens) == 1);
    assert(!remote_gateway::access_token_identity("unknown", access_tokens));
    assert(!remote_gateway::access_token_identity("", {"first", ""}));
    assert(!remote_gateway::access_token_matches("", {"first", ""}));

    assert((remote_gateway::utf8_to_utf16("A") == std::vector<std::uint16_t>{0x0041}));
    assert((remote_gateway::utf8_to_utf16("\xE4\xB8\xAD") ==
            std::vector<std::uint16_t>{0x4E2D}));
    assert((remote_gateway::utf8_to_utf16("\xF0\x9F\x98\x80") ==
            std::vector<std::uint16_t>{0xD83D, 0xDE00}));
    assert(remote_gateway::utf8_to_utf16("abcdef", 3).size() == 3);
    assert(remote_gateway::session_identity("SERVER.local", "Administrator") ==
           remote_gateway::session_identity("server.LOCAL", "administrator"));
    assert(remote_gateway::session_identity("server-a", "administrator") !=
           remote_gateway::session_identity("server-b", "administrator"));
    assert(remote_gateway::session_identity("server", "administrator") !=
           remote_gateway::session_identity("server", "operator"));

    assert(remote_gateway::reconnect_delay(0) == 0s);
    assert(remote_gateway::reconnect_delay(1) == 2s);
    assert(remote_gateway::reconnect_delay(2) == 4s);
    assert(remote_gateway::reconnect_delay(3) == 8s);
    assert(remote_gateway::reconnect_delay(5) == 30s);
    assert(remote_gateway::reconnect_delay(100) == 30s);
    std::stop_source cancelled_wait;
    cancelled_wait.request_stop();
    assert(!remote_gateway::interruptible_wait(cancelled_wait.get_token(), 10s));

    bool rejected_zero_capacity = false;
    try {
        LatestFrameQueue invalid(0);
    }
    catch (const std::invalid_argument&) {
        rejected_zero_capacity = true;
    }
    assert(rejected_zero_capacity);

    LatestFrameQueue queue(2);
    queue.push(Frame {.sequence = 1});
    queue.push(Frame {.sequence = 2});
    queue.push(Frame {.sequence = 3});

    assert(queue.size() == 2);
    assert(queue.dropped_frames() == 1);

    std::stop_source stop_source;
    auto first = queue.pop(stop_source.get_token());
    auto second = queue.pop(stop_source.get_token());
    assert(first && first->sequence == 2);
    assert(second && second->sequence == 3);

    remote_gateway::TestPatternSource source(64, 48, 30);
    std::stop_source source_stop;
    int received = 0;
    source.run(source_stop.get_token(), [&](Frame frame) {
        assert(frame.width == 64);
        assert(frame.height == 48);
        assert(frame.stride == 64 * 4);
        assert(frame.pixels.size() == 64U * 48U * 4U);
        ++received;
        source_stop.request_stop();
    });
    assert(received == 1);

    remote_gateway::TestPatternSource paced_source(4, 4, 30);
    std::stop_source paced_stop;
    int paced_frames = 0;
    const auto started_at = std::chrono::steady_clock::now();
    paced_source.run(paced_stop.get_token(), [&](Frame) {
        if (++paced_frames == 4) {
            paced_stop.request_stop();
        }
    });
    const auto elapsed = std::chrono::steady_clock::now() - started_at;
    assert(elapsed >= 90ms);
    assert(elapsed < 500ms);

    return 0;
}
