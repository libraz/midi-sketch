#!/usr/bin/env python3
"""
check_pitch_crossing.py - Check if accompaniment tracks cross above vocal pitch

In pop arrangement, the vocal melody should occupy the highest register.
When accompaniment tracks (Chord, Motif, Arpeggio, Aux) play notes above
the vocal, the melody gets buried. This script detects such crossings.
"""

import subprocess
import json
import sys
import argparse
from pathlib import Path
from dataclasses import dataclass, field
from typing import Optional
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed
import threading


# MIDI note name lookup
NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

TICKS_PER_BEAT = 480
TICKS_PER_BAR = 1920

# Default tracks to check (Bass, Drums, SE excluded)
DEFAULT_CHECK_TRACKS = ["chord", "motif", "arpeggio", "aux"]

# Blueprint names
BLUEPRINT_NAMES = {
    0: "Traditional",
    1: "RhythmLock",
    2: "StoryPop",
    3: "Ballad",
    4: "IdolStandard",
    5: "IdolHyper",
    6: "IdolKawaii",
    7: "IdolCoolPop",
    8: "IdolEmo",
}


def pitch_name(pitch: int) -> str:
    """Convert MIDI pitch number to note name."""
    octave = (pitch // 12) - 1
    note = NOTE_NAMES[pitch % 12]
    return f"{note}{octave}"


def tick_to_bar_beat(tick: int) -> tuple[int, float]:
    """Convert tick to bar number and beat position."""
    bar = tick // TICKS_PER_BAR + 1
    beat = (tick % TICKS_PER_BAR) / TICKS_PER_BEAT + 1
    return bar, beat


@dataclass
class Violation:
    tick: int
    bar: int
    beat: float
    track: str
    vocal_pitch: int
    crossing_pitch: int
    excess_semitones: int
    severity: str  # "high", "medium", "low"


@dataclass
class TrackSummary:
    track: str
    violation_count: int = 0
    max_excess: int = 0
    violations: list[Violation] = field(default_factory=list)


@dataclass
class TestResult:
    seed: int
    style: int
    chord: int
    blueprint: int
    total_violations: int = 0
    total_vocal_notes: int = 0
    total_accompaniment_notes: int = 0
    track_summaries: dict[str, TrackSummary] = field(default_factory=dict)
    violations: list[Violation] = field(default_factory=list)
    error: Optional[str] = None

    @property
    def violation_rate(self) -> float:
        if self.total_accompaniment_notes == 0:
            return 0.0
        return self.total_violations / self.total_accompaniment_notes * 100

    @property
    def has_violations(self) -> bool:
        return self.total_violations > 0

    @property
    def has_high_severity(self) -> bool:
        return any(v.severity == "high" for v in self.violations)

    def cli_command(self) -> str:
        return (f"./build/bin/midisketch_cli --json "
                f"--seed {self.seed} --style {self.style} "
                f"--chord {self.chord} --blueprint {self.blueprint}")


def classify_severity(excess_semitones: int) -> str:
    """Classify violation severity by excess semitones."""
    if excess_semitones >= 5:
        return "high"
    elif excess_semitones >= 3:
        return "medium"
    else:
        return "low"


def find_crossings(
    vocal_notes: list[dict],
    track_notes: list[dict],
    track_name: str,
    threshold: int = 0,
) -> list[Violation]:
    """Find notes in track_notes that cross above vocal notes."""
    if not vocal_notes or not track_notes:
        return []

    violations = []

    # Build interval tree-like structure for vocal notes
    # Sort vocal notes by start for efficient lookup
    vocal_sorted = sorted(vocal_notes, key=lambda n: n["start_ticks"])

    for note in track_notes:
        n_start = note["start_ticks"]
        n_end = n_start + note["duration_ticks"]
        n_pitch = note["pitch"]

        # Find overlapping vocal notes
        for vn in vocal_sorted:
            v_start = vn["start_ticks"]
            v_end = v_start + vn["duration_ticks"]

            # Early exit: vocal notes past our range
            if v_start >= n_end:
                break

            # Check overlap
            if v_end <= n_start:
                continue

            # Overlapping: check pitch crossing
            v_pitch = vn["pitch"]
            excess = n_pitch - v_pitch - threshold
            if excess > 0:
                bar, beat = tick_to_bar_beat(max(n_start, v_start))
                violations.append(Violation(
                    tick=max(n_start, v_start),
                    bar=bar,
                    beat=beat,
                    track=track_name,
                    vocal_pitch=v_pitch,
                    crossing_pitch=n_pitch,
                    excess_semitones=excess,
                    severity=classify_severity(excess),
                ))
                break  # One violation per accompaniment note is enough

    return violations


def analyze_output_json(
    data: dict,
    check_tracks: list[str],
    threshold: int = 0,
) -> tuple[list[Violation], dict[str, TrackSummary], int, int]:
    """Analyze output.json data for pitch crossings."""
    tracks = data.get("tracks", [])

    # Find vocal track
    vocal_notes = []
    track_notes_map: dict[str, list[dict]] = {}

    for t in tracks:
        name = t.get("name", "").lower()
        notes = t.get("notes", [])
        if name == "vocal":
            vocal_notes = notes
        elif name in check_tracks:
            track_notes_map[name] = notes

    if not vocal_notes:
        return [], {}, 0, 0

    # Find crossings per track
    all_violations = []
    track_summaries = {}
    total_acc_notes = 0

    for track_name in check_tracks:
        notes = track_notes_map.get(track_name, [])
        total_acc_notes += len(notes)
        crossings = find_crossings(vocal_notes, notes, track_name, threshold)

        summary = TrackSummary(track=track_name)
        summary.violation_count = len(crossings)
        summary.max_excess = max((v.excess_semitones for v in crossings), default=0)
        summary.violations = crossings

        track_summaries[track_name] = summary
        all_violations.extend(crossings)

    # Sort by tick
    all_violations.sort(key=lambda v: v.tick)

    return all_violations, track_summaries, len(vocal_notes), total_acc_notes


def run_single_test(
    cli_path: str,
    seed: int,
    style: int,
    chord: int,
    blueprint: int,
    check_tracks: list[str],
    threshold: int,
    work_dir: Path,
    output_dir: Path,
) -> TestResult:
    """Run a single generation and check for pitch crossings."""
    cmd = [
        cli_path,
        "--json",
        "--seed", str(seed),
        "--style", str(style),
        "--chord", str(chord),
        "--blueprint", str(blueprint),
    ]

    # Use unique output file per test
    output_file = output_dir / f"output_{seed}_{style}_{chord}_{blueprint}.json"

    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=60,
            cwd=work_dir,
        )

        if result.returncode != 0:
            return TestResult(
                seed=seed, style=style, chord=chord, blueprint=blueprint,
                error=f"CLI error: {result.stderr[:200]}",
            )

        # Read output.json
        std_output = work_dir / "output.json"
        if not std_output.exists():
            return TestResult(
                seed=seed, style=style, chord=chord, blueprint=blueprint,
                error="output.json not found",
            )

        with open(std_output) as f:
            data = json.load(f)

        violations, track_summaries, vocal_count, acc_count = analyze_output_json(
            data, check_tracks, threshold
        )

        return TestResult(
            seed=seed,
            style=style,
            chord=chord,
            blueprint=blueprint,
            total_violations=len(violations),
            total_vocal_notes=vocal_count,
            total_accompaniment_notes=acc_count,
            track_summaries=track_summaries,
            violations=violations,
        )

    except subprocess.TimeoutExpired:
        return TestResult(
            seed=seed, style=style, chord=chord, blueprint=blueprint,
            error="Timeout (>60s)",
        )
    except Exception as e:
        return TestResult(
            seed=seed, style=style, chord=chord, blueprint=blueprint,
            error=str(e)[:200],
        )


