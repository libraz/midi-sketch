"""Tests for multi-dimensional composition quality analysis."""

import unittest

from conftest import Note, MusicAnalyzer, TICKS_PER_BAR, TICKS_PER_BEAT


def _note(channel, start, duration, pitch):
    return Note(
        start=start,
        duration=duration,
        pitch=pitch,
        velocity=80,
        channel=channel,
    )


def _section(section_type, name, start_bar, end_bar):
    return {
        "type": section_type,
        "name": name,
        "start_ticks": (start_bar - 1) * TICKS_PER_BAR,
        "end_ticks": end_bar * TICKS_PER_BAR,
    }


class TestMultiDimensionalQuality(unittest.TestCase):
    """Verify lead balance, unintended solos, and pause balance checks."""

    def test_motif_overtaking_vocal_is_lead_dominance_error(self):
        notes = []
        for beat in range(32):
            tick = beat * TICKS_PER_BEAT
            notes.append(_note(0, tick, TICKS_PER_BEAT, 65))
            notes.append(_note(3, tick, TICKS_PER_BEAT, 72))

        result = MusicAnalyzer(notes).analyze_all()
        issues = [
            i for i in result.issues
            if i.subcategory == "lead_dominance" and i.track == "Motif/Vocal"
        ]

        self.assertTrue(any(i.severity.value == "error" for i in issues))
        self.assertGreaterEqual(issues[0].details["above_ratio"], 0.9)

    def test_low_motif_under_vocal_is_not_lead_dominance(self):
        notes = []
        for beat in range(32):
            tick = beat * TICKS_PER_BEAT
            notes.append(_note(0, tick, TICKS_PER_BEAT, 72))
            notes.append(_note(3, tick, TICKS_PER_BEAT, 57))

        result = MusicAnalyzer(notes).analyze_all()
        issues = [
            i for i in result.issues
            if i.subcategory == "lead_dominance" and i.track == "Motif/Vocal"
        ]

        self.assertEqual(issues, [])

    def test_high_dense_interlude_line_is_unintended_solo_spotlight(self):
        sections = [_section("Interlude", "Interlude", 1, 4)]
        notes = []
        for bar in range(4):
            bar_start = bar * TICKS_PER_BAR
            for idx in range(8):
                notes.append(_note(
                    3,
                    bar_start + idx * (TICKS_PER_BEAT // 2),
                    TICKS_PER_BEAT // 2,
                    74 + (idx % 3),
                ))

        result = MusicAnalyzer(notes, metadata={"sections": sections}).analyze_all()
        issues = [
            i for i in result.issues
            if i.subcategory == "unintended_solo_spotlight"
        ]

        self.assertTrue(any(i.severity.value == "warning" for i in issues))

    def test_low_intro_riff_is_not_unintended_solo_spotlight(self):
        sections = [_section("Intro", "Intro", 1, 4)]
        notes = []
        for bar in range(4):
            bar_start = bar * TICKS_PER_BAR
            for idx in range(4):
                notes.append(_note(
                    3,
                    bar_start + idx * TICKS_PER_BEAT,
                    TICKS_PER_BEAT,
                    55 + (idx % 5),
                ))

        result = MusicAnalyzer(notes, metadata={"sections": sections}).analyze_all()
        issues = [
            i for i in result.issues
            if i.subcategory == "unintended_solo_spotlight"
        ]

        self.assertEqual(issues, [])

    def test_bar_length_pause_before_chorus_is_flagged(self):
        sections = [
            _section("A", "A", 1, 4),
            _section("Chorus", "Chorus", 5, 8),
        ]
        notes = [
            _note(0, 0, TICKS_PER_BAR * 3, 64),
            _note(1, 0, TICKS_PER_BAR * 3, 60),
            _note(0, TICKS_PER_BAR * 4, TICKS_PER_BAR, 72),
            _note(1, TICKS_PER_BAR * 4, TICKS_PER_BAR, 60),
        ]

        result = MusicAnalyzer(notes, metadata={"sections": sections}).analyze_all()
        issues = [
            i for i in result.issues
            if i.subcategory == "section_pause_balance"
        ]

        self.assertTrue(any(i.severity.value == "warning" for i in issues))

    def test_rhythmlock_chord_pulse_variation_is_not_motif_degradation(self):
        sections = [
            _section("A", "A1", 1, 4),
            _section("A", "A2", 5, 8),
        ]
        notes = []
        first_pitches = [60, 64, 67, 64, 60, 64, 67, 64]
        second_pitches = [65, 69, 72, 69, 65, 69, 72, 69]
        for section_start_bar, pitches in [(1, first_pitches), (5, second_pitches)]:
            section_start = (section_start_bar - 1) * TICKS_PER_BAR
            for bar in range(4):
                bar_start = section_start + bar * TICKS_PER_BAR
                for idx, pitch in enumerate(pitches):
                    notes.append(_note(
                        3,
                        bar_start + idx * (TICKS_PER_BEAT // 2),
                        TICKS_PER_BEAT // 4,
                        pitch,
                    ))

        result = MusicAnalyzer(
            notes,
            blueprint=1,
            metadata={"sections": sections},
        ).analyze_all()
        issues = [
            i for i in result.issues
            if i.subcategory == "motif_degradation"
        ]

        self.assertEqual(issues, [])

    def test_rhythmlock_small_pickup_offset_is_not_grid_warning(self):
        notes = []
        for beat in range(32):
            tick = beat * TICKS_PER_BEAT + 30
            notes.append(_note(0, tick, TICKS_PER_BEAT // 2, 72))
            notes.append(_note(3, tick, TICKS_PER_BEAT // 4, 60 + (beat % 3)))

        result = MusicAnalyzer(notes, blueprint=1).analyze_all()
        issues = [
            i for i in result.issues
            if i.subcategory == "grid_strictness"
        ]

        self.assertFalse(any(i.severity.value == "warning" for i in issues))

    def test_rhythmlock_support_repeated_pitch_is_not_melodic_error(self):
        notes = [
            _note(5, idx * (TICKS_PER_BEAT // 2), TICKS_PER_BEAT // 4, 60)
            for idx in range(10)
        ]

        result = MusicAnalyzer(notes, blueprint=1).analyze_all()
        issues = [
            i for i in result.issues
            if i.subcategory == "consecutive_same_pitch" and i.track == "Aux"
        ]

        self.assertTrue(issues)
        self.assertTrue(all(i.severity.value == "info" for i in issues))


if __name__ == "__main__":
    unittest.main()
