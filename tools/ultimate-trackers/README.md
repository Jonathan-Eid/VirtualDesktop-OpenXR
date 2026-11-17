# Ultimate Tracker input for Nightfall / VDXR

Work in progress on `experimental/steamvr-free-ultimate-trackers`, based on the
Nightfall-enabled VDXR main branch. This is a separate source adapter from the
Vive Tracker 3.0 / Lighthouse / libsurvive prototype.

## First milestone

Identify HTC Ultimate Tracker USB HID interfaces on Windows and collect bounded,
passive input reports. Device detection, incoming reports, decoded poses,
calibrated poses and usable in-game tracking are separate acceptance steps.
No hardware success is implied by a successful build or synthetic tests.

Build the probe with Visual Studio C++ tools and CMake on Windows:

```powershell
cmake -S tools/ultimate-trackers -B build/ultimate-tracker-probe -A x64
cmake --build build/ultimate-tracker-probe --config Release
ctest --test-dir build/ultimate-tracker-probe -C Release --output-on-failure
```

Enumerate the supported HTC HID interfaces without reading reports:

```powershell
./build/ultimate-tracker-probe/Release/ultimate-tracker-probe.exe
```

Read matching dongle interfaces for 12 seconds, recording raw reports only in a
new local file (the output directory must exist):

```powershell
./build/ultimate-tracker-probe/Release/ultimate-tracker-probe.exe --vid 0x0bb4 --pid 0x0350 --capture-ms 12000 --raw-output build/ultimate-raw.jsonl
```

For a directly cabled Ultimate Tracker use product ID `0x06a3` instead. The tool
refuses to overwrite a capture file. Standard output contains metadata, counts
and report lengths; it does not contain raw report bytes. Captures are capped
at 30 seconds and 8 MiB. This stage intentionally does not decode physical poses.

## Offline runtime checks

The opt-in source selector is `VDXR_TRACKER_SOURCE=ultimate`. Its versioned
local channel is `Local\\VDXR.Ultimate.v1`; the producer must supply measured,
calibrated Quest STAGE poses before an application starts. Configured roles
are latched for that runtime instance. The passive USB probe is not that producer.

```powershell
cmake -S tools/ultimate-runtime-tests -B build/ultimate-runtime-tests -A x64 -DVDXR_RUNTIME_DLL=C:/path/to/virtualdesktop-openxr.dll
cmake --build build/ultimate-runtime-tests --config Release
ctest --test-dir build/ultimate-runtime-tests -C Release --output-on-failure
```

The IPC test checks protocol/clock metadata, malformed and stale samples,
nonblocking contention and abandoned-mutex recovery. The runtime test loads the
actual DLL and enumerates synthetic waist/ankle role paths without a headset.
It does not exercise physical tracking or in-game pose location. On November
17, 2026, the full modified runtime built in Release/x64, both offline runtime
tests passed, and the probe's fixture test passed. Those tests alone do not
establish hardware behavior.

Subsequent physical checks detected the Ultimate dongle and decoded stationary
and moving pose packets on Windows with SteamVR absent and HTC's tracker server
running. The native passive source also passed a bounded live smoke test. See
[VALIDATION.md](VALIDATION.md) for the scope and remaining acceptance work.

## Native passive source

`ultimate_hid_source.h` exposes `ultimate_hid::Source`: `start`, `snapshot`,
`error` and `stop`. CMake builds the `ultimate-hid-source` static library and a
bounded five-second `ultimate-source-probe` diagnostic. The source reads only
Ultimate dongle HID interfaces and sends no feature/output reports. HTC's
existing Windows tracker server currently handles initialization and room-map
management; this code does not replace it.

The source returns raw-space position and normalized XYZW quaternions, opaque
six-byte radio identifiers, packet counters, raw status flags and report-arrival
QPC timestamps. It deduplicates by tracker identifier, discards unsupported/lost
states, clears loss caches and expires missing updates after 100 ms. Report
receipt time is not a verified sensor timestamp. No hardware tracking-confidence,
physical-axis or latency claim follows from the decoder.

