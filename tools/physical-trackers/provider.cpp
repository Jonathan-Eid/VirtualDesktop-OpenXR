// SPDX-License-Identifier: MIT
#include "../../virtualdesktop-openxr/external_trackers.h"
#include <libsurvive/survive_api.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
volatile LONG stopped = 0;
BOOL WINAPI stop(DWORD) { InterlockedExchange(&stopped, 1); return TRUE; }
struct Handle {
    HANDLE value{};
    ~Handle() { if (value) CloseHandle(value); }
};
// Standard Hamilton composition: result = a * b. XYZW quaternion order.
std::array<double, 4> multiply(const std::array<double, 4>& a, const std::array<double, 4>& b) {
    return {a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1],
            a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0],
            a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3],
            a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2]};
}
}

int main(int argc, char** argv) try {
    std::cout << std::unitbuf;
    using namespace external_trackers;
    std::map<std::string, size_t> serialRoles;
    std::string transformFile;
    bool probe = false;
    double duration = 0;
    std::vector<char*> surviveArgs{argv[0]};
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--probe") probe = true;
        else if (arg == "--duration" && i + 1 < argc) {
            duration = std::stod(argv[++i]);
            if (!std::isfinite(duration) || duration <= 0) throw std::runtime_error("Duration must be positive seconds");
        }
        else if (arg == "--transform" && i + 1 < argc) transformFile = argv[++i];
        else if (arg == "--map" && i + 1 < argc) {
            std::string mapping = argv[++i];
            const auto split = mapping.find('=');
            const auto role = mapping.substr(0, split);
            auto found = std::find_if(Roles.begin(), Roles.end(), [&](const char* name) { return role == name; });
            if (split == std::string::npos || split + 1 == mapping.size() || found == Roles.end())
                throw std::runtime_error("Expected --map role=hardware-serial");
            const auto index = size_t(found - Roles.begin());
            for (const auto& entry : serialRoles) if (entry.second == index) throw std::runtime_error("Duplicate role");
            if (!serialRoles.emplace(mapping.substr(split + 1), index).second) throw std::runtime_error("Duplicate serial");
        } else if (arg == "--") {
            while (++i < argc) surviveArgs.push_back(argv[i]);
        } else throw std::runtime_error("Usage: vdxr-survive --probe | --map waist=SERIAL --transform stage.txt [-- libsurvive-options]");
    }
    // The transform maps raw libsurvive world coordinates directly to Quest STAGE.
    // It must include the axis conversion as well as room alignment.
    std::array<double, 3> translation{};
    std::array<double, 4> rotation{0,0,0,1};
    if (!probe) {
        if (serialRoles.empty() || transformFile.empty()) throw std::runtime_error("Publishing requires role mappings and an explicit STAGE transform");
        std::ifstream file(transformFile);
        for (auto& x : translation) if (!(file >> x) || !std::isfinite(x)) throw std::runtime_error("Invalid transform position");
        double norm = 0;
        for (auto& x : rotation) {
            if (!(file >> x) || !std::isfinite(x)) throw std::runtime_error("Invalid transform quaternion");
            norm += x*x;
        }
        std::string extra;
        if ((file >> extra) || std::abs(norm-1) > 0.01) throw std::runtime_error("Transform requires exactly x y z qx qy qz qw with a unit quaternion");
        for (auto& x : rotation) x /= std::sqrt(norm);
    }

    Handle owner{CreateMutexW(nullptr, TRUE, L"Local\\VDXR.Libsurvive.Writer.v1")};
    if (!owner.value || GetLastError() == ERROR_ALREADY_EXISTS) throw std::runtime_error("Another provider owns the channel");
    Handle lock{CreateMutexW(nullptr, FALSE, MutexName)};
    Handle mapping;
    void* view = nullptr;
    struct ViewGuard { void*& view; ~ViewGuard() { if (view) UnmapViewOfFile(view); } } viewGuard{view};
    State state;
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    state.frequency = frequency.QuadPart;
    for (const auto& entry : serialRoles) state.samples[entry.second].configured = 1;
    if (!probe) {
        mapping.value = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(State), MappingName);
        if (mapping.value) view = MapViewOfFile(mapping.value, FILE_MAP_WRITE, 0, 0, sizeof(State));
        if (!lock.value || !view) throw std::runtime_error("Cannot create tracker IPC");
    }
    const auto publish = [&] {
        if (!view) return;
        const auto result = WaitForSingleObject(lock.value, 100);
        if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) throw std::runtime_error("IPC lock failed");
        std::memcpy(view, &state, sizeof(state));
        ReleaseMutex(lock.value);
    };
    publish();
    SetConsoleCtrlHandler(stop, TRUE);
    std::cout << "Initializing libsurvive (SteamVR must be closed)...\n";
    auto* context = survive_simple_init(int(surviveArgs.size()), surviveArgs.data());
    if (!context) throw std::runtime_error("libsurvive initialization failed");
    struct ContextGuard { SurviveSimpleContext* ctx; ~ContextGuard() { survive_simple_close(ctx); } } contextGuard{context};
    survive_simple_start_thread(context);
    std::cout << (probe ? "Probe only; no poses published.\n" : "Publishing physical poses. Start the OpenXR app after this message.\n");
    const auto started = std::chrono::steady_clock::now();
    while (!InterlockedCompareExchange(&stopped, 0, 0) && survive_simple_is_running(context)) {
        if (duration > 0 && std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count() >= duration) break;
        const auto* object = survive_simple_get_next_updated(context);
        if (!object) { Sleep(1); continue; }
        if (survive_simple_object_get_type(object) != SurviveSimpleObject_OBJECT) continue;
        const char* serial = survive_simple_serial_number(object);
        if (!serial) continue;
        SurvivePose pose;
        const double sampleTime = survive_simple_object_get_latest_pose(object, &pose);
        const double age = survive_simple_run_time_since_epoch(context) - sampleTime;
        if (sampleTime <= 0 || !std::isfinite(age) || age < -0.005 || age > 0.1) continue;
        if (probe) {
            std::cout << serial << " " << pose.Pos[0] << " " << pose.Pos[1] << " " << pose.Pos[2]
                      << " " << pose.Rot[1] << " " << pose.Rot[2] << " " << pose.Rot[3] << " " << pose.Rot[0] << "\n";
            continue;
        }
        const auto found = serialRoles.find(serial);
        if (found == serialRoles.end()) continue;
        auto& sample = state.samples[found->second];
        const auto p = multiply(multiply(rotation, {pose.Pos[0],pose.Pos[1],pose.Pos[2],0}),
                                {-rotation[0],-rotation[1],-rotation[2],rotation[3]});
        const auto q = multiply(rotation, {pose.Rot[1],pose.Rot[2],pose.Rot[3],pose.Rot[0]});
        for (size_t i=0; i<3; ++i) sample.position[i] = float(p[i]+translation[i]);
        for (size_t i=0; i<4; ++i) sample.orientation[i] = float(q[i]);
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        sample.qpc = now.QuadPart - int64_t(std::max(0.0, age) * state.frequency);
        publish();
    }
    for (auto& sample : state.samples) sample.qpc = 0;
    publish();
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    return 1;
}
