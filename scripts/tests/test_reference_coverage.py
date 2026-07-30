"""Coverage tests for analyzer blueprint/style tables."""

import json
import unittest
from pathlib import Path
from types import SimpleNamespace

import check_dissonance
from music_analyzer.blueprints import BLUEPRINT_PROFILES
from music_analyzer.cli import _batch_values
from music_analyzer.constants import PRODUCTION_BLUEPRINT_IDS, STYLE_PRESET_COUNT
from compare_generation_to_targets import (
    ARTIFACT_METRICS,
    COMPARED_METRICS,
    category_blueprints,
)


ROOT = Path(__file__).resolve().parents[2]
REFERENCE_DIR = ROOT / "backup" / "reference"


class TestReferenceCoverage(unittest.TestCase):
    """Ensure analyzer coverage tracks current generator IDs."""

    def test_all_blueprints_have_scoring_profiles(self):
        self.assertEqual(set(BLUEPRINT_PROFILES.keys()), set(range(10)))

    def test_reference_categories_cover_all_blueprints(self):
        data = json.loads((REFERENCE_DIR / "target_profiles.json").read_text())
        covered = set()
        for category in data["categories"].values():
            covered.update(category["blueprints"])

        self.assertEqual(covered, set(range(10)))

    def test_batch_constants_include_current_styles_and_blueprints(self):
        self.assertEqual(STYLE_PRESET_COUNT, 17)
        self.assertEqual(check_dissonance.STYLE_PRESET_COUNT, STYLE_PRESET_COUNT)
        self.assertEqual(check_dissonance.PRODUCTION_BLUEPRINT_COUNT,
                         len(PRODUCTION_BLUEPRINT_IDS))

    def test_batch_all_and_quick_include_every_public_blueprint(self):
        all_args = SimpleNamespace(quick=False, styles="all", chords="all",
                                   blueprints="all")
        styles, _, blueprints = _batch_values(all_args)
        self.assertEqual(styles, list(range(STYLE_PRESET_COUNT)))
        self.assertEqual(blueprints, list(PRODUCTION_BLUEPRINT_IDS))

        quick_args = SimpleNamespace(quick=True, styles="0", chords="0",
                                     blueprints="all")
        _, _, quick_blueprints = _batch_values(quick_args)
        self.assertEqual(quick_blueprints, list(PRODUCTION_BLUEPRINT_IDS))

    def test_target_comparison_expands_every_category_blueprint(self):
        data = json.loads((REFERENCE_DIR / "target_profiles.json").read_text())
        for category, target in data["categories"].items():
            with self.subTest(category=category):
                self.assertEqual(category_blueprints(target),
                                 sorted(target["blueprints"]))

    def test_quantization_artifacts_are_not_quality_boundaries(self):
        self.assertIn("eighth_grid_ratio", ARTIFACT_METRICS)
        self.assertTrue(set(ARTIFACT_METRICS).isdisjoint(COMPARED_METRICS))


if __name__ == "__main__":
    unittest.main()
