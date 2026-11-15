// SPDX-License-Identifier: MIT
#include "../../virtualdesktop-openxr/external_trackers.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int main() try {
    using namespace external_trackers;
    State state;
    LARGE_INTEGER frequency, now;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&now);
    state.frequency = frequency.QuadPart;
    auto& sample = state.samples[1];
    sample.configured = 1;
    sample.qpc = now.QuadPart;
    require(fresh(sample, now.QuadPart, state.frequency), "fresh sample rejected");
    require(!fresh(sample, now.QuadPart + state.frequency, state.frequency), "stale sample accepted");
    require(!fresh(sample, now.QuadPart - 1, state.frequency), "future sample accepted");
    sample.orientation[3] = 0;
    require(!fresh(sample, now.QuadPart, state.frequency), "zero quaternion accepted");
    sample.orientation[3] = 1;
    sample.position[0] = std::numeric_limits<float>::quiet_NaN();
    require(!fresh(sample, now.QuadPart, state.frequency), "NaN accepted");
    sample.position[0] = 0;
    Reader reader;
    State received;
    require(!reader.read(received), "run tests with the real provider stopped");
    HANDLE lock = CreateMutexW(nullptr, FALSE, MutexName);
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(State), MappingName);
    require(lock && mapping, "create IPC");
    auto* view = static_cast<State*>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(State)));
    require(view != nullptr, "map IPC");
    *view = state;
    require(reader.read(received) && received.samples[1].configured, "IPC round trip");
    require(fresh(received.samples[1], now.QuadPart, received.frequency), "IPC pose corrupted");
    view->version = 99;
    require(!reader.read(received), "wrong version accepted");
    *view = state;
    view->frequency = 1;
    require(!reader.read(received), "wrong clock accepted");
    *view = state;
    HANDLE held = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bool acceptedWhileLocked = true;
    std::thread writer([&] {
        WaitForSingleObject(lock, INFINITE);
        SetEvent(held);
        WaitForSingleObject(release, INFINITE);
        ReleaseMutex(lock);
    });
    WaitForSingleObject(held, INFINITE);
    acceptedWhileLocked = reader.read(received);
    SetEvent(release);
    writer.join();
    require(!acceptedWhileLocked, "read while writer held mutex");
    require(reader.read(received), "reader did not recover");
    std::thread abandoned([&] { WaitForSingleObject(lock, INFINITE); });
    abandoned.join();
    require(!reader.read(received), "abandoned write accepted");
    view->samples[1].qpc = 0;
    require(reader.read(received) && !fresh(received.samples[1], now.QuadPart, state.frequency), "shutdown pose accepted");
    UnmapViewOfFile(view);
    CloseHandle(mapping); CloseHandle(lock); CloseHandle(held); CloseHandle(release);
    std::cout << "Passed: IPC round trip, freshness, invalid data, version/clock, contention, abandonment, shutdown\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << "\n"; return 1; }
