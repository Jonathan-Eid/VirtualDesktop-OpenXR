import math
import struct
import unittest

from analyze_capture import parse_report


def report(kind: int, payload: bytes, command: int = 0x28) -> dict:
    header = bytes([command, 1, 0]) + b"ABCDEF" + struct.pack("<HB", kind, len(payload))
    raw = b"\x00" + header + payload
    raw += bytes(65 - len(raw))
    return {"length": len(raw), "data_hex": raw.hex()}


def pose_payload(*, q=(1.0, 0.0, 0.0, 0.0), pos=(0.1, -0.2, 0.3), flags=0x12) -> bytes:
    # Reference wire quaternion order is [w,z,y,x]. The second byte is a
    # button fragment; trailing two bytes are present in observed 39B payloads.
    return (
        bytes([7, 0])
        + struct.pack("<3f", *pos)
        + struct.pack("<4e", q[0], q[3], q[2], q[1])
        + struct.pack("<3e", 0.0, 0.0, 0.0)
        + struct.pack("<4e", 0.0, 0.0, 0.0, 0.0)
        + bytes([flags, 0, 0])
    )


class AnalyzeCaptureTests(unittest.TestCase):
    def test_observed_39_byte_pose_layout(self):
        header, parsed = parse_report(report(0x110, pose_payload(q=(0.5, 0.5, 0.5, 0.5))))
        self.assertEqual((header[0], header[2], header[3]), (0x28, 0x110, 39))
        self.assertIsNotNone(parsed)
        self.assertEqual(parsed["quaternion_wxyz"], [0.5, 0.5, 0.5, 0.5])
        self.assertEqual(parsed["pose_flags_raw"], 0x12)

    def test_ack_payload_never_decoded_or_returned(self):
        secret_like_text = b"WIFI-password-example"
        header, parsed = parse_report(report(0x101, secret_like_text))
        self.assertEqual((header[0], header[2], header[3]), (0x28, 0x101, len(secret_like_text)))
        self.assertIsNone(parsed)

    def test_rejects_bad_declared_length(self):
        record = report(0x110, pose_payload())
        record["length"] -= 1
        with self.assertRaises(ValueError):
            parse_report(record)

    def test_rejects_nonfinite_position(self):
        with self.assertRaises(ValueError):
            parse_report(report(0x110, pose_payload(pos=(math.nan, 0.0, 0.0))))

    def test_preserves_pose_when_auxiliary_angular_data_is_nonfinite(self):
        payload = bytearray(pose_payload())
        struct.pack_into("<e", payload, 28, math.inf)
        _, parsed = parse_report(report(0x110, bytes(payload)))
        self.assertIsNotNone(parsed)
        self.assertIsNone(parsed["auxiliary_half_fields_28"])

    def test_rejects_implausible_quaternion(self):
        with self.assertRaises(ValueError):
            parse_report(report(0x110, pose_payload(q=(0.0, 0.0, 0.0, 0.0))))


if __name__ == "__main__":
    unittest.main()