def analyze_existing_file(
    cli_path: str,
    midi_path: Path,
    check_tracks: list[str],
    threshold: int,
    work_dir: Path,
) -> TestResult:
    """Analyze an existing MIDI file for pitch crossings."""
    # Generate JSON from MIDI file
    cmd = [cli_path, "--json", "--input", str(midi_path)]

    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=60,
            cwd=work_dir,
        )

        if result.returncode != 0:
            return TestResult(
                seed=0, style=0, chord=0, blueprint=0,
                error=f"CLI error: {result.stderr[:200]}",
            )

        std_output = work_dir / "output.json"
        if not std_output.exists():
            return TestResult(
                seed=0, style=0, chord=0, blueprint=0,
                error="output.json not found",
            )

        with open(std_output) as f:
            data = json.load(f)

        violations, track_summaries, vocal_count, acc_count = analyze_output_json(
            data, check_tracks, threshold
        )

        return TestResult(
            seed=0, style=0, chord=0, blueprint=0,
            total_violations=len(violations),
            total_vocal_notes=vocal_count,
            total_accompaniment_notes=acc_count,
            track_summaries=track_summaries,
            violations=violations,
        )

    except subprocess.TimeoutExpired:
        return TestResult(seed=0, style=0, chord=0, blueprint=0, error="Timeout (>60s)")
    except Exception as e:
        return TestResult(seed=0, style=0, chord=0, blueprint=0, error=str(e)[:200])


