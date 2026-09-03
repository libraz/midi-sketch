"""Tests that vocal checks measure against the range the song was written for.

The generator takes the vocal range as a parameter, so a fixed window in the
analyzer either misses violations (when it is wider than the real range) or
invents them (when the caller asked for a different voice).
"""

import unittest

from conftest import Note, MusicAnalyzer, Severity, TICKS_PER_BAR, TICKS_PER_BEAT
from music_analyzer.constants import DEFAULT_VOCAL_RANGE


def _vocal(pitches, start_bar=1):
    """One vocal note per beat, from the given pitches."""
    return [
        Note(start=(start_bar - 1) * TICKS_PER_BAR + idx * TICKS_PER_BEAT,
             duration=TICKS_PER_BEAT - 60, pitch=pitch, velocity=90, channel=0)
        for idx, pitch in enumerate(pitches)
    ]


def _issues(notes, metadata, subcategory):
    result = MusicAnalyzer(notes, metadata=metadata).analyze_all()
    return [issue for issue in result.issues if issue.subcategory == subcategory]


class TestRangeViolations(unittest.TestCase):
    """Range violations are measured against the generated range."""

    def test_the_default_matches_the_generator(self):
        # src/cli/args.cpp documents --vocal-low 60 / --vocal-high 79.
        self.assertEqual(DEFAULT_VOCAL_RANGE, (60, 79))

    def test_a_note_at_the_top_of_the_default_range_is_accepted(self):
        notes = _vocal([72, 74, 76, 79, 76, 74, 72, 71])

        self.assertEqual(_issues(notes, {}, "range_high"), [])

    def test_a_note_above_the_default_range_is_reported(self):
        notes = _vocal([72, 74, 76, 80, 76, 74, 72, 71])

        issues = _issues(notes, {}, "range_high")

        self.assertEqual(len(issues), 1)
        self.assertEqual(issues[0].details["pitch"], 80)
        self.assertEqual(issues[0].details["expected_high"], 79)

    def test_a_song_written_for_a_lower_voice_is_judged_by_its_own_range(self):
        metadata = {'vocal_low': 50, 'vocal_high': 69}
        notes = _vocal([50, 55, 60, 64, 69, 64, 60, 55])

        # Every note is below the shipped default and none is a violation.
        self.assertEqual(_issues(notes, metadata, "range_low"), [])
        self.assertEqual(_issues(notes, metadata, "range_high"), [])
        self.assertEqual(len(_issues(notes, {}, "range_low")), 3)

    def test_a_violation_of_the_declared_range_is_still_reported(self):
        metadata = {'vocal_low': 50, 'vocal_high': 69}
        notes = _vocal([50, 55, 60, 71, 69, 64, 60, 55])

        issues = _issues(notes, metadata, "range_high")

        self.assertEqual(len(issues), 1)
        self.assertEqual(issues[0].details["expected_high"], 69)

    def test_a_nonsensical_declared_range_falls_back_to_the_default(self):
        metadata = {'vocal_low': 90, 'vocal_high': 40}
        notes = _vocal([72, 74, 76, 79, 76, 74, 72, 71])

        self.assertEqual(_issues(notes, metadata, "range_high"), [])
        self.assertEqual(_issues(notes, metadata, "range_low"), [])


class TestClimaxIsCountedOnce(unittest.TestCase):
    """One musical fact, at most one issue.

    The peak note sitting outside the chorus used to be reported twice, once as
    INFO and once as WARNING, so it was deducted for twice.
    """

    SECTIONS = [
        {'type': 'verse', 'name': 'A', 'start_ticks': 0,
         'end_ticks': 8 * TICKS_PER_BAR, 'start_bar': 1, 'bars': 8},
        {'type': 'chorus', 'name': 'Chorus', 'start_ticks': 8 * TICKS_PER_BAR,
         'end_ticks': 16 * TICKS_PER_BAR, 'start_bar': 9, 'bars': 8},
    ]

    @staticmethod
    def _peak_in_the_verse():
        notes = _vocal([67, 69, 79, 69, 67, 65, 67, 69])          # peak in bar 1
        notes += _vocal([67, 69, 71, 69, 67, 65, 67, 69], start_bar=9)
        return notes

    def _climax_issues(self, blueprint):
        result = MusicAnalyzer(self._peak_in_the_verse(), blueprint=blueprint,
                               metadata={'sections': self.SECTIONS}).analyze_all()
        return [issue for issue in result.issues
                if 'climax' in issue.subcategory]

    def test_a_misplaced_climax_is_reported_once(self):
        # Blueprint 3 (Ballad) expects the climax in the chorus.
        issues = self._climax_issues(3)

        self.assertEqual(len(issues), 1)
        self.assertEqual(issues[0].subcategory, "climax_placement")
        self.assertEqual(issues[0].severity, Severity.WARNING)

    def test_a_blueprint_that_expects_no_particular_climax_is_not_told_off(self):
        # Blueprint 0 (Traditional) declares expected_climax_section = "any".
        self.assertEqual(self._climax_issues(0), [])


class TestTessituraCentre(unittest.TestCase):
    """The comfortable centre follows the song's range."""

    def test_a_centred_melody_is_not_flagged(self):
        notes = _vocal([69, 71, 72, 72, 71, 69, 67, 72])

        self.assertEqual(_issues(notes, {}, "tessitura"), [])

    def test_a_melody_pinned_to_the_top_of_its_range_is_flagged(self):
        notes = _vocal([77, 78, 79, 79, 78, 77, 79, 78])

        issues = _issues(notes, {}, "tessitura")

        self.assertTrue(any("Tessitura center" in issue.message for issue in issues))
        centre_issue = next(i for i in issues if "Tessitura center" in i.message)
        self.assertEqual(centre_issue.details["comfortable_high"], 76)  # E5
        self.assertEqual(centre_issue.details["comfortable_low"], 63)

    def test_the_comfortable_window_moves_with_the_declared_range(self):
        metadata = {'vocal_low': 50, 'vocal_high': 69}
        # The same pitches sit at the top of the default range and in the
        # middle of this one.
        notes = _vocal([59, 60, 61, 60, 59, 60, 61, 60])

        self.assertEqual(_issues(notes, metadata, "tessitura"), [])
        self.assertTrue(_issues(notes, {}, "tessitura"))


if __name__ == '__main__':
    unittest.main()
