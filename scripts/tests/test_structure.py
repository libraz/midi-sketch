"""Tests for structure analysis: empty tracks, track balance."""

import unittest

from conftest import Note, MusicAnalyzer, TICKS_PER_BAR


class TestStructureAnalysis(unittest.TestCase):
    """Test structure analysis."""

    def test_empty_track_detection(self):
        """Empty melodic track should be flagged."""
        notes = [
            Note(start=0, duration=TICKS_PER_BAR, pitch=36, velocity=80, channel=2),
            Note(start=TICKS_PER_BAR, duration=TICKS_PER_BAR, pitch=36, velocity=80, channel=2),
        ]

        result = MusicAnalyzer(notes).analyze_all()

        empty_issues = [i for i in result.issues if i.subcategory == "empty_track"]
        self.assertGreaterEqual(len(empty_issues), 3)


if __name__ == "__main__":
    unittest.main()
