#include "remotelink/latest_frame_queue.hpp"
#include "remotelink/input_text.hpp"
#include "remotelink/security_policy.hpp"
#include "remotelink/reconnect_policy.hpp"
#include "remotelink/session_identity.hpp"
#include "remotelink/test_pattern_source.hpp"

#include <cassert>
#include <chrono>
#include <stdexcept>

using namespace std::chrono_literals;

int main() {
    using remotelink::Frame;
    using remotelink::LatestFrameQueue;

    assert(remotelink::host_is_allowed("10.17.12.55", "10.17.12.55"));
    assert(remotelink::host_is_allowed("10.17.12.55", " 192.168.1.5, 10.17.12.55 "));
    assert(!remotelink::host_is_allowed("10.17.12.5", "10.17.12.55"));
    assert(!remotelink::host_is_allowed("10.17.12.55.evil", "10.17.12.55"));
    assert(!remotelink::host_is_allowed("", "10.17.12.55"));
    assert(remotelink::host_is_allowed("10.17.12.55", "10.0.0.0/8"));
    assert(remotelink::host_is_allowed("192.168.9.176", "192.168.0.0/16"));
    assert(remotelink::host_is_allowed("172.31.255.254", "172.16.0.0/12"));
    assert(!remotelink::host_is_allowed("172.32.0.1", "172.16.0.0/12"));
    assert(!remotelink::host_is_allowed("8.8.8.8", "10.0.0.0/8,192.168.0.0/16"));
    assert(remotelink::host_is_allowed("8.8.8.8", "*"));
    assert(remotelink::host_is_allowed("rdp.example.com", "*"));
    assert(remotelink::host_is_allowed("2001:db8::10", "*"));
    assert(!remotelink::host_is_allowed("", "*"));
    assert(remotelink::constant_time_equal("secret", "secret"));
    assert(!remotelink::constant_time_equal("secret", "Secret"));
    assert(!remotelink::constant_time_equal("short", "shorter"));
    const auto access_tokens = remotelink::parse_access_tokens(" first,second , third ");
    assert(access_tokens.size() == 3);
    assert(remotelink::access_token_matches("second", access_tokens));
    assert(!remotelink::access_token_matches("unknown", access_tokens));
    assert(remotelink::access_token_identity("second", access_tokens) == 1);
    assert(!remotelink::access_token_identity("unknown", access_tokens));
    assert(!remotelink::access_token_identity("", {"first", ""}));
    assert(!remotelink::access_token_matches("", {"first", ""}));

    assert((remotelink::utf8_to_utf16("A") == std::vector<std::uint16_t>{0x0041}));
    assert((remotelink::utf8_to_utf16("\xE4\xB8\xAD") ==
            std::vector<std::uint16_t>{0x4E2D}));
    assert((remotelink::utf8_to_utf16("\xF0\x9F\x98\x80") ==
            std::vector<std::uint16_t>{0xD83D, 0xDE00}));
    assert(remotelink::utf8_to_utf16("abcdef", 3).size() == 3);
    assert(remotelink::session_identity("SERVER.local", "Administrator") ==
           remotelink::session_identity("server.LOCAL", "administrator"));
    assert(remotelink::session_identity("server-a", "administrator") !=
           remotelink::session_identity("server-b", "administrator"));
    assert(remotelink::session_identity("server", "administrator") !=
           remotelink::session_identity("server", "operator"));

    assert(remotelink::reconnect_delay(0) == 0s);
    assert(remotelink::reconnect_delay(1) == 2s);
    assert(remotelink::reconnect_delay(2) == 4s);
    assert(remotelink::reconnect_delay(3) == 8s);
    assert(remotelink::reconnect_delay(5) == 30s);
    assert(remotelink::reconnect_delay(100) == 30s);
    std::stop_source cancelled_wait;
    cancelled_wait.request_stop();
    assert(!remotelink::interruptible_wait(cancelled_wait.get_token(), 10s));

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

    remotelink::TestPatternSource source(64, 48, 30);
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

    remotelink::TestPatternSource paced_source(4, 4, 30);
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
