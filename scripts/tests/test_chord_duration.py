"""Tests for chord duration consistency check (A2)."""

import unittest

from conftest import (
    Note, MusicAnalyzer, Severity,
    TICKS_PER_BAR, TICKS_PER_BEAT,
)


def _make_chord_onset(tick, pitches, durations):
    """Create chord notes with different durations per voice."""
    notes = []
    for pitch, dur in zip(pitches, durations):
        notes.append(Note(
            start=tick, duration=dur, pitch=pitch,
            velocity=80, channel=1,
        ))
    return notes


class TestChordDurationConsistency(unittest.TestCase):
    """Chord voices at same onset should have consistent durations."""

    def test_consistent_durations_no_issue(self):
        """All voices same duration should not trigger issue."""
        notes = []
        for bar in range(8):
            tick = bar * TICKS_PER_BAR
            notes.extend(_make_chord_onset(
                tick, [60, 64, 67], [TICKS_PER_BAR] * 3,
            ))

        result = MusicAnalyzer(notes).analyze_all()

        issues = [
            i for i in result.issues
            if i.subcategory == "chord_duration_mismatch"
        ]
        self.assertEqual(len(issues), 0)

    def test_high_ratio_triggers_error(self):
        """Duration ratio > 3.0 should trigger ERROR."""
        notes = []
        for bar in range(8):
            tick = bar * TICKS_PER_BAR
            # Ratio: 1920/120 = 16:1
            notes.extend(_make_chord_onset(
                tick, [60, 64, 67],
                [TICKS_PER_BAR, TICKS_PER_BAR, 120],
            ))

        result = MusicAnalyzer(notes).analyze_all()

        error_issues = [
            i for i in result.issues
            if i.subcategory == "chord_duration_mismatch"
            and i.severity == Severity.ERROR
        ]
        self.assertGreater(len(error_issues), 0)

    def test_moderate_ratio_triggers_warning(self):
        """Duration ratio between 2.0 and 3.0 should trigger WARNING."""
        notes = []
        for bar in range(8):
            tick = bar * TICKS_PER_BAR
            # Ratio: 960/400 = 2.4
            notes.extend(_make_chord_onset(
                tick, [60, 64, 67],
                [960, 960, 400],
            ))

        result = MusicAnalyzer(notes).analyze_all()

        warning_issues = [
            i for i in result.issues
            if i.subcategory == "chord_duration_mismatch"
            and i.severity == Severity.WARNING
        ]
        self.assertGreater(len(warning_issues), 0)

    def test_aggregate_warning_when_widespread(self):
        """When >30% of onsets are inconsistent, aggregate WARNING issued."""
        notes = []
        for bar in range(10):
            tick = bar * TICKS_PER_BAR
            # All onsets inconsistent (ratio 8:1)
            notes.extend(_make_chord_onset(
                tick, [60, 64, 67],
                [TICKS_PER_BAR, TICKS_PER_BAR, TICKS_PER_BEAT // 2],
            ))

        result = MusicAnalyzer(notes).analyze_all()

        aggregate_issues = [
            i for i in result.issues
            if i.subcategory == "chord_duration_mismatch"
            and "widespread" in i.message
        ]
        self.assertGreater(len(aggregate_issues), 0)

    def test_single_voice_onsets_skipped(self):
        """Onsets with only 1 voice should not be checked."""
        notes = []
        for bar in range(8):
            tick = bar * TICKS_PER_BAR
            notes.append(Note(
                start=tick, duration=120, pitch=60,
                velocity=80, channel=1,
            ))

        result = MusicAnalyzer(notes).analyze_all()

        issues = [
            i for i in result.issues
            if i.subcategory == "chord_duration_mismatch"
        ]
        self.assertEqual(len(issues), 0)


if __name__ == "__main__":
    unittest.main()
