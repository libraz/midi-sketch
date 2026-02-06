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
        """4+ consecutive bars with same voicing should be flagged."""
        notes = []
        voicing = [60, 64, 67]
        for bar in range(6):
            notes.extend(make_chord_notes(bar * TICKS_PER_BAR, voicing))

        result = MusicAnalyzer(notes).analyze_all()

        rep_issues = [i for i in result.issues if i.subcategory == "chord_repetition"]
        self.assertEqual(len(rep_issues), 1)
        self.assertIn("consecutive bars", rep_issues[0].message)
        self.assertIn("same voicing", rep_issues[0].message)

    def test_quarter_note_rhythm_not_counted_as_repetition(self):
        """Same voicing hit 4x per bar should count as 1 bar, not 4 repeats."""
        notes = []
        voicing = [60, 64, 67]
        # 3 bars with quarter-note rhythm (4 hits per bar)
        for bar in range(3):
            for beat in range(4):
                tick = bar * TICKS_PER_BAR + beat * TICKS_PER_BEAT
                notes.extend(make_chord_notes(tick, voicing, duration=TICKS_PER_BEAT))

        result = MusicAnalyzer(notes).analyze_all()

        # 3 bars is below threshold (need 4+ consecutive bars), so no repetition issue
        rep_issues = [i for i in result.issues if i.subcategory == "chord_repetition"]
        self.assertEqual(len(rep_issues), 0)

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


class TestBassChordDegrees(unittest.TestCase):
    """Test bass chord degree analysis (pitch vs chord root)."""

    @staticmethod
    def _bass_with_prov(tick, pitch, chord_degree):
        """Create bass note with provenance chord_degree."""
        return Note(start=tick, duration=TICKS_PER_BEAT, pitch=pitch,
                    velocity=80, channel=2,
                    provenance={'chord_degree': chord_degree, 'source': 'bass_pattern'})

    def test_all_root_notes_no_issue(self):
        """Bass playing root of each chord should not be flagged."""
        # I(C)=36, IV(F)=41, V(G)=43, vi(A)=45
        notes = [
            self._bass_with_prov(0, 36, 0),                    # C on I chord
            self._bass_with_prov(TICKS_PER_BAR, 41, 3),        # F on IV chord
            self._bass_with_prov(TICKS_PER_BAR * 2, 43, 4),    # G on V chord
            self._bass_with_prov(TICKS_PER_BAR * 3, 45, 5),    # A on vi chord
        ]
        result = MusicAnalyzer(notes).analyze_all()
        degree_issues = [i for i in result.issues if i.subcategory == "bass_chord_degrees"]
        self.assertEqual(len(degree_issues), 0)

    def test_non_chord_tones_flagged(self):
        """Bass playing non-chord-tones should be flagged when ratio > 30%."""
        # All notes on I chord (C major: C, E, G) but playing D (non-chord-tone)
        notes = [self._bass_with_prov(i * TICKS_PER_BEAT, 38, 0) for i in range(10)]  # D on I
        result = MusicAnalyzer(notes).analyze_all()
        degree_issues = [i for i in result.issues
                         if i.subcategory == "bass_chord_degrees"
                         and i.severity == Severity.WARNING]
        self.assertEqual(len(degree_issues), 1)
        self.assertIn("non-chord-tone", degree_issues[0].message)

    def test_chord_tones_accepted(self):
        """Bass playing 3rd/5th of chord should not trigger warning."""
        notes = [
            self._bass_with_prov(0, 40, 0),                    # E (3rd) on I
            self._bass_with_prov(TICKS_PER_BEAT, 43, 0),       # G (5th) on I
            self._bass_with_prov(TICKS_PER_BEAT * 2, 41, 3),   # F (root) on IV
            self._bass_with_prov(TICKS_PER_BEAT * 3, 48, 3),   # C (5th) on IV
        ]
        result = MusicAnalyzer(notes).analyze_all()
        degree_issues = [i for i in result.issues
                         if i.subcategory == "bass_chord_degrees"
                         and i.severity == Severity.WARNING]
        self.assertEqual(len(degree_issues), 0)

    def test_low_root_fifth_info(self):
        """Low root+5th ratio should produce INFO (not WARNING)."""
        # Mix: 4 root, 6 thirds = 40% root+5th (low but not non-chord-tone)
        notes = []
        for i in range(4):
            notes.append(self._bass_with_prov(i * TICKS_PER_BEAT, 36, 0))  # C root on I
        for i in range(6):
            notes.append(self._bass_with_prov((4 + i) * TICKS_PER_BEAT, 40, 0))  # E 3rd on I
        result = MusicAnalyzer(notes).analyze_all()
        degree_issues = [i for i in result.issues if i.subcategory == "bass_chord_degrees"]
        # Should get INFO (low root+5th) but not WARNING (non-chord-tone ratio is 0%)
        warnings = [i for i in degree_issues if i.severity == Severity.WARNING]
        self.assertEqual(len(warnings), 0)
        infos = [i for i in degree_issues if i.severity == Severity.INFO]
        self.assertEqual(len(infos), 1)


