#!/usr/bin/env python3
"""
Tests for music_analyzer.py

Verifies chord track analysis, bass track analysis, and edge case detection.
"""

import sys
import unittest
from typing import List

# Import from music_analyzer
from music_analyzer import (
    Note, Issue, MusicAnalyzer, Severity, Category,
    note_name, tick_to_bar, TICKS_PER_BAR, TICKS_PER_BEAT
)


class TestNoteHelpers(unittest.TestCase):
    """Test helper functions."""

    def test_note_name(self):
        self.assertEqual(note_name(60), "C4")
        self.assertEqual(note_name(69), "A4")
        self.assertEqual(note_name(36), "C2")
        self.assertEqual(note_name(84), "C6")

    def test_tick_to_bar(self):
        self.assertEqual(tick_to_bar(0), 1)
        self.assertEqual(tick_to_bar(1919), 1)
        self.assertEqual(tick_to_bar(1920), 2)
        self.assertEqual(tick_to_bar(3840), 3)


class TestChordTrackAnalysis(unittest.TestCase):
    """Test chord track analysis."""

    def _make_chord_notes(self, tick: int, pitches: List[int], duration: int = 480) -> List[Note]:
        """Create chord notes at given tick."""
        return [
            Note(start=tick, duration=duration, pitch=p, velocity=80, channel=1)
            for p in pitches
        ]

    def test_single_voice_detection(self):
        """Single voice chord should be flagged as WARNING."""
        notes = []
        # Single voice chord
        notes.extend(self._make_chord_notes(0, [60]))  # C4 only
        notes.extend(self._make_chord_notes(TICKS_PER_BAR, [64]))  # E4 only

        analyzer = MusicAnalyzer(notes)
        result = analyzer.analyze_all()

        thin_issues = [i for i in result.issues
                      if i.subcategory == "thin_voicing" and i.severity == Severity.WARNING]
        self.assertEqual(len(thin_issues), 2)
        self.assertIn("Only 1 voice", thin_issues[0].message)

    def test_two_voice_detection(self):
        """Two voice chord should be flagged as INFO."""
        notes = self._make_chord_notes(0, [60, 64])  # C4, E4

        analyzer = MusicAnalyzer(notes)
        result = analyzer.analyze_all()

        thin_issues = [i for i in result.issues
                      if i.subcategory == "thin_voicing" and i.severity == Severity.INFO]
        self.assertEqual(len(thin_issues), 1)
        self.assertIn("Only 2 voices", thin_issues[0].message)

    def test_three_voice_normal(self):
        """Three voice chord should not be flagged."""
        notes = self._make_chord_notes(0, [60, 64, 67])  # C4, E4, G4

        analyzer = MusicAnalyzer(notes)
        result = analyzer.analyze_all()

        thin_issues = [i for i in result.issues if i.subcategory == "thin_voicing"]
        self.assertEqual(len(thin_issues), 0)

    def test_dense_voicing_detection(self):
        """6+ voice chord should be flagged."""
        notes = self._make_chord_notes(0, [60, 64, 67, 72, 76, 79])  # 6 voices

        analyzer = MusicAnalyzer(notes)
        result = analyzer.analyze_all()

        dense_issues = [i for i in result.issues if i.subcategory == "dense_voicing"]
        self.assertEqual(len(dense_issues), 1)

    def test_chord_register_low(self):
        """Chord below C3 should be flagged."""
        notes = self._make_chord_notes(0, [36, 48, 52])  # C2 is below C3

        analyzer = MusicAnalyzer(notes)
        result = analyzer.analyze_all()

        low_issues = [i for i in result.issues if i.subcategory == "chord_register_low"]
        self.assertEqual(len(low_issues), 1)
        self.assertIn("C2", low_issues[0].message)

    def test_chord_register_high(self):
        """Chord above C6 should be flagged."""
        notes = self._make_chord_notes(0, [60, 72, 88])  # E6 is above C6

        analyzer = MusicAnalyzer(notes)
        result = analyzer.analyze_all()

        high_issues = [i for i in result.issues if i.subcategory == "chord_register_high"]
        self.assertEqual(len(high_issues), 1)
        self.assertIn("E6", high_issues[0].message)

    def test_chord_above_vocal(self):
        """Chord exceeding vocal ceiling should be flagged."""
        notes = []
        # Vocal with ceiling at G4 (67)
        notes.append(Note(start=0, duration=TICKS_PER_BAR, pitch=60, velocity=100, channel=0))  # C4
        notes.append(Note(start=0, duration=TICKS_PER_BAR, pitch=67, velocity=100, channel=0))  # G4 ceiling
        # Chord with note above vocal ceiling
        notes.extend(self._make_chord_notes(0, [60, 64, 72]))  # C4, E4, C5 - C5 > G4+2

        analyzer = MusicAnalyzer(notes)
        result = analyzer.analyze_all()

        above_issues = [i for i in result.issues if i.subcategory == "chord_above_vocal"]
        self.assertEqual(len(above_issues), 1)
        self.assertIn("exceeds vocal ceiling", above_issues[0].message)

    def test_consecutive_same_voicing(self):
        """5+ consecutive same voicing should be flagged."""
        notes = []
        voicing = [60, 64, 67]  # C major
        for bar in range(6):  # 6 consecutive same voicing
            tick = bar * TICKS_PER_BAR
            notes.extend(self._make_chord_notes(tick, voicing))

        analyzer = MusicAnalyzer(notes)
        result = analyzer.analyze_all()

        rep_issues = [i for i in result.issues if i.subcategory == "chord_repetition"]
        self.assertEqual(len(rep_issues), 1)
        self.assertIn("consecutive same voicing", rep_issues[0].message)

    def test_no_consecutive_when_voicing_changes(self):
        """Different voicings should not trigger repetition warning."""
        notes = []
        voicings = [
            [60, 64, 67],  # C
            [62, 65, 69],  # Dm
            [64, 67, 71],  # Em
            [65, 69, 72],  # F
        ]
        for bar, voicing in enumerate(voicings):
            tick = bar * TICKS_PER_BAR
            notes.extend(self._make_chord_notes(tick, voicing))

        analyzer = MusicAnalyzer(notes)
        result = analyzer.analyze_all()

        rep_issues = [i for i in result.issues if i.subcategory == "chord_repetition"]
        self.assertEqual(len(rep_issues), 0)