class ProgressCounter:
    def __init__(self, total: int):
        self.total = total
        self.current = 0
        self.lock = threading.Lock()
        self.failed = 0
        self.warned = 0
        self.errors = 0

    def increment(self, result: TestResult):
        with self.lock:
            self.current += 1
            if result.error:
                self.errors += 1
            elif result.has_high_severity:
                self.failed += 1
            elif result.has_violations:
                self.warned += 1


def run_tests(
    cli_path: str,
    seeds: list[int],
    styles: list[int],
    chords: list[int],
    blueprints: list[int],
    check_tracks: list[str],
    threshold: int,
    verbose: bool = False,
    parallel: int = 1,
) -> list[TestResult]:
    """Run tests across all combinations."""
    work_dir = Path.cwd()
    output_dir = work_dir / ".pitch_crossing_check"
    output_dir.mkdir(exist_ok=True)

    configs = [
        (seed, style, chord, blueprint)
        for seed in seeds
        for style in styles
        for chord in chords
        for blueprint in blueprints
    ]

    total = len(configs)
    print(f"Running {total} tests" + (f" with {parallel} parallel workers" if parallel > 1 else "") + "...\n")

    results = []

    if parallel > 1:
        counter = ProgressCounter(total)
        results_dict = {}

        with ThreadPoolExecutor(max_workers=parallel) as executor:
            futures = {
                executor.submit(
                    run_single_test, cli_path, seed, style, chord, blueprint,
                    check_tracks, threshold, work_dir, output_dir
                ): (seed, style, chord, blueprint)
                for seed, style, chord, blueprint in configs
            }

            for future in as_completed(futures):
                config = futures[future]
                result = future.result()
                results_dict[config] = result
                counter.increment(result)

                with counter.lock:
                    i = counter.current
                    if result.error:
                        print(f"[{i:4d}/{total}] seed={config[0]:3d} style={config[1]:2d} chord={config[2]:2d} bp={config[3]}: ERROR")
                    elif result.has_high_severity:
                        print(f"[{i:4d}/{total}] seed={config[0]:3d} style={config[1]:2d} chord={config[2]:2d} bp={config[3]}: "
                              f"\033[31mFAIL\033[0m violations={result.total_violations}")
                    elif result.has_violations:
                        print(f"[{i:4d}/{total}] seed={config[0]:3d} style={config[1]:2d} chord={config[2]:2d} bp={config[3]}: "
                              f"\033[33mWARN\033[0m violations={result.total_violations}")
                    elif verbose:
                        print(f"[{i:4d}/{total}] seed={config[0]:3d} style={config[1]:2d} chord={config[2]:2d} bp={config[3]}: OK")
                    else:
                        print(f"\r[{i:4d}/{total}] Testing... (F:{counter.failed} W:{counter.warned} E:{counter.errors})", end="", flush=True)

        results = [results_dict[config] for config in configs]

        if not verbose:
            print("\r" + " " * 60 + "\r", end="")
    else:
        for i, (seed, style, chord, blueprint) in enumerate(configs, 1):
            result = run_single_test(
                cli_path, seed, style, chord, blueprint,
                check_tracks, threshold, work_dir, output_dir
            )
            results.append(result)

            if result.error:
                print(f"[{i:4d}/{total}] seed={seed:3d} style={style:2d} chord={chord:2d} bp={blueprint}: ERROR")
            elif result.has_high_severity:
                print(f"[{i:4d}/{total}] seed={seed:3d} style={style:2d} chord={chord:2d} bp={blueprint}: "
                      f"\033[31mFAIL\033[0m violations={result.total_violations}")
            elif result.has_violations:
                print(f"[{i:4d}/{total}] seed={seed:3d} style={style:2d} chord={chord:2d} bp={blueprint}: "
                      f"\033[33mWARN\033[0m violations={result.total_violations}")
            elif verbose:
                print(f"[{i:4d}/{total}] seed={seed:3d} style={style:2d} chord={chord:2d} bp={blueprint}: OK")
            else:
                print(f"\r[{i:4d}/{total}] Testing...", end="", flush=True)

        if not verbose:
            print("\r" + " " * 40 + "\r", end="")

    return results


