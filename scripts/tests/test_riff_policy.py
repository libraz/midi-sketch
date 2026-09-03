"""Tests that riff policy and rhythm-sync compliance are actually scored.

A declared policy only means something if the three values resolve to three
different verdicts, and a declared requirement only means something if the
blueprint declaring it is the one that gets checked.
"""

import unittest
from collections import defaultdict

from conftest import Note, Severity, TICKS_PER_BAR, TICKS_PER_BEAT
from music_analyzer.analyzers.arrangement import (
    RIFF_CONTOUR_MIN_SIMILARITY,
    RIFF_RHYTHM_MIN_SIMILARITY,
    ArrangementAnalyzer,
    best_window_similarity,
    riff_min_similarity,
)
from music_analyzer.blueprints import BLUEPRINT_PROFILES, BlueprintProfile

# A two-bar riff and three re-voicings of it, chosen so the contour similarity
# the analyzer computes lands on either side of each policy's bound.
_RIFF = [60, 62, 64, 65, 67, 65, 64, 62]
_KEEPS_MOST_OF_THE_SHAPE = [66, 72, 66, 60, 64, 68, 67, 66]   # similarity 0.57
_KEEPS_SOME_OF_THE_SHAPE = [64, 62, 72, 61, 69, 72, 64, 68]   # similarity 0.43
_KEEPS_ALMOST_NONE = [72, 62, 72, 68, 67, 61, 61, 65]         # similarity 0.29

_SECTIONS = [
    {'type': 'chorus', 'start_ticks': 0, 'end_ticks': 8 * TICKS_PER_BAR,
     'bars': 8, 'start_bar': 1},
    {'type': 'chorus', 'start_ticks': 8 * TICKS_PER_BAR,
     'end_ticks': 16 * TICKS_PER_BAR, 'bars': 8, 'start_bar': 9},
]


