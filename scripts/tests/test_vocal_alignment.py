"""Tests for vocal-harmonic rhythm alignment check (A3)."""

import unittest

from conftest import (
    Note, MusicAnalyzer, Severity,
    TICKS_PER_BAR, TICKS_PER_BEAT,
    make_vocal_note, make_chord_notes,
)


class TestVocalHarmonicAlignment(unittest.TestCase):
    """Vocal rhythm should respond to chord changes."""

    def _make_uniform_vocal(self, bars=16, spacing=None):
        """Create vocal with perfectly uniform IOI (all same spacing)."""
        if spacing is None:
            spacing = TICKS_PER_BEAT // 2  # 8th notes
        notes = []
        total_ticks = bars * TICKS_PER_BAR
        tick = 0
        while tick < total_ticks:
            notes.append(make_vocal_note(tick, 65, duration=spacing - 10))
            tick += spacing
        return notes

    def _make_chord_progression(self, bars=16):
        """Create chord changes every 2 bars."""
        notes = []
        chords = [[60, 64, 67], [65, 69, 72], [67, 71, 74], [69, 72, 76]]
        for bar in range(bars):
            tick = bar * TICKS_PER_BAR
            chord = chords[(bar // 2) % len(chords)]
            notes.extend(make_chord_notes(tick, chord, duration=TICKS_PER_BAR))
        return notes

    def test_uniform_vocal_with_no_emphasis_warning(self):
        """Highly uniform vocal ignoring chord changes triggers WARNING."""
        vocal = self._make_uniform_vocal(bars=16)
        chords = self._make_chord_progression(bars=16)

        result = MusicAnalyzer(vocal + chords).analyze_all()

        align_issues = [
            i for i in result.issues
            if i.subcategory == "vocal_harmonic_misalign"
        ]
        self.assertGreater(len(align_issues), 0)

    def test_varied_vocal_no_issue(self):
        """Vocal with varied IOI should not trigger alignment issue."""
        notes = []
        chords = self._make_chord_progression(bars=16)

        # Create vocal with mixed durations
        tick = 0
        for bar in range(16):
            bar_start = bar * TICKS_PER_BAR
            # Different spacing per beat
            spacings = [TICKS_PER_BEAT, TICKS_PER_BEAT // 2,
                        TICKS_PER_BEAT // 2, TICKS_PER_BEAT]
            pos = bar_start
            for sp in spacings:
                notes.append(make_vocal_note(pos, 65, duration=sp - 10))
                pos += sp

        result = MusicAnalyzer(notes + chords).analyze_all()

        align_issues = [
            i for i in result.issues
            if i.subcategory == "vocal_harmonic_misalign"
        ]
        self.assertEqual(len(align_issues), 0)

    def test_ultra_vocaloid_skipped(self):
        """UltraVocaloid style should skip this check."""
        vocal = self._make_uniform_vocal(bars=16)
        chords = self._make_chord_progression(bars=16)

        result = MusicAnalyzer(
            vocal + chords,
            metadata={'vocal_style': 3},  # VOCAL_STYLE_ULTRA_VOCALOID
        ).analyze_all()

        align_issues = [
            i for i in result.issues
            if i.subcategory == "vocal_harmonic_misalign"
        ]
        self.assertEqual(len(align_issues), 0)

    def test_no_chord_track_no_issue(self):
        """Without chord track, alignment check should not trigger."""
        vocal = self._make_uniform_vocal(bars=16)

        result = MusicAnalyzer(vocal).analyze_all()

        align_issues = [
            i for i in result.issues
            if i.subcategory == "vocal_harmonic_misalign"
        ]
        self.assertEqual(len(align_issues), 0)


if __name__ == "__main__":
    unittest.main()
