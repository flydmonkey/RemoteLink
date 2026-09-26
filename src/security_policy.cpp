#include "remotelink/security_policy.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>

namespace remotelink {
namespace {

std::string_view trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }
    return value;
}

bool parse_ipv4(std::string_view value, std::uint32_t& result) {
    std::array<unsigned, 4> octets{};
    for (std::size_t index = 0; index < octets.size(); ++index) {
        const auto separator = value.find('.');
        const auto part = value.substr(0, separator);
        if (part.empty() || part.size() > 3) return false;
        unsigned octet = 0;
        for (const char character : part) {
            if (!std::isdigit(static_cast<unsigned char>(character))) return false;
            octet = octet * 10 + static_cast<unsigned>(character - '0');
        }
        if (octet > 255) return false;
        octets[index] = octet;
        if (index + 1 == octets.size()) {
            if (separator != std::string_view::npos) return false;
        } else {
            if (separator == std::string_view::npos) return false;
            value.remove_prefix(separator + 1);
        }
    }
    result = (octets[0] << 24U) | (octets[1] << 16U) |
             (octets[2] << 8U) | octets[3];
    return true;
}

bool matches_cidr(std::string_view host, std::string_view entry) {
    const auto slash = entry.find('/');
    if (slash == std::string_view::npos) return false;
    std::uint32_t address = 0;
    std::uint32_t network = 0;
    if (!parse_ipv4(host, address) || !parse_ipv4(entry.substr(0, slash), network)) return false;
    const auto prefix_text = entry.substr(slash + 1);
    if (prefix_text.empty() || prefix_text.size() > 2) return false;
    unsigned prefix = 0;
    for (const char character : prefix_text) {
        if (!std::isdigit(static_cast<unsigned char>(character))) return false;
        prefix = prefix * 10 + static_cast<unsigned>(character - '0');
    }
    if (prefix > 32) return false;
    const std::uint32_t mask = prefix == 0 ? 0 : (~std::uint32_t{0} << (32U - prefix));
    return (address & mask) == (network & mask);
}

}  // namespace

bool host_is_allowed(std::string_view host, std::string_view comma_separated_allowlist) {
    host = trim(host);
    if (host.empty()) return false;

    while (!comma_separated_allowlist.empty()) {
        const auto separator = comma_separated_allowlist.find(',');
        const auto entry = trim(comma_separated_allowlist.substr(0, separator));
        // A single wildcard intentionally disables host filtering. This applies
        // equally to DNS names, IPv4 addresses, and IPv6 addresses.
        if (!entry.empty() && (entry == "*" || entry == host || matches_cidr(host, entry))) return true;
        if (separator == std::string_view::npos) break;
        comma_separated_allowlist.remove_prefix(separator + 1);
    }
    return false;
}

bool constant_time_equal(std::string_view left, std::string_view right) noexcept {
    unsigned char difference = static_cast<unsigned char>(left.size() ^ right.size());
    const std::size_t length = std::max(left.size(), right.size());
    for (std::size_t index = 0; index < length; ++index) {
        const unsigned char left_byte = index < left.size() ? left[index] : 0;
        const unsigned char right_byte = index < right.size() ? right[index] : 0;
        difference |= static_cast<unsigned char>(left_byte ^ right_byte);
    }
    return difference == 0;
}

std::vector<std::string> parse_access_tokens(std::string_view comma_separated_tokens) {
    std::vector<std::string> tokens;
    while (!comma_separated_tokens.empty()) {
        const auto separator = comma_separated_tokens.find(',');
        const auto token = trim(comma_separated_tokens.substr(0, separator));
        if (!token.empty()) tokens.emplace_back(token);
        if (separator == std::string_view::npos) break;
        comma_separated_tokens.remove_prefix(separator + 1);
    }
    return tokens;
}

bool access_token_matches(std::string_view supplied,
                          const std::vector<std::string>& allowed_tokens) noexcept {
    bool matched = false;
    for (const auto& allowed : allowed_tokens) {
        // Evaluate every token so the matching position is not exposed by an
        // early return.
        matched = (!allowed.empty() && constant_time_equal(supplied, allowed)) || matched;
    }
    return matched;
}

std::optional<std::size_t> access_token_identity(
    std::string_view supplied, const std::vector<std::string>& allowed_tokens) noexcept {
    std::optional<std::size_t> identity;
    for (std::size_t index = 0; index < allowed_tokens.size(); ++index) {
        if (!allowed_tokens[index].empty() &&
            constant_time_equal(supplied, allowed_tokens[index]) && !identity) identity = index;
    }
    return identity;
}

}  // namespace remotelink
