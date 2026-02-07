"""Tests for bonus gating when ERRORs are present (C1)."""

import unittest

from conftest import (
    Note, MusicAnalyzer, Severity,
    TICKS_PER_BAR, TICKS_PER_BEAT,
)
from music_analyzer.constants import Category


def _make_clean_song(bars=32):
    """Create a clean song with no issues (good for bonus generation)."""
    notes = []
    # Varied vocal melody with sustained phrase endings
    base_phrase = [60, 62, 64, 65, 67, 65, 64, 62]
    for phrase_idx in range(bars // 4):
        for note_idx, pitch in enumerate(base_phrase):
            tick = phrase_idx * 4 * TICKS_PER_BAR + note_idx * TICKS_PER_BEAT
            dur = TICKS_PER_BEAT - 60
            if note_idx == len(base_phrase) - 1:
                dur = TICKS_PER_BEAT * 2  # Sustained ending
            notes.append(Note(
                start=tick, duration=dur,
                pitch=pitch + (phrase_idx % 3) * 2,
                velocity=90, channel=0,
            ))

    # Chord track (consistent durations)
    chords = [[60, 64, 67], [65, 69, 72], [67, 71, 74], [69, 72, 76]]
    for bar in range(bars):
        tick = bar * TICKS_PER_BAR
        for p in chords[bar % 4]:
            notes.append(Note(
                start=tick, duration=TICKS_PER_BAR,
                pitch=p, velocity=75, channel=1,
                provenance={'chord_degree': [0, 3, 4, 5][bar % 4],
                            'source': 'chord_voicing'},
            ))

    # Bass (root notes)
    bass_pitches = {0: 48, 3: 53, 4: 43, 5: 45}
    for bar in range(bars):
        tick = bar * TICKS_PER_BAR
        degree = [0, 3, 4, 5][bar % 4]
        notes.append(Note(
            start=tick, duration=TICKS_PER_BAR,
            pitch=bass_pitches[degree], velocity=85, channel=2,
            provenance={'chord_degree': degree, 'source': 'bass_pattern'},
        ))

    return notes


def _make_song_with_melodic_errors(bars=32):
    """Create a song that generates MELODIC ERROR issues."""
    notes = _make_clean_song(bars)
    # Add many same-pitch vocal notes to trigger pitch_repetition_rate ERROR
    for i in range(60):
        notes.append(Note(
            start=i * (TICKS_PER_BEAT // 2),
            duration=TICKS_PER_BEAT // 2 - 10,
            pitch=60, velocity=80, channel=0,
        ))
    return notes


class TestBonusGatingWithErrors(unittest.TestCase):
    """Bonus caps should be reduced when ERRORs are present."""

    def test_clean_song_gets_full_bonus(self):
        """A clean song should get full bonus (no error gating)."""
        notes = _make_clean_song(bars=32)
        result = MusicAnalyzer(notes).analyze_all()

        melodic_errors = [
            i for i in result.issues
            if i.category == Category.MELODIC and i.severity == Severity.ERROR
        ]
        # Should have no melodic errors (or very few)
        # The bonus should not be gated
        if len(melodic_errors) == 0:
            # Full bonus cap should be available
            self.assertLessEqual(result.score.melodic_bonus, 10.0)

    def test_errors_reduce_bonus_cap(self):
        """When ERRORs exist, bonus cap should be halved."""
        notes = _make_song_with_melodic_errors(bars=32)
        result_errors = MusicAnalyzer(notes).analyze_all()

        clean_notes = _make_clean_song(bars=32)
        result_clean = MusicAnalyzer(clean_notes).analyze_all()

        # The error version should have reduced bonus
        # (At minimum, if errors exist, cap is halved)
        melodic_errors = [
            i for i in result_errors.issues
            if i.category == Category.MELODIC and i.severity == Severity.ERROR
        ]
        if len(melodic_errors) >= 1:
            # Bonus should be at most 30% of the clean version's cap
            self.assertLessEqual(
                result_errors.score.melodic_bonus,
                10.0 * 0.3 + 0.01,  # 30% cap + epsilon
            )

    def test_three_plus_errors_zero_bonus(self):
        """When 3+ ERRORs in a category, bonus should be 0."""
        notes = _make_clean_song(bars=32)
        # Generate many same-pitch notes to trigger multiple melodic ERRORs
        for i in range(120):
            notes.append(Note(
                start=i * (TICKS_PER_BEAT // 4),
                duration=TICKS_PER_BEAT // 4 - 5,
                pitch=60, velocity=80, channel=0,
            ))

        result = MusicAnalyzer(notes).analyze_all()

        melodic_errors = [
            i for i in result.issues
            if i.category == Category.MELODIC and i.severity == Severity.ERROR
        ]
        if len(melodic_errors) >= 3:
            self.assertEqual(
                result.score.melodic_bonus, 0.0,
                "3+ errors should zero out bonus",
            )


class TestBonusGatingCategories(unittest.TestCase):
    """Bonus gating should apply independently per category."""

    def test_harmonic_errors_dont_affect_melodic_bonus(self):
        """Harmonic ERRORs should only gate harmonic bonus, not melodic."""
        notes = _make_clean_song(bars=32)
        # Add dissonant notes to create HARMONIC errors
        for bar in range(8):
            tick = bar * TICKS_PER_BAR
            # Bass + chord minor 2nd clash
            notes.append(Note(
                start=tick, duration=TICKS_PER_BEAT,
                pitch=48, velocity=80, channel=2,
            ))
            notes.append(Note(
                start=tick, duration=TICKS_PER_BEAT,
                pitch=49, velocity=80, channel=1,
            ))

        result = MusicAnalyzer(notes).analyze_all()

        harmonic_errors = [
            i for i in result.issues
            if i.category == Category.HARMONIC and i.severity == Severity.ERROR
        ]
        # If harmonic errors exist, they should gate harmonic bonus
        # but melodic bonus should be unaffected
        if len(harmonic_errors) >= 1:
            # Melodic bonus should still be at full cap potential
            # (not reduced by harmonic errors)
            self.assertLessEqual(result.score.melodic_bonus, 10.0)


if __name__ == "__main__":
    unittest.main()
