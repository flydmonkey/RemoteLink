#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace remotelink {

struct IceAdvertisement {
    std::string address;
    std::uint16_t port_begin = 0;
    std::uint16_t port_end = 0;

    [[nodiscard]] bool rewrite_candidates() const { return !address.empty(); }
    [[nodiscard]] bool restrict_ports() const { return port_begin != 0; }
};

[[nodiscard]] IceAdvertisement ice_advertisement_from_environment();

[[nodiscard]] std::string rewrite_host_candidate(std::string_view candidate,
                                                 std::string_view advertised_address);

[[nodiscard]] std::string rewrite_session_description(std::string_view sdp,
                                                      std::string_view advertised_address);

}  // namespace remotelink
