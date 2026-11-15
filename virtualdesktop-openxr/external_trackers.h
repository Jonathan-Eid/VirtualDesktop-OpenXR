// SPDX-License-Identifier: MIT
#pragma once

#include <windows.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>

// Private, versioned IPC for the experimental SteamVR-free provider. Poses are
// in the current Quest STAGE space: metres, right handed, quaternion XYZW.
namespace external_trackers {
inline constexpr wchar_t MappingName[] = L"Local\\VDXR.Libsurvive.v1";
inline constexpr wchar_t MutexName[] = L"Local\\VDXR.Libsurvive.Lock.v1";
inline constexpr uint32_t Magic = 0x56585431;
inline constexpr std::array<const char*, 14> Roles = {
    "chest", "waist", "left_shoulder", "right_shoulder", "left_elbow", "right_elbow",
    "left_wrist", "right_wrist", "left_knee", "right_knee", "left_ankle", "right_ankle",
    "left_foot", "right_foot"};
#pragma pack(push, 8)
struct Sample {
    int64_t qpc{};
    float position[3]{};
    float orientation[4]{0, 0, 0, 1};
    uint32_t configured{};
};
struct State {
    uint32_t magic{Magic};
    uint32_t version{1};
    uint32_t bytes{sizeof(State)};
    uint32_t reserved{};
    int64_t frequency{};
    Sample samples[Roles.size()]{};
};
#pragma pack(pop)
static_assert(sizeof(Sample) == 40 && sizeof(State) == 584, "IPC ABI changed");

inline bool fresh(const Sample& sample, int64_t now, int64_t frequency) {
    if (!sample.configured || sample.qpc <= 0 || frequency <= 0 || now < sample.qpc ||
        now - sample.qpc > frequency / 10) {
        return false;
    }
    double norm = 0;
    for (float v : sample.position) if (!std::isfinite(v)) return false;
    for (float v : sample.orientation) {
        if (!std::isfinite(v)) return false;
        norm += double(v) * v;
    }
    return std::abs(norm - 1) < 0.01;
}

class Reader {
public:
    ~Reader() {
        if (m_view) UnmapViewOfFile(m_view);
        if (m_mapping) CloseHandle(m_mapping);
        if (m_mutex) CloseHandle(m_mutex);
    }
    bool read(State& state) const {
        std::lock_guard<std::mutex> guard(m_localMutex);
        // Retry opening if the provider was not running yet.
        if (!m_mutex) m_mutex = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, MutexName);
        if (!m_mapping) m_mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, MappingName);
        if (!m_view && m_mapping) m_view = MapViewOfFile(m_mapping, FILE_MAP_READ, 0, 0, sizeof(State));
        if (!m_mutex || !m_view) return false;
        const auto result = WaitForSingleObject(m_mutex, 0);
        if (result == WAIT_ABANDONED) {
            ReleaseMutex(m_mutex);
            return false;
        }
        if (result != WAIT_OBJECT_0) return false; // Never wait on the rendering thread.
        std::memcpy(&state, m_view, sizeof(state));
        ReleaseMutex(m_mutex);
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        return state.magic == Magic && state.version == 1 && state.bytes == sizeof(State) &&
               state.frequency == frequency.QuadPart;
    }
private:
    mutable std::mutex m_localMutex;
    mutable HANDLE m_mapping{}, m_mutex{};
    mutable void* m_view{};
};
} // namespace external_trackers
