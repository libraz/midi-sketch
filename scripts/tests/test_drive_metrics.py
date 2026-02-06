"""Tests for rhythm drive metrics analysis (uptempo forward momentum)."""

import unittest

from conftest import (
    Note, MusicAnalyzer, Severity,
    TICKS_PER_BAR, TICKS_PER_BEAT,
    make_bass_note, make_chord_notes,
)


def _make_sections(chorus_start_bar, chorus_end_bar):
    """Create metadata sections list with a Chorus."""
    return [
        {
            'type': 'verse',
            'start_bar': 1,
            'end_bar': chorus_start_bar - 1,
        },
        {
            'type': 'chorus',
            'start_bar': chorus_start_bar,
            'end_bar': chorus_end_bar,
        },
    ]


def _make_kick(tick, velocity=100):
    """Create a kick drum note (pitch 36, channel 9)."""
    return Note(
        start=tick, duration=120, pitch=36,
        velocity=velocity, channel=9,
    )


def _make_snare(tick, velocity=100):
    """Create a snare drum note (pitch 38, channel 9)."""
    return Note(
        start=tick, duration=120, pitch=38,
        velocity=velocity, channel=9,
    )


class TestDriveMetricsBpmGate(unittest.TestCase):
    """Drive metrics should only run for BPM >= 140."""

    def test_skipped_at_low_bpm(self):
        """No drive_deficit issues should appear when BPM < 140."""
        # All on-beat quarter-note bass in chorus -- would trigger at 140+
        sections = _make_sections(5, 12)
        chorus_start = (5 - 1) * TICKS_PER_BAR
        notes = []
        for bar in range(5, 13):
            for beat in range(4):
                tick = (bar - 1) * TICKS_PER_BAR + beat * TICKS_PER_BEAT
                notes.append(make_bass_note(tick, 36, duration=TICKS_PER_BEAT))

        result = MusicAnalyzer(
            notes, metadata={'bpm': 120, 'sections': sections}
        ).analyze_all()

        drive_issues = [
            idx for idx in result.issues if idx.subcategory == "drive_deficit"
        ]
        self.assertEqual(len(drive_issues), 0)

    def test_runs_at_140_bpm(self):
        """Drive_deficit analysis should activate at BPM == 140."""
        sections = _make_sections(5, 12)
        notes = []
        for bar in range(5, 13):
            for beat in range(4):
                tick = (bar - 1) * TICKS_PER_BAR + beat * TICKS_PER_BEAT
                notes.append(make_bass_note(tick, 36, duration=TICKS_PER_BEAT))

        result = MusicAnalyzer(
            notes, metadata={'bpm': 140, 'sections': sections}
        ).analyze_all()

        drive_issues = [
            idx for idx in result.issues if idx.subcategory == "drive_deficit"
        ]
        self.assertGreater(len(drive_issues), 0)


