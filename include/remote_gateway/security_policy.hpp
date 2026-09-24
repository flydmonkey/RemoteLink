#pragma once

#include <string_view>
#include <string>
#include <vector>
#include <optional>

namespace remote_gateway {

bool host_is_allowed(std::string_view host, std::string_view comma_separated_allowlist);
bool constant_time_equal(std::string_view left, std::string_view right) noexcept;
std::vector<std::string> parse_access_tokens(std::string_view comma_separated_tokens);
bool access_token_matches(std::string_view supplied,
                          const std::vector<std::string>& allowed_tokens) noexcept;
std::optional<std::size_t> access_token_identity(
    std::string_view supplied, const std::vector<std::string>& allowed_tokens) noexcept;

}  // namespace remote_gateway
