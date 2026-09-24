#include "remote_gateway/input_text.hpp"

namespace remote_gateway {

std::vector<std::uint16_t> utf8_to_utf16(const std::string& text,
                                         std::size_t maximum_units) {
    std::vector<std::uint16_t> result;
    for (std::size_t index = 0; index < text.size() && result.size() < maximum_units;) {
        const auto first = static_cast<unsigned char>(text[index]);
        std::uint32_t codepoint = 0;
        std::size_t length = 0;
        if (first < 0x80) { codepoint = first; length = 1; }
        else if ((first & 0xE0) == 0xC0) { codepoint = first & 0x1F; length = 2; }
        else if ((first & 0xF0) == 0xE0) { codepoint = first & 0x0F; length = 3; }
        else if ((first & 0xF8) == 0xF0) { codepoint = first & 0x07; length = 4; }
        else { ++index; continue; }
        if (index + length > text.size()) break;
        bool valid = true;
        for (std::size_t offset = 1; offset < length; ++offset) {
            const auto continuation = static_cast<unsigned char>(text[index + offset]);
            if ((continuation & 0xC0) != 0x80) { valid = false; break; }
            codepoint = (codepoint << 6) | (continuation & 0x3F);
        }
        if (!valid) { ++index; continue; }
        index += length;
        if (codepoint <= 0xFFFF && !(codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
            result.push_back(static_cast<std::uint16_t>(codepoint));
        } else if (codepoint <= 0x10FFFF && result.size() + 1 < maximum_units) {
            codepoint -= 0x10000;
            result.push_back(static_cast<std::uint16_t>(0xD800 + (codepoint >> 10)));
            result.push_back(static_cast<std::uint16_t>(0xDC00 + (codepoint & 0x3FF)));
        }
    }
    return result;
}

}  // namespace remote_gateway
