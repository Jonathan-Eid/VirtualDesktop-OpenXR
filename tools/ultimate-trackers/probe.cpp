#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>

#include "probe_core.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

namespace {

using ultimate_tracker_probe::EscapeJson;
using ultimate_tracker_probe::HexBytes;
using ultimate_tracker_probe::IsValidCaptureDuration;
using ultimate_tracker_probe::IsValidInputReportLength;
using ultimate_tracker_probe::ParseUnsigned;
using ultimate_tracker_probe::RawOutputWithinLimit;
using ultimate_tracker_probe::kMaxInputReportBytes;

constexpr USHORT kHtcVendorId = 0x0bb4;
constexpr USHORT kHtcTrackerProductId = 0x06a3;
constexpr USHORT kHtcWirelessDongleProductId = 0x0350;
// These identifiers are reported by the public pyvut reverse-engineering repo;
// they are used only for matching devices, with no copied parser or wire logic.

volatile LONG g_stopRequested = FALSE;

BOOL WINAPI OnConsoleControl(DWORD controlType) {
    if (controlType == CTRL_C_EVENT || controlType == CTRL_BREAK_EVENT || controlType == CTRL_CLOSE_EVENT) {
        InterlockedExchange(&g_stopRequested, TRUE);
        return TRUE;
    }
    return FALSE;
}

struct Options {
    unsigned captureMs = 0;
    std::wstring rawOutput;
    std::optional<USHORT> vendorId;
    std::optional<USHORT> productId;
    bool allHid = false;
    bool help = false;
};

struct DeviceInfo {
    unsigned index = 0;
    std::wstring path;
    USHORT vendorId = 0;
    USHORT productId = 0;
    USHORT version = 0;
    USHORT usagePage = 0;
    USAGE usage = 0;
    USHORT inputReportBytes = 0;
    USHORT outputReportBytes = 0;
    USHORT featureReportBytes = 0;
    std::wstring manufacturer;
    std::wstring product;
    bool metadataAvailable = false;
    std::string error;
};

struct CaptureDevice {
    DeviceInfo info;
    HANDLE handle = INVALID_HANDLE_VALUE;
    HANDLE event = nullptr;
    OVERLAPPED overlapped{};
    std::vector<std::uint8_t> buffer;
    bool pending = false;
    bool active = true;
};

std::string NarrowUtf8(const std::wstring& input) {
    if (input.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input.data(),
        static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string result(static_cast<std::size_t>(needed), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input.data(),
        static_cast<int>(input.size()), result.data(), needed, nullptr, nullptr) != needed) {
        return {};
    }
    return result;
}

std::wstring ReadHidString(HANDLE handle, BOOLEAN (WINAPI* getter)(HANDLE, PVOID, ULONG)) {
    std::vector<wchar_t> buffer(256, L'\0');
    if (!getter(handle, buffer.data(), static_cast<ULONG>(buffer.size() * sizeof(wchar_t)))) return {};
    buffer.back() = L'\0';
    return std::wstring(buffer.data());
}

std::string HexNumber(unsigned value, unsigned width = 4) {
    constexpr char digits[] = "0123456789abcdef";
    std::string output(width, '0');
    for (unsigned i = 0; i < width; ++i) {
        output[width - i - 1] = digits[value & 0xf];
        value >>= 4;
    }
    return output;
}

void EmitError(const std::string& where, DWORD error) {
    std::cout << "{\"type\":\"error\",\"where\":\"" << EscapeJson(where)
              << "\",\"win32_error\":" << error << "}\n";
}

void EmitDevice(const DeviceInfo& device) {
    std::cout << "{\"type\":\"device\",\"index\":" << device.index
              << ",\"vid\":\"0x" << HexNumber(device.vendorId)
              << "\",\"pid\":\"0x" << HexNumber(device.productId)
              << "\",\"version\":\"0x" << HexNumber(device.version)
              << "\",\"usage_page\":\"0x" << HexNumber(device.usagePage)
              << "\",\"usage\":\"0x" << HexNumber(device.usage)
              << "\",\"input_report_bytes\":" << device.inputReportBytes
              << ",\"output_report_bytes\":" << device.outputReportBytes
              << ",\"feature_report_bytes\":" << device.featureReportBytes
              << ",\"manufacturer\":\"" << EscapeJson(NarrowUtf8(device.manufacturer))
              << "\",\"product\":\"" << EscapeJson(NarrowUtf8(device.product))
              << "\",\"path\":\"" << EscapeJson(NarrowUtf8(device.path)) << "\"";
    if (!device.error.empty()) std::cout << ",\"error\":\"" << EscapeJson(device.error) << "\"";
    std::cout << "}\n";
}

void EmitReportSummary(const CaptureDevice& device, unsigned sequence, ULONGLONG elapsedMs, DWORD length) {
    std::cout << "{\"type\":\"input_report_summary\",\"index\":" << device.info.index
              << ",\"vid\":\"0x" << HexNumber(device.info.vendorId)
              << "\",\"pid\":\"0x" << HexNumber(device.info.productId)
              << "\",\"sequence\":" << sequence
              << ",\"elapsed_ms\":" << elapsedMs
              << ",\"length\":" << length << "}\n";
}

bool WriteRawReport(std::ofstream& rawOutput, const CaptureDevice& device,
                    unsigned sequence, ULONGLONG elapsedMs, DWORD length, std::size_t& totalBytes) {
    std::ostringstream line;
    line << "{\"type\":\"input_report\",\"index\":" << device.info.index
         << ",\"vid\":\"0x" << HexNumber(device.info.vendorId)
         << "\",\"pid\":\"0x" << HexNumber(device.info.productId)
         << "\",\"sequence\":" << sequence
         << ",\"elapsed_ms\":" << elapsedMs
         << ",\"length\":" << length
         << ",\"data_hex\":\"" << HexBytes(device.buffer.data(), length) << "\"}\n";
    const std::string record = line.str();
    if (!RawOutputWithinLimit(totalBytes, record.size())) return false;
    rawOutput.write(record.data(), static_cast<std::streamsize>(record.size()));
    rawOutput.flush();
    if (!rawOutput) return false;
    totalBytes += record.size();
    return true;
}

void PrintUsage() {
    std::cerr << "Usage: ultimate-tracker-probe [--capture-ms 1..30000 --raw-output FILE] [--vid 0x0bb4 --pid 0x06a3|0x0350] [--all-hid]\n"
              << "Default enumeration matches the commonly reported HTC tracker and dongle VID/PIDs.\n"
              << "--all-hid only lists HID metadata and cannot be combined with capture.\n"
              << "Capture reads input reports only; raw bytes go only to FILE. No feature/output reports or configuration.\n"
              << "stdout contains JSON Lines metadata and report counts/lengths; no pose decoding is performed.\n";
}

bool ParseOptions(int argc, wchar_t** argv, Options& options, std::string& error) {
    bool explicitVid = false;
    bool explicitPid = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg(argv[i]);
        if (arg == L"--help" || arg == L"-h") {
            options.help = true;
        } else if (arg == L"--all-hid") {
            options.allHid = true;
        } else if ((arg == L"--capture-ms" || arg == L"--vid" || arg == L"--pid" || arg == L"--raw-output") && i + 1 < argc) {
            if (arg == L"--raw-output") {
                options.rawOutput = argv[++i];
                if (options.rawOutput.empty()) {
                    error = "--raw-output requires a non-empty file path";
                    return false;
                }
                continue;
            }
            const std::string value = NarrowUtf8(argv[++i]);
            unsigned parsed = 0;
            if (arg == L"--capture-ms") {
                if (!ParseUnsigned(value, 0xffffffffu, parsed) || !IsValidCaptureDuration(parsed)) {
                    error = "capture duration must be an integer from 0 through 30000 ms";
                    return false;
                }
                options.captureMs = parsed;
            } else {
                if (!ParseUnsigned(value, 0xffff, parsed)) {
                    error = "VID and PID must be 16-bit decimal or hexadecimal integers";
                    return false;
                }
                if (arg == L"--vid") { options.vendorId = static_cast<USHORT>(parsed); explicitVid = true; }
                else { options.productId = static_cast<USHORT>(parsed); explicitPid = true; }
            }
        } else {
            error = "unknown or incomplete argument: " + NarrowUtf8(arg);
            return false;
        }
    }
    if (explicitVid != explicitPid) {
        error = "--vid and --pid must be supplied together";
        return false;
    }
    if (explicitVid) options.allHid = false;
    if (options.allHid && options.captureMs != 0) {
        error = "--all-hid is enumeration-only; choose a VID/PID pair before capture";
        return false;
    }
    if (options.captureMs != 0 && options.rawOutput.empty()) {
        error = "--capture-ms requires --raw-output FILE so raw reports are kept out of stdout";
        return false;
    }
    if (options.captureMs == 0 && !options.rawOutput.empty()) {
        error = "--raw-output requires a nonzero --capture-ms duration";
        return false;
    }
    return true;
}

