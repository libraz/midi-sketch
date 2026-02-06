"""Tests for structure analysis: empty tracks, track balance, chorus density."""

import unittest

from conftest import Note, MusicAnalyzer, TICKS_PER_BAR, TICKS_PER_BEAT


def _make_section(section_type, name, start_bar, end_bar):
    """Create a metadata section dict from bar numbers (1-indexed)."""
    return {
        'type': section_type,
        'name': name,
        'start_ticks': (start_bar - 1) * TICKS_PER_BAR,
        'end_ticks': end_bar * TICKS_PER_BAR,
    }


def _fill_notes(start_bar, end_bar, notes_per_bar, channels=None):
    """Generate evenly spaced notes across bars on given channels.

    Returns a list of Note objects spread across the specified channels
    (default: 0-5, all non-drum melodic channels).
    """
    if channels is None:
        channels = [0, 1, 2, 3, 4, 5]
    notes = []
    for bar in range(start_bar, end_bar + 1):
        bar_start = (bar - 1) * TICKS_PER_BAR
        spacing = max(1, TICKS_PER_BAR // max(1, notes_per_bar))
        for idx in range(notes_per_bar):
            tick = bar_start + idx * spacing
            channel = channels[idx % len(channels)]
            pitch = 60 + (idx % 12)
            notes.append(Note(
                start=tick,
                duration=min(spacing, TICKS_PER_BEAT),
                pitch=pitch,
                velocity=80,
                channel=channel,
            ))
    return notes


class TestStructureAnalysis(unittest.TestCase):
    """Test structure analysis."""

    def test_empty_track_detection(self):
        """Empty melodic track should be flagged."""
        notes = [
            Note(start=0, duration=TICKS_PER_BAR, pitch=36, velocity=80, channel=2),
            Note(start=TICKS_PER_BAR, duration=TICKS_PER_BAR, pitch=36, velocity=80, channel=2),
        ]

        result = MusicAnalyzer(notes).analyze_all()

        empty_issues = [i for i in result.issues if i.subcategory == "empty_track"]
        self.assertGreaterEqual(len(empty_issues), 3)


class TestChorusDensityInversion(unittest.TestCase):
    """Test chorus density inversion detection."""

    def _get_inversion_issues(self, result):
        """Extract chorus_density_inversion issues from result."""
        return [i for i in result.issues
                if i.subcategory == "chorus_density_inversion"]

    def test_chorus_thinner_than_a_section(self):
        """Chorus with lower density than A-section should trigger WARNING."""
        sections = [
            _make_section('A', 'A', 1, 8),
            _make_section('Chorus', 'Chorus', 9, 16),
        ]
        # A-section: 20 notes/bar, Chorus: 10 notes/bar
        notes = _fill_notes(1, 8, 20) + _fill_notes(9, 16, 10)
        metadata = {'sections': sections}

        result = MusicAnalyzer(notes, metadata=metadata).analyze_all()
        issues = self._get_inversion_issues(result)

        warnings = [i for i in issues if i.severity.value == "warning"]
        self.assertGreaterEqual(len(warnings), 1)
        msg = warnings[0].message
        self.assertIn("A-section", msg)
        self.assertIn("chorus_vs_a", warnings[0].details.get("comparison", ""))

    def test_chorus_thinner_than_b_section(self):
        """Chorus with lower density than B-section should trigger WARNING."""
        sections = [
            _make_section('A', 'A', 1, 8),
            _make_section('B', 'B', 9, 16),
            _make_section('Chorus', 'Chorus', 17, 24),
        ]
        # A: 15 notes/bar, B: 30 notes/bar, Chorus: 20 notes/bar
        # Chorus 20 < B 30 * 0.85 = 25.5 => WARNING
        notes = (_fill_notes(1, 8, 15)
                 + _fill_notes(9, 16, 30)
                 + _fill_notes(17, 24, 20))
        metadata = {'sections': sections}

        result = MusicAnalyzer(notes, metadata=metadata).analyze_all()
        issues = self._get_inversion_issues(result)

        b_warnings = [i for i in issues
                      if i.severity.value == "warning"
                      and i.details.get("comparison") == "chorus_vs_b"]
        self.assertGreaterEqual(len(b_warnings), 1)
        self.assertIn("B-section", b_warnings[0].message)

    def test_mild_inversion_info(self):
        """Chorus slightly below max(A, B) density should trigger INFO only."""
        sections = [
            _make_section('A', 'A', 1, 8),
            _make_section('Chorus', 'Chorus', 9, 16),
        ]
        # A: 20 notes/bar, Chorus: 19 notes/bar
        # 19 >= 20 * 0.9 = 18 => no WARNING
        # 19 < 20 * 0.95 = 19.0 => INFO (since 19 < 19.0 is False,
        # let's use 18.5 effective via 37 notes in 2 bars scaled)
        # Actually: 19 < 19.0 is false. Use 18 notes/bar for chorus.
        # 18 >= 20 * 0.9 = 18.0 => no WARNING (18 >= 18.0)
        # 18 < 20 * 0.95 = 19.0 => INFO
        notes = _fill_notes(1, 8, 20) + _fill_notes(9, 16, 18)
        metadata = {'sections': sections}

        result = MusicAnalyzer(notes, metadata=metadata).analyze_all()
        issues = self._get_inversion_issues(result)

        # Should have INFO but no WARNING
        warnings = [i for i in issues if i.severity.value == "warning"]
        infos = [i for i in issues if i.severity.value == "info"]
        self.assertEqual(len(warnings), 0)
        self.assertGreaterEqual(len(infos), 1)
        self.assertIn("slightly thinner", infos[0].message)

    def test_no_issue_when_chorus_denser(self):
        """No issue should fire when chorus is denser than other sections."""
        sections = [
            _make_section('A', 'A', 1, 8),
            _make_section('B', 'B', 9, 16),
            _make_section('Chorus', 'Chorus', 17, 24),
        ]
        # A: 10, B: 15, Chorus: 25
        notes = (_fill_notes(1, 8, 10)
                 + _fill_notes(9, 16, 15)
                 + _fill_notes(17, 24, 25))
        metadata = {'sections': sections}

        result = MusicAnalyzer(notes, metadata=metadata).analyze_all()
        issues = self._get_inversion_issues(result)
        self.assertEqual(len(issues), 0)

    def test_no_issue_without_metadata_sections(self):
        """No inversion issue when metadata has no explicit sections."""
        notes = _fill_notes(1, 16, 15)
        # No metadata sections at all
        result = MusicAnalyzer(notes).analyze_all()
        issues = self._get_inversion_issues(result)
        self.assertEqual(len(issues), 0)

    def test_no_issue_without_chorus(self):
        """No inversion issue when there are no Chorus sections."""
        sections = [
            _make_section('A', 'A', 1, 8),
            _make_section('B', 'B', 9, 16),
        ]
        notes = _fill_notes(1, 8, 20) + _fill_notes(9, 16, 10)
        metadata = {'sections': sections}

        result = MusicAnalyzer(notes, metadata=metadata).analyze_all()
        issues = self._get_inversion_issues(result)
        self.assertEqual(len(issues), 0)

    def test_case_insensitive_section_type(self):
        """Section type matching should be case-insensitive."""
        sections = [
            _make_section('a', 'Verse A', 1, 8),
            _make_section('chorus', 'Chorus 1', 9, 16),
        ]
        # A: 25 notes/bar, Chorus: 10 notes/bar => WARNING
        notes = _fill_notes(1, 8, 25) + _fill_notes(9, 16, 10)
        metadata = {'sections': sections}

        result = MusicAnalyzer(notes, metadata=metadata).analyze_all()
        issues = self._get_inversion_issues(result)

        warnings = [i for i in issues if i.severity.value == "warning"]
        self.assertGreaterEqual(len(warnings), 1)

    def test_drum_notes_excluded(self):
        """Drum track (channel 9) should not count toward density."""
        sections = [
            _make_section('A', 'A', 1, 8),
            _make_section('Chorus', 'Chorus', 9, 16),
        ]
        # A: 15 melodic notes/bar, Chorus: 5 melodic + 30 drum notes/bar
        # Only melodic counts: Chorus 5 < A 15 * 0.9 = 13.5 => WARNING
        a_notes = _fill_notes(1, 8, 15)
        chorus_melodic = _fill_notes(9, 16, 5)
        chorus_drums = _fill_notes(9, 16, 30, channels=[9])
        notes = a_notes + chorus_melodic + chorus_drums
        metadata = {'sections': sections}

        result = MusicAnalyzer(notes, metadata=metadata).analyze_all()
        issues = self._get_inversion_issues(result)

        warnings = [i for i in issues if i.severity.value == "warning"]
        self.assertGreaterEqual(len(warnings), 1,
                                "Drum notes should not prevent density inversion detection")

    def test_multiple_chorus_sections_averaged(self):
        """Multiple chorus sections should be averaged, not compared individually."""
        sections = [
            _make_section('A', 'A', 1, 8),
            _make_section('Chorus', 'Chorus 1', 9, 16),
            _make_section('Chorus', 'Chorus 2', 17, 24),
        ]
        # A: 20 notes/bar
        # Chorus 1: 25 notes/bar, Chorus 2: 25 notes/bar => avg 25
        # 25 > 20 * 0.95 => no issue
        notes = (_fill_notes(1, 8, 20)
                 + _fill_notes(9, 16, 25)
                 + _fill_notes(17, 24, 25))
        metadata = {'sections': sections}

        result = MusicAnalyzer(notes, metadata=metadata).analyze_all()
        issues = self._get_inversion_issues(result)
        self.assertEqual(len(issues), 0)

    def test_details_contain_density_values(self):
        """Issue details should include density values."""
        sections = [
            _make_section('A', 'A', 1, 8),
            _make_section('Chorus', 'Chorus', 9, 16),
        ]
        notes = _fill_notes(1, 8, 20) + _fill_notes(9, 16, 10)
        metadata = {'sections': sections}

        result = MusicAnalyzer(notes, metadata=metadata).analyze_all()
        issues = self._get_inversion_issues(result)

        self.assertGreater(len(issues), 0)
        details = issues[0].details
        self.assertIn("chorus_density", details)
        self.assertIn("a_density", details)
        self.assertIn("ratio", details)
        self.assertGreater(details["a_density"], details["chorus_density"])


if __name__ == "__main__":
    unittest.main()
