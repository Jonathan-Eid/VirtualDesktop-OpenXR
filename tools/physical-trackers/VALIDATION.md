# Validation recorded November 15, 2026

Windows x64, Visual Studio 2022 MSVC 19.44, Windows SDK 10.0.26100.

- Modified standalone VDXR Release DLL: builds successfully.
- Pinned libsurvive native library, Lighthouse plugins, CLI and provider: build successfully.
- CTest shared-memory test: passes, including stale/future/malformed poses, protocol/clock mismatch, mutex contention and abandoned writes.
- CTest integration test: the actual VDXR DLL negotiates with the test harness, creates an instance and enumerates synthetic waist/left-ankle paths without an HMD or SteamVR. The synthetic values are explicitly test data.
- Python calibration tests: 5 pass, covering a known rigid transform/quaternion, a 180-degree rotation, degenerate/nonfinite inputs, and simultaneous recovery of known room/mounting transforms.
- Physical USB probe: Watchman receivers and Lighthouse 2.0 signals detected. A 90-second self-calibration probe produced 1,166 pose records from one physical tracker with SteamVR processes absent. A subsequent probe also produced poses using an existing calibration-file import. Both probes exited.

This establishes pose-source feasibility on the tested PC. It does **not** establish positional accuracy, movement response, tracking latency, occlusion recovery, correct mounting orientation, Quest space alignment, or usable in-game full-body tracking. No runtime was installed globally or substituted into the Virtual Desktop installation.

The Quest was connected through Virtual Desktop and the ADB keep-awake command was accepted. A diagnostic frame-loop bug initially produced identity poses; adding wait/begin/end frames produced changing HMD poses. Headless and empty-layer sessions reported zero controller bindings with both stock and modified VDXR. Adding actual black stereo projections enabled changing left-controller poses with all four VALID/TRACKED flags set. The first failed paired run collected zero usable samples. The issue was in the diagnostic setup, not evidence that the headset stream or native controller was broken.

A subsequent paired run collected 1,737 samples with SteamVR absent. The full rigid-fit calibration was rejected. Two short segments fit separately, but cross-validation between them showed roughly 36 degrees of orientation disagreement. Their fitted mounting rotations differed by about 37 degrees, while fitted room rotations differed by about 2 degrees. This suggests a changed tracker/controller relationship, but does not exclude tracking error. No room transform was accepted or applied. The black diagnostic view persisted in Virtual Desktop after the first capture; restarting the Quest app restored the streamed desktop. The recorder now requests and ends its session before destroying it.

Next: collect matched tracker/controller poses; validate the room transform against independent movement; verify physical role poses in an OpenXR application; add optical-confidence handling before reporting TRACKED flags.