def print_violation_detail(v: Violation, indent: str = "    "):
    """Print detailed information about a single violation."""
    color = "\033[31m" if v.severity == "high" else "\033[33m" if v.severity == "medium" else "\033[36m"
    reset = "\033[0m"
    print(f"{indent}Bar {v.bar}, beat {v.beat:.1f} (tick {v.tick})")
    print(f"{indent}  {color}{v.track:8s}{reset}  "
          f"vocal={pitch_name(v.vocal_pitch):4s}({v.vocal_pitch})  "
          f"crossing={pitch_name(v.crossing_pitch):4s}({v.crossing_pitch})  "
          f"excess=+{v.excess_semitones} semitones [{v.severity}]")


def print_summary(results: list[TestResult], check_tracks: list[str], threshold: int) -> bool:
    """Print comprehensive summary."""
    total = len(results)
    errors = [r for r in results if r.error]
    high_sev = [r for r in results if r.has_high_severity]
    with_violations = [r for r in results if r.has_violations and not r.has_high_severity]

    print("\n" + "=" * 80)
    print("PITCH CROSSING CHECK SUMMARY")
    print("=" * 80)

    print(f"\n  Check tracks: {', '.join(check_tracks)}")
    print(f"  Threshold: {threshold} semitones")

    # Basic counts
    print(f"\n{'Test Results':40s}")
    print("-" * 40)
    print(f"  Total tests:             {total:>6d}")
    print(f"  Clean (no crossings):    {total - len(errors) - len(high_sev) - len(with_violations):>6d}")
    print(f"  Warnings (1-4 semitones):{len(with_violations):>6d}")
    print(f"  \033[31mFailed (5+ semitones):   {len(high_sev):>6d}\033[0m")
    print(f"  Errors:                  {len(errors):>6d}")

    # Aggregate stats
    total_violations = sum(r.total_violations for r in results)
    total_acc_notes = sum(r.total_accompaniment_notes for r in results)
    overall_rate = (total_violations / total_acc_notes * 100) if total_acc_notes > 0 else 0

    print(f"\n{'Aggregate Statistics':40s}")
    print("-" * 40)
    print(f"  Total crossing violations: {total_violations:>6d}")
    print(f"  Total accompaniment notes: {total_acc_notes:>6d}")
    print(f"  Overall violation rate:    {overall_rate:>5.1f}%")

    # Per-track breakdown
    track_agg: dict[str, dict] = defaultdict(lambda: {"count": 0, "max_excess": 0, "high": 0, "medium": 0, "low": 0})
    for r in results:
        for track_name, summary in r.track_summaries.items():
            agg = track_agg[track_name]
            agg["count"] += summary.violation_count
            agg["max_excess"] = max(agg["max_excess"], summary.max_excess)
            for v in summary.violations:
                agg[v.severity] += 1

    if any(v["count"] > 0 for v in track_agg.values()):
        print(f"\n{'Violations by Track':40s}")
        print("-" * 60)
        print(f"  {'Track':<12s} {'Count':>6s} {'Max Excess':>10s} {'High':>6s} {'Medium':>6s} {'Low':>6s}")
        print(f"  {'-'*12} {'-'*6} {'-'*10} {'-'*6} {'-'*6} {'-'*6}")
        for track_name in check_tracks:
            agg = track_agg.get(track_name, {"count": 0, "max_excess": 0, "high": 0, "medium": 0, "low": 0})
            if agg["count"] > 0:
                print(f"  {track_name:<12s} {agg['count']:>6d} {agg['max_excess']:>10d} "
                      f"{agg['high']:>6d} {agg['medium']:>6d} {agg['low']:>6d}")

    # Blueprint breakdown
    bp_stats: dict[int, dict] = {}
    for r in results:
        bp = r.blueprint
        if bp not in bp_stats:
            bp_stats[bp] = {"tests": 0, "violations": 0, "high_count": 0}
        bp_stats[bp]["tests"] += 1
        bp_stats[bp]["violations"] += r.total_violations
        bp_stats[bp]["high_count"] += sum(1 for v in r.violations if v.severity == "high")

    if len(bp_stats) > 1:
        problematic = [(bp, s) for bp, s in bp_stats.items() if s["violations"] > 0]
        if problematic:
            print(f"\n{'Violations by Blueprint':40s}")
            print("-" * 40)
            for bp, stats in sorted(problematic, key=lambda x: -x[1]["violations"]):
                bp_name = BLUEPRINT_NAMES.get(bp, f"Blueprint{bp}")
                print(f"  {bp_name:20s} violations:{stats['violations']:>4d} high:{stats['high_count']:>3d} /{stats['tests']} tests")

    # Show worst cases
    worst = sorted([r for r in results if r.has_violations], key=lambda r: -r.total_violations)[:5]
    if worst:
        print(f"\n{'Worst Cases (top 5)':40s}")
        print("-" * 80)
        for r in worst:
            bp_name = BLUEPRINT_NAMES.get(r.blueprint, f"bp{r.blueprint}")
            print(f"\n  seed={r.seed}, style={r.style}, chord={r.chord}, {bp_name}")
            print(f"  violations={r.total_violations}, rate={r.violation_rate:.1f}%")
            print(f"  Reproduce: {r.cli_command()}")
            # Show first few violations
            for v in r.violations[:3]:
                print_violation_detail(v, indent="    ")
            if len(r.violations) > 3:
                print(f"    ... and {len(r.violations) - 3} more")

    print("\n" + "=" * 80)
    passed = len(high_sev) == 0 and len(errors) == 0
    if passed:
        if with_violations:
            print(f"\033[33mRESULT: PASSED with warnings ({sum(r.total_violations for r in with_violations)} minor crossings)\033[0m")
        else:
            print("\033[32mRESULT: PASSED\033[0m")
    else:
        print("\033[31mRESULT: FAILED\033[0m")
    print("=" * 80)

    return passed


