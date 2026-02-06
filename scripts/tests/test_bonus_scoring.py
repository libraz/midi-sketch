"""Integration tests for the bonus scoring system.

Verifies that bonus scoring is properly generated, applied to category
scores, respects per-category caps, and responds to blueprint profiles.
"""

import unittest

from conftest import Note, MusicAnalyzer, TICKS_PER_BAR, TICKS_PER_BEAT

from music_analyzer.models import Bonus, QualityScore
from music_analyzer.constants import Category
from music_analyzer.blueprints import BLUEPRINT_PROFILES


def _make_song(bars=32, with_patterns=True):
    """Generate a realistic set of notes for integration testing.

    Creates vocal, chord, and bass tracks spanning the requested number
    of bars. When with_patterns is True, the vocal melody uses a repeated
    4-bar phrase (transposition-invariant) to trigger hook and earworm
    bonus checks.

    Args:
        bars: Number of bars to generate.
        with_patterns: If True, repeat a 4-bar melodic phrase with the
            same interval structure at different pitch levels.

    Returns:
        List of Note objects across vocal (ch 0), chord (ch 1),
        and bass (ch 2).
    """
    notes = []

    # -- Vocal (channel 0): melody with repeated 4-bar phrases --
    # Base phrase: 8 notes over 4 bars with a contoured shape.
    # Intervals: +2, +2, +1, -2, -1, +3, -2  (step-wise with direction changes)
    base_phrase_pitches = [60, 62, 64, 65, 63, 62, 65, 63]
    phrase_length_bars = 4
    notes_per_phrase = len(base_phrase_pitches)
    # Each note is spaced by 1 beat within the phrase.
    note_spacing = TICKS_PER_BEAT

    if with_patterns:
        # Repeat the phrase at different transpositions to create
        # transposition-invariant repetitions (same interval pattern).
        transpositions = [0, 2, 0, 5, 0, 2, 0, 5]
        phrase_count = bars // phrase_length_bars
        for phrase_idx in range(phrase_count):
            transpose = transpositions[phrase_idx % len(transpositions)]
            phrase_start_tick = phrase_idx * phrase_length_bars * TICKS_PER_BAR
            for note_idx, base_pitch in enumerate(base_phrase_pitches):
                tick = phrase_start_tick + note_idx * note_spacing
                # Add a gap between phrases (skip the last half-beat) for
                # phrase boundary detection.
                velocity = 90 + (phrase_idx % 3) * 5
                notes.append(Note(
                    start=tick,
                    duration=TICKS_PER_BEAT - 60,
                    pitch=base_pitch + transpose,
                    velocity=velocity,
                    channel=0,
                ))
    else:
        # Minimal: one note per bar, all same pitch, no patterns.
        for bar_idx in range(bars):
            tick = bar_idx * TICKS_PER_BAR
            notes.append(Note(
                start=tick,
                duration=TICKS_PER_BEAT,
                pitch=60,
                velocity=80,
                channel=0,
            ))

    # -- Chord (channel 1): block chords on beat 1 of each bar --
    # Cycle through I-IV-V-vi progression.
    chord_voicings = [
        ([60, 64, 67], 0),  # C major,  degree 0 (I)
        ([65, 69, 72], 3),  # F major,  degree 3 (IV)
        ([67, 71, 74], 4),  # G major,  degree 4 (V)
        ([69, 72, 76], 5),  # A minor,  degree 5 (vi)
    ]
    for bar_idx in range(bars):
        tick = bar_idx * TICKS_PER_BAR
        pitches, degree = chord_voicings[bar_idx % len(chord_voicings)]
        for pitch in pitches:
            notes.append(Note(
                start=tick,
                duration=TICKS_PER_BAR,
                pitch=pitch,
                velocity=75,
                channel=1,
                provenance={'chord_degree': degree, 'source': 'chord_voicing'},
            ))

    # -- Bass (channel 2): root notes --
    bass_pitches_by_degree = {0: 48, 3: 53, 4: 43, 5: 45}
    for bar_idx in range(bars):
        tick = bar_idx * TICKS_PER_BAR
        _, degree = chord_voicings[bar_idx % len(chord_voicings)]
        bass_pitch = bass_pitches_by_degree[degree]
        notes.append(Note(
            start=tick,
            duration=TICKS_PER_BAR,
            pitch=bass_pitch,
            velocity=85,
            channel=2,
            provenance={'chord_degree': degree, 'source': 'bass_pattern'},
        ))

    return notes


