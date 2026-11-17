#!/usr/bin/env python3
"""Offline, read-only summarizer for ultimate-tracker-probe JSONL captures.

Protocol layout is based on public reverse-engineering notes in nijkah/pyvut:
12-byte dongle incoming header, 0x110 pose payload, and <BB12s8s6s8sB
payload fields. This tool does not implement or invoke any dongle commands.
"""

from __future__ import annotations

import argparse
import collections
import json
import math
import struct
import sys
from pathlib import Path
from typing import Any

POSE_TYPE = 0x0110
ACK_TYPE = 0x0101
INCOMING_CMD = 0x28
HEADER_BYTES = 12
POSE_BYTES_USED = 0x25
POSE_LENGTHS = {0x25, 0x27}


def parse_report(record: dict[str, Any]) -> tuple[tuple[int, int, int, int], dict[str, Any] | None]:
    """Return (cmd, type, payload length) and a validated numeric pose, if any."""
    hex_data = record.get("data_hex")
    if not isinstance(hex_data, str) or len(hex_data) > 8192 or len(hex_data) % 2:
        raise ValueError("invalid data_hex")
    try:
        raw = bytes.fromhex(hex_data)
    except ValueError as exc:
        raise ValueError("invalid data_hex") from exc
    declared_total = record.get("length")
    if not isinstance(declared_total, int) or declared_total != len(raw) or len(raw) > 4096:
        raise ValueError("record length mismatch or out of bounds")
    # Windows HID ReadFile includes an unnumbered report-ID placeholder byte;
    # the 64-byte payload after it matches the dongle header parser layout.
    if len(raw) == 65 and raw[0] == 0:
        raw = raw[1:]
    if len(raw) < HEADER_BYTES:
        raise ValueError("short dongle header")
    cmd = raw[0]
    packet_index = int.from_bytes(raw[1:3], "little")
    report_type = int.from_bytes(raw[9:11], "little")
    payload_len = raw[11]
    if payload_len > len(raw) - HEADER_BYTES:
        raise ValueError("declared payload exceeds report")
    payload = raw[HEADER_BYTES : HEADER_BYTES + payload_len]
    if cmd != INCOMING_CMD or report_type != POSE_TYPE or payload_len not in POSE_LENGTHS:
        return (cmd, packet_index, report_type, payload_len), None
    if len(payload) < POSE_BYTES_USED:
        raise ValueError("short pose payload")
    index, buttons = payload[0], payload[1]
    x, y, z = struct.unpack_from("<3f", payload, 2)
    # Wire order is documented as [w,z,y,x]; publish reordered [w,x,y,z].
    w, qz, qy, qx = struct.unpack_from("<4e", payload, 14)
    q = (w, qx, qy, qz)
    accel = struct.unpack_from("<3e", payload, 22)
    angular = struct.unpack_from("<4e", payload, 28)
    tracking = payload[36]
    if not all(math.isfinite(v) for v in (x, y, z, *q)):
        raise ValueError("non-finite position or orientation")
    if any(abs(v) > 10000 for v in (x, y, z)):
        raise ValueError("position outside conservative bounds")
    qnorm = math.sqrt(sum(v * v for v in q))
    if not 0.5 <= qnorm <= 1.5:
        raise ValueError("quaternion outside conservative norm bounds")
    return (cmd, packet_index, report_type, payload_len), {
        "packet_index": packet_index,
        "index": index,
        "buttons_fragment": buttons,
        "coordinate_frame": "raw tracker HID; uncalibrated and basis unknown",
        "position": [round(v, 6) for v in (x, y, z)],
        "quaternion_wxyz": [round(v, 6) for v in q],
        "quaternion_norm": round(qnorm, 6),
        "auxiliary_half_fields_22": [round(v, 5) for v in accel] if all(map(math.isfinite, accel)) else None,
        "auxiliary_half_fields_28": [round(v, 5) for v in angular] if all(map(math.isfinite, angular)) else None,
        "pose_flags_raw": tracking,
        "unknown_trailing_bytes": [payload[37], payload[38]] if payload_len == 0x27 else [],
    }


def analyze(path: Path, max_poses: int) -> dict[str, Any]:
    counts: collections.Counter[tuple[int, int, int]] = collections.Counter()
    poses: list[dict[str, Any]] = []
    malformed = 0
    total = 0
    with path.open("r", encoding="utf-8") as stream:
        for line in stream:
            if len(line) > 10000:
                malformed += 1
                continue
            try:
                record = json.loads(line)
                if not isinstance(record, dict) or record.get("type") != "input_report":
                    continue
                total += 1
                header, pose = parse_report(record)
                counts[header] += 1
                if pose is not None and len(poses) < max_poses:
                    poses.append(pose)
            except (ValueError, TypeError, json.JSONDecodeError):
                malformed += 1
    grouped: collections.Counter[tuple[int, int, int]] = collections.Counter()
    for (cmd, _packet, kind, size), count in counts.items():
        grouped[(cmd, kind, size)] += count
    headers = [
        {"command": f"0x{cmd:02x}", "type": f"0x{kind:04x}", "payload_bytes": size, "count": count}
        for (cmd, kind, size), count in sorted(grouped.items())
    ]
    pose_count = sum(n for (cmd, _packet, kind, size), n in counts.items() if cmd == INCOMING_CMD and kind == POSE_TYPE and size in POSE_LENGTHS)
    ack_count = sum(n for (_cmd, _packet, kind, _size), n in counts.items() if kind == ACK_TYPE)
    return {
        "tool": "ultimate-tracker-offline-analyzer",
        "input_reports": total,
        "malformed_or_rejected_records": malformed,
        "header_histogram": headers,
        "ack_records_payload_suppressed": ack_count,
        "pose_records_matching_reference_layout": pose_count,
        "validated_numeric_pose_samples": poses,
        "pose_samples_truncated": max(0, pose_count - len(poses)),
        "limitations": [
            "Offline decoding only; no hardware I/O or protocol commands.",
            "Payload layout follows community reverse-engineering, not an HTC Windows SDK.",
            "Pose flags and button semantics are emitted as raw numeric fields only; meanings remain unverified.",
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path, help="local JSONL capture created with --raw-output")
    parser.add_argument("--max-poses", type=int, default=8, help="maximum numeric pose samples to print (0..100)")
    args = parser.parse_args()
    if not 0 <= args.max_poses <= 100:
        parser.error("--max-poses must be between 0 and 100")
    try:
        result = analyze(args.capture, args.max_poses)
    except OSError as exc:
        print(f"cannot read capture: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(result, separators=(",", ":"), allow_nan=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
