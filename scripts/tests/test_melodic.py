"""Tests for melodic analysis: consecutive pitch, isolated notes, leaps."""

import unittest

from conftest import (
    MusicAnalyzer, Note, Severity,
    TICKS_PER_BAR, TICKS_PER_BEAT,
    make_bass_note, make_vocal_note,
)
from music_analyzer.analyzers.melodic import MelodicAnalyzer
from music_analyzer.blueprints import BLUEPRINT_PROFILES


class TestMelodicAnalysis(unittest.TestCase):
    """Test melodic analysis for vocal track."""

    def test_consecutive_same_pitch_warning(self):
        """6-7 consecutive same pitch should be WARNING."""
        notes = [make_vocal_note(i * TICKS_PER_BEAT, 67) for i in range(7)]

        result = MusicAnalyzer(notes).analyze_all()

        same_pitch_warnings = [i for i in result.issues
                               if i.subcategory == "consecutive_same_pitch"
                               and i.severity == Severity.WARNING]
        self.assertGreater(len(same_pitch_warnings), 0)

    def test_consecutive_same_pitch_error(self):
        """8+ consecutive same pitch should be ERROR."""
        notes = [make_vocal_note(i * TICKS_PER_BEAT, 67) for i in range(9)]

        result = MusicAnalyzer(notes).analyze_all()

        same_pitch_errors = [i for i in result.issues
                             if i.subcategory == "consecutive_same_pitch"
                             and i.severity == Severity.ERROR]
        self.assertGreater(len(same_pitch_errors), 0)

    def test_consecutive_same_pitch_5_not_flagged(self):
        """5 consecutive same pitch should NOT be flagged."""
        notes = [make_vocal_note(i * TICKS_PER_BEAT, 67) for i in range(5)]

        result = MusicAnalyzer(notes).analyze_all()

        same_pitch_issues = [i for i in result.issues
                             if i.subcategory == "consecutive_same_pitch"]
        self.assertEqual(len(same_pitch_issues), 0)

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


class TestMelodyStyleConfidence(unittest.TestCase):
    @staticmethod
    def _targets(samples):
        return {
            "categories": {
                "ballad": {
                    "blueprints": [3],
                    "melody": {
                        "n": samples,
                        "step_ratio": {"min": 0.2, "med": 0.3, "max": 0.4},
                    },
                },
            },
        }

    def _check(self, samples):
        analyzer = MelodicAnalyzer([], {0: []}, metadata={"blueprint": 3})
        analyzer._check_genre_coloring({"step_ratio": 0.9}, self._targets(samples))
        return analyzer.issues

    def test_single_reference_is_informational_only(self):
        issues = self._check(samples=1)
        self.assertEqual(len(issues), 1)
        self.assertEqual(issues[0].severity, Severity.INFO)
        self.assertEqual(issues[0].details["min_samples"], 3)

    def test_three_references_enforce_style_warning(self):
        issues = self._check(samples=3)
        self.assertEqual(len(issues), 1)
        self.assertEqual(issues[0].severity, Severity.WARNING)
        self.assertEqual(issues[0].details["metric"], "step_ratio")


class TestRhythmSyncMelodicTolerance(unittest.TestCase):
    def test_every_rhythm_sync_profile_relaxes_repeated_aux_notes(self):
        notes = [
            Note(start=index * TICKS_PER_BEAT, duration=TICKS_PER_BEAT,
                 pitch=72, velocity=80, channel=5)
            for index in range(8)
        ]
        for blueprint in (1, 5, 7, 9):
            with self.subTest(blueprint=blueprint):
                analyzer = MelodicAnalyzer(
                    notes, {5: notes}, profile=BLUEPRINT_PROFILES[blueprint]
                )
                analyzer._analyze_consecutive_same_pitch()
                issues = [i for i in analyzer.issues
                          if i.subcategory == "consecutive_same_pitch"]
                self.assertEqual(len(issues), 1)
                self.assertEqual(issues[0].severity, Severity.INFO)


if __name__ == "__main__":
    unittest.main()


class TestInstrumentRegisterSpace(unittest.TestCase):
    """Instrument ranges are internal-register constants, judged in that space."""

    @staticmethod
    def _bass_range_issues(key, modulation=0):
        modulation_tick = 4 * TICKS_PER_BAR if modulation else 0
        notes = []
        for i, internal in enumerate(range(40, 56)):
            tick = i * TICKS_PER_BEAT
            shift = key + (modulation if modulation_tick and tick >= modulation_tick else 0)
            notes.append(make_bass_note(tick, internal + shift))
        metadata = {"key": key, "modulation_tick": modulation_tick,
                    "modulation_semitones": modulation}
        result = MusicAnalyzer(notes, metadata=metadata).analyze_all()
        return sorted((i.subcategory, i.tick) for i in result.issues
                      if i.track == "Bass" and i.subcategory.startswith("range"))

    def test_bass_in_its_internal_register_passes_in_every_key(self):
        self.assertEqual(self._bass_range_issues(0), [])
        self.assertEqual(self._bass_range_issues(11), [])
        # Key 11 plus a modulation of 2 is 13 semitones, not one.
        self.assertEqual(self._bass_range_issues(11, modulation=2), [])

    def test_out_of_register_bass_is_flagged_the_same_in_every_key(self):
        def issues(key):
            notes = [make_bass_note(0, 50 + key), make_bass_note(TICKS_PER_BEAT, 64 + key)]
            result = MusicAnalyzer(notes, metadata={"key": key}).analyze_all()
            return [(i.subcategory, i.tick) for i in result.issues
                    if i.track == "Bass" and i.subcategory.startswith("range")]
        self.assertEqual(issues(0), [("range_high", TICKS_PER_BEAT)])
        self.assertEqual(issues(11), issues(0))