class TestBassTrackAnalysis(unittest.TestCase):
    """Test bass track analysis."""

    def _make_bass_note(self, tick: int, pitch: int, duration: int = 480) -> Note:
        """Create bass note."""
        return Note(start=tick, duration=duration, pitch=pitch, velocity=80, channel=2)

    def test_bass_monotony_detection(self):
        """8+ consecutive same bass notes should be flagged."""
        notes = []
        for i in range(10):  # 10 consecutive C2
            notes.append(self._make_bass_note(i * TICKS_PER_BEAT, 36))

        analyzer = MusicAnalyzer(notes)
        result = analyzer.analyze_all()

        mono_issues = [i for i in result.issues if i.subcategory == "bass_monotony"]
        self.assertEqual(len(mono_issues), 1)
        self.assertIn("consecutive", mono_issues[0].message)

    def test_bass_range_normal(self):
        """Bass within E1-C4 should not be flagged."""
        notes = [
            self._make_bass_note(0, 28),  # E1
            self._make_bass_note(TICKS_PER_BEAT, 36),  # C2
            self._make_bass_note(TICKS_PER_BEAT * 2, 48),  # C3
            self._make_bass_note(TICKS_PER_BEAT * 3, 60),  # C4
        ]

        analyzer = MusicAnalyzer(notes)
        result = analyzer.analyze_all()

        range_issues = [i for i in result.issues
                       if i.subcategory in ["range_low", "range_high"] and i.track == "Bass"]
        self.assertEqual(len(range_issues), 0)


