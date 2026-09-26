#include "remotelink/ice_advertisement.hpp"

#include <cassert>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace {
void set_env(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value == nullptr ? "" : value);
#else
    if (value == nullptr) unsetenv(name);
    else setenv(name, value, 1);
#endif
}

void clear_ice_env() {
    set_env("REMOTELINK_ICE_ADVERTISED_ADDRESS", nullptr);
    set_env("REMOTELINK_ICE_UDP_PORT_MIN", nullptr);
    set_env("REMOTELINK_ICE_UDP_PORT_MAX", nullptr);
}
}  // namespace

int main() {
    const std::string original =
        "a=candidate:1 1 UDP 2114977791 172.17.0.2 56746 typ host";
    const auto rewritten = remotelink::rewrite_host_candidate(original, "192.0.2.10");
    assert(rewritten == "a=candidate:1 1 UDP 2114977791 192.0.2.10 56746 typ host");
    assert(remotelink::rewrite_host_candidate(
               "candidate:8 1 UDP 1694498815 172.17.0.2 50001 typ srflx raddr 0.0.0.0 rport 0",
               "192.0.2.10")
               .find("172.17.0.2") != std::string::npos);
    assert(remotelink::rewrite_host_candidate(original, "").find("172.17.0.2") != std::string::npos);
    assert(remotelink::rewrite_host_candidate("not-a-candidate", "192.0.2.10") == "not-a-candidate");

    const std::string sdp =
        "v=0\r\n"
        "o=- 1 0 IN IP4 127.0.0.1\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "a=candidate:1 1 UDP 2114977791 172.17.0.2 56746 typ host\r\n"
        "a=candidate:2 1 UDP 1694498815 172.17.0.2 56746 typ srflx\r\n";
    const auto description = remotelink::rewrite_session_description(sdp, "192.0.2.10");
    assert(description.find("IN IP4 192.0.2.10") != std::string::npos);
    assert(description.find("IN IP4 127.0.0.1") == std::string::npos);
    assert(description.find("IN IP4 0.0.0.0") == std::string::npos);
    assert(description.find("192.0.2.10 56746 typ host") != std::string::npos);
    assert(description.find("172.17.0.2 56746 typ srflx") != std::string::npos);

    clear_ice_env();
    const auto unset = remotelink::ice_advertisement_from_environment();
    assert(!unset.rewrite_candidates());
    assert(!unset.restrict_ports());

    set_env("REMOTELINK_ICE_ADVERTISED_ADDRESS", "192.0.2.10");
    const auto defaults = remotelink::ice_advertisement_from_environment();
    assert(defaults.address == "192.0.2.10");
    assert(defaults.port_begin == 50000);
    assert(defaults.port_end == 50019);

    set_env("REMOTELINK_ICE_UDP_PORT_MIN", "40000");
    set_env("REMOTELINK_ICE_UDP_PORT_MAX", "40003");
    const auto limited = remotelink::ice_advertisement_from_environment();
    assert(limited.port_begin == 40000);
    assert(limited.port_end == 40003);

    set_env("REMOTELINK_ICE_ADVERTISED_ADDRESS", "not-an-address");
    bool rejected = false;
    try { (void)remotelink::ice_advertisement_from_environment(); }
    catch (const std::runtime_error&) { rejected = true; }
    assert(rejected);
    clear_ice_env();
    return 0;
}
