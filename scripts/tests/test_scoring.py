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
from music_analyzer.blueprints import BLUEPRINT_PROFILES
from music_analyzer.models import QualityScore


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

    def test_metadata_free_analysis_uses_traditional_profile(self):
        analyzer = MusicAnalyzer([])
        self.assertIs(analyzer.profile, BLUEPRINT_PROFILES[0])

        score = QualityScore(melodic=100, harmonic=100, rhythm=0,
                             arrangement=0, structure=100)
        score.calculate_overall()
        self.assertEqual(score.overall, 60.0)

    def test_metadata_blueprint_selects_its_profile(self):
        analyzer = MusicAnalyzer([], metadata={"blueprint": 1})
        self.assertIs(analyzer.profile, BLUEPRINT_PROFILES[1])

    def test_same_issue_set_has_same_score_at_any_note_density(self):
        issue = Issue(Severity.WARNING, Category.HARMONIC, "dissonance", "", 0)
        sparse = MusicAnalyzer([])
        sparse.issues = [issue]
        dense_notes = [
            Note(start=index * TICKS_PER_BEAT, duration=TICKS_PER_BEAT,
                 pitch=60, velocity=80, channel=0)
            for index in range(2_500)
        ]
        dense = MusicAnalyzer(dense_notes)
        dense.issues = [issue]

        sparse_score = sparse._calculate_scores([])
        dense_score = dense._calculate_scores([])
        self.assertEqual(sparse_score.harmonic, dense_score.harmonic)
        self.assertEqual(sparse_score.details["penalty_normalization"], "none")


if __name__ == "__main__":
    unittest.main()
