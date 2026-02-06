"""Tests for melodic analysis: consecutive pitch, isolated notes, leaps."""

import unittest

from conftest import (
    MusicAnalyzer, Severity,
    TICKS_PER_BAR, TICKS_PER_BEAT,
    make_vocal_note,
)


class TestMelodicAnalysis(unittest.TestCase):
    """Test melodic analysis for vocal track."""

    def test_consecutive_same_pitch(self):
        """6+ consecutive same pitch should be ERROR."""
        notes = [make_vocal_note(i * TICKS_PER_BEAT, 67) for i in range(7)]

        result = MusicAnalyzer(notes).analyze_all()

        same_pitch_errors = [i for i in result.issues
                             if i.subcategory == "consecutive_same_pitch"
                             and i.severity == Severity.ERROR]
        self.assertGreater(len(same_pitch_errors), 0)

    def test_isolated_note(self):
        """Note with large gaps on both sides should be flagged."""
        notes = [
            make_vocal_note(0, 60, TICKS_PER_BEAT),
            make_vocal_note(TICKS_PER_BAR * 3, 67, TICKS_PER_BEAT),
            make_vocal_note(TICKS_PER_BAR * 6, 72, TICKS_PER_BEAT),
        ]

        result = MusicAnalyzer(notes).analyze_all()

        isolated_issues = [i for i in result.issues if i.subcategory == "isolated_note"]
        self.assertEqual(len(isolated_issues), 2)

    def test_large_leap(self):
        """Leap > 12 semitones should be flagged."""
        notes = [
            make_vocal_note(0, 60, TICKS_PER_BEAT),
            make_vocal_note(TICKS_PER_BEAT, 76, TICKS_PER_BEAT),
        ]

        result = MusicAnalyzer(notes).analyze_all()

        leap_issues = [i for i in result.issues if i.subcategory == "large_leap"]
        self.assertGreater(len(leap_issues), 0)


if __name__ == "__main__":
    unittest.main()
