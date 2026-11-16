# Experimental Nightfall backend

The standalone x64 runtime can use Nightfall's Intel Arc streaming bridge via
the local OVRNull backend. This is an experimental transport integration, not
official OpenXR conformance or a change to the Virtual Desktop Streamer.

Build the `virtualdesktop-openxr` and `OVRNull` projects in Release/x64 after
initializing submodules and restoring NuGet packages. `LibOVRRT64_1.dll` must be
beside `virtualdesktop-openxr.dll` and its JSON manifest. The Nightfall bridge
and its QSV/tracking dependencies are built in the matching Nightfall branch:
[setup, architecture and test record](https://github.com/Jonathan-Eid/nightfall/blob/feature/intel-arc-vdxr/docs/plans/active/intel-arc-vdxr.md).

Meteor sets `XR_RUNTIME_JSON` to this runtime's manifest and
`NIGHTFALL_OVR_BRIDGE` to an absolute `meteor-ovr-bridge.dll` path for a configured
application. Without that variable the existing backend-selection behavior
remains. It does not require registering this fork globally.

The versioned local ABI is `OVRNull/nightfall_abi.h` (also included with the
Nightfall bridge). The plugin provides dimensions, Intel adapter LUID, refresh
target, headset optics, floor-relative tracking, Touch inputs/haptics, GPU frame
encoding and Meteor transport. OVRNull composes projection and quad layers into
a stereo sRGB D3D11 texture. Alpha is handled per draw to avoid repeatedly
modifying a shared layer image. Unsupported OVR layer kinds are refused on the
streaming path. Eye visibility is preserved, and decoded pixel checks cover
quad placement and alpha brightness.

The implementation distinguishes stale tracking from a disconnected transport,
waits for a fresh first vsync, converts eye/floor origins, and tags video with
the submitted display time and projection poses. The device-pose API writes
each requested output rather than repeatedly overwriting its first element.
Submission to this backend is synchronous; Meteor's bounded packet transport
remains asynchronous. A frame-time property preserves the submitted timestamp
even for quad-only or empty frames. Native OpenXR grip/aim samples bypass the
vendor raw Touch-origin offsets.

Validation on Arc 140V/Windows 11: the Khronos loader and Vulkan application
probe delivered 24 2560×1344 frames through the QSV/Meteor path for H.264 and
HEVC, and an H.264 projection/quad-only/multiple-quad test. The production wire
validator accepted every frame and acknowledged END; synthetic tracking checks
verified the floor pose. Native QSV output was independently decoded and checked
for color order, dimensions, no B pictures, and forced midpoint IDR.

On November 16, 2026, a physical Quest 2 paired through Nightfall's normal
Windows host flow, decoded the native OpenXR scene and tracked head movement.
The user confirmed the picture was smoother after the pacing change below.
A 3,600-frame GPU-rendered trial at 1840×1920 per eye drained every packet and
acknowledged END. Delivery averaged about 45 FPS, versus about 35 FPS in the
preceding 1,440-frame trial; these are single trials, not controlled benchmarks.

Nightfall's synchronous render/encode path now consumes a refresh tick that
arrived during submission instead of always waiting for another one. The first
frame still waits for a fresh tick; the normal Virtual Desktop path is unchanged.
H.264, HEVC and decoded quad-pixel/eye-visibility smoke checks pass with this change.
The current Nightfall client/protocol caps this backend at 72 Hz; stable 72 FPS,
90 Hz, controller calibration/haptics, additional games, non-Intel plugins,
32-bit builds and long sessions remain
unvalidated. Use Nightfall's GPU measurement wrapper for performance trials;
the native probe's pixel-reference mode includes CPU rendering/readback/disk I/O.
