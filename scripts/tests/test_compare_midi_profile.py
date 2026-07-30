"""Validation tests for the dependency-free SMF profile parser."""

import tempfile
import unittest
from pathlib import Path

from compare_midi_profile import parse_smf


def write_smf(path: Path, division: int, track_events: bytes) -> None:
    header = b"MThd" + (6).to_bytes(4, "big") + (0).to_bytes(2, "big")
    header += (1).to_bytes(2, "big") + division.to_bytes(2, "big")
    track = b"MTrk" + len(track_events).to_bytes(4, "big") + track_events
    path.write_bytes(header + track)


class ParseSmfValidationTest(unittest.TestCase):
    def parse_events(self, division: int, track_events: bytes):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "input.mid"
            write_smf(path, division, track_events)
            return parse_smf(path)

    def test_rejects_zero_division(self):
        with self.assertRaisesRegex(ValueError, "zero time division"):
            self.parse_events(0, b"\x00\xff\x2f\x00")

    def test_rejects_smpte_division(self):
        with self.assertRaisesRegex(ValueError, "SMPTE"):
            self.parse_events(0xE728, b"\x00\xff\x2f\x00")

    def test_sysex_clears_running_status(self):
        events = (
            b"\x00\x90\x3c\x64"  # Note on C4
            b"\x00\xf0\x01\x7f"  # SysEx
            b"\x00\x3c\x00"       # Invalid: data bytes cannot reuse Note On
        )
        with self.assertRaisesRegex(ValueError, "running status"):
            self.parse_events(480, events)


if __name__ == "__main__":
    unittest.main()
