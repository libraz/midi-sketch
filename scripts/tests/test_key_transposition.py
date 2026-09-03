"""Tests that chord-role classification does not depend on the song key.

Exported pitches carry the song key while chord degrees stay in the internal
C major space the generator composes in, so every check that classifies a note
by its role against the active chord has to map the note back to that space
first.
"""

import unittest

from conftest import Note, MusicAnalyzer, TICKS_PER_BAR, TICKS_PER_BEAT
from music_analyzer.analyzers.base import BaseAnalyzer

# Checks that judge a note by its role against the active chord. Register
# checks (vocal range, low-register muddiness) are deliberately excluded: a
# transposed song really does sit in a different register.
ROLE_SUBCATEGORIES = {
    "bass_chord_degrees",
    "bass_downbeat_root",
    "chord_function",
}

# One bar per entry: chord degree, bass downbeat pitch, bass off-beat pitch.
# Degrees are internal (0=I, 3=IV, 4=V) and pitches are the internal C major
# realization. The line is deliberately imperfect - mostly non-root downbeats,
# some non-chord tones, and a dominant-to-subdominant bar pair - so every
# chord-role check has something to report.
_PROGRESSION = [
    (0, 40, 38),   # I  : E (3rd), then D (non-chord tone)
    (4, 47, 41),   # V  : B (3rd), then F (non-chord tone)
    (3, 41, 47),   # IV : F (root), then B (non-chord tone); V->IV retrograde
    (0, 40, 43),   # I  : E (3rd), then G (5th)
]
_CHORD_VOICINGS = {0: [60, 64, 67], 3: [65, 69, 72], 4: [67, 71, 74]}


def _make_note(tick, pitch, channel, offset, degree, duration=TICKS_PER_BEAT):
    """Build one exported note: sounding pitch shifted, provenance internal."""
    return Note(
        start=tick,
        duration=duration,
        pitch=pitch + offset,
        velocity=80,
        channel=channel,
        provenance={
            'chord_degree': degree,
            'source': 'bass_pattern' if channel == 2 else 'chord_voicing',
            'original_pitch': pitch,
        },
    )


def _song(offset, bars=8):
    """The same song exported in C major (offset 0) or transposed by offset."""
    notes = []
    for bar in range(bars):
        degree, root, second = _PROGRESSION[bar % len(_PROGRESSION)]
        tick = bar * TICKS_PER_BAR
        notes.append(_make_note(tick, root, 2, offset, degree))
        notes.append(_make_note(tick + TICKS_PER_BEAT * 2, second, 2, offset, degree))
        for pitch in _CHORD_VOICINGS[degree]:
            notes.append(
                _make_note(tick, pitch, 1, offset, degree, duration=TICKS_PER_BAR)
            )
    return notes


def _role_issues(notes):
    """Issues from the chord-role checks, as comparable tuples."""
    result = MusicAnalyzer(notes).analyze_all()
    return sorted(
        (issue.subcategory, issue.severity.value, issue.message, issue.tick)
        for issue in result.issues
        if issue.subcategory in ROLE_SUBCATEGORIES
    )


class TestChordRoleIsKeyIndependent(unittest.TestCase):
    """The same song in different keys must get the same chord-role verdict."""

    def test_role_checks_report_issues_in_c_major(self):
        issues = _role_issues(_song(0))
        self.assertTrue(issues, "fixture must exercise the chord-role checks")
        self.assertEqual(
            {subcategory for subcategory, _, _, _ in issues},
            ROLE_SUBCATEGORIES,
        )

    def test_role_checks_match_across_keys(self):
        baseline = _role_issues(_song(0))
        for offset in (1, 2, 5, 7, 11):
            with self.subTest(key_offset=offset):
                self.assertEqual(_role_issues(_song(offset)), baseline)


