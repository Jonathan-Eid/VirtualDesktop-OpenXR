#include "../../virtualdesktop-openxr/ultimate_tracker_ipc.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <atomic>

using namespace ultimate_tracker_ipc;
static void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static void publish(State* target, HANDLE mutex, const State& state) {
    check(WaitForSingleObject(mutex, 1000) == WAIT_OBJECT_0, "acquire fixture mutex");
    *target = state;
    ReleaseMutex(mutex);
}
int main() try {
    std::cout << "Synthetic fixture only; no hardware/provider is involved.\n";
    HANDLE mutex = CreateMutexW(nullptr, FALSE, MutexName);
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(State), MappingName);
    check(mutex && mapping && GetLastError() != ERROR_ALREADY_EXISTS, "create isolated fixture mapping (another provider may be running)");
    auto* view = static_cast<State*>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(State)));
    check(view != nullptr, "map fixture");
    LARGE_INTEGER f{}, n{}; check(QueryPerformanceFrequency(&f) && QueryPerformanceCounter(&n), "QPC unavailable");
    State state{}; state.frequency = f.QuadPart;
    state.samples[1].configured = 1; state.samples[1].qpc = n.QuadPart; state.samples[1].orientation[3] = 1;
    publish(view, mutex, state);
    Reader reader; State read{}; check(reader.read(read), "valid protocol rejected");
    check(fresh(read.samples[1], n.QuadPart, f.QuadPart), "valid sample rejected");
    auto sample = state.samples[1];
    check(!fresh(sample, n.QuadPart + f.QuadPart / 10 + 1, f.QuadPart), "expired sample accepted");
    check(!fresh(sample, n.QuadPart - 1, f.QuadPart), "future sample accepted");
    check(!fresh(sample, n.QuadPart, 0), "zero frequency accepted");
    sample.position[1] = std::numeric_limits<float>::quiet_NaN(); check(!fresh(sample, n.QuadPart, f.QuadPart), "NaN position accepted");
    sample = state.samples[1]; sample.orientation[0] = 0.5f; check(!fresh(sample, n.QuadPart, f.QuadPart), "non-unit orientation accepted");
    sample = state.samples[1]; sample.configured = 0; check(!fresh(sample, n.QuadPart, f.QuadPart), "unconfigured role accepted");
    state.magic++; publish(view, mutex, state); check(!reader.read(read), "bad magic accepted");
    state.magic = Magic; state.version++; publish(view, mutex, state); check(!reader.read(read), "bad version accepted");
    state.version = 1; state.bytes--; publish(view, mutex, state); check(!reader.read(read), "bad byte size accepted");
    state.bytes = sizeof(State); state.frequency++; publish(view, mutex, state); check(!reader.read(read), "bad QPC frequency accepted");
    state.frequency = f.QuadPart; publish(view, mutex, state);
    std::atomic<bool> locked{false};
    std::thread holder([&] { WaitForSingleObject(mutex, INFINITE); locked = true; std::this_thread::sleep_for(std::chrono::milliseconds(100)); ReleaseMutex(mutex); });
    while (!locked.load()) std::this_thread::yield();
    check(!reader.read(read), "busy mutex caused a blocking/successful read"); holder.join();
    std::thread abandon([&] { WaitForSingleObject(mutex, INFINITE); }); abandon.join();
    check(!reader.read(read), "abandoned mutex accepted");
    check(reader.read(read), "reader failed after abandoned mutex recovery");
    UnmapViewOfFile(view); CloseHandle(mapping); CloseHandle(mutex);
    std::cout << "Passed synthetic checks: protocol validation, pose validation/expiry, nonblocking contention, and abandoned mutex recovery.\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << "\n"; return 1; }
