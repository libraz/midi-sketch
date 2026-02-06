"""Batch test runner for music analysis.

Provides parallel batch testing across parameter combinations,
progress tracking, and summary reporting.
"""

import os
import shutil
import subprocess
import tempfile
import threading
from collections import Counter, defaultdict
from pathlib import Path
from typing import List
from concurrent.futures import ThreadPoolExecutor, as_completed

from .constants import BLUEPRINT_NAMES, Severity
from .models import TestResult, QualityScore
from .loader import load_json_output, load_json_metadata
from .analyzer import MusicAnalyzer


class ProgressCounter:
    """Thread-safe progress counter for batch testing."""

    def __init__(self, total: int):
        self.total = total
        self.current = 0
        self.lock = threading.Lock()
        self.passed = 0
        self.failed = 0
        self.errors = 0

    def increment(self, result: TestResult):
        """Increment counter and classify result."""
        with self.lock:
            self.current += 1
            if result.error:
                self.errors += 1
            elif result.error_count > 0:
                self.failed += 1
            else:
                self.passed += 1


def run_single_test(
    cli_path: str,
    seed: int,
    style: int,
    chord: int,
    blueprint: int,
    work_dir: Path,
) -> TestResult:
    """Run a single generation and analysis.

    Invokes the CLI to generate a MIDI file, then loads the JSON output
    and runs MusicAnalyzer with blueprint-aware scoring. Uses a unique
    temporary directory to avoid file collisions in parallel mode.

    Args:
        cli_path: Path to the midisketch_cli binary.
        seed: Random seed.
        style: Style preset index.
        chord: Chord progression index.
        blueprint: Blueprint index.
        work_dir: Working directory (used to resolve relative cli_path).

    Returns:
        TestResult with scores and issue counts.
    """
    # Resolve CLI path to absolute before changing work dir
    cli_abs = str((work_dir / cli_path).resolve()) if not os.path.isabs(cli_path) else cli_path

    cmd = [
        cli_abs,
        "--analyze",
        "--json",
        "--seed", str(seed),
        "--style", str(style),
        "--chord", str(chord),
        "--blueprint", str(blueprint),
    ]

    tmp_dir = None
    try:
        # Each worker gets its own temp directory to avoid file collisions
        tmp_dir = tempfile.mkdtemp(prefix="midisketch_batch_")

        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=60,
            cwd=tmp_dir,
        )

        if result.returncode != 0:
            return TestResult(
                seed=seed, style=style, chord=chord, blueprint=blueprint,
                error=f"CLI error: {result.stderr[:200]}",
            )

        # Load output.json from the temp directory
        output_file = Path(tmp_dir) / "output.json"
        if not output_file.exists():
            return TestResult(
                seed=seed, style=style, chord=chord, blueprint=blueprint,
                error="output.json not found",
            )

        notes = load_json_output(str(output_file))
        metadata = load_json_metadata(str(output_file))
        analyzer = MusicAnalyzer(notes, blueprint=blueprint, metadata=metadata)
        analysis = analyzer.analyze_all()

        error_count = sum(
            1 for idx in analysis.issues if idx.severity == Severity.ERROR
        )
        warning_count = sum(
            1 for idx in analysis.issues if idx.severity == Severity.WARNING
        )
        info_count = sum(
            1 for idx in analysis.issues if idx.severity == Severity.INFO
        )

        return TestResult(
            seed=seed,
            style=style,
            chord=chord,
            blueprint=blueprint,
            score=analysis.score,
            error_count=error_count,
            warning_count=warning_count,
            info_count=info_count,
        )

    except subprocess.TimeoutExpired:
        return TestResult(
            seed=seed, style=style, chord=chord, blueprint=blueprint,
            error="Timeout (>60s)",
        )
    except Exception as exc:
        return TestResult(
            seed=seed, style=style, chord=chord, blueprint=blueprint,
            error=str(exc)[:200],
        )
    finally:
        if tmp_dir and os.path.exists(tmp_dir):
            shutil.rmtree(tmp_dir, ignore_errors=True)