bool Matches(const DeviceInfo& info, const Options& options) {
    if (options.vendorId && options.productId) {
        return info.vendorId == *options.vendorId && info.productId == *options.productId;
    }
    if (options.allHid) return true;
    return info.vendorId == kHtcVendorId &&
        (info.productId == kHtcTrackerProductId || info.productId == kHtcWirelessDongleProductId);
}

std::vector<DeviceInfo> EnumerateDevices(const Options& options) {
    GUID hidGuid{};
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO deviceSet = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceSet == INVALID_HANDLE_VALUE) {
        EmitError("SetupDiGetClassDevsW", GetLastError());
        return {};
    }

    std::vector<DeviceInfo> found;
    for (DWORD index = 0;; ++index) {
        SP_DEVICE_INTERFACE_DATA interfaceData{};
        interfaceData.cbSize = sizeof(interfaceData);
        if (!SetupDiEnumDeviceInterfaces(deviceSet, nullptr, &hidGuid, index, &interfaceData)) {
            const DWORD error = GetLastError();
            if (error != ERROR_NO_MORE_ITEMS) EmitError("SetupDiEnumDeviceInterfaces", error);
            break;
        }

        DWORD requiredBytes = 0;
        SetupDiGetDeviceInterfaceDetailW(deviceSet, &interfaceData, nullptr, 0, &requiredBytes, nullptr);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || requiredBytes < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
            EmitError("SetupDiGetDeviceInterfaceDetailW(size)", GetLastError());
            continue;
        }
        std::vector<std::uint8_t> detailStorage(requiredBytes);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailStorage.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(deviceSet, &interfaceData, detail, requiredBytes, nullptr, nullptr)) {
            EmitError("SetupDiGetDeviceInterfaceDetailW", GetLastError());
            continue;
        }

        HANDLE handle = CreateFileW(detail->DevicePath, 0,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        DeviceInfo info{};
        info.path = detail->DevicePath;
        if (handle == INVALID_HANDLE_VALUE) {
            info.error = "metadata open failed, win32=" + std::to_string(GetLastError());
            info.index = index;
            if (options.allHid) found.push_back(std::move(info));
            continue;
        }

        HIDD_ATTRIBUTES attributes{};
        attributes.Size = sizeof(attributes);
        if (!HidD_GetAttributes(handle, &attributes)) {
            const DWORD error = GetLastError();
            CloseHandle(handle);
            EmitError("HidD_GetAttributes", error);
            continue;
        }
        info.vendorId = attributes.VendorID;
        info.productId = attributes.ProductID;
        info.version = attributes.VersionNumber;
        info.index = index;
        info.manufacturer = ReadHidString(handle, HidD_GetManufacturerString);
        info.product = ReadHidString(handle, HidD_GetProductString);

        PHIDP_PREPARSED_DATA preparsed = nullptr;
        if (HidD_GetPreparsedData(handle, &preparsed)) {
            HIDP_CAPS caps{};
            if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS) {
                info.usagePage = caps.UsagePage;
                info.usage = caps.Usage;
                info.inputReportBytes = caps.InputReportByteLength;
                info.outputReportBytes = caps.OutputReportByteLength;
                info.featureReportBytes = caps.FeatureReportByteLength;
                info.metadataAvailable = true;
            } else {
                info.error = "HidP_GetCaps failed";
            }
            HidD_FreePreparsedData(preparsed);
        } else {
            info.error = "HidD_GetPreparsedData failed, win32=" + std::to_string(GetLastError());
        }
        CloseHandle(handle);

        if (Matches(info, options)) found.push_back(std::move(info));
    }
    SetupDiDestroyDeviceInfoList(deviceSet);
    return found;
}

