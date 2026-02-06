"""Tests for harmonic analysis: chord voicing, bass line, dissonance."""

import unittest

from conftest import (
    Note, MusicAnalyzer, Severity,
    TICKS_PER_BAR, TICKS_PER_BEAT,
    make_chord_notes, make_bass_note,
)


class TestChordTrackAnalysis(unittest.TestCase):
    """Test chord track analysis."""

    def test_single_voice_detection(self):
        """Single voice chord should be flagged as WARNING."""
        notes = []
        notes.extend(make_chord_notes(0, [60]))
        notes.extend(make_chord_notes(TICKS_PER_BAR, [64]))

        result = MusicAnalyzer(notes).analyze_all()

        thin_issues = [i for i in result.issues
                       if i.subcategory == "thin_voicing" and i.severity == Severity.WARNING]
        self.assertEqual(len(thin_issues), 2)
        self.assertIn("Only 1 voice", thin_issues[0].message)

    def test_two_voice_detection(self):
        """Two voice chord should be flagged as INFO."""
        notes = make_chord_notes(0, [60, 64])

        result = MusicAnalyzer(notes).analyze_all()

        thin_issues = [i for i in result.issues
                       if i.subcategory == "thin_voicing" and i.severity == Severity.INFO]
        self.assertEqual(len(thin_issues), 1)
        self.assertIn("Only 2 voices", thin_issues[0].message)

    def test_three_voice_normal(self):
        """Three voice chord should not be flagged."""
        notes = make_chord_notes(0, [60, 64, 67])

        result = MusicAnalyzer(notes).analyze_all()

        thin_issues = [i for i in result.issues if i.subcategory == "thin_voicing"]
        self.assertEqual(len(thin_issues), 0)

    def test_dense_voicing_detection(self):
        """6+ voice chord should be flagged."""
        notes = make_chord_notes(0, [60, 64, 67, 72, 76, 79])

        result = MusicAnalyzer(notes).analyze_all()

        dense_issues = [i for i in result.issues if i.subcategory == "dense_voicing"]
        self.assertEqual(len(dense_issues), 1)

    def test_chord_register_low(self):
        """Chord below C3 should be flagged."""
        notes = make_chord_notes(0, [36, 48, 52])

        result = MusicAnalyzer(notes).analyze_all()

        low_issues = [i for i in result.issues if i.subcategory == "chord_register_low"]
        self.assertEqual(len(low_issues), 1)
        self.assertIn("C2", low_issues[0].message)

    def test_chord_register_high(self):
        """Chord above C6 should be flagged."""
        notes = make_chord_notes(0, [60, 72, 88])

        result = MusicAnalyzer(notes).analyze_all()

        high_issues = [i for i in result.issues if i.subcategory == "chord_register_high"]
        self.assertEqual(len(high_issues), 1)
        self.assertIn("E6", high_issues[0].message)

    def test_chord_above_vocal(self):
        """Chord exceeding vocal ceiling should be flagged."""
        notes = [
            Note(start=0, duration=TICKS_PER_BAR, pitch=60, velocity=100, channel=0),
            Note(start=0, duration=TICKS_PER_BAR, pitch=67, velocity=100, channel=0),
        ]
        notes.extend(make_chord_notes(0, [60, 64, 72]))

        result = MusicAnalyzer(notes).analyze_all()

        above_issues = [i for i in result.issues if i.subcategory == "chord_above_vocal"]
        self.assertEqual(len(above_issues), 1)
        self.assertIn("exceeds vocal ceiling", above_issues[0].message)

    def test_consecutive_same_voicing(self):
        """5+ consecutive same voicing should be flagged."""
        notes = []
        voicing = [60, 64, 67]
        for bar in range(6):
            notes.extend(make_chord_notes(bar * TICKS_PER_BAR, voicing))

        result = MusicAnalyzer(notes).analyze_all()

        rep_issues = [i for i in result.issues if i.subcategory == "chord_repetition"]
        self.assertEqual(len(rep_issues), 1)
        self.assertIn("consecutive same voicing", rep_issues[0].message)

    def test_no_consecutive_when_voicing_changes(self):
        """Different voicings should not trigger repetition warning."""
        notes = []
        voicings = [[60, 64, 67], [62, 65, 69], [64, 67, 71], [65, 69, 72]]
        for bar, voicing in enumerate(voicings):
            notes.extend(make_chord_notes(bar * TICKS_PER_BAR, voicing))

        result = MusicAnalyzer(notes).analyze_all()

        rep_issues = [i for i in result.issues if i.subcategory == "chord_repetition"]
        self.assertEqual(len(rep_issues), 0)


