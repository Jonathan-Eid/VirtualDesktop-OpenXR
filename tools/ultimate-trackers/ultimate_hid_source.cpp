#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>

#include "ultimate_hid_source.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <condition_variable>
#include <cwctype>
#include <cstring>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

namespace ultimate_hid {
namespace {

constexpr USHORT kVendorId = 0x0bb4;
constexpr USHORT kDongleProductId = 0x0350;
constexpr std::size_t kExpectedWindowsReportBytes = 65;
constexpr std::size_t kMaxInputReportBytes = 4096;
constexpr std::size_t kMaxDevices = 5;
constexpr std::size_t kHeaderBytes = 12;
constexpr std::uint8_t kIncomingCommand = 0x28;
constexpr std::uint16_t kPoseType = 0x0110;
constexpr std::uint8_t kFullPoseBits = 0x02;

std::uint16_t ReadU16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[1]) << 8);
}

float ReadHalf(const std::uint8_t* p) {
    const std::uint16_t h = ReadU16(p);
    const std::uint32_t sign = static_cast<std::uint32_t>(h & 0x8000u) << 16;
    std::uint32_t exponent = (h >> 10) & 0x1fu;
    std::uint32_t mantissa = h & 0x03ffu;
    std::uint32_t bits = 0;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {
            int shift = 0;
            while ((mantissa & 0x0400u) == 0) {
                mantissa <<= 1;
                ++shift;
            }
            mantissa &= 0x03ffu;
            const std::uint32_t floatExponent = static_cast<std::uint32_t>(127 - 15 - shift + 1);
            bits = sign | (floatExponent << 23) | (mantissa << 13);
        }
    } else if (exponent == 0x1fu) {
        bits = sign | 0x7f800000u | (mantissa << 13);
    } else {
        exponent += 127 - 15;
        bits = sign | (exponent << 23) | (mantissa << 13);
    }
    float result = 0.0f;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

std::int64_t QpcNow() {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return value.QuadPart;
}

std::int64_t QpcFrequency() {
    LARGE_INTEGER value{};
    QueryPerformanceFrequency(&value);
    return value.QuadPart;
}

struct Device {
    HANDLE handle = INVALID_HANDLE_VALUE;
    HANDLE event = nullptr;
    OVERLAPPED overlapped{};
    std::vector<std::uint8_t> buffer;
    bool pending = false;
    bool active = true;

    ~Device() {
        if (event != nullptr) CloseHandle(event);
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    }
};

void CancelAndDrain(Device& device) {
    if (device.handle == INVALID_HANDLE_VALUE || !device.pending) return;
    CancelIoEx(device.handle, &device.overlapped);
    DWORD ignored = 0;
    GetOverlappedResult(device.handle, &device.overlapped, &ignored, TRUE);
    device.pending = false;
}

bool BeginRead(Device& device) {
    if (!device.active || device.handle == INVALID_HANDLE_VALUE) return false;
    ResetEvent(device.event);
    std::memset(&device.overlapped, 0, sizeof(device.overlapped));
    device.overlapped.hEvent = device.event;
    DWORD received = 0;
    if (ReadFile(device.handle, device.buffer.data(), static_cast<DWORD>(device.buffer.size()),
                 &received, &device.overlapped)) {
        // The expected HID interface uses overlapped completion. Treat an
        // immediate completion as non-pending; the worker handles it inline.
        device.pending = true;
        return true;
    }
    const DWORD error = GetLastError();
    if (error == ERROR_IO_PENDING) {
        device.pending = true;
        return true;
    }
    device.active = false;
    return false;
}

bool OpenDongle(const wchar_t* path, std::unique_ptr<Device>& result) {
    HANDLE handle = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;

    HIDD_ATTRIBUTES attributes{};
    attributes.Size = sizeof(attributes);
    if (!HidD_GetAttributes(handle, &attributes) || attributes.VendorID != kVendorId ||
        attributes.ProductID != kDongleProductId) {
        CloseHandle(handle);
        return false;
    }

    PHIDP_PREPARSED_DATA preparsed = nullptr;
    if (!HidD_GetPreparsedData(handle, &preparsed)) {
        CloseHandle(handle);
        return false;
    }
    HIDP_CAPS caps{};
    const NTSTATUS status = HidP_GetCaps(preparsed, &caps);
    HidD_FreePreparsedData(preparsed);
    if (status != HIDP_STATUS_SUCCESS || caps.InputReportByteLength < kExpectedWindowsReportBytes ||
        caps.InputReportByteLength > kMaxInputReportBytes) {
        CloseHandle(handle);
        return false;
    }

    auto device = std::make_unique<Device>();
    device->handle = handle;
    device->buffer.resize(caps.InputReportByteLength);
    device->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (device->event == nullptr) return false;
    result = std::move(device);
    return true;
}