The separate calibration app can link this library with
`VDXR_ULTIMATE_SOURCE_DIR` pointing to this tools directory. Only a measured and
validated calibration may transform these raw samples into the runtime's Quest
STAGE channel.

An offline analyzer is available for locally recorded files:

```powershell
python tools/ultimate-trackers/analyze_capture.py build/ultimate-raw.jsonl --max-poses 0
python tools/ultimate-trackers/test_analyze_capture.py -v
```

It suppresses ACK payload contents and keeps raw status/auxiliary fields separate
from any verified tracking interpretation. Do not commit capture files or maps.

The initial probe does not pair devices, rebuild maps, write firmware, change
Wi-Fi configuration, install drivers or change the active OpenXR runtime.
It does not publish uncalibrated hardware data as Quest-space poses.

## Mapping and calibration

For the first Windows implementation, use VIVE Hub to pair the Ultimate Tracker
with its wireless dongle and create or retain its room tracking map. Keep a map
that already works in the current room. Mapping is not needed merely to enumerate
the USB device.

The room map lets the tracker locate itself. A second alignment places that map
in the Quest's current STAGE coordinate space. Our VDXR Space Calibrator's
regular calibration interface and solver are candidates for this second step,
but its current acquisition code still uses libsurvive and must be adapted.
An old SteamVR space-calibration transform is not automatically a valid transform
for Nightfall's current Quest space. Quest recentering or boundary changes must
invalidate or revalidate alignment.

Save mapping work and close VIVE Hub and SteamVR before a direct-access probe.
Another process may own the USB interface; access failure is not proof of an
unsupported or broken tracker. Avoid running competing providers simultaneously.

Official mapping procedure:
https://www.vive.com/us/support/ultimate-tracker/category_howto/creating-a-tracking-map.html

## Planned integration boundary

`Ultimate hardware -> Windows source provider -> calibrated metric poses -> VDXR tracker input -> OpenXR app`

Reuse the tracker branch's OpenXR role bindings, bounded shared-memory reader,
stale-pose checks and STAGE-to-application-space conversion. Replace the
libsurvive-specific acquisition and source-coordinate conversion. Keep the
Nightfall head/hand transport, layer compositor, frame pairing, audio and 90 Hz
changes from main.

An Ultimate sample must retain its actual source time and tracking state.
Callback arrival time alone must not hide queued sample age. Rotation-only,
mapping, lost and stale states must not become fresh, fully tracked 6DoF poses.
Current synthetic runtime tests cannot establish real SDK confidence, hardware
coordinate axes, latency, accuracy or pose prediction.

## Source research

HTC's public SDK is a Linux beta with prebuilt x64/ARM64 tracking libraries:
https://github.com/ViveRoboticsSoftware/vive-ultimate-tracker-sdk

The root package license points to HTC's EULA even though some public headers
carry Apache-2.0 identifiers. The published Linux library is not a native Windows
DLL and is not linked into this Windows probe.

For USB protocol research, the public community repository documents HTC vendor
ID `0x0bb4`, direct Ultimate Tracker product ID `0x06a3`, and Ultimate dongle
product ID `0x0350`:
https://github.com/nijkah/pyvut/tree/fcfcd33f4c1f16b0d84f5f741dc1319abdc7942a

This reference contains state-changing experiments and is not run by our probe.
The dependency-free Windows probe is independently implemented using Windows HID
APIs. No community tracker implementation is vendored here.

The separate calibration app is:
https://github.com/Jonathan-Eid/VDXR-Space-Calibrator

## Hardware acceptance after transport works

1. Detect the correct dongle or directly connected Ultimate Tracker.
2. Receive bounded, well-formed reports while the tracker is powered on.
3. Decode and validate moving position/orientation, source timestamps and loss states.
4. Align source space to the active Quest STAGE space using measured paired poses.
5. Verify independent movements, occlusion/disconnect/recenter behavior and correct
   role poses in a real OpenXR app with SteamVR closed.

Do not mark the feature complete before these physical checks pass.
