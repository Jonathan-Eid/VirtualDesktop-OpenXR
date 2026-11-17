#pragma once

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace ultimate_tracker_probe {

constexpr unsigned kMaxCaptureMs = 30000;
constexpr unsigned kMaxInputReportBytes = 4096;
constexpr std::size_t kMaxRawOutputBytes = 8 * 1024 * 1024;

inline bool ParseUnsigned(std::string_view text, unsigned maxValue, unsigned& value) {
    if (text.empty()) return false;

    unsigned base = 10;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text.remove_prefix(2);
    }
    if (text.empty()) return false;

    unsigned parsed = 0;
    for (const char ch : text) {
        unsigned digit = 0;
        if (ch >= '0' && ch <= '9') digit = static_cast<unsigned>(ch - '0');
        else if (base == 16 && ch >= 'a' && ch <= 'f') digit = static_cast<unsigned>(ch - 'a' + 10);
        else if (base == 16 && ch >= 'A' && ch <= 'F') digit = static_cast<unsigned>(ch - 'A' + 10);
        else return false;
        if (digit >= base || digit > maxValue || parsed > (maxValue - digit) / base) return false;
        parsed = parsed * base + digit;
    }
    value = parsed;
    return true;
}

inline bool IsValidCaptureDuration(unsigned milliseconds) {
    return milliseconds <= kMaxCaptureMs;
}

inline bool IsValidInputReportLength(unsigned bytes) {
    return bytes > 0 && bytes <= kMaxInputReportBytes;
}

inline bool RawOutputWithinLimit(std::size_t currentBytes, std::size_t additionalBytes) {
    return additionalBytes <= kMaxRawOutputBytes && currentBytes <= kMaxRawOutputBytes - additionalBytes;
}

inline std::string HexBytes(const std::uint8_t* bytes, std::size_t length) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < length; ++i) {
        if (i != 0) output << ' ';
        output << std::setw(2) << static_cast<unsigned>(bytes[i]);
    }
    return output.str();
}

inline std::string EscapeJson(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char raw : value) {
        const auto ch = static_cast<unsigned char>(raw);
        switch (ch) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (ch < 0x20) {
                    constexpr char digits[] = "0123456789abcdef";
                    escaped += "\\u00";
                    escaped += digits[(ch >> 4) & 0x0f];
                    escaped += digits[ch & 0x0f];
                } else {
                    escaped += static_cast<char>(ch);
                }
        }
    }
    return escaped;
}

} // namespace ultimate_tracker_probe