std::vector<std::unique_ptr<Device>> EnumerateDongles() {
    GUID hidGuid{};
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO info = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    std::vector<std::unique_ptr<Device>> devices;
    if (info == INVALID_HANDLE_VALUE) return devices;

    for (DWORD index = 0; index < 256 && devices.size() < kMaxDevices; ++index) {
        SP_DEVICE_INTERFACE_DATA interfaceData{};
        interfaceData.cbSize = sizeof(interfaceData);
        if (!SetupDiEnumDeviceInterfaces(info, nullptr, &hidGuid, index, &interfaceData)) {
            if (GetLastError() == ERROR_NO_MORE_ITEMS) break;
            continue;
        }
        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(info, &interfaceData, nullptr, 0, &required, nullptr);
        if (required < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || required > 65536) continue;
        std::vector<std::uint8_t> storage(required);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(info, &interfaceData, detail, required, nullptr, nullptr)) continue;
        std::wstring normalizedPath(detail->DevicePath);
        std::transform(normalizedPath.begin(), normalizedPath.end(), normalizedPath.begin(),
            [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
        if (normalizedPath.find(L"vid_0bb4&pid_0350") == std::wstring::npos) continue;
        std::unique_ptr<Device> device;
        if (OpenDongle(detail->DevicePath, device)) devices.push_back(std::move(device));
    }
    SetupDiDestroyDeviceInfoList(info);
    return devices;
}

} // namespace

bool DecodeInputReport(const std::uint8_t* bytes, std::size_t length,
                       std::int64_t receivedQpc, Pose& pose) {
    if (bytes == nullptr || length != kExpectedWindowsReportBytes || bytes[0] != 0) return false;
    const std::uint8_t* report = bytes + 1;
    if (report[0] != kIncomingCommand || ReadU16(report + 9) != kPoseType) return false;
    const std::uint8_t payloadLength = report[11];
    if (payloadLength != 0x25 && payloadLength != 0x27) return false;
    if (kHeaderBytes + payloadLength > length - 1) return false;
    const std::uint8_t* payload = report + 12;
    const std::uint8_t flags = payload[36];
    if ((flags & 0x07u) != kFullPoseBits) return false;
    Pose decoded{};
    decoded.packet_index = ReadU16(report + 1);
    std::copy(report + 3, report + 9, decoded.device_id.begin());
    decoded.flags = flags;
    decoded.received_qpc = receivedQpc;
    std::memcpy(decoded.position, payload + 2, sizeof(decoded.position));
    for (float value : decoded.position) {
        if (!std::isfinite(value) || std::fabs(value) > 10000.0f) return false;
    }

    const float w = ReadHalf(payload + 14);
    const float z = ReadHalf(payload + 16);
    const float y = ReadHalf(payload + 18);
    const float x = ReadHalf(payload + 20);
    const float norm = std::sqrt(x * x + y * y + z * z + w * w);
    if (!std::isfinite(norm) || norm < 0.98f || norm > 1.02f) return false;
    decoded.orientation_xyzw[0] = x / norm;
    decoded.orientation_xyzw[1] = y / norm;
    decoded.orientation_xyzw[2] = z / norm;
    decoded.orientation_xyzw[3] = w / norm;
    pose = decoded;
    return true;
}

struct Source::Impl {
    mutable std::mutex mutex;
    std::condition_variable readyCondition;
    std::map<std::array<std::uint8_t, 6>, Pose> cache;
    std::map<std::array<std::uint8_t, 6>, std::uint16_t> lastPacketByTracker;
    std::string errorText;
    std::thread worker;
    HANDLE stopEvent = nullptr;
    bool started = false;
    bool ready = false;
    bool running = false;

    void SetError(std::string text) {
        std::lock_guard<std::mutex> lock(mutex);
        errorText = std::move(text);
    }

    void Publish(const Pose& pose) {
        std::lock_guard<std::mutex> lock(mutex);
        const auto cached = cache.find(pose.device_id);
        if (cached == cache.end() && cache.size() >= kMaxDevices) return;
        const auto previous = lastPacketByTracker.find(pose.device_id);
        if (previous != lastPacketByTracker.end() && previous->second == pose.packet_index) return;
        lastPacketByTracker[pose.device_id] = pose.packet_index;
        cache[pose.device_id] = pose;
    }

    void Invalidate(const std::array<std::uint8_t, 6>& deviceId) {
        std::lock_guard<std::mutex> lock(mutex);
        cache.erase(deviceId);
    }

    void ClearCache() {
        std::lock_guard<std::mutex> lock(mutex);
        cache.clear();
        lastPacketByTracker.clear();
    }

    void SetErrorIfEmpty(const char* message) {
        std::lock_guard<std::mutex> lock(mutex);
        if (errorText.empty()) errorText = message;
    }

