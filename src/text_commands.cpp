#include "text_commands.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

std::string reverse_lines(const std::string& text) {
    std::vector<std::string_view> lines;
    for (std::size_t start = 0; start < text.size();) {
        const auto newline = text.find('\n', start);
        const auto end = newline == std::string::npos ? text.size() : newline + 1;
        lines.emplace_back(text.data() + start, end - start);
        start = end;
    }
    std::string result;
    result.reserve(text.size());
    for (auto line = lines.rbegin(); line != lines.rend(); ++line) result.append(*line);
    return result;
}

bool reverse_characters(const std::string& text, std::string& result, std::string& error) {
    result.clear();
    result.reserve(text.size());
    for (std::size_t start = 0; start < text.size();) {
        const auto newline = text.find('\n', start);
        const auto end = newline == std::string::npos ? text.size() : newline;
        auto content_end = end;
        if (newline != std::string::npos && end > start && text[end - 1] == '\r') --content_end;
        std::vector<std::string_view> characters;
        for (auto i = start; i < content_end;) {
            const auto lead = static_cast<unsigned char>(text[i]);
            std::size_t length = 0;
            std::uint32_t point = 0;
            if (lead < 0x80) { length = 1; point = lead; }
            else if (lead >= 0xc2 && lead <= 0xdf) { length = 2; point = lead & 0x1f; }
            else if (lead >= 0xe0 && lead <= 0xef) { length = 3; point = lead & 0x0f; }
            else if (lead >= 0xf0 && lead <= 0xf4) { length = 4; point = lead & 0x07; }
            if (!length || length > content_end - i) { error = "invalid UTF-8 input"; return false; }
            for (std::size_t j = 1; j < length; ++j) {
                const auto byte = static_cast<unsigned char>(text[i + j]);
                if ((byte & 0xc0) != 0x80) { error = "invalid UTF-8 input"; return false; }
                point = (point << 6) | (byte & 0x3f);
            }
            if ((length == 3 && point < 0x800) || (length == 4 && point < 0x10000) ||
                (point >= 0xd800 && point <= 0xdfff) || point > 0x10ffff) {
                error = "invalid UTF-8 input";
                return false;
            }
            characters.emplace_back(text.data() + i, length);
            i += length;
        }
        for (auto c = characters.rbegin(); c != characters.rend(); ++c) result.append(*c);
        if (content_end != end) result.push_back('\r');
        if (newline != std::string::npos) result.push_back('\n');
        start = newline == std::string::npos ? text.size() : newline + 1;
    }
    return true;
}
