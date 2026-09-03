"""Tests for vertical dissonance classification and the checks built on it.

Two separate concerns: the classification must agree with the shipped analyzer
over the whole domain, and the harmonic checks must be able to surface a clash
the generator's placement path cannot prevent.
"""

import unittest

from conftest import Note, MusicAnalyzer, Severity, TICKS_PER_BAR, TICKS_PER_BEAT
from music_analyzer.dissonance_rules import (
    WIDE_SEPARATION_SEMITONES,
    classify_interval_dissonance,
    interval_name,
)


class TestIntervalClassification(unittest.TestCase):
    """The interval/degree to severity mapping, case by case.

    Expectations are stated from the rules themselves rather than copied from
    the implementation: a minor 2nd and its compound beat hardest, a major 2nd
    only beats in close position, a major 7th is a colour on I and IV, a
    tritone belongs to V and vii, and two octaves of separation ends it.
    """

    def test_minor_second_and_minor_ninth_are_the_harshest(self):
        for degree in range(7):
            self.assertEqual(classify_interval_dissonance(1, degree), Severity.ERROR)
            self.assertEqual(classify_interval_dissonance(13, degree), Severity.ERROR)

    def test_close_major_second_beats_but_the_major_ninth_does_not(self):
        self.assertEqual(classify_interval_dissonance(2, 0), Severity.ERROR)
        self.assertIsNone(classify_interval_dissonance(14, 0))
        self.assertIsNone(classify_interval_dissonance(26, 0))

    def test_major_seventh_is_a_colour_on_tonic_and_subdominant(self):
        self.assertEqual(classify_interval_dissonance(11, 0), Severity.WARNING)
        self.assertEqual(classify_interval_dissonance(11, 3), Severity.WARNING)
        for degree in (1, 2, 4, 5, 6):
            self.assertEqual(classify_interval_dissonance(11, degree), Severity.ERROR)

    def test_compound_major_seventh_is_only_notable(self):
        self.assertEqual(classify_interval_dissonance(23, 5), Severity.INFO)

    def test_tritone_belongs_to_the_dominant_and_the_leading_tone_chord(self):
        for degree in (4, 6):
            self.assertIsNone(classify_interval_dissonance(6, degree))
            self.assertIsNone(classify_interval_dissonance(18, degree))
        for degree in (0, 1, 2, 3, 5):
            self.assertEqual(classify_interval_dissonance(6, degree), Severity.WARNING)
            self.assertEqual(classify_interval_dissonance(18, degree), Severity.INFO)

    def test_unknown_harmony_does_not_excuse_a_tritone(self):
        self.assertEqual(classify_interval_dissonance(6, -1), Severity.WARNING)

    def test_wide_separation_ends_the_clash(self):
        for semitones in (25, 30, 35, 36, 47):
            self.assertGreater(semitones, WIDE_SEPARATION_SEMITONES)
            for degree in range(7):
                self.assertIsNone(classify_interval_dissonance(semitones, degree))

    def test_consonant_intervals_are_never_reported(self):
        for semitones in (0, 3, 4, 5, 7, 8, 9, 10, 12, 15, 16, 17, 19, 24):
            for degree in range(7):
                self.assertIsNone(
                    classify_interval_dissonance(semitones, degree),
                    f"{semitones} semitones over degree {degree}",
                )

    def test_interval_names(self):
        self.assertEqual(interval_name(1), "minor 2nd")
        self.assertEqual(interval_name(13), "minor 9th")
        self.assertEqual(interval_name(2), "major 2nd")
        self.assertEqual(interval_name(6), "tritone")
        self.assertEqual(interval_name(11), "major 7th")


class TestDissonanceAfterChordChange(unittest.TestCase):
    """A pair legal where it starts can become a clash where it ends.

    Placement judges a note against the chord sounding when it starts, so this
    is the shape of clash the generator structurally cannot rule out.
    """

    @staticmethod
    def _metadata():
        # Bar 1 is V, bar 2 is vi.
        return {
            "chords": [
                {"tick": 0, "endTick": TICKS_PER_BAR, "degree": 4},
                {"tick": TICKS_PER_BAR, "endTick": 2 * TICKS_PER_BAR, "degree": 5},
            ]
        }

    @staticmethod
    def _sustained_tritone():
        # B4 against F5 is the tritone that defines G7; it starts inside the V
        # bar and sustains into the vi bar, where nothing explains it.
        return [
            Note(start=TICKS_PER_BEAT * 2, duration=TICKS_PER_BAR, pitch=71,
                 velocity=80, channel=1),
            Note(start=TICKS_PER_BEAT * 2, duration=TICKS_PER_BAR, pitch=77,
                 velocity=80, channel=3),
        ]

    def _issues(self, notes, metadata, subcategory):
        result = MusicAnalyzer(notes, metadata=metadata).analyze_all()
        return [issue for issue in result.issues if issue.subcategory == subcategory]

    def test_tritone_sustained_into_the_next_chord_is_reported(self):
        issues = self._issues(self._sustained_tritone(), self._metadata(),
                              "dissonance_after_chord_change")

        self.assertEqual(len(issues), 1)
        self.assertEqual(issues[0].tick, TICKS_PER_BAR)
        self.assertEqual(issues[0].details["interval"], "tritone")
        self.assertEqual(issues[0].details["chord_degree"], 5)

    def test_the_same_pair_is_not_double_reported_as_a_plain_clash(self):
        issues = self._issues(self._sustained_tritone(), self._metadata(),
                              "dissonance")

        self.assertEqual(issues, [], "the tritone is legal where it starts")

    def test_a_tritone_that_stays_inside_the_dominant_is_left_alone(self):
        notes = [
            Note(start=0, duration=TICKS_PER_BEAT, pitch=71, velocity=80, channel=1),
            Note(start=0, duration=TICKS_PER_BEAT, pitch=77, velocity=80, channel=3),
        ]

        self.assertEqual(
            self._issues(notes, self._metadata(), "dissonance_after_chord_change"), []
        )
        self.assertEqual(self._issues(notes, self._metadata(), "dissonance"), [])

    def test_a_clash_present_from_the_start_is_reported_by_the_interval_check(self):
        notes = [
            Note(start=0, duration=TICKS_PER_BEAT, pitch=71, velocity=80, channel=1),
            Note(start=0, duration=TICKS_PER_BEAT, pitch=72, velocity=80, channel=3),
        ]

        issues = self._issues(notes, self._metadata(), "dissonance")

        self.assertEqual(len(issues), 1)
        self.assertEqual(issues[0].details["interval"], "minor 2nd")


if __name__ == '__main__':
    unittest.main()
