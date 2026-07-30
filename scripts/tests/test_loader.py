"""Regression tests for analyzer JSON input validation."""

import json
import tempfile
import unittest
from pathlib import Path

from music_analyzer.loader import load_json_output


class TestJsonOutputLoader(unittest.TestCase):
    def _write(self, data):
        temp = tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False)
        try:
            json.dump(data, temp)
            return Path(temp.name)
        finally:
            temp.close()

    def test_requires_explicit_track_channel(self):
        path = self._write({"tracks": [{"notes": [{"pitch": 60}]}]})
        try:
            with self.assertRaisesRegex(ValueError, "missing required 'channel'"):
                load_json_output(str(path))
        finally:
            path.unlink()

    def test_rejects_out_of_range_note_values_with_location(self):
        path = self._write({
            "tracks": [{"channel": 0, "notes": [{"pitch": 128, "duration_ticks": 480}]}],
        })
        try:
            with self.assertRaisesRegex(ValueError, "track 0 note 0"):
                load_json_output(str(path))
        finally:
            path.unlink()

    def test_loads_valid_typed_note(self):
        path = self._write({
            "tracks": [{"channel": "2", "notes": [{
                "start_ticks": "120", "duration_ticks": "480", "pitch": "48", "velocity": "90",
            }]}],
        })
        try:
            notes = load_json_output(str(path))
        finally:
            path.unlink()
        self.assertEqual((notes[0].start, notes[0].duration, notes[0].pitch,
                          notes[0].velocity, notes[0].channel), (120, 480, 48, 90, 2))

    def test_metadata_preserves_exported_chord_timeline(self):
        from music_analyzer.loader import load_json_metadata

        path = self._write({"tracks": [], "chords": [{"tick": 0, "endTick": 480, "degree": 4}]})
        try:
            metadata = load_json_metadata(str(path))
        finally:
            path.unlink()
        self.assertEqual(metadata["chords"], [{"tick": 0, "endTick": 480, "degree": 4}])