class TestDeclaredKeyIsUsedDirectly(unittest.TestCase):
    """When the output states its key, nothing is reconstructed from the notes."""

    @staticmethod
    def _analyzer(notes, metadata):
        by_channel = {}
        for note in notes:
            by_channel.setdefault(note.channel, []).append(note)
        return BaseAnalyzer(notes, by_channel, metadata=metadata)

    @staticmethod
    def _bare_notes(offset, channel=2, count=8):
        """Sounding notes with no provenance at all."""
        return [
            Note(start=idx * TICKS_PER_BEAT, duration=TICKS_PER_BEAT,
                 pitch=36 + offset, velocity=80, channel=channel)
            for idx in range(count)
        ]

    def test_key_is_read_rather_than_estimated(self):
        # Without provenance the reconstruction has nothing to work with and
        # would answer 0; the declared key has to win.
        analyzer = self._analyzer(self._bare_notes(5), {'key': 5})

        self.assertEqual(analyzer.transposition_at(0), 5)
        self.assertEqual(analyzer.declared_transposition_at(0), 5)

    def test_modulation_applies_from_its_tick_onwards(self):
        metadata = {'key': 3, 'modulation_tick': 4 * TICKS_PER_BAR,
                    'modulation_semitones': 2}
        analyzer = self._analyzer(self._bare_notes(3), metadata)

        self.assertEqual(analyzer.transposition_at(0), 3)
        self.assertEqual(analyzer.transposition_at(4 * TICKS_PER_BAR - 1), 3)
        self.assertEqual(analyzer.transposition_at(4 * TICKS_PER_BAR), 5)
        self.assertEqual(analyzer.transposition_at(9 * TICKS_PER_BAR), 5)

    def test_a_zero_modulation_tick_means_no_modulation(self):
        metadata = {'key': 3, 'modulation_tick': 0, 'modulation_semitones': 0}
        analyzer = self._analyzer(self._bare_notes(3), metadata)

        self.assertEqual(analyzer.transposition_at(0), 3)
        self.assertEqual(analyzer.transposition_at(64 * TICKS_PER_BAR), 3)

    def test_declared_and_reconstructed_agree(self):
        # Same song read two ways: with the key stated, and as older output
        # where it has to come from the notes' provenance.
        declared = self._analyzer(_song(5), {'key': 5})
        reconstructed = self._analyzer(_song(5), {})

        bass = [note for note in _song(5) if note.channel == 2]
        self.assertTrue(bass)
        for note in bass:
            self.assertEqual(declared.internal_pitch_class(note),
                             reconstructed.internal_pitch_class(note))

    def test_untransposed_channels_are_left_alone(self):
        metadata = {'key': 5, 'transposed_channels': {0, 1, 2}}
        drum = Note(start=0, duration=TICKS_PER_BEAT, pitch=38, velocity=90,
                    channel=9)
        bass = Note(start=0, duration=TICKS_PER_BEAT, pitch=41, velocity=80,
                    channel=2)
        analyzer = self._analyzer([drum, bass], metadata)

        self.assertEqual(analyzer.internal_pitch_class(drum), 38 % 12)
        self.assertEqual(analyzer.internal_pitch_class(bass), (41 - 5) % 12)

    def test_the_declared_flag_beats_the_legacy_channel_list(self):
        # SE (channel 15) used to be written untransposed and is not any more.
        note = Note(start=0, duration=TICKS_PER_BEAT, pitch=65, velocity=80,
                    channel=15)
        stated = self._analyzer([note], {'key': 5, 'transposed_channels': {15}})
        legacy = self._analyzer([note], {'key': 5})

        self.assertEqual(stated.internal_pitch_class(note), (65 - 5) % 12)
        self.assertEqual(legacy.internal_pitch_class(note), 65 % 12)


class TestTranspositionRecovery(unittest.TestCase):
    """The offset from the internal space is recovered from note provenance."""

    @staticmethod
    def _analyzer(notes):
        by_channel = {}
        for note in notes:
            by_channel.setdefault(note.channel, []).append(note)
        return BaseAnalyzer(notes, by_channel)

    def test_offset_matches_the_key(self):
        for offset in range(12):
            with self.subTest(key_offset=offset):
                analyzer = self._analyzer(_song(offset))
                self.assertEqual(analyzer.transposition_at(0), offset)
                self.assertEqual(
                    analyzer.transposition_at(TICKS_PER_BAR * 7), offset
                )

    def test_internal_pitch_class_undoes_the_key(self):
        analyzer = self._analyzer(_song(5))
        bass = [note for note in _song(5) if note.channel == 2][0]
        self.assertEqual(bass.pitch, 45)  # internal E2 sounding as A2
        self.assertEqual(analyzer.internal_pitch_class(bass), 4)

    def test_modulation_resolves_per_bar(self):
        # Four bars in the original key followed by four a whole tone up.
        notes = _song(0, bars=4)
        for note in _song(2, bars=8)[len(notes):]:
            notes.append(note)
        analyzer = self._analyzer(notes)

        self.assertEqual(analyzer.transposition_at(0), 0)
        self.assertEqual(analyzer.transposition_at(TICKS_PER_BAR * 7), 2)

    def test_output_without_provenance_is_read_as_internal(self):
        notes = [
            Note(start=0, duration=TICKS_PER_BEAT, pitch=41, velocity=80, channel=2)
        ]
        analyzer = self._analyzer(notes)
        self.assertEqual(analyzer.transposition_at(0), 0)
        self.assertEqual(analyzer.internal_pitch_class(notes[0]), 5)


if __name__ == '__main__':
    unittest.main()