bool StartRead(CaptureDevice& device) {
    ResetEvent(device.event);
    device.overlapped = {};
    device.overlapped.hEvent = device.event;
    DWORD bytesRead = 0;
    const BOOL result = ReadFile(device.handle, device.buffer.data(),
        static_cast<DWORD>(device.buffer.size()), &bytesRead, &device.overlapped);
    if (!result) {
        const DWORD error = GetLastError();
        if (error == ERROR_IO_PENDING) {
            device.pending = true;
            return true;
        }
        device.pending = false;
        device.info.error = "ReadFile failed, win32=" + std::to_string(error);
        device.active = false;
        return false;
    }
    device.pending = true;
    SetEvent(device.event);
    return true;
}

void CloseCaptureDevice(CaptureDevice& device) {
    if (device.handle != INVALID_HANDLE_VALUE) {
        if (device.pending) {
            CancelIoEx(device.handle, &device.overlapped);
            DWORD ignored = 0;
            // Keep both OVERLAPPED and its buffer alive until cancellation completes.
            GetOverlappedResult(device.handle, &device.overlapped, &ignored, TRUE);
            device.pending = false;
        }
        CloseHandle(device.handle);
        device.handle = INVALID_HANDLE_VALUE;
    }
    if (device.event) {
        CloseHandle(device.event);
        device.event = nullptr;
    }
}