def run_batch_tests(
    cli_path: str,
    seeds: List[int],
    styles: List[int],
    chords: List[int],
    blueprints: List[int],
    parallel: int = 1,
    verbose: bool = False,
) -> List[TestResult]:
    """Run batch tests across parameter combinations.

    Args:
        cli_path: Path to the midisketch_cli binary.
        seeds: List of random seeds to test.
        styles: List of style preset indices.
        chords: List of chord progression indices.
        blueprints: List of blueprint indices.
        parallel: Number of parallel workers.
        verbose: Whether to print each result.

    Returns:
        List of TestResult objects in config order.
    """
    work_dir = Path.cwd()
    results = []

    configs = [
        (seed, style, chord, blueprint)
        for seed in seeds
        for style in styles
        for chord in chords
        for blueprint in blueprints
    ]

    total = len(configs)
    parallel_msg = f" with {parallel} workers" if parallel > 1 else ""
    print(f"Running {total} tests{parallel_msg}...")
    print()

    if parallel > 1:
        counter = ProgressCounter(total)
        results_dict = {}

        with ThreadPoolExecutor(max_workers=parallel) as executor:
            futures = {
                executor.submit(
                    run_single_test,
                    cli_path, seed, style, chord, blueprint, work_dir,
                ): (seed, style, chord, blueprint)
                for seed, style, chord, blueprint in configs
            }

            for future in as_completed(futures):
                config = futures[future]
                result = future.result()
                results_dict[config] = result
                counter.increment(result)

                with counter.lock:
                    idx = counter.current
                    if result.error:
                        print(
                            f"[{idx:4d}/{total}] "
                            f"seed={config[0]:3d} bp={config[3]}: ERROR"
                        )
                    elif result.error_count > 0:
                        print(
                            f"[{idx:4d}/{total}] "
                            f"seed={config[0]:3d} bp={config[3]}: "
                            f"\033[31mFAIL\033[0m "
                            f"score={result.score.overall:.1f} "
                            f"errors={result.error_count}"
                        )
                    elif verbose:
                        print(
                            f"[{idx:4d}/{total}] "
                            f"seed={config[0]:3d} bp={config[3]}: "
                            f"OK score={result.score.overall:.1f}"
                        )
                    else:
                        print(
                            f"\r[{idx:4d}/{total}] Testing... "
                            f"(P:{counter.passed} F:{counter.failed} "
                            f"E:{counter.errors})",
                            end="", flush=True,
                        )

        results = [results_dict[config] for config in configs]
        if not verbose:
            print("\r" + " " * 60 + "\r", end="")
    else:
        for idx, (seed, style, chord, blueprint) in enumerate(configs, 1):
            result = run_single_test(
                cli_path, seed, style, chord, blueprint, work_dir,
            )
            results.append(result)

            if result.error:
                print(
                    f"[{idx:4d}/{total}] "
                    f"seed={seed:3d} bp={blueprint}: ERROR"
                )
            elif result.error_count > 0:
                print(
                    f"[{idx:4d}/{total}] "
                    f"seed={seed:3d} bp={blueprint}: "
                    f"\033[31mFAIL\033[0m "
                    f"score={result.score.overall:.1f} "
                    f"errors={result.error_count}"
                )
            elif verbose:
                print(
                    f"[{idx:4d}/{total}] "
                    f"seed={seed:3d} bp={blueprint}: "
                    f"OK score={result.score.overall:.1f}"
                )
            else:
                print(
                    f"\r[{idx:4d}/{total}] Testing...",
                    end="", flush=True,
                )

        if not verbose:
            print("\r" + " " * 40 + "\r", end="")

    return results