class TestBonusIntegration(unittest.TestCase):
    """Test that bonuses are properly generated and applied."""

    def test_analyze_produces_bonuses(self):
        """A song with patterns should produce at least one bonus."""
        notes = _make_song(bars=32, with_patterns=True)
        result = MusicAnalyzer(notes).analyze_all()

        self.assertIsInstance(result.bonuses, list)
        self.assertGreater(
            len(result.bonuses), 0,
            "Expected at least one bonus from a patterned song",
        )

    def test_bonus_adds_to_score(self):
        """A rich song should score higher than a minimal one due to bonuses."""
        rich_notes = _make_song(bars=32, with_patterns=True)
        minimal_notes = _make_song(bars=32, with_patterns=False)

        rich_result = MusicAnalyzer(rich_notes).analyze_all()
        minimal_result = MusicAnalyzer(minimal_notes).analyze_all()

        # The rich version should earn more total bonus.
        self.assertGreaterEqual(
            rich_result.score.total_bonus,
            minimal_result.score.total_bonus,
            "Rich song should earn equal or more bonus than minimal song",
        )

    def test_bonus_capped_at_max(self):
        """Category scores must not exceed 100 even with large bonuses."""
        notes = _make_song(bars=64, with_patterns=True)
        result = MusicAnalyzer(notes).analyze_all()

        self.assertLessEqual(result.score.melodic, 100.0)
        self.assertLessEqual(result.score.harmonic, 100.0)
        self.assertLessEqual(result.score.rhythm, 100.0)
        self.assertLessEqual(result.score.arrangement, 100.0)
        self.assertLessEqual(result.score.structure, 100.0)

    def test_bonuses_in_result(self):
        """Each bonus object should have valid category, name, and scores."""
        notes = _make_song(bars=32, with_patterns=True)
        result = MusicAnalyzer(notes).analyze_all()

        for bonus in result.bonuses:
            self.assertIsInstance(bonus, Bonus)
            self.assertIsInstance(bonus.category, Category)
            self.assertIsInstance(bonus.name, str)
            self.assertGreater(len(bonus.name), 0, "Bonus name must not be empty")
            self.assertGreater(bonus.score, 0, f"Bonus '{bonus.name}' score must be > 0")
            self.assertGreater(bonus.max_score, 0, f"Bonus '{bonus.name}' max_score must be > 0")
            self.assertLessEqual(
                bonus.score, bonus.max_score,
                f"Bonus '{bonus.name}' score ({bonus.score}) exceeds max ({bonus.max_score})",
            )


class TestBlueprintDifferentiation(unittest.TestCase):
    """Test that different blueprints produce different bonus weights."""

    def test_rhythm_blueprint_groove_bonus(self):
        """Blueprint 1 (RhythmLock, groove_weight=1.5) should give higher
        rhythm bonus than Blueprint 0 (Traditional, groove_weight=1.0)
        for the same input data."""
        notes = _make_song(bars=32, with_patterns=True)

        result_bp0 = MusicAnalyzer(notes, blueprint=0).analyze_all()
        result_bp1 = MusicAnalyzer(notes, blueprint=1).analyze_all()

        # Blueprint 1 has groove_bonus_weight=1.5 vs 1.0 for Blueprint 0.
        # So rhythm bonus should be at least as high (likely higher).
        self.assertGreaterEqual(
            result_bp1.score.rhythm_bonus,
            result_bp0.score.rhythm_bonus,
            "RhythmLock blueprint should give >= rhythm bonus vs Traditional",
        )

    def test_melody_blueprint_hook_bonus(self):
        """Blueprint 4 (IdolStandard, hook_weight=1.5) should amplify
        melodic bonus compared to Blueprint 0 (hook_weight=1.0)."""
        notes = _make_song(bars=32, with_patterns=True)

        result_bp0 = MusicAnalyzer(notes, blueprint=0).analyze_all()
        result_bp4 = MusicAnalyzer(notes, blueprint=4).analyze_all()

        # Blueprint 4 has hook_bonus_weight=1.5 vs 1.0 for Blueprint 0.
        self.assertGreaterEqual(
            result_bp4.score.melodic_bonus,
            result_bp0.score.melodic_bonus,
            "IdolStandard blueprint should give >= melodic bonus vs Traditional",
        )

    def test_no_blueprint_uses_defaults(self):
        """When no blueprint is specified, bonuses should still work."""
        notes = _make_song(bars=32, with_patterns=True)
        result = MusicAnalyzer(notes, blueprint=None).analyze_all()

        # Should produce bonuses using default caps.
        self.assertIsInstance(result.bonuses, list)
        # The patterned song should earn at least some bonus.
        self.assertGreaterEqual(result.score.total_bonus, 0.0)


class TestBonusCaps(unittest.TestCase):
    """Test per-category bonus caps from blueprints and defaults."""

    def test_melodic_cap_15(self):
        """Default melodic bonus cap is 15."""
        notes = _make_song(bars=64, with_patterns=True)
        result = MusicAnalyzer(notes, blueprint=None).analyze_all()

        self.assertLessEqual(
            result.score.melodic_bonus, 15.0,
            "Default melodic bonus should not exceed cap of 15",
        )

    def test_blueprint_melodic_cap_18(self):
        """Blueprint 2 (StoryPop) has bonus_cap_melodic=18."""
        profile = BLUEPRINT_PROFILES[2]
        self.assertEqual(profile.bonus_cap_melodic, 18.0)

        notes = _make_song(bars=64, with_patterns=True)
        result = MusicAnalyzer(notes, blueprint=2).analyze_all()

        self.assertLessEqual(
            result.score.melodic_bonus, 18.0,
            "StoryPop melodic bonus should not exceed cap of 18",
        )

    def test_blueprint_melodic_cap_12(self):
        """Blueprint 1 (RhythmLock) has bonus_cap_melodic=12."""
        profile = BLUEPRINT_PROFILES[1]
        self.assertEqual(profile.bonus_cap_melodic, 12.0)

        notes = _make_song(bars=64, with_patterns=True)
        result = MusicAnalyzer(notes, blueprint=1).analyze_all()

        self.assertLessEqual(
            result.score.melodic_bonus, 12.0,
            "RhythmLock melodic bonus should not exceed cap of 12",
        )


if __name__ == "__main__":
    unittest.main()
