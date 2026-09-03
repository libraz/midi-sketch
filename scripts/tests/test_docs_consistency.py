"""Tests that the tooling README describes what the analyzer actually does.

The category table and the thresholds it quotes are the contract readers type
against, so they are checked against the enum and the constants the analyzer
uses rather than kept in step by hand.
"""

import re
import unittest
from pathlib import Path

from conftest import Category
from music_analyzer.constants import TRACK_NAMES
from music_analyzer.analyzers.melodic import (
    MAX_UNEXAMINED_LEAP,
    SAME_PITCH_ERROR_RUN,
    SAME_PITCH_WARN_RUN,
)

README = Path(__file__).resolve().parents[1] / "README.md"


def _category_table_rows():
    """Rows of the '### What each category detects' table as (name, text)."""
    text = README.read_text()
    section = text.split("### What each category detects", 1)[1]
    section = section.split("###", 1)[0]
    rows = []
    for line in section.splitlines():
        if not line.startswith("|"):
            continue
        cells = [cell.strip() for cell in line.strip("|").split("|")]
        if len(cells) != 2 or cells[0] in ("Category", "---"):
            continue
        rows.append((cells[0], cells[1]))
    return rows


class TestCategoryTable(unittest.TestCase):
    """The documented categories are exactly the ones the CLI accepts."""

    def test_table_lists_every_category_and_nothing_else(self):
        rows = _category_table_rows()
        self.assertTrue(rows, "category table not found in the README")
        documented = [name for name, _ in rows]
        self.assertEqual(
            sorted(documented),
            sorted(category.value for category in Category),
            "the README must document exactly the categories the analyzer emits",
        )

    def test_melodic_row_quotes_the_implemented_thresholds(self):
        melodic = dict(_category_table_rows())["melodic"]
        numbers = {int(match) for match in re.findall(r"\b(\d+)\b", melodic)}
        for threshold in (
            SAME_PITCH_WARN_RUN,
            SAME_PITCH_ERROR_RUN,
            MAX_UNEXAMINED_LEAP,
        ):
            self.assertIn(
                threshold, numbers,
                f"the melodic row must quote the implemented threshold {threshold}",
            )


class TestTrackFilterDocumentation(unittest.TestCase):
    """Every filterable track name is reachable from the documentation."""

    def test_readme_lists_every_track_name(self):
        text = README.read_text()
        for name in TRACK_NAMES.values():
            self.assertIn(
                name, text,
                f"track {name} can be filtered but is not documented",
            )


if __name__ == '__main__':
    unittest.main()
