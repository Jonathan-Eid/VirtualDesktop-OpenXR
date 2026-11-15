# Validation recorded November 15, 2026

Windows x64, Visual Studio 2022 MSVC 19.44, Windows SDK 10.0.26100.

- Modified standalone VDXR Release DLL: builds successfully.
- Pinned libsurvive native library, Lighthouse plugins, CLI and provider: build successfully.
- CTest shared-memory test: passes, including stale/future/malformed poses, protocol/clock mismatch, mutex contention and abandoned writes.
- CTest integration test: the actual VDXR DLL negotiates with the test harness, creates an instance and enumerates synthetic waist/left-ankle paths without an HMD or SteamVR. The synthetic values are explicitly test data.
- Python calibration tests: 3 pass, covering a known rigid transform/quaternion, a 180-degree rotation and degenerate/nonfinite inputs.
- Physical USB probe: Watchman receivers and Lighthouse 2.0 signals detected. A 90-second self-calibration probe produced 1,166 pose records from one physical tracker with SteamVR processes absent. A subsequent probe also produced poses using an existing calibration-file import. Both probes exited.

This establishes pose-source feasibility on the tested PC. It does **not** establish positional accuracy, movement response, tracking latency, occlusion recovery, correct mounting orientation, Quest space alignment, or usable in-game full-body tracking. No runtime was installed globally or substituted into the Virtual Desktop installation.

Next: connect Quest via Virtual Desktop; measure matched tracker/controller reference points; validate the resulting room transform against independent points; verify physical role poses in an OpenXR application; add optical-confidence handling before reporting TRACKED flags.
