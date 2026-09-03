"""Tests for structure analysis: empty tracks, track balance, chorus density."""

import unittest

from conftest import Note, MusicAnalyzer, TICKS_PER_BAR, TICKS_PER_BEAT
from music_analyzer.analyzers.base import BaseAnalyzer, is_high_energy_section_name


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


class TestEnergyCurve(unittest.TestCase):
    """Test top-level energy curve calculation."""

    def test_drum_only_bar_contributes_energy(self):
        notes = [
            Note(
                start=beat * TICKS_PER_BEAT,
                duration=TICKS_PER_BEAT // 4,
                pitch=36,
                velocity=100,
                channel=9,
            )
            for beat in range(4)
        ]

        curve = MusicAnalyzer(notes)._calculate_energy_curve()

        self.assertEqual(len(curve), 1)
        self.assertGreater(curve[0][1], 0.0)


class DummyAnalyzer(BaseAnalyzer):
    """Concrete analyzer for BaseAnalyzer section tests."""

    def analyze(self):
        return []


class TestBaseAnalyzerSections(unittest.TestCase):
    """Test shared section resolution."""

    def test_metadata_sections_take_precedence_over_estimation(self):
        """Explicit generated sections should be used instead of energy guesses."""
        sections = [
            _make_section('A', 'A', 1, 8),
            _make_section('Chorus', 'Chorus', 9, 16),
        ]
        notes = _fill_notes(1, 8, 12, channels=[0]) + _fill_notes(9, 16, 2, channels=[0])
        analyzer = DummyAnalyzer(
            notes=notes,
            notes_by_channel={0: notes},
            metadata={'sections': sections},
        )

        resolved = analyzer.sections

        self.assertEqual(len(resolved), 2)
        self.assertEqual(resolved[0]['type'], 'verse')
        self.assertEqual(resolved[1]['type'], 'chorus')
        self.assertEqual(resolved[1]['start_ticks'], 8 * TICKS_PER_BAR)

    def test_generated_zero_based_start_bar_is_normalized_from_ticks(self):
        """Generated start_bar must not shift analyzer section boundaries."""
        sections = [
            {
                'name': 'A',
                'type': 'A',
                'startTick': 0,
                'endTick': 4 * TICKS_PER_BAR,
                'start_bar': 0,
                'bars': 4,
            },
            {
                'name': 'Chorus',
                'type': 'Chorus',
                'startTick': 4 * TICKS_PER_BAR,
                'endTick': 8 * TICKS_PER_BAR,
                'start_bar': 4,
                'bars': 4,
            },
        ]
        notes = _fill_notes(1, 8, 2, channels=[0])
        analyzer = DummyAnalyzer(
            notes=notes,
            notes_by_channel={0: notes},
            metadata={'sections': sections},
        )

        resolved = analyzer.sections

        self.assertEqual(
            [(section['start_bar'], section['end_bar']) for section in resolved],
            [(1, 4), (5, 8)],
        )

    def test_generated_section_names_map_to_distinct_roles(self):
        # Every section name the generator can emit. The chorus role carries the
        # sections the core calls high energy (Chorus, MixBreak, Drop); the
        # transitional ones (Intro, Interlude, Outro, Chant) frame the song. B is
        # the deliberate exception: it is the pre-chorus, and the tension curve
        # needs it to read as the run-up into the chorus rather than a chorus.
        expected = {
            'Intro': 'instrumental',
            'A': 'verse',
            'B': 'bridge',
            'Chorus': 'chorus',
            'Bridge': 'bridge',
            'Interlude': 'instrumental',
            'Outro': 'instrumental',
            'Chant': 'instrumental',
            'MixBreak': 'chorus',
            'Drop': 'chorus',
        }
        for raw_type, normalized in expected.items():
            with self.subTest(raw_type=raw_type):
                self.assertEqual(DummyAnalyzer._normalize_section_type(raw_type), normalized)

    def test_high_energy_matches_the_generators_own_classification(self):
        # isHighEnergySection in src/core/section_types.h: Chorus, B, MixBreak
        # and Drop. This is a separate axis from the structural role, which is
        # why B is high energy without being a chorus.
        expected = {
            'Intro': False,
            'A': False,
            'B': True,
            'Chorus': True,
            'Bridge': False,
            'Interlude': False,
            'Outro': False,
            'Chant': False,
            'MixBreak': True,
            'Drop': True,
        }
        for raw_type, high_energy in expected.items():
            with self.subTest(raw_type=raw_type):
                self.assertEqual(is_high_energy_section_name(raw_type), high_energy)

    def test_the_two_axes_are_independent(self):
        # B is the case that cannot be expressed with one field.
        self.assertTrue(is_high_energy_section_name('B'))
        self.assertEqual(DummyAnalyzer._normalize_section_type('B'), 'bridge')

    def test_sections_carry_both_axes(self):
        sections = [
            {'type': 'B', 'startTick': 0, 'endTick': 4 * TICKS_PER_BAR,
             'start_bar': 1, 'bars': 4},
            {'type': 'Chorus', 'startTick': 4 * TICKS_PER_BAR,
             'endTick': 8 * TICKS_PER_BAR, 'start_bar': 5, 'bars': 4},
            {'type': 'A', 'startTick': 8 * TICKS_PER_BAR,
             'endTick': 12 * TICKS_PER_BAR, 'start_bar': 9, 'bars': 4},
        ]
        notes = _fill_notes(1, 12, 2, channels=[0])
        analyzer = DummyAnalyzer(notes=notes, notes_by_channel={0: notes},
                                 metadata={'sections': sections})

        resolved = analyzer.sections

        self.assertEqual([section['type'] for section in resolved],
                         ['bridge', 'chorus', 'verse'])
        self.assertEqual([analyzer.is_high_energy(section) for section in resolved],
                         [True, True, False])

    def test_foreign_labels_fall_back_without_swallowing_the_pre_chorus(self):
        expected = {
            'Pre-Chorus': 'bridge',
            'pre chorus': 'bridge',
            'Verse 2': 'verse',
            'Final Chorus': 'chorus',
            'Hook': 'chorus',
            'Guitar Solo': 'instrumental',
        }
        for raw_type, normalized in expected.items():
            with self.subTest(raw_type=raw_type):
                self.assertEqual(DummyAnalyzer._normalize_section_type(raw_type), normalized)

    def test_chord_timeline_overrides_nearest_note_provenance(self):
        notes = [Note(start=0, duration=TICKS_PER_BAR, pitch=60, velocity=80,
                      channel=1, provenance={"chord_degree": 0})]
        analyzer = DummyAnalyzer(
            notes=notes,
            notes_by_channel={1: notes},
            metadata={"chords": [
                {"tick": 0, "endTick": TICKS_PER_BEAT, "degree": 4},
                {"tick": TICKS_PER_BEAT, "endTick": TICKS_PER_BAR, "degree": 5},
            ]},
        )

        self.assertEqual(analyzer.get_chord_degree_at(0), 4)
        self.assertEqual(analyzer.get_chord_degree_at(TICKS_PER_BEAT), 5)
        self.assertIsNone(analyzer.chord_at(TICKS_PER_BAR))


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