def save_json_report(results: list[TestResult], check_tracks: list[str], threshold: int, output_path: str):
    """Save detailed JSON report."""
    # Per-track aggregate
    track_agg: dict[str, dict] = defaultdict(lambda: {"count": 0, "max_excess": 0})
    for r in results:
        for track_name, summary in r.track_summaries.items():
            track_agg[track_name]["count"] += summary.violation_count
            track_agg[track_name]["max_excess"] = max(
                track_agg[track_name]["max_excess"], summary.max_excess
            )

    total_violations = sum(r.total_violations for r in results)
    total_acc = sum(r.total_accompaniment_notes for r in results)

    report = {
        "config": {
            "check_tracks": check_tracks,
            "threshold": threshold,
        },
        "summary": {
            "total_tests": len(results),
            "tests_with_violations": len([r for r in results if r.has_violations]),
            "tests_with_high_severity": len([r for r in results if r.has_high_severity]),
            "errors": len([r for r in results if r.error]),
            "total_violations": total_violations,
            "total_accompaniment_notes": total_acc,
            "violation_rate": (total_violations / total_acc * 100) if total_acc > 0 else 0,
        },
        "by_track": {
            track: {"violation_count": agg["count"], "max_excess_semitones": agg["max_excess"]}
            for track, agg in track_agg.items()
        },
        "cases_with_violations": [
            {
                "seed": r.seed,
                "style": r.style,
                "chord": r.chord,
                "blueprint": r.blueprint,
                "total_violations": r.total_violations,
                "violation_rate": r.violation_rate,
                "reproduce_command": r.cli_command(),
                "violations": [
                    {
                        "tick": v.tick,
                        "bar": v.bar,
                        "beat": v.beat,
                        "track": v.track,
                        "vocal_pitch": v.vocal_pitch,
                        "vocal_note": pitch_name(v.vocal_pitch),
                        "crossing_pitch": v.crossing_pitch,
                        "crossing_note": pitch_name(v.crossing_pitch),
                        "excess_semitones": v.excess_semitones,
                        "severity": v.severity,
                    }
                    for v in r.violations
                ],
            }
            for r in results
            if r.has_violations
        ],
    }

    with open(output_path, "w") as f:
        json.dump(report, f, indent=2)
    print(f"\nDetailed report saved to: {output_path}")


