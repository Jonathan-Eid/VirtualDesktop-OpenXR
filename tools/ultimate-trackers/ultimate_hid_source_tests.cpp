#include "ultimate_hid_source.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

namespace {

#define CHECK(expression) do { \
    if (!(expression)) { \
        std::cerr << "check failed at line " << __LINE__ << ": " #expression "\n"; \
        return 1; \
    } \
} while (false)

void PutU16(std::array<std::uint8_t, 65>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

void PutFloat(std::array<std::uint8_t, 65>& bytes, std::size_t offset, float value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

std::array<std::uint8_t, 65> MakePose(std::uint8_t flags = 0x12, std::uint8_t payloadLength = 0x27) {
    std::array<std::uint8_t, 65> bytes{};
    bytes[0] = 0;      // Windows unnumbered HID report-ID placeholder
    bytes[1] = 0x28;   // dongle incoming-report command
    PutU16(bytes, 2, 0x1234);
    for (std::size_t i = 0; i < 6; ++i) bytes[4 + i] = static_cast<std::uint8_t>(i + 1);
    PutU16(bytes, 10, 0x0110);
    bytes[12] = payloadLength;
    const std::size_t p = 13;
    bytes[p] = 7;
    bytes[p + 1] = 0;
    PutFloat(bytes, p + 2, 1.25f);
    PutFloat(bytes, p + 6, -2.5f);
    PutFloat(bytes, p + 10, 3.75f);
    PutU16(bytes, p + 14, 0x3c00); // w = 1 in binary16
    PutU16(bytes, p + 16, 0x0000); // z
    PutU16(bytes, p + 18, 0x0000); // y
    PutU16(bytes, p + 20, 0x0000); // x
    bytes[p + 36] = flags;
    bytes[p + 37] = 0xa5; // unknown trailing bytes for 0x27
    bytes[p + 38] = 0x5a;
    return bytes;
}

} // namespace

int main() {
    using ultimate_hid::DecodeInputReport;
    using ultimate_hid::Pose;
    auto bytes = MakePose();
    Pose pose{};
    CHECK(DecodeInputReport(bytes.data(), bytes.size(), 987654, pose));
    CHECK(pose.device_id[0] == 1 && pose.device_id[5] == 6);
    CHECK(pose.packet_index == 0x1234);
    CHECK(pose.flags == 0x12);
    CHECK(pose.position[0] == 1.25f && pose.position[1] == -2.5f && pose.position[2] == 3.75f);
    CHECK(pose.orientation_xyzw[0] == 0.0f && pose.orientation_xyzw[1] == 0.0f);
    CHECK(pose.orientation_xyzw[2] == 0.0f && pose.orientation_xyzw[3] == 1.0f);
    CHECK(pose.received_qpc == 987654);
    bytes = MakePose(0x02, 0x25);
    CHECK(DecodeInputReport(bytes.data(), bytes.size(), 1, pose));

    bytes = MakePose(0x03);
    CHECK(!DecodeInputReport(bytes.data(), bytes.size(), 1, pose));
    bytes = MakePose(0x04);
    CHECK(!DecodeInputReport(bytes.data(), bytes.size(), 1, pose));
    bytes = MakePose(0x02, 0x24);
    CHECK(!DecodeInputReport(bytes.data(), bytes.size(), 1, pose));
    bytes = MakePose();
    PutFloat(bytes, 15, std::numeric_limits<float>::quiet_NaN());
    CHECK(!DecodeInputReport(bytes.data(), bytes.size(), 1, pose));
    bytes = MakePose();
    PutFloat(bytes, 15, std::numeric_limits<float>::infinity());
    CHECK(!DecodeInputReport(bytes.data(), bytes.size(), 1, pose));
    bytes = MakePose();
    PutU16(bytes, 27, 0x0000); // zero quaternion norm
    CHECK(!DecodeInputReport(bytes.data(), bytes.size(), 1, pose));
    bytes = MakePose();
    PutU16(bytes, 27, 0x3c66); // w is outside the observed near-unit tolerance
    CHECK(!DecodeInputReport(bytes.data(), bytes.size(), 1, pose));
    bytes = MakePose();
    bytes[1] = 0x29;
    CHECK(!DecodeInputReport(bytes.data(), bytes.size(), 1, pose));
    CHECK(!DecodeInputReport(bytes.data(), bytes.size() - 1, 1, pose));
    CHECK(!DecodeInputReport(nullptr, 65, 1, pose));
    return 0;
}
