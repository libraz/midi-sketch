"""Tests for the CLI/WASM parity checker's own effectiveness.

The checker only means something if it exercises the default-config path and
refuses to call two silent outputs a match, so both properties are asserted
here rather than left to inspection.
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from verify_wasm_cli import (  # noqa: E402
    Config,
    MIN_SOUNDING_TRACKS,
    cli_command,
    sounding_output_errors,
    sweep_configs,
    wasm_command,
)

_OPTIONAL_FLAGS = ("--bpm", "--vocal-attitude", "--vocal-low", "--vocal-high")


def _events(tracks):
    """Events JSON with the given (name, note_count) tracks."""
    return {
        "tracks": [
            {"name": name, "notes": [{"pitch": 60}] * count}
            for name, count in tracks
        ]
    }


_HEALTHY = _events([("Vocal", 40), ("Chord", 60), ("Bass", 30), ("Drums", 90)])


class TestDefaultConfigPath(unittest.TestCase):
    """Unset fields must reach neither entry point."""

    def test_unset_fields_are_not_passed_to_either_side(self):
        cfg = Config(seed=1, style=0)
        for command in (cli_command(cfg), wasm_command(cfg, Path("/tmp/x.mid"))):
            for flag in _OPTIONAL_FLAGS:
                self.assertNotIn(
                    flag, command,
                    f"{flag} must be omitted so the side resolves its own default",
                )

    def test_set_fields_are_passed_to_both_sides(self):
        cfg = Config(seed=1, style=0, bpm=128, vocal_attitude=1,
                     vocal_low=57, vocal_high=81)
        for command in (cli_command(cfg), wasm_command(cfg, Path("/tmp/x.mid"))):
            for flag, value in (("--bpm", "128"), ("--vocal-attitude", "1"),
                                ("--vocal-low", "57"), ("--vocal-high", "81")):
                self.assertIn(flag, command)
                self.assertEqual(command[command.index(flag) + 1], value)

    def test_sweep_covers_the_default_vocal_range(self):
        defaulted = [
            cfg for cfg in sweep_configs()
            if cfg.vocal_low is None and cfg.vocal_high is None
            and cfg.vocal_attitude is None and cfg.bpm is None
        ]
        self.assertTrue(
            defaulted,
            "the sweep must generate at least one config that leaves the vocal "
            "range and BPM to each side's default-config path",
        )

    def test_sweep_also_covers_an_explicit_vocal_range(self):
        explicit = [
            cfg for cfg in sweep_configs()
            if cfg.vocal_low is not None and cfg.vocal_high is not None
        ]
        self.assertTrue(explicit, "the sweep must also cover an explicit vocal range")


class TestSoundingOutputGate(unittest.TestCase):
    """Silence must never be reported as parity."""

    def test_healthy_output_passes(self):
        self.assertEqual(sounding_output_errors(_HEALTHY, "CLI"), [])

    def test_no_notes_at_all_is_reported(self):
        empty = _events([("Vocal", 0), ("Chord", 0), ("Bass", 0)])
        errors = sounding_output_errors(empty, "WASM")
        self.assertTrue(errors)
        self.assertIn("no notes at all", errors[0])

    def test_too_few_sounding_tracks_is_reported(self):
        thin = _events([("Vocal", 40), ("Chord", 0), ("Bass", 0)])
        errors = sounding_output_errors(thin, "CLI")
        self.assertTrue(any("carry notes" in error for error in errors))

    def test_silent_vocal_is_reported(self):
        instrumental = _events([("Vocal", 0), ("Chord", 60), ("Bass", 30),
                                ("Drums", 90)])
        errors = sounding_output_errors(instrumental, "CLI")
        self.assertTrue(any("vocal track is silent" in error for error in errors))

    def test_threshold_matches_a_full_pop_sketch(self):
        # Vocal, chord and bass are the minimum a pop sketch always carries.
        self.assertEqual(MIN_SOUNDING_TRACKS, 3)


if __name__ == '__main__':
    unittest.main()
