# Ultimate Tracker Windows validation — November 17, 2026

Environment: Windows x64, HTC Ultimate wireless dongle (`0bb4:0350`), one powered
Ultimate Tracker, user-confirmed existing room map. The dongle exposed a 65-byte
HID input report. SteamVR processes were absent. HTC's ViveTrackerServer,
ViveHubServer and the separate VIVE Streaming service were running. This is a
Windows **HTC-service-assisted** read path, not independent tracker initialization.

## Physical captures

| Check | Observation |
| --- | --- |
| 12-second stationary capture | 1,504 HID reports; 1,500 reports matched command `0x28`, type `0x0110`, payload length 39. |
| Stationary candidate poses | Position range about 1.04/0.90/0.55 mm over raw axes; quaternion norm approximately 0.99993; raw flag byte `0x12`. |
| 20-second user movement capture | 2,362 HID reports; 2,348 decoded candidate poses; position and orientation finite; quaternion norms 0.999658–1.000344; raw flag byte `0x02`. |
| Motion response | Raw-coordinate range 1.2817/0.6864/1.2100 m; quaternion angle relative to first sample reached 179.63 degrees. These are decoded values, not independently measured physical distances or accuracy. |
| Pose packet sequence | Counter advanced by one between every consecutive pose packet in each recorded capture. No source-time or full motion latency conclusion follows. |
| Native passive source smoke | One tracker observed; packet advanced; finite orientations; source stopped cleanly after the bounded run. |

The packet interpretation follows public protocol research, not an HTC Windows
SDK ABI. Quaternion wire fields are reordered from WZYX to XYZW. Raw flags,
unknown trailing fields, source axes and mounting conventions still require
further physical validation. Some auxiliary half-float fields are nonfinite;
the analyzer reports those optional values as null rather than rejecting valid
position/orientation data. Their units/meaning are not established here.

## Software checks

- Full modified VDXR Release/x64 build succeeded.
- Synthetic protocol validation and actual-runtime synthetic role enumeration
  passed. They do not exercise physical tracker-to-app pose location.
- Probe argument/report-size fixtures and native decoder fixtures passed.
- Offline analyzer fixtures passed, including suppression of ACK payload text,
  malformed data and nonfinite transform rejection.
- Native source rejects unrecognized packet layouts, non-full-pose status low
  bits, nonfinite transforms and quaternions outside its near-unit bounds. It
  normalizes accepted half-float quaternions, deduplicates by tracker ID and
  invalidates loss/stale samples. It never asserts optical TRACKED confidence.

Native source time is **Windows QPC at report receipt**, not a verified camera
or tracker acquisition timestamp. Snapshot age measures only time since that
receipt. The reader sends no feature/output reports, does not rebuild a room
map and does not modify pairing or firmware.

## Remaining acceptance

- Verify physical axes, origin/mounting orientation and scale with independent
  movement measurements.
- Align Ultimate map space with the current Quest STAGE space in the calibration
  app; validate reserved movements and publish only the calibrated transform.
- Check camera occlusion, disconnect/reconnect and Quest recenter invalidation.
- Verify role poses inside a compatible OpenXR application with SteamVR closed.
- Validate multi-tracker identity, loss and packet-counter behavior on hardware.

No raw capture, room map, serial/radio identifier or personal calibration profile
is committed to the repository. Full-body tracking is not yet accepted as working.
