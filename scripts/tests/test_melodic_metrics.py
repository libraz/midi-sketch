"""Tests for melodic_metrics (measurement library) and the two-layer table.

Covers:
- Metric correctness on small hand-written melodies with known values.
- Anti-uniformity lint: the per-category melody ranges in
  target_profiles.json must actually differ between genres on key metrics.
  If categories collapse to identical ranges, genre coloring has silently
  degenerated to a uniform evaluator.
"""

import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import melodic_metrics as mm

TARGETS_PATH = (Path(__file__).resolve().parents[2]
                / "backup" / "reference" / "target_profiles.json")

DIV = 480


def seq(*pitches, start=0, step=DIV, dur=DIV):
    """Quarter-note sequence from pitches."""
    return [(start + i * step, dur, p) for i, p in enumerate(pitches)]


class TestSkyline(unittest.TestCase):
    def test_keeps_highest_per_onset(self):
        notes = [(0, DIV, 60), (0, DIV, 67), (DIV, DIV, 64)]
        self.assertEqual(mm.skyline(notes), [(0, DIV, 67), (DIV, DIV, 64)])


class TestIntervalDistribution(unittest.TestCase):
    def test_classification(self):
        # 60->60 same, 60->62 step, 62->65 small leap, 65->72 large leap
        dist = mm.interval_distribution(seq(60, 60, 62, 65, 72))
        self.assertEqual(dist["moves"], 4)
        self.assertAlmostEqual(dist["same_ratio"], 0.25)
        self.assertAlmostEqual(dist["step_ratio"], 0.25)
        self.assertAlmostEqual(dist["leap_small_ratio"], 0.25)
        self.assertAlmostEqual(dist["leap_large_ratio"], 0.25)


class TestLeapRecovery(unittest.TestCase):
    def test_step_recovery(self):
        # 60 ->67 (leap up) ->65 (step down: recovered)
        r = mm.leap_recovery(seq(60, 67, 65))
        self.assertEqual(r["leaps"], 1)
        self.assertEqual(r["step_recovery_rate"], 1.0)
        self.assertEqual(r["contrary_rate"], 1.0)

    def test_unrecovered_leap(self):
        # 60 ->67 (leap up) ->72 (continues up: not recovered)
        r = mm.leap_recovery(seq(60, 67, 72))
        self.assertEqual(r["leaps"], 1)
        self.assertEqual(r["step_recovery_rate"], 0.0)
        self.assertEqual(r["contrary_rate"], 0.0)

    def test_trailing_leap_skipped(self):
        # Leap with no following move is not an opportunity
        r = mm.leap_recovery(seq(60, 67))
        self.assertEqual(r["leaps"], 0)
        self.assertIsNone(r["step_recovery_rate"])


class TestSameDirectionLeapChains(unittest.TestCase):
    def test_arpeggio_detected(self):
        # C-E-G-C: three consecutive upward leaps (4,3,5) = chain of 3
        r = mm.same_direction_leap_chains(seq(60, 64, 67, 72))
        self.assertEqual(r["max_chain"], 3)
        self.assertEqual(r["chains_3plus"], 1)

    def test_stepwise_line_clean(self):
        r = mm.same_direction_leap_chains(seq(60, 62, 64, 65, 67))
        self.assertEqual(r["max_chain"], 0)
        self.assertEqual(r["chains_3plus"], 0)

    def test_direction_change_breaks_chain(self):
        # up-leap, up-leap, DOWN-leap: max same-direction chain = 2
        r = mm.same_direction_leap_chains(seq(60, 64, 68, 64))
        self.assertEqual(r["max_chain"], 2)
        self.assertEqual(r["chains_3plus"], 0)


