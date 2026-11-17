// SPDX-License-Identifier: MIT
// Synthetic IPC integration test only; does not exercise physical hardware.
#include "../../virtualdesktop-openxr/ultimate_tracker_ipc.h"
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int main(int argc, char** argv) try {
    using namespace ultimate_tracker_ipc;
    check(argc == 2, "Expected runtime DLL path");
    HANDLE existing = OpenFileMappingW(FILE_MAP_READ, FALSE, MappingName);
    check(!existing, "Ultimate IPC mapping already exists; stop the provider before the synthetic test");
    LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
    State state{}; state.frequency = frequency.QuadPart;
    state.samples[1].configured = 1; // waist
    state.samples[10].configured = 1; // left ankle
    HANDLE mutex = CreateMutexW(nullptr, FALSE, MutexName);
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(State), MappingName);
    check(mapping && GetLastError() != ERROR_ALREADY_EXISTS, "Create synthetic IPC mapping");
    auto* view = static_cast<State*>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(State)));
    check(view && mutex, "Map synthetic IPC"); *view = state;
    SetEnvironmentVariableA("VDXR_TRACKER_SOURCE", "ultimate");
    HMODULE library = LoadLibraryA(argv[1]); check(library != nullptr, "Load runtime DLL");
    const auto negotiate = reinterpret_cast<PFN_xrNegotiateLoaderRuntimeInterface>(GetProcAddress(library, "xrNegotiateLoaderRuntimeInterface"));
    check(negotiate != nullptr, "Find runtime negotiation entry");
    XrNegotiateLoaderInfo loader{}; loader.structType = XR_LOADER_INTERFACE_STRUCT_LOADER_INFO; loader.structVersion = XR_LOADER_INFO_STRUCT_VERSION; loader.structSize = sizeof(loader);
    loader.minInterfaceVersion = loader.maxInterfaceVersion = 1; loader.minApiVersion = XR_MAKE_VERSION(1,0,0); loader.maxApiVersion = XR_CURRENT_API_VERSION;
    XrNegotiateRuntimeRequest request{}; request.structType = XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST; request.structVersion = XR_RUNTIME_INFO_STRUCT_VERSION; request.structSize = sizeof(request);
    check(negotiate(&loader, &request) == XR_SUCCESS, "Negotiate runtime");
    PFN_xrVoidFunction function{};
    check(request.getInstanceProcAddr(XR_NULL_HANDLE, "xrCreateInstance", &function) == XR_SUCCESS, "Get create instance");
    const auto create = reinterpret_cast<PFN_xrCreateInstance>(function);
    const char* extension = XR_HTCX_VIVE_TRACKER_INTERACTION_EXTENSION_NAME;
    XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO}; std::strcpy(info.applicationInfo.applicationName, "VDXR synthetic tracker test");
    info.applicationInfo.apiVersion = XR_MAKE_VERSION(1,0,0); info.enabledExtensionCount = 1; info.enabledExtensionNames = &extension;
    XrInstance instance{}; check(create(&info, &instance) == XR_SUCCESS, "Create instance without headset");
    check(request.getInstanceProcAddr(instance, "xrEnumerateViveTrackerPathsHTCX", &function) == XR_SUCCESS, "Get tracker enumeration");
    const auto enumerate = reinterpret_cast<PFN_xrEnumerateViveTrackerPathsHTCX>(function);
    uint32_t count = 0; check(enumerate(instance, 0, &count, nullptr) == XR_SUCCESS && count == 2, "Enumerate synthetic roles without body tracking");
    std::vector<XrViveTrackerPathsHTCX> paths(count, {XR_TYPE_VIVE_TRACKER_PATHS_HTCX});
    check(enumerate(instance, count, &count, paths.data()) == XR_SUCCESS, "Read tracker paths");
    check(request.getInstanceProcAddr(instance, "xrPathToString", &function) == XR_SUCCESS, "Get path conversion");
    const auto pathToString = reinterpret_cast<PFN_xrPathToString>(function);
    for (uint32_t i=0; i<count; ++i) {
        char text[XR_MAX_PATH_LENGTH]{}; uint32_t bytes{};
        check(pathToString(instance, paths[i].rolePath, sizeof(text), &bytes, text) == XR_SUCCESS, "Read role path");
        check(std::string(text) == std::string("/user/vive_tracker_htcx/role/") + (i==0 ? "waist" : "left_ankle"), "Unexpected synthetic role");
    }
    check(enumerate(instance, 0, &count, nullptr) == XR_SUCCESS && count == 2, "Roles disappeared as samples go stale");
    check(request.getInstanceProcAddr(instance, "xrDestroyInstance", &function) == XR_SUCCESS, "Get destroy");
    check(reinterpret_cast<PFN_xrDestroyInstance>(function)(instance) == XR_SUCCESS, "Destroy instance");
    FreeLibrary(library); UnmapViewOfFile(view); CloseHandle(mapping); CloseHandle(mutex);
    std::cout << "Passed: built runtime enumerates synthetic waist/ankle roles with no headset or body tracking. Not a hardware test.\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << "\n"; return 1; }