def print_batch_summary(results: List[TestResult]) -> bool:
    """Print batch test summary with arrangement scores.

    Args:
        results: List of TestResult objects from batch run.

    Returns:
        True if all tests passed (no errors, no failures).
    """
    total = len(results)
    errors = [res for res in results if res.error]
    failed = [res for res in results if res.error_count > 0]
    passed = [res for res in results if not res.error and res.error_count == 0]

    print()
    print("=" * 80)
    print("BATCH TEST SUMMARY")
    print("=" * 80)

    print(f"\n{'Results':40s}")
    print("-" * 40)
    print(f"  Total tests:             {total:>6d}")
    print(f"  \033[32mPassed:                  {len(passed):>6d}\033[0m")
    print(f"  \033[31mFailed:                  {len(failed):>6d}\033[0m")
    print(f"  Errors:                  {len(errors):>6d}")

    # Score statistics
    valid_results = [res for res in results if not res.error]
    if valid_results:
        scores = [res.score.overall for res in valid_results]
        avg_score = sum(scores) / len(scores)
        min_score = min(scores)
        max_score = max(scores)

        # Category averages (including arrangement)
        avg_melodic = sum(
            res.score.melodic for res in valid_results
        ) / len(valid_results)
        avg_harmonic = sum(
            res.score.harmonic for res in valid_results
        ) / len(valid_results)
        avg_rhythm = sum(
            res.score.rhythm for res in valid_results
        ) / len(valid_results)
        avg_arrangement = sum(
            res.score.arrangement for res in valid_results
        ) / len(valid_results)
        avg_structure = sum(
            res.score.structure for res in valid_results
        ) / len(valid_results)

        print(f"\n{'Score Statistics':40s}")
        print("-" * 40)
        print(f"  Average:     {avg_score:6.1f}")
        print(f"  Min:         {min_score:6.1f}")
        print(f"  Max:         {max_score:6.1f}")
        print(f"  Categories:  M:{avg_melodic:.0f} H:{avg_harmonic:.0f} "
              f"R:{avg_rhythm:.0f} A:{avg_arrangement:.0f} S:{avg_structure:.0f}")

        # Grade distribution
        grades = Counter(res.score.grade for res in valid_results)
        print(f"\n{'Grade Distribution':40s}")
        print("-" * 40)
        for grade in ['A', 'B', 'C', 'D', 'F']:
            count = grades.get(grade, 0)
            pct = count / len(valid_results) * 100
            bar = '#' * int(pct / 5)
            print(f"  {grade}: {count:4d} ({pct:5.1f}%) {bar}")

    # Blueprint breakdown
    bp_stats = defaultdict(
        lambda: {
            'count': 0,
            'score_sum': 0,
            'errors': 0,
            'failed': 0,
        }
    )
    for res in results:
        bp_id = res.blueprint
        bp_stats[bp_id]['count'] += 1
        if res.error:
            bp_stats[bp_id]['errors'] += 1
        elif res.error_count > 0:
            bp_stats[bp_id]['failed'] += 1
        else:
            bp_stats[bp_id]['score_sum'] += res.score.overall

    if len(bp_stats) > 1:
        print(f"\n{'Results by Blueprint':40s}")
        print("-" * 40)
        for bp_id in sorted(bp_stats.keys()):
            stats = bp_stats[bp_id]
            bp_name = BLUEPRINT_NAMES.get(bp_id, f"BP{bp_id}")
            passed_count = (
                stats['count'] - stats['errors'] - stats['failed']
            )
            avg = (
                stats['score_sum'] / passed_count
                if passed_count > 0 else 0
            )
            print(
                f"  {bp_name:15s} "
                f"passed:{passed_count:3d} "
                f"failed:{stats['failed']:3d} "
                f"avg:{avg:5.1f}"
            )

    # Worst cases
    if failed:
        print(f"\n{'Worst Cases (lowest scores)':40s}")
        print("-" * 40)
        worst = sorted(failed, key=lambda res: res.score.overall)[:5]
        for res in worst:
            bp_name = BLUEPRINT_NAMES.get(res.blueprint, f"BP{res.blueprint}")
            print(
                f"  seed={res.seed:3d} style={res.style:2d} "
                f"{bp_name:12s} score={res.score.overall:5.1f} "
                f"errors={res.error_count}"
            )
            print(f"    {res.cli_command()}")

    print()
    print("=" * 80)
    is_passed = len(failed) == 0 and len(errors) == 0
    if is_passed:
        print("\033[32mRESULT: PASSED\033[0m")
    else:
        print("\033[31mRESULT: FAILED\033[0m")
    print("=" * 80)

    return is_passed