def _motif(start_tick, pitches, bars=4):
    return [
        Note(start=start_tick + bar * TICKS_PER_BAR + idx * (TICKS_PER_BEAT // 2),
             duration=TICKS_PER_BEAT // 2, pitch=pitch, velocity=80, channel=3)
        for bar in range(bars)
        for idx, pitch in enumerate(pitches)
    ]


def _analyzer(notes, profile, metadata=None):
    notes = sorted(notes, key=lambda note: (note.start, note.pitch))
    by_channel = defaultdict(list)
    for note in notes:
        by_channel[note.channel].append(note)
    return ArrangementAnalyzer(notes=notes, notes_by_channel=by_channel,
                               profile=profile, metadata=metadata or {})


def _contour_issues(second_riff, policy):
    notes = _motif(0, _RIFF) + _motif(8 * TICKS_PER_BAR, second_riff)
    analyzer = _analyzer(notes, BlueprintProfile("Test", "RhythmSync", policy),
                         {'sections': _SECTIONS})
    analyzer._analyze_motif_contour_preservation()
    return [issue for issue in analyzer.issues
            if issue.subcategory == "motif_contour_preservation"]


class TestRiffPolicyBounds(unittest.TestCase):
    """The three policies resolve to three ordered bounds on both axes."""

    def test_each_axis_separates_every_policy(self):
        for axis, bounds in (("contour", RIFF_CONTOUR_MIN_SIMILARITY),
                             ("rhythm", RIFF_RHYTHM_MIN_SIMILARITY)):
            with self.subTest(axis=axis):
                self.assertEqual(set(bounds),
                                 {"LockedPitch", "Locked", "Evolving", "Free"})
                self.assertEqual(len(set(bounds.values())), len(bounds),
                                 "a shared bound cannot tell the policies apart")
                self.assertGreater(bounds["LockedPitch"], bounds["Locked"])
                self.assertGreater(bounds["Locked"], bounds["Evolving"])
                self.assertGreater(bounds["Evolving"], bounds["Free"])

    def test_verbatim_policy_is_the_strictest(self):
        # BehavioralLoop is the only blueprint that repeats the riff verbatim,
        # so scoring it like the re-voicing policies would let it drift.
        self.assertEqual(BLUEPRINT_PROFILES[9].riff_policy, "LockedPitch")
        for bounds in (RIFF_CONTOUR_MIN_SIMILARITY, RIFF_RHYTHM_MIN_SIMILARITY):
            self.assertEqual(max(bounds.values()), bounds["LockedPitch"])

    def test_every_blueprint_declares_a_known_policy(self):
        for blueprint, profile in BLUEPRINT_PROFILES.items():
            with self.subTest(blueprint=blueprint):
                self.assertIn(profile.riff_policy, RIFF_CONTOUR_MIN_SIMILARITY)

    def test_unknown_policy_gets_the_loosest_bound(self):
        loose = riff_min_similarity(None, RIFF_CONTOUR_MIN_SIMILARITY)
        self.assertEqual(loose, RIFF_CONTOUR_MIN_SIMILARITY["Free"])


class TestContourPreservationByPolicy(unittest.TestCase):
    """The same re-voiced riff gets a different verdict per policy."""

    def test_a_locked_riff_may_not_drift_where_an_evolving_one_may(self):
        self.assertEqual(len(_contour_issues(_KEEPS_MOST_OF_THE_SHAPE, "Locked")), 1)
        self.assertEqual(_contour_issues(_KEEPS_MOST_OF_THE_SHAPE, "Evolving"), [])
        self.assertEqual(_contour_issues(_KEEPS_MOST_OF_THE_SHAPE, "Free"), [])

    def test_an_evolving_riff_may_not_drift_where_a_free_one_may(self):
        self.assertEqual(len(_contour_issues(_KEEPS_SOME_OF_THE_SHAPE, "Locked")), 1)
        self.assertEqual(len(_contour_issues(_KEEPS_SOME_OF_THE_SHAPE, "Evolving")), 1)
        self.assertEqual(_contour_issues(_KEEPS_SOME_OF_THE_SHAPE, "Free"), [])

    def test_a_riff_that_is_gone_is_reported_under_every_policy(self):
        for policy in ("Locked", "Evolving", "Free"):
            with self.subTest(policy=policy):
                self.assertEqual(len(_contour_issues(_KEEPS_ALMOST_NONE, policy)), 1)

    def test_an_unchanged_riff_is_never_reported(self):
        for policy in ("Locked", "Evolving", "Free"):
            with self.subTest(policy=policy):
                self.assertEqual(_contour_issues(_RIFF, policy), [])


class TestSectionLengthDoesNotCountAsRiffChange(unittest.TestCase):
    """A riff that runs longer is still the same riff.

    Sections of the same type differ in length, so comparing the whole stream
    charges the difference in length as a difference in rhythm.
    """

    def test_a_pattern_repeated_twice_matches_the_original(self):
        cell = [2, 2, 4, 2, 2, 4]

        self.assertEqual(best_window_similarity(cell, cell + cell), 1.0)
        self.assertEqual(best_window_similarity(cell + cell, cell), 1.0)

    def test_a_pattern_found_later_in_the_longer_one_matches(self):
        cell = [2, 2, 4]

        self.assertEqual(best_window_similarity(cell, [8, 8] + cell + [8]), 1.0)

    def test_identical_patterns_match(self):
        self.assertEqual(best_window_similarity([1, 2, 3], [1, 2, 3]), 1.0)

    def test_an_unrelated_pattern_does_not_match(self):
        self.assertLess(best_window_similarity([1, 1, 1, 1], [7, 9, 5, 3]), 0.5)

    def test_an_empty_pattern_matches_nothing(self):
        self.assertEqual(best_window_similarity([], [1, 2, 3]), 0.0)
        self.assertEqual(best_window_similarity([1, 2, 3], []), 0.0)

    def test_a_longer_section_holding_the_same_riff_is_not_reported(self):
        # An 8-bar chorus and a 16-bar chorus carrying the same riff. Comparing
        # the streams whole scores this 0.51 purely on length.
        sections = [
            {'type': 'chorus', 'start_ticks': 0, 'end_ticks': 8 * TICKS_PER_BAR,
             'bars': 8, 'start_bar': 1},
            {'type': 'chorus', 'start_ticks': 8 * TICKS_PER_BAR,
             'end_ticks': 24 * TICKS_PER_BAR, 'bars': 16, 'start_bar': 9},
        ]
        notes = _motif(0, _RIFF, bars=8) + _motif(8 * TICKS_PER_BAR, _RIFF, bars=16)
        analyzer = _analyzer(notes, BlueprintProfile("Test", "RhythmSync", "Locked"),
                             {'sections': sections})

        analyzer._analyze_motif_rhythm_preservation()

        self.assertEqual(
            [issue for issue in analyzer.issues
             if issue.subcategory == "motif_rhythm_preservation"],
            [],
        )

    def test_a_longer_section_with_a_different_riff_is_still_reported(self):
        sections = [
            {'type': 'chorus', 'start_ticks': 0, 'end_ticks': 8 * TICKS_PER_BAR,
             'bars': 8, 'start_bar': 1},
            {'type': 'chorus', 'start_ticks': 8 * TICKS_PER_BAR,
             'end_ticks': 24 * TICKS_PER_BAR, 'bars': 16, 'start_bar': 9},
        ]
        # Second chorus: same pitches, but every onset moved onto a triplet-ish
        # grid so the rhythm no longer matches.
        notes = _motif(0, _RIFF, bars=8)
        for bar in range(16):
            bar_start = (8 + bar) * TICKS_PER_BAR
            for idx, pitch in enumerate(_RIFF):
                notes.append(Note(start=bar_start + idx * 160, duration=160,
                                  pitch=pitch, velocity=80, channel=3))
        analyzer = _analyzer(notes, BlueprintProfile("Test", "RhythmSync", "Locked"),
                             {'sections': sections})

        analyzer._analyze_motif_rhythm_preservation()

        self.assertEqual(
            len([issue for issue in analyzer.issues
                 if issue.subcategory == "motif_rhythm_preservation"]),
            1,
        )


class TestRhythmSyncIsCheckedWhereItIsDeclared(unittest.TestCase):
    """A blueprint is not exempt from the requirement it declares."""

    @staticmethod
    def _out_of_sync_notes(bars=8):
        notes = []
        for bar in range(bars):
            bar_tick = bar * TICKS_PER_BAR
            # Vocal on the beats, motif on the off-beats: nothing lines up.
            for beat in range(4):
                notes.append(Note(start=bar_tick + beat * TICKS_PER_BEAT,
                                  duration=TICKS_PER_BEAT // 2, pitch=72,
                                  velocity=90, channel=0))
                notes.append(Note(start=bar_tick + beat * TICKS_PER_BEAT
                                  + TICKS_PER_BEAT // 2,
                                  duration=TICKS_PER_BEAT // 2, pitch=60,
                                  velocity=80, channel=3))
        return notes

    def _sync_issues(self, profile):
        analyzer = _analyzer(self._out_of_sync_notes(), profile)
        analyzer._analyze_blueprint_rhythm_sync()
        return [issue for issue in analyzer.issues
                if issue.subcategory == "rhythm_sync"]

    def test_rhythmlock_is_checked_like_its_siblings(self):
        # Blueprint 1 is the profile that used to be skipped by name.
        issues = self._sync_issues(BLUEPRINT_PROFILES[1])

        self.assertEqual(len(issues), 1)
        self.assertEqual(issues[0].severity, Severity.WARNING)

    def test_every_blueprint_declaring_the_requirement_is_checked(self):
        declaring = [bp for bp, profile in BLUEPRINT_PROFILES.items()
                     if profile.rhythm_sync_required]
        self.assertTrue(declaring)
        for blueprint in declaring:
            with self.subTest(blueprint=blueprint):
                self.assertEqual(len(self._sync_issues(BLUEPRINT_PROFILES[blueprint])), 1)

    def test_blueprints_that_do_not_declare_it_are_left_alone(self):
        for blueprint, profile in BLUEPRINT_PROFILES.items():
            if profile.rhythm_sync_required:
                continue
            with self.subTest(blueprint=blueprint):
                self.assertEqual(self._sync_issues(profile), [])

    def test_paradigm_correlation_runs_for_every_rhythmsync_blueprint(self):
        for blueprint, profile in BLUEPRINT_PROFILES.items():
            if profile.paradigm != "RhythmSync":
                continue
            with self.subTest(blueprint=blueprint):
                analyzer = _analyzer(self._out_of_sync_notes(), profile)
                analyzer._analyze_blueprint_paradigm()
                issues = [issue for issue in analyzer.issues
                          if issue.subcategory == "blueprint_paradigm"]
                self.assertEqual(len(issues), 1)


if __name__ == '__main__':
    unittest.main()
