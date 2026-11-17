#include "ultimate_hid_source.h"

#include <windows.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
#include <thread>
#include <vector>

namespace {

std::int64_t NowQpc() {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}

std::int64_t QpcFrequency() {
    LARGE_INTEGER value{};
    QueryPerformanceFrequency(&value);
    return value.QuadPart;
}

} // namespace

int main() {
    ultimate_hid::Source source;
    if (!source.start()) {
        std::cerr << "source_start_failed: " << source.error() << "\n";
        source.stop();
        return 2;
    }

    std::map<std::array<std::uint8_t, 6>, std::uint16_t> lastPacket;
    std::map<std::array<std::uint8_t, 6>, std::uint16_t> firstPacket;
    std::map<std::array<std::uint8_t, 6>, std::uint8_t> lastFlags;
    std::map<std::array<std::uint8_t, 6>, double> latestAgeMs;
    std::uint64_t observations = 0;
    std::uint64_t finiteQuaternionSamples = 0;
    std::int64_t maxAgeQpc = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto poses = source.snapshot();
        const auto now = NowQpc();
        for (const auto& pose : poses) {
            ++observations;
            bool finite = true;
            for (float component : pose.orientation_xyzw) finite = finite && std::isfinite(component);
            if (finite) ++finiteQuaternionSamples;
            const auto age = now - pose.received_qpc;
            if (age > maxAgeQpc) maxAgeQpc = age;
            const auto hz = QpcFrequency();
            latestAgeMs[pose.device_id] = hz > 0 && age >= 0
                ? 1000.0 * static_cast<double>(age) / static_cast<double>(hz) : 0.0;
            firstPacket.try_emplace(pose.device_id, pose.packet_index);
            lastPacket[pose.device_id] = pose.packet_index;
            lastFlags[pose.device_id] = pose.flags;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    source.stop();

    const auto hz = QpcFrequency();
    const double maxAgeMs = hz > 0 ? 1000.0 * static_cast<double>(maxAgeQpc) / static_cast<double>(hz) : 0.0;
    std::cout << "source_probe_seconds=5\n"
              << "trackers_observed=" << lastPacket.size() << "\n"
              << "snapshot_observations=" << observations << "\n"
              << "finite_quaternion_observations=" << finiteQuaternionSamples << "\n"
              << "maximum_snapshot_age_ms=" << maxAgeMs << "\n";
    for (const auto& entry : lastPacket) {
        const auto flags = lastFlags[entry.first];
        const bool advanced = firstPacket[entry.first] != entry.second;
        std::cout << "tracker_packet_advanced=" << (advanced ? "true" : "false")
                  << " latest_snapshot_age_ms=" << latestAgeMs[entry.first]
                  << " flags_raw=0x" << std::hex << static_cast<unsigned>(flags) << std::dec << "\n";
    }
    if (observations == 0) {
        std::cerr << "no_valid_pose_snapshots; source_error=" << source.error() << "\n";
        return 3;
    }
    return 0;
}