def main():
    parser = argparse.ArgumentParser(
        description="Check if accompaniment tracks cross above vocal pitch",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s --quick              # 10 seeds, style 0, all blueprints
  %(prog)s --medium             # 50 seeds, all styles, all blueprints
  %(prog)s --full               # 100 seeds, all styles, all chords, all blueprints
  %(prog)s --threshold 2        # Allow up to 2 semitones above vocal
  %(prog)s --tracks chord,motif # Only check chord and motif tracks
  %(prog)s -j 4                 # Run with 4 parallel workers
  %(prog)s -i output.mid        # Analyze existing MIDI file

Severity levels:
  high:   5+ semitones above vocal (melody clearly buried)
  medium: 3-4 semitones above vocal (noticeable crossing)
  low:    1-2 semitones above vocal (minor crossing)
        """)

    parser.add_argument("--cli", default="./build/bin/midisketch_cli",
                        help="Path to CLI (default: ./build/bin/midisketch_cli)")

    # Analyze existing MIDI files
    parser.add_argument("--input", "-i", type=str, nargs="+",
                        help="Analyze existing MIDI file(s) instead of generating")

    # Crossing-specific options
    parser.add_argument("--tracks", type=str, default=",".join(DEFAULT_CHECK_TRACKS),
                        help=f"Comma-separated tracks to check (default: {','.join(DEFAULT_CHECK_TRACKS)})")
    parser.add_argument("--threshold", type=int, default=0,
                        help="Allowed semitones above vocal (default: 0)")

    # Preset modes
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--quick", action="store_true",
                      help="Quick: 10 seeds x style 0 x chord 0 x all blueprints")
    mode.add_argument("--medium", action="store_true",
                      help="Medium: 50 seeds x all styles x chord 0 x all blueprints")
    mode.add_argument("--full", action="store_true",
                      help="Full: 100 seeds x all styles x all chords x all blueprints")

    # Custom configuration
    parser.add_argument("--seeds", type=int, default=20,
                        help="Number of seeds (default: 20)")
    parser.add_argument("--seed-start", type=int, default=1,
                        help="Starting seed (default: 1)")
    parser.add_argument("--seed-range", type=str,
                        help="Seed range, e.g., '1-50'")
    parser.add_argument("--seed", type=int,
                        help="Single seed to test")
    parser.add_argument("--styles", type=str, default="all",
                        help="Styles: 'all' or comma-separated (default: all)")
    parser.add_argument("--chords", type=str, default="0",
                        help="Chords: 'all' or comma-separated (default: 0)")
    parser.add_argument("--bp", "--blueprints", type=str, default="all", dest="blueprints",
                        help="Blueprints: 'all' or comma-separated (default: all)")
    parser.add_argument("--style", type=int, help="Single style to test")
    parser.add_argument("--chord", type=int, help="Single chord to test")
    parser.add_argument("--blueprint", type=int, help="Single blueprint to test")

    # Parallel execution
    parser.add_argument("-j", "--jobs", type=int, default=1,
                        help="Number of parallel workers (default: 1)")

    # Output options
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="Show all test results")
    parser.add_argument("-o", "--output", type=str,
                        help="Save JSON report to file")

    args = parser.parse_args()

    check_tracks = [t.strip().lower() for t in args.tracks.split(",")]

    # Handle --input mode
    if args.input:
        import glob as glob_module
        work_dir = Path.cwd()
        files = []
        for pattern in args.input:
            files.extend(glob_module.glob(pattern, recursive=True))
        files = sorted(set(files))

        if not files:
            print(f"No files matched: {args.input}")
            sys.exit(1)

        print(f"Analyzing {len(files)} MIDI file(s)...\n")
        all_results = []
        for filepath in files:
            result = analyze_existing_file(
                args.cli, Path(filepath), check_tracks, args.threshold, work_dir
            )
            all_results.append(result)
            if result.error:
                print(f"  {filepath}: ERROR - {result.error}")
            elif result.has_violations:
                print(f"  {filepath}: {result.total_violations} crossings")
            else:
                print(f"  {filepath}: OK")

        if args.output:
            save_json_report(all_results, check_tracks, args.threshold, args.output)
        passed = print_summary(all_results, check_tracks, args.threshold)
        sys.exit(0 if passed else 1)

    # Parse configurations
    if args.quick:
        seeds = list(range(1, 11))
        styles = [0]
        chords = [0]
        blueprints = list(range(9))
    elif args.medium:
        seeds = list(range(1, 51))
        styles = list(range(13))
        chords = [0]
        blueprints = list(range(9))
    elif args.full:
        seeds = list(range(1, 101))
        styles = list(range(13))
        chords = list(range(20))
        blueprints = list(range(9))
    else:
        # Custom
        if args.seed is not None:
            seeds = [args.seed]
        elif args.seed_range:
            start, end = map(int, args.seed_range.split("-"))
            seeds = list(range(start, end + 1))
        else:
            seeds = list(range(args.seed_start, args.seed_start + args.seeds))

        if args.style is not None:
            styles = [args.style]
        else:
            styles = list(range(15)) if args.styles == "all" else [int(x) for x in args.styles.split(",")]

        if args.chord is not None:
            chords = [args.chord]
        else:
            chords = list(range(22)) if args.chords == "all" else [int(x) for x in args.chords.split(",")]

        if args.blueprint is not None:
            blueprints = [args.blueprint]
        else:
            blueprints = list(range(9)) if args.blueprints == "all" else [int(x) for x in args.blueprints.split(",")]

    # Run tests
    results = run_tests(
        args.cli, seeds, styles, chords, blueprints,
        check_tracks, args.threshold, args.verbose, args.jobs,
    )

    # Save JSON report if requested
    if args.output:
        save_json_report(results, check_tracks, args.threshold, args.output)

    # Print summary
    passed = print_summary(results, check_tracks, args.threshold)
    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    main()
