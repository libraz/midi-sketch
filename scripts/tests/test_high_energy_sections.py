"""Tests that the energy checks measure the sections the generator drives hard.

A section's structural role and its energy level are two different things: the
pre-chorus is a run-up, not a chorus, but the generator counts it among the
high-energy sections. The checks that compare loud against quiet have to follow
the energy axis, or every one of them silently ignores the pre-chorus.
"""

import unittest

from conftest import Note, MusicAnalyzer, TICKS_PER_BAR, TICKS_PER_BEAT

GUITAR_CHANNEL = 6


def _sections(*names, bars=4):
    """Metadata sections of equal length, in order, from generator names."""
    out = []
    for index, name in enumerate(names):
        out.append({
            'type': name,
            'name': name,
            'start_ticks': index * bars * TICKS_PER_BAR,
            'end_ticks': (index + 1) * bars * TICKS_PER_BAR,
            'start_bar': index * bars + 1,
            'bars': bars,
        })
    return out


def _notes(section_index, channels, per_bar, velocity, bars=4, pitch=60):
    out = []
    for bar in range(bars):
        bar_start = (section_index * bars + bar) * TICKS_PER_BAR
        for channel in channels:
            for step in range(per_bar):
                out.append(Note(
                    start=bar_start + step * (TICKS_PER_BAR // max(1, per_bar)),
                    duration=TICKS_PER_BEAT // 2,
                    pitch=pitch + channel,
                    velocity=velocity,
                    channel=channel,
                ))
    return out


def _issues(notes, sections, subcategory):
    result = MusicAnalyzer(notes, metadata={'sections': sections}).analyze_all()
    return [issue for issue in result.issues if issue.subcategory == subcategory]


class TestPreChorusCountsAsTheLoudSide(unittest.TestCase):
    """A song whose only high-energy section is the pre-chorus is still judged."""

    SECTIONS = _sections('A', 'B')

    def test_energy_contrast_is_measured_against_the_pre_chorus(self):
        # The pre-chorus is quieter and sparser than the verse: the contrast
        # this check exists to find, with no section named Chorus anywhere.
        notes = (_notes(0, [0, 1, 2], per_bar=8, velocity=110)
                 + _notes(1, [0, 1, 2], per_bar=2, velocity=50))

        issues = _issues(notes, self.SECTIONS, "energy_contrast")

        self.assertEqual(len(issues), 1)
        self.assertIn("Low verse-chorus energy contrast", issues[0].message)

    def test_a_loud_pre_chorus_is_not_flagged(self):
        notes = (_notes(0, [0, 1, 2], per_bar=2, velocity=50)
                 + _notes(1, [0, 1, 2], per_bar=8, velocity=110))

        self.assertEqual(_issues(notes, self.SECTIONS, "energy_contrast"), [])

    def test_a_thin_pre_chorus_is_reported(self):
        # Two active tracks in the pre-chorus, below the three a full
        # arrangement carries.
        notes = (_notes(0, [0, 1, 2, 3], per_bar=4, velocity=90)
                 + _notes(1, [0, 1], per_bar=4, velocity=90))

        issues = _issues(notes, self.SECTIONS, "section_density")

        self.assertEqual(len(issues), 1)
        self.assertEqual(issues[0].details["active_tracks"], 2)

    def test_guitar_that_does_not_lift_into_the_pre_chorus_is_reported(self):
        notes = (_notes(0, [0, 1, 2], per_bar=4, velocity=90)
                 + _notes(1, [0, 1, 2], per_bar=4, velocity=90)
                 + _notes(0, [GUITAR_CHANNEL], per_bar=4, velocity=110)
                 + _notes(1, [GUITAR_CHANNEL], per_bar=4, velocity=60))

        issues = _issues(notes, self.SECTIONS, "guitar_dynamic_variation")

        self.assertEqual(len(issues), 1)
        self.assertGreater(issues[0].details["avg_verse_velocity"],
                           issues[0].details["avg_chorus_velocity"])

    def test_rhythm_variation_bonus_uses_the_pre_chorus_density(self):
        # Verse sparse, pre-chorus dense: the ratio the bonus rewards.
        notes = (_notes(0, [0], per_bar=2, velocity=90)
                 + _notes(1, [0], per_bar=4, velocity=90))

        result = MusicAnalyzer(notes, metadata={'sections': self.SECTIONS}).analyze_all()
        bonuses = {bonus.name: bonus for bonus in result.bonuses}

        self.assertIn("section_rhythm_variation", bonuses)
        self.assertGreater(bonuses["section_rhythm_variation"].score, 0.0)


if __name__ == '__main__':
    unittest.main()
