"""Tests for quality scoring system."""

import unittest

from conftest import (
    Category,
    Issue,
    Note,
    MusicAnalyzer,
    Severity,
    TICKS_PER_BAR,
    TICKS_PER_BEAT,
)


class TestScoring(unittest.TestCase):
    """Test quality scoring."""

    def test_perfect_score(self):
        """Well-formed music should have high score."""
        notes = []
        melody = [60, 62, 64, 65, 67, 65, 64, 62]
        for i, pitch in enumerate(melody):
            notes.append(Note(start=i * TICKS_PER_BEAT * 2, duration=TICKS_PER_BEAT,
                              pitch=pitch, velocity=100, channel=0))
        chords = [[60, 64, 67], [62, 65, 69], [64, 67, 71], [65, 69, 72]]
        for i, chord in enumerate(chords):
            for p in chord:
                notes.append(Note(start=i * TICKS_PER_BAR, duration=TICKS_PER_BAR,
                                  pitch=p, velocity=80, channel=1))
        bass = [48, 50, 52, 53]
        for i, pitch in enumerate(bass):
            notes.append(Note(start=i * TICKS_PER_BAR, duration=TICKS_PER_BAR,
                              pitch=pitch, velocity=80, channel=2))

        result = MusicAnalyzer(notes).analyze_all()

        self.assertGreater(result.score.overall, 50)

    def test_error_lowers_score(self):
        """Errors should significantly lower score."""
        notes = [
            Note(start=0, duration=TICKS_PER_BAR, pitch=36, velocity=80, channel=2),
            Note(start=0, duration=TICKS_PER_BAR, pitch=37, velocity=80, channel=1),
        ]

        result = MusicAnalyzer(notes).analyze_all()

        self.assertLess(result.score.harmonic, 100)

    def test_new_arrangement_subcategories_have_explicit_penalties(self):
        """Important arrangement issues should not fall back to default weights."""
        analyzer = MusicAnalyzer([])
        analyzer.issues = [
            Issue(Severity.WARNING, Category.ARRANGEMENT, "lead_dominance", "", 0),
            Issue(Severity.WARNING, Category.ARRANGEMENT, "unintended_solo_spotlight", "", 0),
        ]

        score = analyzer._calculate_scores([])

        self.assertEqual(score.details["arrangement_penalty"], 2.5)

    def test_section_pause_balance_has_explicit_penalty(self):
        """Section pause balance should not fall back to default weights."""
        analyzer = MusicAnalyzer([])
        analyzer.issues = [
            Issue(Severity.WARNING, Category.STRUCTURE, "section_pause_balance", "", 0),
        ]

        score = analyzer._calculate_scores([])

        self.assertEqual(score.details["structure_penalty"], 1.0)


if __name__ == "__main__":
    unittest.main()
