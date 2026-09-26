#include "remotelink/ice_advertisement.hpp"

#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace remotelink {
namespace {
constexpr std::uint16_t kDefaultPortBegin = 50000;
constexpr std::uint16_t kDefaultPortEnd = 50019;

bool is_ipv4(std::string_view value) {
    int octets = 0;
    int number = -1;
    int digits = 0;
    for (std::size_t index = 0; index <= value.size(); ++index) {
        const bool end = index == value.size();
        const char character = end ? '.' : value[index];
        if (character == '.') {
            if (digits == 0 || number < 0 || number > 255) return false;
            ++octets;
            number = -1;
            digits = 0;
            if (end) break;
            continue;
        }
        if (character < '0' || character > '9' || digits == 3) return false;
        if (number < 0) number = 0;
        number = number * 10 + (character - '0');
        ++digits;
    }
    return octets == 4;
}

bool is_ipv6(std::string_view value) {
    if (value.size() < 2 || value.size() > 39 || value.find(':') == std::string_view::npos)
        return false;
    for (const char character : value) {
        const bool hex = (character >= '0' && character <= '9') ||
                         (character >= 'a' && character <= 'f') ||
                         (character >= 'A' && character <= 'F');
        if (!hex && character != ':') return false;
    }
    return true;
}

bool is_numeric_address(std::string_view value) { return is_ipv4(value) || is_ipv6(value); }

std::uint16_t parse_port(const char* value, const char* name) {
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    if (end == value || *end != '\0' || parsed < 1 || parsed > 65535) {
        throw std::runtime_error(std::string(name) + " must be a UDP port from 1 to 65535");
    }
    return static_cast<std::uint16_t>(parsed);
}

std::vector<std::string> split_lines(std::string_view text) {
    std::vector<std::string> lines;
    std::string current;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char character = text[index];
        if (character == '\n') {
            if (!current.empty() && current.back() == '\r') current.pop_back();
            lines.push_back(std::move(current));
            current.clear();
            continue;
        }
        current.push_back(character);
    }
    if (!current.empty() && current.back() == '\r') current.pop_back();
    if (!current.empty() || (!text.empty() && text.back() == '\n')) lines.push_back(std::move(current));
    return lines;
}

std::string join_lines(const std::vector<std::string>& lines, bool crlf) {
    std::string joined;
    const char* separator = crlf ? "\r\n" : "\n";
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (index != 0) joined += separator;
        joined += lines[index];
    }
    return joined;
}
}  // namespace

IceAdvertisement ice_advertisement_from_environment() {
    IceAdvertisement advertisement;
    if (const char* address = std::getenv("REMOTELINK_ICE_ADVERTISED_ADDRESS");
        address != nullptr && *address != '\0') {
        if (!is_numeric_address(address)) {
            throw std::runtime_error(
                "REMOTELINK_ICE_ADVERTISED_ADDRESS must be an IPv4 or IPv6 address");
        }
        advertisement.address = address;
    }
    const char* minimum = std::getenv("REMOTELINK_ICE_UDP_PORT_MIN");
    const char* maximum = std::getenv("REMOTELINK_ICE_UDP_PORT_MAX");
    const bool has_minimum = minimum != nullptr && *minimum != '\0';
    const bool has_maximum = maximum != nullptr && *maximum != '\0';
    if (has_minimum != has_maximum) {
        throw std::runtime_error(
            "REMOTELINK_ICE_UDP_PORT_MIN and REMOTELINK_ICE_UDP_PORT_MAX must be set together");
    }
    if (has_minimum) {
        advertisement.port_begin = parse_port(minimum, "REMOTELINK_ICE_UDP_PORT_MIN");
        advertisement.port_end = parse_port(maximum, "REMOTELINK_ICE_UDP_PORT_MAX");
        if (advertisement.port_begin > advertisement.port_end) {
            throw std::runtime_error(
                "REMOTELINK_ICE_UDP_PORT_MIN must not be greater than REMOTELINK_ICE_UDP_PORT_MAX");
        }
    } else if (advertisement.rewrite_candidates()) {
        advertisement.port_begin = kDefaultPortBegin;
        advertisement.port_end = kDefaultPortEnd;
    }
    return advertisement;
}

std::string rewrite_host_candidate(std::string_view candidate, std::string_view advertised_address) {
    if (advertised_address.empty() || !is_numeric_address(advertised_address)) return std::string(candidate);
    std::string body(candidate);
    std::string prefix;
    if (body.rfind("a=", 0) == 0) {
        prefix = "a=";
        body.erase(0, 2);
    }
    if (body.rfind("candidate:", 0) != 0) return std::string(candidate);
    body.erase(0, std::string("candidate:").size());

    std::istringstream input(body);
    std::string foundation, component, transport, priority, address, port, typ, kind;
    if (!(input >> foundation >> component >> transport >> priority >> address >> port >> typ >> kind))
        return std::string(candidate);
    if (typ != "typ" || kind != "host") return std::string(candidate);
    std::string remainder;
    std::getline(input, remainder);

    std::ostringstream rewritten;
    rewritten << prefix << "candidate:" << foundation << ' ' << component << ' ' << transport << ' '
              << priority << ' ' << advertised_address << ' ' << port << " typ host";
    if (!remainder.empty()) rewritten << remainder;
    return rewritten.str();
}

std::string rewrite_session_description(std::string_view sdp, std::string_view advertised_address) {
    if (advertised_address.empty()) return std::string(sdp);
    const bool crlf = sdp.find("\r\n") != std::string_view::npos;
    const auto lines = split_lines(sdp);
    std::vector<std::string> rewritten;
    rewritten.reserve(lines.size());
    const std::string ipv4_loopback = "IN IP4 127.0.0.1";
    const std::string ipv4_unspecified = "IN IP4 0.0.0.0";
    const std::string replacement = std::string("IN IP") +
                                    (advertised_address.find(':') == std::string_view::npos ? "4 " : "6 ") +
                                    std::string(advertised_address);
    for (const auto& line : lines) {
        if (line.rfind("a=candidate:", 0) == 0) {
            rewritten.push_back(rewrite_host_candidate(line, advertised_address));
            continue;
        }
        if (line.rfind("o=", 0) == 0 || line.rfind("c=", 0) == 0) {
            std::string updated = line;
            const auto loopback = updated.find(ipv4_loopback);
            if (loopback != std::string::npos) updated.replace(loopback, ipv4_loopback.size(), replacement);
            const auto unspecified = updated.find(ipv4_unspecified);
            if (unspecified != std::string::npos)
                updated.replace(unspecified, ipv4_unspecified.size(), replacement);
            rewritten.push_back(std::move(updated));
            continue;
        }
        rewritten.push_back(line);
    }
    return join_lines(rewritten, crlf);
}

}  // namespace remotelink