class TestBassDownbeatRoot(unittest.TestCase):
    """Test bass downbeat root analysis."""

    @staticmethod
    def _bass_beat1(bar, pitch, chord_degree):
        """Create bass note on beat 1 of given bar with provenance."""
        tick = bar * TICKS_PER_BAR
        return Note(start=tick, duration=TICKS_PER_BEAT, pitch=pitch,
                    velocity=80, channel=2,
                    provenance={'chord_degree': chord_degree, 'source': 'bass_pattern'})

    def test_root_on_all_downbeats_no_issue(self):
        """Root on all downbeats should not be flagged."""
        notes = [
            self._bass_beat1(0, 36, 0),   # C on I
            self._bass_beat1(1, 41, 3),   # F on IV
            self._bass_beat1(2, 43, 4),   # G on V
            self._bass_beat1(3, 45, 5),   # A on vi
        ]
        result = MusicAnalyzer(notes).analyze_all()
        issues = [i for i in result.issues if i.subcategory == "bass_downbeat_root"]
        self.assertEqual(len(issues), 0)

    def test_fifth_on_downbeats_no_warning(self):
        """5th on downbeats should not trigger WARNING (only INFO if >50% non-root)."""
        notes = [
            self._bass_beat1(0, 43, 0),   # G (5th) on I
            self._bass_beat1(1, 48, 3),   # C (5th) on IV
            self._bass_beat1(2, 38, 4),   # D (5th) on V
            self._bass_beat1(3, 40, 5),   # E (5th) on vi
        ]
        result = MusicAnalyzer(notes).analyze_all()
        warnings = [i for i in result.issues
                    if i.subcategory == "bass_downbeat_root" and i.severity == Severity.WARNING]
        self.assertEqual(len(warnings), 0)

    def test_non_root_non_fifth_flags_warning(self):
        """Mostly non-root/non-5th on downbeats should trigger WARNING."""
        notes = [
            self._bass_beat1(0, 40, 0),   # E (3rd) on I
            self._bass_beat1(1, 45, 3),   # A (3rd) on IV
            self._bass_beat1(2, 47, 4),   # B (3rd) on V
            self._bass_beat1(3, 36, 5),   # C (3rd) on vi
            self._bass_beat1(4, 38, 0),   # D (non-chord) on I
        ]
        result = MusicAnalyzer(notes).analyze_all()
        warnings = [i for i in result.issues
                    if i.subcategory == "bass_downbeat_root" and i.severity == Severity.WARNING]
        self.assertEqual(len(warnings), 1)

    def test_aggregate_single_issue(self):
        """Should produce at most one aggregate issue, not per-note."""
        notes = [self._bass_beat1(i, 40, 0) for i in range(10)]  # E (3rd) on I, all bars
        result = MusicAnalyzer(notes).analyze_all()
        issues = [i for i in result.issues if i.subcategory == "bass_downbeat_root"]
        self.assertLessEqual(len(issues), 1)


