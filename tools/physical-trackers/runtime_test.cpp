// SPDX-License-Identifier: MIT
// Synthetic IPC integration test; this is not a physical tracking test.
#include "../../virtualdesktop-openxr/external_trackers.h"
#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int main(int argc, char** argv) try {
    using namespace external_trackers;
    check(argc == 2, "Expected runtime DLL path");
    State state;
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    state.frequency = frequency.QuadPart;
    state.samples[1].configured = 1; // waist
    state.samples[10].configured = 1; // left ankle, independent of Meta body support
    HANDLE mutex = CreateMutexW(nullptr, FALSE, MutexName);
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(State), MappingName);
    check(mapping && GetLastError() != ERROR_ALREADY_EXISTS, "Stop real provider before synthetic test");
    auto* view = static_cast<State*>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(State)));
    check(view && mutex, "create IPC");
    *view = state;
    SetEnvironmentVariableA("VDXR_TRACKER_SOURCE", "libsurvive");
    HMODULE library = LoadLibraryA(argv[1]);
    check(library != nullptr, "Load runtime DLL");
    const auto negotiate = reinterpret_cast<PFN_xrNegotiateLoaderRuntimeInterface>(GetProcAddress(library, "xrNegotiateLoaderRuntimeInterface"));
    check(negotiate != nullptr, "Find runtime negotiation entry");
    XrNegotiateLoaderInfo loader{};
    loader.structType = XR_LOADER_INTERFACE_STRUCT_LOADER_INFO;
    loader.structVersion = XR_LOADER_INFO_STRUCT_VERSION;
    loader.structSize = sizeof(loader);
    loader.minInterfaceVersion = loader.maxInterfaceVersion = 1;
    loader.minApiVersion = XR_MAKE_VERSION(1,0,0);
    loader.maxApiVersion = XR_CURRENT_API_VERSION;
    XrNegotiateRuntimeRequest request{};
    request.structType = XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST;
    request.structVersion = XR_RUNTIME_INFO_STRUCT_VERSION;
    request.structSize = sizeof(request);
    check(negotiate(&loader, &request) == XR_SUCCESS, "Negotiate runtime");
    PFN_xrVoidFunction function;
    check(request.getInstanceProcAddr(XR_NULL_HANDLE, "xrCreateInstance", &function) == XR_SUCCESS, "Get create instance");
    const auto create = reinterpret_cast<PFN_xrCreateInstance>(function);
    const char* extension = XR_HTCX_VIVE_TRACKER_INTERACTION_EXTENSION_NAME;
    XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
    std::strcpy(info.applicationInfo.applicationName, "VDXR synthetic tracker test");
    info.applicationInfo.apiVersion = XR_MAKE_VERSION(1,0,0);
    info.enabledExtensionCount = 1;
    info.enabledExtensionNames = &extension;
    XrInstance instance;
    check(create(&info, &instance) == XR_SUCCESS, "Create instance without HMD");
    check(request.getInstanceProcAddr(instance, "xrEnumerateViveTrackerPathsHTCX", &function) == XR_SUCCESS, "Get tracker enumeration");
    const auto enumerate = reinterpret_cast<PFN_xrEnumerateViveTrackerPathsHTCX>(function);
    uint32_t count = 0;
    check(enumerate(instance, 0, &count, nullptr) == XR_SUCCESS && count == 2, "Enumerate physical roles without Meta body tracking");
    std::vector<XrViveTrackerPathsHTCX> paths(count, {XR_TYPE_VIVE_TRACKER_PATHS_HTCX});
    check(enumerate(instance, count, &count, paths.data()) == XR_SUCCESS, "Read tracker paths");
    check(request.getInstanceProcAddr(instance, "xrPathToString", &function) == XR_SUCCESS, "Get path conversion");
    const auto pathToString = reinterpret_cast<PFN_xrPathToString>(function);
    for (uint32_t i=0; i<count; ++i) {
        char text[XR_MAX_PATH_LENGTH]; uint32_t bytes;
        check(pathToString(instance, paths[i].rolePath, sizeof(text), &bytes, text) == XR_SUCCESS, "Read role path");
        check(std::string(text) == std::string("/user/vive_tracker_htcx/role/") + (i==0 ? "waist" : "left_ankle"), "Unexpected role");
    }
    // Roles stay enumerated when pose updates stop.
    check(enumerate(instance, 0, &count, nullptr) == XR_SUCCESS && count == 2, "Roles lost on stale poses");
    check(request.getInstanceProcAddr(instance, "xrDestroyInstance", &function) == XR_SUCCESS, "Get destroy");
    check(reinterpret_cast<PFN_xrDestroyInstance>(function)(instance) == XR_SUCCESS, "Destroy instance");
    FreeLibrary(library);
    UnmapViewOfFile(view); CloseHandle(mapping); CloseHandle(mutex);
    std::cout << "Passed: real VDXR DLL loads and enumerates synthetic waist/ankle roles without SteamVR or headset\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << "\n"; return 1; }
