#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace remotelink {

std::vector<std::uint16_t> utf8_to_utf16(const std::string& text,
                                         std::size_t maximum_units = 4096);

}  // namespace remotelink
