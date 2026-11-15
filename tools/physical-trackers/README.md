# Experimental Vive Tracker 3.0 support without SteamVR

This branch adds an opt-in physical tracker source to VDXR. The intended path is:

`Tracker 3.0 + USB receiver -> libsurvive -> local shared memory -> VDXR -> OpenXR application`

Virtual Desktop continues handling the Quest and video streaming. No OpenVR pose bridge is used. No SteamVR process is needed by the provider. This is an engineering prototype, **not validated full-body tracking**.

## Build

Requirements: Git, CMake, Visual Studio 2022 C++ desktop tools and Windows SDK.
From PowerShell, with the provider stopped:

```powershell
./tools/physical-trackers/Build.ps1 -BuildRoot C:/work/vdxr-tracker-build
```

The build pins libsurvive to `44b1e2f0cb95e5697bc0b86f58fd8520a813b713` and uses its default double-precision ABI. Do not substitute an arbitrary old PyPI DLL or a single-precision build. The provider executable must stay next to `libsurvive.dll` and its `plugins` folder. The build does not install the runtime or change the system's active OpenXR runtime.

## First hardware test

1. Close SteamVR. Turn on a previously paired Tracker 3.0 and its base stations; plug in its receiver. Leave the tracker stationary, with sensors visible to multiple stations, during initial calibration.
2. Run the provider from its output directory:

   ```powershell
   ./vdxr-survive.exe --probe --duration 60 -- --configfile C:/work/vdxr-tracker-build/lighthouse.json
   ```

   Output records contain `hardware-serial x y z qx qy qz qw` in **raw libsurvive coordinates**, not Quest coordinates. A receiver being detected or Lighthouse OOTX messages appearing is not proof of valid 6DoF tracking. Require actual changing pose records and stable stationary results.
3. If calibration fails, libsurvive can read a pre-existing SteamVR `lighthousedb.json` by appending `--steamvr-calibration "C:/.../Steam/config/lighthouse/lighthousedb.json"` after `--`. This imports stored calibration only and does not launch SteamVR. Independent self-calibration is the default. See [Monado's calibration notes](https://monado.freedesktop.org/libsurvive.html).

## Align the two tracking spaces

The provider deliberately refuses to publish without an explicit room transform.
Collect matching positions of the same physical point in raw libsurvive space and the **current Quest STAGE space**, in metres. Use at least three widely spaced, non-collinear points; more points and independent validation points are better. Account for the offset between the tracker origin and any controller origin used as a reference. Use stationary, simultaneous measurements. This prototype does not yet include an in-headset sample collector.

Create a CSV headed `sx,sy,sz,tx,ty,tz`, where `s` is raw libsurvive and `t` is Quest STAGE. With Python and numpy installed:

```powershell
python ./tools/physical-trackers/align_points.py samples.csv stage.txt
python -m unittest discover -s ./tools/physical-trackers -p test_alignment.py
```

The tool fits a rigid transform, including the axis conversion, and writes `tx ty tz qx qy qz qw`. It rejects collinear input and a maximum sample residual above 3 cm by default. Its reported residuals describe sample agreement, **not independently verified tracking accuracy**. Check additional points and rotations. Recalibrate after Quest recentering, boundary changes, or base-station movement. Identity is not a valid shortcut unless measured and confirmed.

## Publish and launch a test app

```powershell
./vdxr-survive.exe --map waist=YOUR-HARDWARE-SERIAL --transform stage.txt -- --configfile lighthouse.json
```

Repeat `--map` for other serials. Roles match the existing VDXR role names in `trackers.h`, including `left_ankle`, `right_ankle`, `left_foot` and `right_foot`. Start the provider **before** the OpenXR process: configured roles are latched when VDXR loads and remain enumerated during signal loss. Restart the application after changing mappings or restarting the provider with different roles.

Connect the Quest through Virtual Desktop, with Streamer running. Launch a compatible OpenXR app that enables `XR_HTCX_vive_tracker_interaction` and binds the appropriate tracker pose actions:

```powershell
./tools/physical-trackers/Run-OpenXR.ps1 -Application C:/path/to/OpenXRApp.exe
```

The launcher selects the experimental runtime and source only in that process environment and restores the calling shell afterwards. It does not overwrite Virtual Desktop's bundled runtime or change OpenXR registry settings. Launching through an existing Steam process may not inherit these variables; direct launch of a test application is preferred.

Physical mode is independent of Quest body-tracking support and the Streamer's body-joint emulation toggle. It never silently substitutes estimated body joints for missing physical poses. Omitting `VDXR_TRACKER_SOURCE=libsurvive` keeps upstream body-joint behavior.

## Current limits and validation

- Windows x64 build and synthetic integration tests are supported. The real VDXR DLL can enumerate synthetic waist/ankle roles without a connected HMD or SteamVR; that is not an end-to-end hardware test.
- Shared-memory tests cover clock/version validation, finite normalized poses, 100 ms expiry, contention, abandoned writes and shutdown. The reader does not wait for the provider's mutex on the rendering thread.
- Actual Tracker 3.0 pose accuracy, axis/grip orientation, pairing, recovery and Quest alignment need hardware validation. Libsurvive pose updates do not provide an optical-confidence guarantee through the Simple API; the timeout detects missing updates, not every tracking failure.
- Physical poses currently report VALID bits, not TRACKED bits, because the reference space's tracking flags are not evidence of optical tracking. Apps requiring TRACKED bits need a provider confidence implementation first.
- No pose history, display-time prediction, reported tracker velocity, buttons, haptics, connection events, or automatic recenter recovery yet. Applications requiring those features may not work.
- The existing VDXR role-based persistent paths are retained; they identify configured roles rather than hardware serials. Role reassignment requires an application restart.
- A fresh libsurvive world calibration may change during a session. Freeze a validated calibration before calibrating to Quest; libsurvive documents `SURVIVE_GLOBALSCENESOLVER=0`. Moved stations require a new calibration.

Do not claim success from a build or receiver enumeration alone. Acceptance requires stable physical poses with SteamVR absent, correctly aligned waist/feet in a real OpenXR app, sensible behavior on occlusion/disconnect/recenter, and measured latency/jitter during movement.