void Capture(const std::vector<DeviceInfo>& devices, unsigned durationMs, std::ofstream& rawOutput) {
    std::vector<std::unique_ptr<CaptureDevice>> captureDevices;
    for (const auto& info : devices) {
        if (!info.metadataAvailable || !IsValidInputReportLength(info.inputReportBytes)) {
            std::cout << "{\"type\":\"capture_skipped\",\"index\":" << info.index
                      << ",\"reason\":\"missing or unsafe HID input report length\",\"input_report_bytes\":"
                      << info.inputReportBytes << "}\n";
            continue;
        }
        auto device = std::make_unique<CaptureDevice>();
        device->info = info;
        device->handle = CreateFileW(info.path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
        if (device->handle == INVALID_HANDLE_VALUE) {
            std::cout << "{\"type\":\"capture_open_failed\",\"index\":" << info.index
                      << ",\"win32_error\":" << GetLastError() << "}\n";
            continue;
        }
        device->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!device->event) {
            std::cout << "{\"type\":\"capture_event_failed\",\"index\":" << info.index
                      << ",\"win32_error\":" << GetLastError() << "}\n";
            CloseCaptureDevice(*device);
            continue;
        }
        device->buffer.resize(info.inputReportBytes);
        captureDevices.push_back(std::move(device));
    }

    if (captureDevices.empty()) {
        std::cout << "{\"type\":\"capture_end\",\"reports\":0,\"reason\":\"no readable selected HID interfaces\"}\n";
        return;
    }
    if (captureDevices.size() > MAXIMUM_WAIT_OBJECTS) {
        for (auto& device : captureDevices) CloseCaptureDevice(*device);
        std::cout << "{\"type\":\"capture_end\",\"reports\":0,\"reason\":\"too many selected HID interfaces\"}\n";
        return;
    }

    std::vector<HANDLE> events;
    events.reserve(captureDevices.size());
    for (auto& device : captureDevices) {
        events.push_back(device->event);
        if (!StartRead(*device)) {
            std::cout << "{\"type\":\"capture_read_failed\",\"index\":" << device->info.index
                      << ",\"error\":\"" << EscapeJson(device->info.error) << "\"}\n";
        }
    }
    std::vector<unsigned> sequence(captureDevices.size(), 0);
    const ULONGLONG started = GetTickCount64();
    const ULONGLONG deadline = started + durationMs;
    std::uint64_t reportCount = 0;
    std::size_t rawOutputBytes = 0;

    std::cout << "{\"type\":\"capture_start\",\"duration_ms\":" << durationMs
              << ",\"selected_interfaces\":" << captureDevices.size() << "}\n";
    while (GetTickCount64() < deadline && InterlockedCompareExchange(&g_stopRequested, FALSE, FALSE) == FALSE) {
        if (std::none_of(captureDevices.begin(), captureDevices.end(),
            [](const auto& device) { return device->active; })) break;
        const ULONGLONG now = GetTickCount64();
        const DWORD remaining = static_cast<DWORD>(std::min<ULONGLONG>(deadline - now, 100));
        const DWORD wait = WaitForMultipleObjects(static_cast<DWORD>(events.size()), events.data(), FALSE, remaining);
        if (wait == WAIT_TIMEOUT) continue;
        if (wait == WAIT_FAILED) {
            EmitError("WaitForMultipleObjects", GetLastError());
            break;
        }
        if (static_cast<std::size_t>(wait - WAIT_OBJECT_0) >= events.size()) continue;

        const std::size_t index = wait - WAIT_OBJECT_0;
        auto& device = *captureDevices[index];
        DWORD bytesRead = 0;
        if (!GetOverlappedResult(device.handle, &device.overlapped, &bytesRead, FALSE)) {
            const DWORD error = GetLastError();
            device.pending = false;
            std::cout << "{\"type\":\"capture_read_failed\",\"index\":" << device.info.index
                      << ",\"win32_error\":" << error << "}\n";
            device.active = false;
            ResetEvent(device.event);
            continue;
        }
        device.pending = false;
        if (bytesRead > device.buffer.size()) {
            std::cout << "{\"type\":\"capture_read_rejected\",\"index\":" << device.info.index
                      << ",\"length\":" << bytesRead << "}\n";
            device.active = false;
            ResetEvent(device.event);
            continue;
        }
        ++sequence[index];
        ++reportCount;
        const auto elapsedMs = GetTickCount64() - started;
        EmitReportSummary(device, sequence[index], elapsedMs, bytesRead);
        if (!WriteRawReport(rawOutput, device, sequence[index], elapsedMs, bytesRead, rawOutputBytes)) {
            std::cout << "{\"type\":\"capture_end\",\"reason\":\"raw output file failed or reached 8 MiB safety limit\"}\n";
            break;
        }
        device.buffer.resize(device.info.inputReportBytes);
        if (!StartRead(device)) {
            std::cout << "{\"type\":\"capture_read_failed\",\"index\":" << device.info.index
                      << ",\"error\":\"" << EscapeJson(device.info.error) << "\"}\n";
            device.active = false;
            ResetEvent(device.event);
        }
    }

    for (auto& device : captureDevices) CloseCaptureDevice(*device);
    std::cout << "{\"type\":\"capture_end\",\"duration_ms\":" << (GetTickCount64() - started)
              << ",\"reports\":" << reportCount
              << ",\"stopped_by_user\":" << (InterlockedCompareExchange(&g_stopRequested, FALSE, FALSE) ? "true" : "false")
              << "}\n";
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    Options options;
    std::string error;
    if (!ParseOptions(argc, argv, options, error)) {
        std::cerr << "Error: " << error << "\n";
        PrintUsage();
        return 2;
    }
    if (options.help) {
        PrintUsage();
        return 0;
    }
    SetConsoleCtrlHandler(OnConsoleControl, TRUE);

    const auto devices = EnumerateDevices(options);
    std::cout << "{\"type\":\"enumeration_end\",\"matches\":" << devices.size() << "}\n";
    for (const auto& device : devices) EmitDevice(device);

    if (devices.empty()) {
        std::cerr << "No matching HID interfaces found. Check USB connection and try --all-hid to inspect other devices.\n";
        return 1;
    }
    if (options.captureMs != 0) {
        const std::filesystem::path rawPath(options.rawOutput);
        std::error_code filesystemError;
        if (std::filesystem::exists(rawPath, filesystemError)) {
            std::cerr << "Raw-output file already exists; choose a new path to preserve existing data.\n";
            return 2;
        }
        if (filesystemError) {
            std::cerr << "Cannot inspect raw-output path: " << filesystemError.message() << "\n";
            return 2;
        }
        std::ofstream rawOutput(rawPath, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!rawOutput) {
            std::cerr << "Cannot create raw-output file.\n";
            return 2;
        }
        Capture(devices, options.captureMs, rawOutput);
    }
    return 0;
}
