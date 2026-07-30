"""Regression tests for generated JSON profile selection."""

import unittest

from compare_midi_profile import Note
from reference_motif_report import generated_tracks, profile_track


class GeneratedTrackSelectionTest(unittest.TestCase):
    def setUp(self):
        self.notes = [
            Note(track=0, channel=0, pitch=72, velocity=100, start=0, duration=480),
            Note(track=1, channel=1, pitch=60, velocity=90, start=0, duration=480),
            Note(track=2, channel=2, pitch=48, velocity=90, start=0, duration=480),
            Note(track=3, channel=3, pitch=67, velocity=90, start=0, duration=480),
            Note(track=9, channel=9, pitch=36, velocity=100, start=0, duration=120),
        ]
        self.names = {0: "Vocal", 1: "Chord", 2: "Bass", 3: "Motif", 9: "Drums"}

    def test_role_filter_includes_generated_bass_channel(self):
        tracks = generated_tracks(self.notes, self.names, {"bass"})

        self.assertEqual(len(tracks), 1)
        label, role, _ms_role, notes, lead = tracks[0]
        self.assertEqual(label, "Bass")
        self.assertEqual(role, "bass")
        self.assertEqual(notes[0].channel, 2)
        self.assertEqual(lead[0].channel, 0)

    def test_motif_uses_riff_role_and_drums_are_excluded(self):
        tracks = generated_tracks(self.notes, self.names, {"riff"})

        self.assertEqual(len(tracks), 1)
        self.assertEqual(tracks[0][0], "Motif")
        self.assertEqual(tracks[0][1], "riff")

    def test_density_uses_audible_track_span(self):
        late_notes = [
            Note(track=2, channel=2, pitch=48, velocity=90, start=7680, duration=480),
            Note(track=2, channel=2, pitch=50, velocity=90, start=8160, duration=480),
            Note(track=2, channel=2, pitch=52, velocity=90, start=8640, duration=480),
            Note(track=2, channel=2, pitch=53, velocity=90, start=9120, duration=480),
        ]

        profile = profile_track("generated", "Bass", late_notes, 480, None, role="bass")

        self.assertEqual(profile.notes_per_bar, 4.0)


if __name__ == "__main__":
    unittest.main()