class TestFastRunStepRatio(unittest.TestCase):
    def test_scale_run_is_conjunct(self):
        # Eighth-note scale run: all moves stepwise
        run = seq(60, 62, 64, 65, 67, step=DIV // 2, dur=DIV // 2)
        r = mm.fast_run_step_ratio(run, DIV)
        self.assertEqual(r["runs"], 1)
        self.assertEqual(r["run_conjunct_ratio"], 1.0)

    def test_slow_notes_not_a_run(self):
        # Half-note motion: no fast runs at all
        r = mm.fast_run_step_ratio(seq(60, 64, 67, step=DIV * 2), DIV)
        self.assertEqual(r["runs"], 0)
        self.assertIsNone(r["run_conjunct_ratio"])

    def test_leaping_run_is_disjunct(self):
        run = seq(60, 65, 70, 65, step=DIV // 2, dur=DIV // 2)
        r = mm.fast_run_step_ratio(run, DIV)
        self.assertEqual(r["run_conjunct_ratio"], 0.0)


class TestPhraseArcs(unittest.TestCase):
    def test_arch_classification(self):
        # Rise to interior peak then fall, single phrase
        melody = seq(60, 64, 67, 64, 60)
        r = mm.phrase_arc_distribution(melody, DIV)
        self.assertEqual(r["phrases"], 1)
        self.assertEqual(r["arch_ratio"], 1.0)

    def test_flat_classification(self):
        melody = seq(60, 60, 61, 60, 60)
        r = mm.phrase_arc_distribution(melody, DIV)
        self.assertEqual(r["flat_ratio"], 1.0)

    def test_phrase_split_on_rest(self):
        first = seq(60, 64, 67, 64)
        # Second phrase starts a full bar later (gap >= quarter rest)
        second = seq(60, 64, 67, 64, start=first[-1][0] + DIV * 3)
        r = mm.phrase_arc_distribution(first + second, DIV)
        self.assertEqual(r["phrases"], 2)


class TestSamePitchStreaks(unittest.TestCase):
    def test_chant_streak(self):
        r = mm.same_pitch_streaks(seq(60, 60, 60, 60, 62))
        self.assertEqual(r["max_streak"], 4)


class TestUnsingableMoves(unittest.TestCase):
    def test_fast_octave_plus_jump(self):
        # 16th notes at 200 BPM: IOI = 75ms; 13-semitone jump = unsingable
        fast = seq(60, 74, step=DIV // 4, dur=DIV // 4)
        r = mm.unsingable_moves(fast, DIV, 200.0)
        self.assertEqual(r["unsingable"], 1)

    def test_slow_octave_jump_fine(self):
        r = mm.unsingable_moves(seq(60, 74), DIV, 120.0)
        self.assertEqual(r["unsingable"], 0)


class TestPitchCellConsistency(unittest.TestCase):
    def test_identical_bars_repeat(self):
        bar = [(0, DIV, 60), (DIV, DIV, 64), (DIV * 2, DIV, 67), (DIV * 3, DIV, 64)]
        bar2 = [(s + DIV * 4, d, p) for s, d, p in bar]
        r = mm.pitch_cell_consistency(bar + bar2, DIV)
        self.assertEqual(r["pitch_cell_consistency"], 1.0)

    def test_pitch_change_breaks_match(self):
        bar = [(0, DIV, 60), (DIV, DIV, 64)]
        bar2 = [(DIV * 4, DIV, 60), (DIV * 5, DIV, 65)]  # same rhythm, new pitch
        r = mm.pitch_cell_consistency(bar + bar2, DIV)
        self.assertEqual(r["pitch_cell_consistency"], 0.5)


@unittest.skipUnless(TARGETS_PATH.exists(), "target_profiles.json not built")
class TestTwoLayerTable(unittest.TestCase):
    """Lint the generated table: structure + anti-uniformity guard."""

    @classmethod
    def setUpClass(cls):
        cls.targets = json.loads(TARGETS_PATH.read_text())

    def test_common_rules_present(self):
        rules = self.targets.get("melody_common_rules", {})
        for required in ("same_direction_leap_chains", "octave_leap_contrary",
                         "unsingable_moves"):
            self.assertIn(required, rules)
            self.assertIn("bound", rules[required])
            self.assertIn("corpus_worst", rules[required])

    def test_melody_ranges_exist_per_category(self):
        for cat, info in self.targets["categories"].items():
            self.assertIn("melody", info, f"category {cat} lacks melody ranges")
            self.assertGreaterEqual(info["melody"]["n"], 1)

    def test_categories_are_not_uniform(self):
        """Genre coloring must actually differ between genres.

        If the ranges for discriminating metrics are identical across
        categories, the evaluator has silently degenerated to genre-uniform
        judgment (the failure mode this design exists to prevent).
        """
        discriminating = ("step_ratio", "max_streak", "leap_large_ratio")
        cats = {cat: info["melody"] for cat, info in self.targets["categories"].items()
                if "melody" in info and info["melody"]["n"] >= 2}
        self.assertGreaterEqual(len(cats), 2, "need 2+ multi-reference categories")
        for metric in discriminating:
            ranges = {
                (m[metric]["min"], m[metric]["max"])
                for m in cats.values() if metric in m
            }
            self.assertGreater(
                len(ranges), 1,
                f"{metric}: all categories share an identical range — "
                f"genre coloring has degenerated to uniform")


if __name__ == "__main__":
    unittest.main()
