#include "probe_core.h"

#include <array>
#include <iostream>
#include <string>

using namespace ultimate_tracker_probe;

#define CHECK(expression) do { \
    if (!(expression)) { \
        std::cerr << "check failed at line " << __LINE__ << ": " #expression "\n"; \
        return 1; \
    } \
} while (false)

int main() {
    unsigned value = 0;
    CHECK(ParseUnsigned("0", 30000, value) && value == 0);
    CHECK(ParseUnsigned("30000", 30000, value) && value == 30000);
    CHECK(ParseUnsigned("0x0bb4", 0xffff, value) && value == 0x0bb4);
    CHECK(ParseUnsigned("0X06A3", 0xffff, value) && value == 0x06a3);
    CHECK(!ParseUnsigned("30001", 30000, value));
    CHECK(!ParseUnsigned("-1", 30000, value));
    CHECK(!ParseUnsigned("0x", 0xffff, value));
    CHECK(!ParseUnsigned("0x10000", 0xffff, value));
    CHECK(!ParseUnsigned("12ms", 30000, value));
    CHECK(!ParseUnsigned("9", 0, value));

    CHECK(IsValidCaptureDuration(0));
    CHECK(IsValidCaptureDuration(kMaxCaptureMs));
    CHECK(!IsValidCaptureDuration(kMaxCaptureMs + 1));
    CHECK(!IsValidInputReportLength(0));
    CHECK(IsValidInputReportLength(1));
    CHECK(IsValidInputReportLength(kMaxInputReportBytes));
    CHECK(!IsValidInputReportLength(kMaxInputReportBytes + 1));
    CHECK(RawOutputWithinLimit(0, kMaxRawOutputBytes));
    CHECK(!RawOutputWithinLimit(0, kMaxRawOutputBytes + 1));
    CHECK(!RawOutputWithinLimit(kMaxRawOutputBytes, 1));
    CHECK(!RawOutputWithinLimit(kMaxRawOutputBytes + 1, 0));

    constexpr std::array<std::uint8_t, 4> report{0x00, 0x0a, 0x7f, 0xff};
    CHECK(HexBytes(report.data(), report.size()) == "00 0a 7f ff");
    CHECK(HexBytes(report.data(), 0).empty());

    const std::string untrusted = std::string("name\"\\\n", 7) + char(0x01);
    CHECK(EscapeJson(untrusted) == "name\\\"\\\\\\n\\u0001");
    CHECK(EscapeJson("plain UTF-8: \xc3\xa9") == "plain UTF-8: \xc3\xa9");
    return 0;
}