class TestBassDriveDeficit(unittest.TestCase):
    """Bass drive analysis in chorus for uptempo songs."""

    def _chorus_metadata(self):
        return {
            'bpm': 160,
            'sections': _make_sections(5, 12),
        }

    def test_all_onbeat_quarter_notes_warning(self):
        """Bass with 0% syncopation and no 8th notes triggers WARNING."""
        notes = []
        for bar in range(5, 13):
            for beat in range(4):
                tick = (bar - 1) * TICKS_PER_BAR + beat * TICKS_PER_BEAT
                notes.append(make_bass_note(tick, 36, duration=TICKS_PER_BEAT))

        result = MusicAnalyzer(
            notes, metadata=self._chorus_metadata()
        ).analyze_all()

        bass_drive = [
            idx for idx in result.issues
            if idx.subcategory == "drive_deficit"
            and idx.track == "Bass"
            and idx.severity == Severity.WARNING
        ]
        self.assertEqual(len(bass_drive), 1)
        self.assertIn("no syncopation", bass_drive[0].message)

    def test_syncopated_bass_no_warning(self):
        """Bass with >10% syncopation should not trigger drive_deficit."""
        notes = []
        for bar in range(5, 13):
            for beat in range(4):
                tick = (bar - 1) * TICKS_PER_BAR + beat * TICKS_PER_BEAT
                notes.append(make_bass_note(tick, 36, duration=TICKS_PER_BEAT // 2))
            # Add offbeat 8th note in each bar
            offbeat_tick = (bar - 1) * TICKS_PER_BAR + TICKS_PER_BEAT // 2
            notes.append(make_bass_note(offbeat_tick, 38, duration=TICKS_PER_BEAT // 2))

        result = MusicAnalyzer(
            notes, metadata=self._chorus_metadata()
        ).analyze_all()

        bass_drive = [
            idx for idx in result.issues
            if idx.subcategory == "drive_deficit" and idx.track == "Bass"
        ]
        self.assertEqual(len(bass_drive), 0)

    def test_low_syncopation_info(self):
        """Bass with 5-10% syncopation triggers INFO, not WARNING."""
        notes = []
        note_count = 0
        for bar in range(5, 13):
            for beat in range(4):
                tick = (bar - 1) * TICKS_PER_BAR + beat * TICKS_PER_BEAT
                notes.append(make_bass_note(tick, 36, duration=TICKS_PER_BEAT // 2))
                note_count += 1
        # Add exactly enough offbeat notes to hit ~6-7% syncopation
        # Need >5% and <10% of total. With 32 on-beat notes, add 2 offbeat = 2/34 ~ 5.8%
        for idx in range(2):
            bar = 5 + idx
            offbeat_tick = (bar - 1) * TICKS_PER_BAR + TICKS_PER_BEAT // 2
            notes.append(make_bass_note(offbeat_tick, 38, duration=TICKS_PER_BEAT // 2))

        result = MusicAnalyzer(
            notes, metadata=self._chorus_metadata()
        ).analyze_all()

        bass_drive = [
            idx for idx in result.issues
            if idx.subcategory == "drive_deficit" and idx.track == "Bass"
        ]
        info_issues = [idx for idx in bass_drive if idx.severity == Severity.INFO]
        warning_issues = [idx for idx in bass_drive if idx.severity == Severity.WARNING]
        self.assertGreater(len(info_issues), 0)
        self.assertEqual(len(warning_issues), 0)

    def test_eighth_note_bass_no_warning(self):
        """Bass with >10% 8th notes should not get 'no 8th notes' warning."""
        notes = []
        for bar in range(5, 13):
            for beat in range(4):
                tick = (bar - 1) * TICKS_PER_BAR + beat * TICKS_PER_BEAT
                # Half are 8th notes
                dur = TICKS_PER_BEAT // 2 if beat % 2 == 0 else TICKS_PER_BEAT
                notes.append(make_bass_note(tick, 36, duration=dur))

        result = MusicAnalyzer(
            notes, metadata=self._chorus_metadata()
        ).analyze_all()

        bass_warnings = [
            idx for idx in result.issues
            if idx.subcategory == "drive_deficit"
            and idx.track == "Bass"
            and idx.severity == Severity.WARNING
        ]
        self.assertEqual(len(bass_warnings), 0)


class TestChordDriveDeficit(unittest.TestCase):
    """Chord pad-like detection in chorus for uptempo songs."""

    def _chorus_metadata(self):
        return {
            'bpm': 150,
            'sections': _make_sections(5, 12),
        }

    def test_padlike_chord_info(self):
        """Chord with >80% long notes triggers INFO."""
        notes = []
        for bar in range(5, 13):
            tick = (bar - 1) * TICKS_PER_BAR
            # Whole-bar chord (duration > TICKS_PER_BEAT)
            notes.extend(make_chord_notes(tick, [60, 64, 67], duration=TICKS_PER_BAR))

        result = MusicAnalyzer(
            notes, metadata=self._chorus_metadata()
        ).analyze_all()

        chord_drive = [
            idx for idx in result.issues
            if idx.subcategory == "drive_deficit" and idx.track == "Chord"
        ]
        self.assertEqual(len(chord_drive), 1)
        self.assertEqual(chord_drive[0].severity, Severity.INFO)
        self.assertIn("pad-like", chord_drive[0].message)

    def test_rhythmic_chord_no_issue(self):
        """Chord with mostly short notes should not be flagged."""
        notes = []
        for bar in range(5, 13):
            for beat in range(4):
                tick = (bar - 1) * TICKS_PER_BAR + beat * TICKS_PER_BEAT
                # 8th note chords
                notes.extend(
                    make_chord_notes(tick, [60, 64, 67], duration=TICKS_PER_BEAT // 2)
                )

        result = MusicAnalyzer(
            notes, metadata=self._chorus_metadata()
        ).analyze_all()

        chord_drive = [
            idx for idx in result.issues
            if idx.subcategory == "drive_deficit" and idx.track == "Chord"
        ]
        self.assertEqual(len(chord_drive), 0)


class TestKickDriveDeficit(unittest.TestCase):
    """Kick drum drive analysis in chorus for uptempo songs."""

    def _chorus_metadata(self, bpm=160):
        return {
            'bpm': bpm,
            'sections': _make_sections(5, 12),
        }

    def test_sparse_kick_warning(self):
        """Kick < 2.0/bar triggers WARNING."""
        notes = []
        # Only 1 kick per bar across 8 bars = 1.0/bar
        for bar in range(5, 13):
            tick = (bar - 1) * TICKS_PER_BAR
            notes.append(_make_kick(tick))

        result = MusicAnalyzer(
            notes, metadata=self._chorus_metadata()
        ).analyze_all()

        kick_drive = [
            idx for idx in result.issues
            if idx.subcategory == "drive_deficit"
            and idx.track == "Drums"
            and idx.severity == Severity.WARNING
        ]
        self.assertEqual(len(kick_drive), 1)
        self.assertIn("sparse", kick_drive[0].message)

    def test_moderate_kick_info(self):
        """Kick between 2.0 and 3.0/bar triggers INFO."""
        notes = []
        # 2 kicks per bar = 2.0/bar (but need >2.0 to not be WARNING)
        # Use 2.5/bar: 20 kicks across 8 bars
        for bar in range(5, 13):
            tick = (bar - 1) * TICKS_PER_BAR
            notes.append(_make_kick(tick))
            notes.append(_make_kick(tick + TICKS_PER_BEAT * 2))
        # Add 4 more kicks to get 2.5/bar
        for bar_offset in range(4):
            tick = (5 + bar_offset - 1) * TICKS_PER_BAR + TICKS_PER_BEAT * 3
            notes.append(_make_kick(tick))

        result = MusicAnalyzer(
            notes, metadata=self._chorus_metadata()
        ).analyze_all()

        kick_drive = [
            idx for idx in result.issues
            if idx.subcategory == "drive_deficit"
            and idx.track == "Drums"
        ]
        info_issues = [idx for idx in kick_drive if idx.severity == Severity.INFO]
        warning_issues = [idx for idx in kick_drive if idx.severity == Severity.WARNING]
        self.assertGreater(len(info_issues), 0)
        self.assertEqual(len(warning_issues), 0)

    def test_dense_kick_no_issue(self):
        """Kick >= 3.0/bar should not trigger density issue."""
        notes = []
        # 4 kicks per bar (four-on-the-floor)
        for bar in range(5, 13):
            for beat in range(4):
                tick = (bar - 1) * TICKS_PER_BAR + beat * TICKS_PER_BEAT
                notes.append(_make_kick(tick))

        result = MusicAnalyzer(
            notes, metadata=self._chorus_metadata()
        ).analyze_all()

        kick_density_issues = [
            idx for idx in result.issues
            if idx.subcategory == "drive_deficit"
            and idx.track == "Drums"
            and ("sparse" in idx.message or "density" in idx.message.lower())
        ]
        self.assertEqual(len(kick_density_issues), 0)

    def test_kick_no_offbeat_at_150plus_info(self):
        """All on-beat kick at BPM >= 150 triggers offbeat drive INFO."""
        notes = []
        # 4 kicks per bar, all on beat (0% syncopation)
        for bar in range(5, 13):
            for beat in range(4):
                tick = (bar - 1) * TICKS_PER_BAR + beat * TICKS_PER_BEAT
                notes.append(_make_kick(tick))

        result = MusicAnalyzer(
            notes, metadata=self._chorus_metadata(bpm=155)
        ).analyze_all()

        offbeat_issues = [
            idx for idx in result.issues
            if idx.subcategory == "drive_deficit"
            and idx.track == "Drums"
            and "offbeat" in idx.message
        ]
        self.assertEqual(len(offbeat_issues), 1)

    def test_kick_offbeat_no_issue_below_150(self):
        """All on-beat kick at BPM 140-149 should not trigger offbeat INFO."""
        notes = []
        for bar in range(5, 13):
            for beat in range(4):
                tick = (bar - 1) * TICKS_PER_BAR + beat * TICKS_PER_BEAT
                notes.append(_make_kick(tick))

        result = MusicAnalyzer(
            notes, metadata=self._chorus_metadata(bpm=145)
        ).analyze_all()

        offbeat_issues = [
            idx for idx in result.issues
            if idx.subcategory == "drive_deficit"
            and idx.track == "Drums"
            and "offbeat" in idx.message
        ]
        self.assertEqual(len(offbeat_issues), 0)

    def test_kick_with_syncopation_no_offbeat_issue(self):
        """Kick with >10% offbeat notes should not trigger offbeat INFO."""
        notes = []
        for bar in range(5, 13):
            # Beats 1 and 3 on beat
            for beat in [0, 2]:
                tick = (bar - 1) * TICKS_PER_BAR + beat * TICKS_PER_BEAT
                notes.append(_make_kick(tick))
            # Offbeat kick between beat 2 and 3
            offbeat_tick = (bar - 1) * TICKS_PER_BAR + TICKS_PER_BEAT + TICKS_PER_BEAT // 2
            notes.append(_make_kick(offbeat_tick))

        result = MusicAnalyzer(
            notes, metadata=self._chorus_metadata(bpm=160)
        ).analyze_all()

        offbeat_issues = [
            idx for idx in result.issues
            if idx.subcategory == "drive_deficit"
            and idx.track == "Drums"
            and "offbeat" in idx.message
        ]
        self.assertEqual(len(offbeat_issues), 0)


class TestDriveMetricsSectionFallback(unittest.TestCase):
    """Drive metrics should fall back to estimated sections."""

    def test_no_metadata_sections_uses_estimated(self):
        """When metadata has no sections, estimated sections are used."""
        # Create enough notes to generate estimated chorus sections
        notes = []
        # Vocal notes with high density/velocity for bars 9-16 (likely estimated as chorus)
        for bar in range(1, 25):
            for beat in range(4):
                tick = (bar - 1) * TICKS_PER_BAR + beat * TICKS_PER_BEAT
                vel = 110 if 9 <= bar <= 16 else 60
                notes.append(Note(
                    start=tick, duration=TICKS_PER_BEAT,
                    pitch=65, velocity=vel, channel=0,
                ))
                # On-beat quarter bass in all bars
                notes.append(make_bass_note(tick, 36, duration=TICKS_PER_BEAT))

        result = MusicAnalyzer(
            notes, metadata={'bpm': 160}
        ).analyze_all()

        # Should still detect drive deficit in the estimated chorus
        drive_issues = [
            idx for idx in result.issues if idx.subcategory == "drive_deficit"
        ]
        # At minimum, bass drive should be detected (all on-beat quarter notes)
        self.assertGreater(len(drive_issues), 0)


if __name__ == "__main__":
    unittest.main()
