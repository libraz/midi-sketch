"""Coverage tests for analyzer blueprint/style tables."""

import json
import unittest
from pathlib import Path

import check_dissonance
from music_analyzer.blueprints import BLUEPRINT_PROFILES


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
        self.assertEqual(check_dissonance.STYLE_PRESET_COUNT, 17)
        self.assertEqual(check_dissonance.PRODUCTION_BLUEPRINT_COUNT, 10)


if __name__ == "__main__":
    unittest.main()
