"""Tests for arrangement analysis: sub-melody vocal crossing detection."""

import unittest

from conftest import Note, MusicAnalyzer, TICKS_PER_BAR, TICKS_PER_BEAT


def _make_note(channel, start, duration, pitch, velocity=80):
    """Create a Note with the given parameters."""
    return Note(
        start=start, duration=duration, pitch=pitch,
        velocity=velocity, channel=channel,
    )


class TestSubmelodyVocalCrossing(unittest.TestCase):
    """Test _analyze_submelody_vocal_crossing detection."""

    def _get_crossing_issues(self, notes):
        """Run analysis and return only submelody_vocal_crossing issues."""
        result = MusicAnalyzer(notes).analyze_all()
        return [
            iss for iss in result.issues
            if iss.subcategory == "submelody_vocal_crossing"
        ]

    def test_aux_above_vocal_warning(self):
        """Aux consistently above vocal should produce WARNING (>10%)."""
        notes = []
        # Vocal at pitch 65 (F4) for 16 beats
        for beat in range(16):
            tick = beat * TICKS_PER_BEAT
            notes.append(_make_note(0, tick, TICKS_PER_BEAT, 65))
        # Aux at pitch 72 (C5) - above vocal for all 16 beats
        for beat in range(16):
            tick = beat * TICKS_PER_BEAT
            notes.append(_make_note(5, tick, TICKS_PER_BEAT, 72))

        issues = self._get_crossing_issues(notes)

        # Should have at least a crossing issue for Aux
        aux_crossing = [
            iss for iss in issues
            if iss.track == "Aux/Vocal" and "sounds above" in iss.message
        ]
        self.assertTrue(
            any(iss.severity.value == "warning" for iss in aux_crossing),
            f"Expected WARNING for Aux above vocal, got: {aux_crossing}",
        )

    def test_aux_below_vocal_no_crossing(self):
        """Aux below vocal should not produce crossing issues."""
        notes = []
        # Vocal at pitch 72 (C5) for 16 beats
        for beat in range(16):
            tick = beat * TICKS_PER_BEAT
            notes.append(_make_note(0, tick, TICKS_PER_BEAT, 72))
        # Aux at pitch 60 (C4) - below vocal
        for beat in range(16):
            tick = beat * TICKS_PER_BEAT
            notes.append(_make_note(5, tick, TICKS_PER_BEAT, 60))

        issues = self._get_crossing_issues(notes)

        # No "sounds above" issues expected
        above_issues = [
            iss for iss in issues if "sounds above" in iss.message
        ]
        self.assertEqual(len(above_issues), 0)

    def test_motif_above_vocal_warning(self):
        """Motif consistently above vocal should produce WARNING (>15%)."""
        notes = []
        # Vocal at pitch 65 (F4) for 16 beats
        for beat in range(16):
            tick = beat * TICKS_PER_BEAT
            notes.append(_make_note(0, tick, TICKS_PER_BEAT, 65))
        # Motif at pitch 74 (D5) - above vocal for all 16 beats
        for beat in range(16):
            tick = beat * TICKS_PER_BEAT
            notes.append(_make_note(3, tick, TICKS_PER_BEAT, 74))

        issues = self._get_crossing_issues(notes)

        motif_crossing = [
            iss for iss in issues
            if iss.track == "Motif/Vocal" and "sounds above" in iss.message
        ]
        self.assertTrue(
            any(iss.severity.value == "warning" for iss in motif_crossing),
            f"Expected WARNING for Motif above vocal, got: {motif_crossing}",
        )

    def test_motif_above_vocal_info_threshold(self):
        """Motif above vocal ~12% of time should produce INFO (>8% but <15%)."""
        notes = []
        total_beats = 50
        # Vocal at pitch 65 (F4) for all beats
        for beat in range(total_beats):
            tick = beat * TICKS_PER_BEAT
            notes.append(_make_note(0, tick, TICKS_PER_BEAT, 65))
        # Motif below vocal for most beats (pitch 58)
        for beat in range(total_beats):
            tick = beat * TICKS_PER_BEAT
            # ~12% above: beats 0-5 above (6 out of 50 = 12%)
            pitch = 74 if beat < 6 else 58
            notes.append(_make_note(3, tick, TICKS_PER_BEAT, pitch))

        issues = self._get_crossing_issues(notes)

        motif_above = [
            iss for iss in issues
            if iss.track == "Motif/Vocal" and "sounds above" in iss.message
        ]
        self.assertTrue(
            any(iss.severity.value == "info" for iss in motif_above),
            f"Expected INFO for Motif above vocal at ~12%, got: {motif_above}",
        )

    def test_aux_info_threshold(self):
        """Aux above vocal ~8% of time should produce INFO (>5% but <10%)."""
        notes = []
        total_beats = 50
        # Vocal at pitch 65 for all beats
        for beat in range(total_beats):
            tick = beat * TICKS_PER_BEAT
            notes.append(_make_note(0, tick, TICKS_PER_BEAT, 65))
        # Aux: 4 out of 50 beats above (8%)
        for beat in range(total_beats):
            tick = beat * TICKS_PER_BEAT
            pitch = 74 if beat < 4 else 58
            notes.append(_make_note(5, tick, TICKS_PER_BEAT, pitch))

        issues = self._get_crossing_issues(notes)

        aux_above = [
            iss for iss in issues
            if iss.track == "Aux/Vocal" and "sounds above" in iss.message
        ]
        self.assertTrue(
            any(iss.severity.value == "info" for iss in aux_above),
            f"Expected INFO for Aux above vocal at ~8%, got: {aux_above}",
        )

    def test_range_encroachment_detected(self):
        """Aux max pitch >= vocal median should flag range encroachment."""
        notes = []
        # Vocal alternating between 60 and 72 (median = 66)
        for beat in range(16):
            tick = beat * TICKS_PER_BEAT
            pitch = 60 if beat % 2 == 0 else 72
            notes.append(_make_note(0, tick, TICKS_PER_BEAT, pitch))
        # Aux with max pitch 68 (above vocal median 66), but mostly below vocal
        for beat in range(16):
            tick = beat * TICKS_PER_BEAT
            # Keep aux below concurrent vocal to avoid "sounds above" issues
            # but have a max that encroaches the vocal tessitura
            pitch = 55 if beat != 7 else 68
            notes.append(_make_note(5, tick, TICKS_PER_BEAT, pitch))

        issues = self._get_crossing_issues(notes)

        encroach = [iss for iss in issues if "encroaches" in iss.message]
        self.assertGreaterEqual(
            len(encroach), 1,
            f"Expected range encroachment issue, got: {issues}",
        )

    def test_no_issues_when_tracks_empty(self):
        """No sub-melody tracks should produce no crossing issues."""
        notes = []
        # Only vocal
        for beat in range(16):
            tick = beat * TICKS_PER_BEAT
            notes.append(_make_note(0, tick, TICKS_PER_BEAT, 65))

        issues = self._get_crossing_issues(notes)

        # May still get range encroachment but no "sounds above"
        above_issues = [
            iss for iss in issues if "sounds above" in iss.message
        ]
        self.assertEqual(len(above_issues), 0)

    def test_no_issues_without_vocal(self):
        """No vocal track should produce no crossing issues."""
        notes = []
        # Only Aux and Motif, no vocal
        for beat in range(16):
            tick = beat * TICKS_PER_BEAT
            notes.append(_make_note(5, tick, TICKS_PER_BEAT, 72))
            notes.append(_make_note(3, tick, TICKS_PER_BEAT, 74))

        issues = self._get_crossing_issues(notes)
        self.assertEqual(len(issues), 0)

    def test_non_overlapping_beats_not_counted(self):
        """Notes that do not overlap in time should not count as crossings."""
        notes = []
        # Vocal in first 8 beats
        for beat in range(8):
            tick = beat * TICKS_PER_BEAT
            notes.append(_make_note(0, tick, TICKS_PER_BEAT, 65))
        # Aux in beats 8-15 (no overlap), above vocal register
        for beat in range(8, 16):
            tick = beat * TICKS_PER_BEAT
            notes.append(_make_note(5, tick, TICKS_PER_BEAT, 80))

        issues = self._get_crossing_issues(notes)

        above_issues = [
            iss for iss in issues if "sounds above" in iss.message
        ]
        self.assertEqual(len(above_issues), 0)


if __name__ == "__main__":
    unittest.main()