class TestBassChordSpacing(unittest.TestCase):
    """Test bass-chord spacing analysis."""

    def test_unison_not_flagged(self):
        """Bass doubling lowest chord note (unison) should not be flagged."""
        notes = [make_bass_note(0, 48)]  # C3
        notes.extend(make_chord_notes(0, [48, 55, 60]))  # C3, G3, C4
        result = MusicAnalyzer(notes).analyze_all()
        spacing_issues = [i for i in result.issues if i.subcategory == "bass_chord_spacing"]
        self.assertEqual(len(spacing_issues), 0)

    def test_wide_spacing_not_flagged(self):
        """Bass-chord interval >= P4 should not be flagged."""
        notes = [make_bass_note(0, 36)]  # C2
        notes.extend(make_chord_notes(0, [48, 55, 60]))  # C3, G3, C4
        result = MusicAnalyzer(notes).analyze_all()
        spacing_issues = [i for i in result.issues if i.subcategory == "bass_chord_spacing"]
        self.assertEqual(len(spacing_issues), 0)

    def test_close_spacing_flagged(self):
        """Multiple close spacing instances should produce aggregate WARNING."""
        notes = []
        for bar in range(10):
            tick = bar * TICKS_PER_BAR
            notes.append(make_bass_note(tick, 48))  # C3
            notes.extend(make_chord_notes(tick, [50, 55, 60]))  # D3 (2st away), G3, C4
        result = MusicAnalyzer(notes).analyze_all()
        spacing_issues = [i for i in result.issues
                          if i.subcategory == "bass_chord_spacing"
                          and i.severity == Severity.WARNING]
        self.assertEqual(len(spacing_issues), 1)
        self.assertIn("close", spacing_issues[0].message)

    def test_aggregate_single_issue(self):
        """Should produce at most one aggregate issue, not per-note."""
        notes = []
        for bar in range(20):
            tick = bar * TICKS_PER_BAR
            notes.append(make_bass_note(tick, 48))
            notes.extend(make_chord_notes(tick, [50, 55, 60]))
        result = MusicAnalyzer(notes).analyze_all()
        spacing_issues = [i for i in result.issues if i.subcategory == "bass_chord_spacing"]
        self.assertLessEqual(len(spacing_issues), 1)


class TestSubcategoryPenaltyCap(unittest.TestCase):
    """Test that subcategory penalty is capped to prevent score destruction."""

    def test_many_warnings_capped(self):
        """Many WARNING-level issues from one subcategory should be capped."""
        notes = []
        # Create 20 thin voicing issues (WARNING level, penalty 0.8 each)
        # Without cap: 0.8 * 20 = 16.0 penalty
        # With cap (0.8 * 5 = 4.0): only 4.0 penalty
        for i in range(20):
            tick = i * TICKS_PER_BAR
            # Single-voice chord = thin_voicing WARNING (0.8 penalty)
            notes.append(Note(start=tick, duration=TICKS_PER_BEAT,
                              pitch=60 + (i % 12), velocity=80, channel=1))
        result = MusicAnalyzer(notes).analyze_all()
        thin_issues = [i for i in result.issues
                       if i.subcategory == "thin_voicing" and i.severity == Severity.WARNING]
        self.assertGreater(len(thin_issues), 5)
        # With cap, penalty should be 4.0, not 16.0
        # Harmonic score = 100 - 4.0 / note_factor * 8 (should be well above 0)
        self.assertGreater(result.score.harmonic, 0)

    def test_single_issue_no_cap_effect(self):
        """A single issue should not be affected by the cap."""
        notes = [
            Note(start=0, duration=TICKS_PER_BAR, pitch=60, velocity=80, channel=0),
            Note(start=0, duration=TICKS_PER_BAR, pitch=61, velocity=80, channel=1),
        ]
        result = MusicAnalyzer(notes).analyze_all()
        dissonance_issues = [i for i in result.issues if i.subcategory == "dissonance"]
        self.assertGreater(len(dissonance_issues), 0)
        # Score should still be penalized
        self.assertLess(result.score.harmonic, 100)


if __name__ == "__main__":
    unittest.main()