class TestDissonanceDetection(unittest.TestCase):
    """Test dissonance detection including sus chords."""

    def test_minor_second_detection(self):
        """Minor 2nd interval should be flagged."""
        notes = [
            Note(start=0, duration=TICKS_PER_BAR, pitch=60, velocity=80, channel=0),  # C4 vocal
            Note(start=0, duration=TICKS_PER_BAR, pitch=61, velocity=80, channel=1),  # C#4 chord
        ]

        analyzer = MusicAnalyzer(notes)
        result = analyzer.analyze_all()

        dissonance_issues = [i for i in result.issues if i.subcategory == "dissonance"]
        self.assertGreater(len(dissonance_issues), 0)
        self.assertIn("minor 2nd", dissonance_issues[0].message)

    def test_major_seventh_detection(self):
        """Major 7th interval should be flagged."""
        notes = [
            Note(start=0, duration=TICKS_PER_BAR, pitch=48, velocity=80, channel=2),  # C3 bass
            Note(start=0, duration=TICKS_PER_BAR, pitch=59, velocity=80, channel=1),  # B3 chord
        ]

        analyzer = MusicAnalyzer(notes)
        result = analyzer.analyze_all()

        dissonance_issues = [i for i in result.issues if i.subcategory == "dissonance"]
        self.assertGreater(len(dissonance_issues), 0)
        self.assertIn("major 7th", dissonance_issues[0].message)

    def test_sus4_not_flagged_as_error(self):
        """Sus4 chord (C-F-G) should not be flagged as error dissonance."""
        # Sus4: C4, F4, G4 - no minor 2nd or major 7th
        notes = [
            Note(start=0, duration=TICKS_PER_BAR, pitch=60, velocity=80, channel=1),  # C4
            Note(start=0, duration=TICKS_PER_BAR, pitch=65, velocity=80, channel=1),  # F4
            Note(start=0, duration=TICKS_PER_BAR, pitch=67, velocity=80, channel=1),  # G4
        ]

        analyzer = MusicAnalyzer(notes)
        result = analyzer.analyze_all()

        dissonance_errors = [i for i in result.issues
                            if i.subcategory == "dissonance" and i.severity == Severity.ERROR]
        self.assertEqual(len(dissonance_errors), 0)

    def test_sus2_not_flagged_as_error(self):
        """Sus2 chord (C-D-G) should not be flagged as error dissonance.
        Note: Major 2nd (C-D) might be flagged as warning but not error."""
        # Sus2: C4, D4, G4
        notes = [
            Note(start=0, duration=TICKS_PER_BAR, pitch=60, velocity=80, channel=1),  # C4
            Note(start=0, duration=TICKS_PER_BAR, pitch=62, velocity=80, channel=1),  # D4
            Note(start=0, duration=TICKS_PER_BAR, pitch=67, velocity=80, channel=1),  # G4
        ]

        analyzer = MusicAnalyzer(notes)
        result = analyzer.analyze_all()

        # Sus2 has major 2nd which is NOT flagged in current implementation
        # (only minor 2nd/major 7th are flagged)
        dissonance_errors = [i for i in result.issues
                            if i.subcategory == "dissonance" and i.severity == Severity.ERROR]
        self.assertEqual(len(dissonance_errors), 0)