class TestBassTrackAnalysis(unittest.TestCase):
    """Test bass track analysis."""

    def test_bass_monotony_detection(self):
        """8+ consecutive same bass notes should be flagged."""
        notes = [make_bass_note(i * TICKS_PER_BEAT, 36) for i in range(10)]

        result = MusicAnalyzer(notes).analyze_all()

        mono_issues = [i for i in result.issues if i.subcategory == "bass_monotony"]
        self.assertEqual(len(mono_issues), 1)
        self.assertIn("consecutive", mono_issues[0].message)

    def test_bass_range_normal(self):
        """Bass within E1-C4 should not be flagged."""
        notes = [
            make_bass_note(0, 28),
            make_bass_note(TICKS_PER_BEAT, 36),
            make_bass_note(TICKS_PER_BEAT * 2, 48),
            make_bass_note(TICKS_PER_BEAT * 3, 60),
        ]

        result = MusicAnalyzer(notes).analyze_all()

        range_issues = [i for i in result.issues
                        if i.subcategory in ["range_low", "range_high"] and i.track == "Bass"]
        self.assertEqual(len(range_issues), 0)


class TestDissonanceDetection(unittest.TestCase):
    """Test dissonance detection including sus chords."""

    def test_minor_second_detection(self):
        """Minor 2nd interval should be flagged."""
        notes = [
            Note(start=0, duration=TICKS_PER_BAR, pitch=60, velocity=80, channel=0),
            Note(start=0, duration=TICKS_PER_BAR, pitch=61, velocity=80, channel=1),
        ]

        result = MusicAnalyzer(notes).analyze_all()

        dissonance_issues = [i for i in result.issues if i.subcategory == "dissonance"]
        self.assertGreater(len(dissonance_issues), 0)
        self.assertIn("minor 2nd", dissonance_issues[0].message)

    def test_major_seventh_detection(self):
        """Major 7th interval should be flagged."""
        notes = [
            Note(start=0, duration=TICKS_PER_BAR, pitch=48, velocity=80, channel=2),
            Note(start=0, duration=TICKS_PER_BAR, pitch=59, velocity=80, channel=1),
        ]

        result = MusicAnalyzer(notes).analyze_all()

        dissonance_issues = [i for i in result.issues if i.subcategory == "dissonance"]
        self.assertGreater(len(dissonance_issues), 0)
        self.assertIn("major 7th", dissonance_issues[0].message)

    def test_sus4_not_flagged_as_error(self):
        """Sus4 chord (C-F-G) should not be flagged as error dissonance."""
        notes = [
            Note(start=0, duration=TICKS_PER_BAR, pitch=60, velocity=80, channel=1),
            Note(start=0, duration=TICKS_PER_BAR, pitch=65, velocity=80, channel=1),
            Note(start=0, duration=TICKS_PER_BAR, pitch=67, velocity=80, channel=1),
        ]

        result = MusicAnalyzer(notes).analyze_all()

        dissonance_errors = [i for i in result.issues
                             if i.subcategory == "dissonance" and i.severity == Severity.ERROR]
        self.assertEqual(len(dissonance_errors), 0)

    def test_sus2_not_flagged_as_error(self):
        """Sus2 chord (C-D-G) should not be flagged as error dissonance."""
        notes = [
            Note(start=0, duration=TICKS_PER_BAR, pitch=60, velocity=80, channel=1),
            Note(start=0, duration=TICKS_PER_BAR, pitch=62, velocity=80, channel=1),
            Note(start=0, duration=TICKS_PER_BAR, pitch=67, velocity=80, channel=1),
        ]

        result = MusicAnalyzer(notes).analyze_all()

        dissonance_errors = [i for i in result.issues
                             if i.subcategory == "dissonance" and i.severity == Severity.ERROR]
        self.assertEqual(len(dissonance_errors), 0)


if __name__ == "__main__":
    unittest.main()
