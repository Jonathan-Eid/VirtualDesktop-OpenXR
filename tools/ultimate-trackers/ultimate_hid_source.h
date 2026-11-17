#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ultimate_hid {

struct Pose {
    std::array<std::uint8_t, 6> device_id{};
    std::uint16_t packet_index = 0;
    std::uint8_t flags = 0;
    float position[3]{};
    // Unit quaternion in x, y, z, w order, in the raw tracker HID frame.
    float orientation_xyzw[4]{};
    std::int64_t received_qpc = 0;
};

// Pure decoder for the observed Windows HID framing. Returns false for any
// non-pose, unsupported length, non-finite transform, or non-full-pose flag.
bool DecodeInputReport(const std::uint8_t* bytes, std::size_t length,
                       std::int64_t receivedQpc, Pose& pose);

class Source {
public:
    Source();
    ~Source();
    Source(const Source&) = delete;
    Source& operator=(const Source&) = delete;

    // Starts a passive reader for HTC VID 0x0bb4 / wireless dongle PID 0x0350.
    bool start();
    void stop();
    std::vector<Pose> snapshot() const;
    std::string error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ultimate_hid