class TestMelodicAnalysis(unittest.TestCase):
    """Test melodic analysis for vocal track."""

    def _make_vocal_note(self, tick: int, pitch: int, duration: int = 480) -> Note:
        """Create vocal note."""
        return Note(start=tick, duration=duration, pitch=pitch, velocity=100, channel=0)

    def test_consecutive_same_pitch(self):
        """6+ consecutive same pitch should be ERROR."""
        notes = []
        for i in range(7):  # 7 consecutive G4
            notes.append(self._make_vocal_note(i * TICKS_PER_BEAT, 67))

        analyzer = MusicAnalyzer(notes)
        result = analyzer.analyze_all()

        same_pitch_errors = [i for i in result.issues
                           if i.subcategory == "consecutive_same_pitch"
                           and i.severity == Severity.ERROR]
        self.assertGreater(len(same_pitch_errors), 0)

    def test_isolated_note(self):
        """Note with large gaps on both sides should be flagged."""
        notes = [
            self._make_vocal_note(0, 60, TICKS_PER_BEAT),  # First note
            self._make_vocal_note(TICKS_PER_BAR * 3, 67, TICKS_PER_BEAT),  # Isolated (3 bars gap)
            self._make_vocal_note(TICKS_PER_BAR * 6, 72, TICKS_PER_BEAT),  # Also isolated (3 bars gap)
        ]

        analyzer = MusicAnalyzer(notes)
        result = analyzer.analyze_all()

        isolated_issues = [i for i in result.issues if i.subcategory == "isolated_note"]
        # Both middle and last notes are isolated (large gaps on both sides)
        self.assertEqual(len(isolated_issues), 2)

    def test_large_leap(self):
        """Leap > 12 semitones should be flagged."""
        notes = [
            self._make_vocal_note(0, 60, TICKS_PER_BEAT),  # C4
            self._make_vocal_note(TICKS_PER_BEAT, 76, TICKS_PER_BEAT),  # E5 (16 semitones up)
        ]

        analyzer = MusicAnalyzer(notes)
        result = analyzer.analyze_all()

        leap_issues = [i for i in result.issues if i.subcategory == "large_leap"]
        self.assertGreater(len(leap_issues), 0)


class TestStructureAnalysis(unittest.TestCase):
    """Test structure analysis."""

    def test_empty_track_detection(self):
        """Empty melodic track should be flagged."""
        # Only bass notes, no vocal/motif/aux
        notes = [
            Note(start=0, duration=TICKS_PER_BAR, pitch=36, velocity=80, channel=2),
            Note(start=TICKS_PER_BAR, duration=TICKS_PER_BAR, pitch=36, velocity=80, channel=2),
        ]

        analyzer = MusicAnalyzer(notes)
        result = analyzer.analyze_all()

        empty_issues = [i for i in result.issues if i.subcategory == "empty_track"]
        # Should flag Vocal, Motif, Aux as empty
        self.assertGreaterEqual(len(empty_issues), 3)


class TestScoring(unittest.TestCase):
    """Test quality scoring."""

    def test_perfect_score(self):
        """Well-formed music should have high score."""
        notes = []
        # Simple melody
        melody = [60, 62, 64, 65, 67, 65, 64, 62]
        for i, pitch in enumerate(melody):
            notes.append(Note(start=i * TICKS_PER_BEAT * 2, duration=TICKS_PER_BEAT,
                            pitch=pitch, velocity=100, channel=0))
        # Simple chords
        chords = [[60, 64, 67], [62, 65, 69], [64, 67, 71], [65, 69, 72]]
        for i, chord in enumerate(chords):
            for p in chord:
                notes.append(Note(start=i * TICKS_PER_BAR, duration=TICKS_PER_BAR,
                                pitch=p, velocity=80, channel=1))
        # Simple bass
        bass = [48, 50, 52, 53]
        for i, pitch in enumerate(bass):
            notes.append(Note(start=i * TICKS_PER_BAR, duration=TICKS_PER_BAR,
                            pitch=pitch, velocity=80, channel=2))

        analyzer = MusicAnalyzer(notes)
        result = analyzer.analyze_all()

        # Should have reasonable score
        self.assertGreater(result.score.overall, 50)

    def test_error_lowers_score(self):
        """Errors should significantly lower score."""
        # Just dissonant notes
        notes = [
            Note(start=0, duration=TICKS_PER_BAR, pitch=36, velocity=80, channel=2),  # C2 bass
            Note(start=0, duration=TICKS_PER_BAR, pitch=37, velocity=80, channel=1),  # C#2 chord - m2
        ]

        analyzer = MusicAnalyzer(notes)
        result = analyzer.analyze_all()

        self.assertLess(result.score.harmonic, 100)


if __name__ == "__main__":
    unittest.main()
