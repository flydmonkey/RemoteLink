#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace remotelink {

inline std::string session_identity(std::string_view hostname,
                                    std::string_view username) {
    std::string identity;
    identity.reserve(hostname.size() + username.size() + 1);
    const auto append_folded = [&identity](std::string_view value) {
        std::transform(value.begin(), value.end(), std::back_inserter(identity),
            [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
    };
    append_folded(hostname);
    identity.push_back('\0');
    append_folded(username);
    return identity;
}

}  // namespace remotelink
