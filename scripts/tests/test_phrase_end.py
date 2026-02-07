"""Tests for phrase-end short note detection (A1)."""

import unittest

from conftest import (
    Note, MusicAnalyzer, Severity,
    TICKS_PER_BAR, TICKS_PER_BEAT,
    make_vocal_note,
)


class TestPhraseEndResolution(unittest.TestCase):
    """Phrase endings should have sustained notes for resolution."""

    def _make_phrase(self, start_tick, note_count, end_duration):
        """Create a phrase of notes where the last note has given duration.

        Notes are spaced at 8th-note intervals (TICKS_PER_BEAT // 2),
        with a half-beat gap after the phrase for phrase boundary detection.
        """
        notes = []
        spacing = TICKS_PER_BEAT // 2
        for i in range(note_count - 1):
            notes.append(make_vocal_note(
                start_tick + i * spacing, 60 + (i % 5),
                duration=spacing - 10,
            ))
        # Last note with specified duration
        last_start = start_tick + (note_count - 1) * spacing
        notes.append(make_vocal_note(last_start, 65, duration=end_duration))
        return notes

    def test_short_ending_detected(self):
        """Phrases ending with short notes should trigger WARNING."""
        notes = []
        # Create 3 phrases, all ending with 16th note (120 ticks)
        for phrase_idx in range(3):
            start = phrase_idx * TICKS_PER_BAR * 2
            notes.extend(self._make_phrase(start, 6, end_duration=120))

        result = MusicAnalyzer(notes).analyze_all()

        phrase_end_issues = [
            i for i in result.issues
            if i.subcategory == "phrase_end_short"
        ]
        self.assertGreater(len(phrase_end_issues), 0)

    def test_sustained_ending_no_issue(self):
        """Phrases ending with sustained notes should not trigger issue."""
        notes = []
        for phrase_idx in range(3):
            start = phrase_idx * TICKS_PER_BAR * 2
            notes.extend(self._make_phrase(
                start, 6, end_duration=TICKS_PER_BEAT,
            ))

        result = MusicAnalyzer(notes).analyze_all()

        phrase_end_issues = [
            i for i in result.issues
            if i.subcategory == "phrase_end_short"
        ]
        self.assertEqual(len(phrase_end_issues), 0)

    def test_short_phrases_skipped(self):
        """Phrases with fewer than 4 notes should not be checked."""
        # 3-note phrases with short endings
        notes = []
        for phrase_idx in range(4):
            start = phrase_idx * TICKS_PER_BAR * 2
            notes.extend(self._make_phrase(start, 3, end_duration=60))

        result = MusicAnalyzer(notes).analyze_all()

        phrase_end_issues = [
            i for i in result.issues
            if i.subcategory == "phrase_end_short"
        ]
        self.assertEqual(len(phrase_end_issues), 0)

    def test_aggregate_warning_when_most_phrases_short(self):
        """When >70% of phrases end short, an aggregate WARNING is issued."""
        notes = []
        # 5 phrases, all ending short
        for phrase_idx in range(5):
            start = phrase_idx * TICKS_PER_BAR * 2
            notes.extend(self._make_phrase(start, 6, end_duration=100))

        result = MusicAnalyzer(notes).analyze_all()

        aggregate_issues = [
            i for i in result.issues
            if i.subcategory == "phrase_end_short"
            and "Most phrases" in i.message
        ]
        self.assertGreater(len(aggregate_issues), 0)


if __name__ == "__main__":
    unittest.main()
