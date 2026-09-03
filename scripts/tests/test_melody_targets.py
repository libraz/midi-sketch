"""Tests that the melody discipline layers never disappear in silence.

Both layers need a target profile measured from a reference corpus. When that
profile is not configured the analysis has to say the layers did not run: a
song that was never checked must not be indistinguishable from one that broke
no rule.
"""

import json
import os
import tempfile
import unittest
from pathlib import Path

from conftest import Note, MusicAnalyzer, Severity, TICKS_PER_BEAT
from music_analyzer import melody_targets

_DISCIPLINE_SUBCATEGORIES = {"melody_common", "melody_style"}


def _vocal_line(count=32):
    """A vocal long enough for the discipline checks to consider it."""
    pitches = [60, 62, 64, 65, 67, 65, 64, 62]
    return [
        Note(start=idx * TICKS_PER_BEAT, duration=TICKS_PER_BEAT,
             pitch=pitches[idx % len(pitches)], velocity=90, channel=0)
        for idx in range(count)
    ]


def _targets_with_impossible_common_rule():
    """A profile whose Layer 1 rule any melody violates."""
    return {
        "melody_common_rules": {
            "step_ratio_floor": {
                "metric": "step_ratio",
                "bound": 1.5,
                "direction": "min",
                "severity": "warning",
                "description": "every move must be a step",
            }
        },
        "categories": {},
    }


class TestMelodyDisciplineActivation(unittest.TestCase):
    """The layers are either evaluated or reported as not evaluated."""

    def setUp(self):
        self._saved_env = os.environ.pop(melody_targets.TARGETS_ENV_VAR, None)
        melody_targets.set_targets_path(None)

    def tearDown(self):
        melody_targets.set_targets_path(None)
        if self._saved_env is not None:
            os.environ[melody_targets.TARGETS_ENV_VAR] = self._saved_env

    def _issues(self, notes=None):
        result = MusicAnalyzer(notes or _vocal_line()).analyze_all()
        return result.issues

    def test_missing_profile_is_reported_not_skipped(self):
        melody_targets.set_targets_path("/nonexistent/melody-targets.json")

        issues = self._issues()

        inactive = [i for i in issues
                    if i.subcategory == "melody_discipline_inactive"]
        self.assertEqual(len(inactive), 1)
        self.assertIn("did not run", inactive[0].message)

    def test_missing_profile_emits_no_discipline_verdict(self):
        melody_targets.set_targets_path("/nonexistent/melody-targets.json")

        verdicts = [i for i in self._issues()
                    if i.subcategory in _DISCIPLINE_SUBCATEGORIES]
        self.assertEqual(verdicts, [], "a layer that did not run cannot pass or fail")

    def test_unreadable_profile_names_the_path(self):
        melody_targets.set_targets_path("/nonexistent/melody-targets.json")

        inactive = [i for i in self._issues()
                    if i.subcategory == "melody_discipline_inactive"]

        self.assertEqual(len(inactive), 1)
        self.assertIn("/nonexistent/melody-targets.json", inactive[0].message)

    def test_configured_profile_actually_runs_layer_one(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            path = Path(tmpdir) / "target_profiles.json"
            path.write_text(json.dumps(_targets_with_impossible_common_rule()))
            melody_targets.set_targets_path(path)

            issues = self._issues()

        inactive = [i for i in issues
                    if i.subcategory == "melody_discipline_inactive"]
        self.assertEqual(inactive, [], "the layer ran, so nothing is inactive")

        common = [i for i in issues if i.subcategory == "melody_common"]
        self.assertEqual(len(common), 1)
        self.assertEqual(common[0].severity, Severity.WARNING)
        self.assertIn("step_ratio_floor", common[0].message)

    def test_environment_variable_configures_the_profile(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            path = Path(tmpdir) / "target_profiles.json"
            path.write_text(json.dumps(_targets_with_impossible_common_rule()))
            os.environ[melody_targets.TARGETS_ENV_VAR] = str(path)
            try:
                issues = self._issues()
            finally:
                del os.environ[melody_targets.TARGETS_ENV_VAR]

        self.assertTrue([i for i in issues if i.subcategory == "melody_common"])

    def test_default_location_is_used_when_nothing_is_configured(self):
        self.assertIsNone(melody_targets.configured_path())
        self.assertEqual(melody_targets.targets_path(),
                         melody_targets.DEFAULT_TARGETS_PATH)

    def test_configuration_overrides_the_default_location(self):
        melody_targets.set_targets_path("/somewhere/else.json")

        self.assertEqual(melody_targets.targets_path(), Path("/somewhere/else.json"))


if __name__ == '__main__':
    unittest.main()