    void Run() {
        auto devices = EnumerateDongles();
        if (devices.empty()) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                ready = true;
                running = false;
                errorText = "no accessible HTC VID 0x0bb4 / PID 0x0350 dongle HID input interface";
            }
            readyCondition.notify_all();
            return;
        }

        for (auto& device : devices) {
            if (!BeginRead(*device)) device->active = false;
        }

        bool failed = std::none_of(devices.begin(), devices.end(), [](const auto& d) { return d->active; });
        {
            std::lock_guard<std::mutex> lock(mutex);
            ready = true;
            running = !failed;
            if (failed) errorText = "could not start passive HID input read";
        }
        readyCondition.notify_all();
        if (failed) return;

        while (WaitForSingleObject(stopEvent, 0) != WAIT_OBJECT_0) {
            std::vector<HANDLE> events;
            std::vector<Device*> mapping;
            events.push_back(stopEvent);
            for (auto& device : devices) {
                if (device->active && device->pending) {
                    events.push_back(device->event);
                    mapping.push_back(device.get());
                }
            }
            if (events.size() == 1) break;
            const DWORD wait = WaitForMultipleObjects(static_cast<DWORD>(events.size()), events.data(), FALSE, 20);
            if (wait == WAIT_OBJECT_0) break;
            if (wait == WAIT_TIMEOUT) continue;
            if (wait < WAIT_OBJECT_0 + 1 || wait >= WAIT_OBJECT_0 + static_cast<DWORD>(events.size())) {
                failed = true;
                break;
            }
            Device& device = *mapping[wait - WAIT_OBJECT_0 - 1];
            DWORD received = 0;
            if (!GetOverlappedResult(device.handle, &device.overlapped, &received, FALSE)) {
                if (GetLastError() == ERROR_IO_INCOMPLETE) continue;
                device.active = false;
                CancelAndDrain(device);
                failed = true;
                SetErrorIfEmpty("dongle HID input read failed; pose cache cleared");
                ClearCache();
                continue;
            }
            device.pending = false;
            if (received > 0 && static_cast<std::size_t>(received) <= device.buffer.size()) {
                Pose pose{};
                const auto* raw = device.buffer.data();
                const bool framedPose = received == kExpectedWindowsReportBytes && raw[0] == 0 &&
                    raw[1] == kIncomingCommand && ReadU16(raw + 10) == kPoseType &&
                    (raw[12] == 0x25 || raw[12] == 0x27);
                if (framedPose) {
                    std::array<std::uint8_t, 6> deviceId{};
                    std::copy(raw + 4, raw + 10, deviceId.begin());
                    const std::uint8_t flags = raw[13 + 36];
                    if ((flags & 0x07u) != kFullPoseBits) {
                        Invalidate(deviceId);
                    } else if (DecodeInputReport(raw, received, QpcNow(), pose)) {
                        Publish(pose);
                    } else {
                        // A malformed transform from a tracker is not a
                        // trustworthy continuation of its prior pose.
                        Invalidate(deviceId);
                    }
                }
            }
            if (device.active && !BeginRead(device)) {
                device.active = false;
                failed = true;
                SetErrorIfEmpty("could not restart dongle HID input read; pose cache cleared");
                ClearCache();
            }
        }

        for (auto& device : devices) CancelAndDrain(*device);
        ClearCache();
        {
            std::lock_guard<std::mutex> lock(mutex);
            running = false;
            if (failed && errorText.empty()) errorText = "dongle HID input read failed; pose cache cleared";
        }
    }
};

Source::Source() : impl_(std::make_unique<Impl>()) {}
Source::~Source() { stop(); }

bool Source::start() {
    std::unique_lock<std::mutex> lock(impl_->mutex);
    if (impl_->started) return impl_->running;
    impl_->errorText.clear();
    impl_->cache.clear();
    impl_->lastPacketByTracker.clear();
    impl_->ready = false;
    impl_->stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (impl_->stopEvent == nullptr) {
        impl_->errorText = "cannot create HID source stop event";
        impl_->ready = true;
        return false;
    }
    impl_->started = true;
    impl_->worker = std::thread([state = impl_.get()] { state->Run(); });
    impl_->readyCondition.wait(lock, [this] { return impl_->ready; });
    return impl_->running;
}

void Source::stop() {
    if (!impl_) return;
    if (impl_->stopEvent != nullptr) SetEvent(impl_->stopEvent);
    if (impl_->worker.joinable()) impl_->worker.join();
    impl_->ClearCache();
    if (impl_->stopEvent != nullptr) {
        CloseHandle(impl_->stopEvent);
        impl_->stopEvent = nullptr;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->running = false;
    impl_->started = false;
}

std::vector<Pose> Source::snapshot() const {
    const std::int64_t now = QpcNow();
    const std::int64_t frequency = QpcFrequency();
    const std::int64_t ageLimit = frequency > 0 ? frequency / 10 : 0;
    std::vector<Pose> result;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (auto it = impl_->cache.begin(); it != impl_->cache.end();) {
        if (ageLimit == 0 || now < it->second.received_qpc ||
            now - it->second.received_qpc > ageLimit) {
            it = impl_->cache.erase(it);
            continue;
        }
        result.push_back(it->second);
        ++it;
    }
    return result;
}

std::string Source::error() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->errorText;
}

} // namespace ultimate_hid
