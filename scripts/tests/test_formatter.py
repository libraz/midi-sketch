"""Tests for non-destructive display filtering semantics."""

import unittest

from conftest import Category, Issue, Note, Severity
from music_analyzer.formatter import apply_filters
from music_analyzer.models import AnalysisResult, QualityScore


class TestBarRangeFilters(unittest.TestCase):
    def test_zero_start_is_a_real_bar_boundary(self):
        result = AnalysisResult(
            notes=[],
            issues=[
                Issue(Severity.INFO, Category.MELODIC, "first", "", 0),
                Issue(Severity.INFO, Category.MELODIC, "later", "", 1920),
            ],
            score=QualityScore(),
        )

        filtered = apply_filters(result, {"bar_start": 0, "bar_end": 1})

        self.assertEqual([issue.subcategory for issue in filtered.issues], ["first"])

    def test_filter_does_not_mutate_analysis_result(self):
        result = AnalysisResult(
            notes=[],
            issues=[
                Issue(Severity.ERROR, Category.HARMONIC, "shown", "", 0),
                Issue(Severity.ERROR, Category.HARMONIC, "hidden", "", 1920),
            ],
            score=QualityScore(),
        )

        displayed = apply_filters(result, {"bar_start": 0, "bar_end": 1})

        self.assertEqual([issue.subcategory for issue in displayed.issues], ["shown"])
        self.assertEqual([issue.subcategory for issue in result.issues], ["shown", "hidden"])
