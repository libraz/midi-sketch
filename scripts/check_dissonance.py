#!/usr/bin/env python3
"""
check_dissonance.py - Comprehensive dissonance testing for midi-sketch

Run multiple seeds/configurations to find dissonance issues.
Outputs detailed information for debugging.
"""

import subprocess
import json
import sys
import argparse
from pathlib import Path
from dataclasses import dataclass, field
from typing import Optional


# Source file mapping for quick navigation
SOURCE_FILES = {
    "melody_phrase": "src/track/melody_designer.cpp",
    "hook": "src/track/melody_designer.cpp",
    "bass_pattern": "src/track/bass.cpp",
    "chord_voicing": "src/track/chord_track.cpp",
    "arpeggio": "src/track/arpeggio.cpp",
    "aux": "src/track/aux.cpp",
    "motif": "src/track/motif.cpp",
    "drums": "src/track/drums.cpp",
    "collision_avoid": "src/core/collision_avoider.cpp",
    "post_process": "src/core/post_processor.cpp",
}


@dataclass
class Issue:
    type: str
    severity: str
    tick: int
    bar: int
    beat: float
    track: str
    pitch: int
    pitch_name: str
    chord_name: str
    chord_tones: list[str]
    provenance_source: str = ""
    original_pitch: int = 0
    description: str = ""
    # For simultaneous_clash: involved notes
    clash_notes: list[dict] = field(default_factory=list)
    interval_name: str = ""
    # For sustained_over_chord_change
    original_chord: str = ""
    new_chord: str = ""


@dataclass
class TestResult:
    seed: int
    style: int
    chord: int
    blueprint: int
    simultaneous_clashes: int = 0
    high_severity: int = 0
    medium_severity: int = 0
    low_severity: int = 0
    total_issues: int = 0
    critical_issues: list[Issue] = field(default_factory=list)
    error: Optional[str] = None

    @property
    def has_critical(self) -> bool:
        return self.simultaneous_clashes > 0

    @property
    def has_warnings(self) -> bool:
        return self.high_severity > 0

    def cli_command(self) -> str:
        return (f"./build/bin/midisketch_cli --analyze "
                f"--seed {self.seed} --style {self.style} "
                f"--chord {self.chord} --blueprint {self.blueprint}")


def parse_issues(analysis: dict) -> list[Issue]:
    """Parse issues from analysis.json into Issue objects."""
    issues = []
    for item in analysis.get("issues", []):
        issue_type = item.get("type", "")

        # Handle provenance - may be in different locations based on issue type
        prov = item.get("provenance", {})
        prov_source = prov.get("generation_source", "") or prov.get("source", "")

        if issue_type == "simultaneous_clash":
            # Clash has multiple notes involved
            notes = item.get("notes", [])
            # Collect provenance sources from all notes
            sources = [n.get("provenance", {}).get("source", "") for n in notes]
            issues.append(Issue(
                type=issue_type,
                severity=item.get("severity", ""),
                tick=item.get("tick", 0),
                bar=item.get("bar", 0),
                beat=item.get("beat", 0),
                track=", ".join(n.get("track", "") for n in notes),
                pitch=notes[0].get("pitch", 0) if notes else 0,
                pitch_name=", ".join(n.get("name", "") for n in notes),
                chord_name="",
                chord_tones=[],
                provenance_source=", ".join(set(s for s in sources if s)),
                original_pitch=0,
                description=f"{item.get('interval_name', '')} clash",
                clash_notes=notes,
                interval_name=item.get("interval_name", ""),
            ))
        elif issue_type == "sustained_over_chord_change":
            # Sustained note over chord change
            issues.append(Issue(
                type=issue_type,
                severity=item.get("severity", ""),
                tick=item.get("tick", 0),
                bar=item.get("bar", 0),
                beat=item.get("beat", 0),
                track=item.get("track", ""),
                pitch=item.get("pitch", 0),
                pitch_name=item.get("pitch_name", ""),
                chord_name=item.get("new_chord", ""),
                chord_tones=item.get("new_chord_tones", []),
                provenance_source=prov_source,  # Use extracted provenance
                original_pitch=prov.get("original_pitch", 0),
                description=f"held over {item.get('original_chord', '')} -> {item.get('new_chord', '')}",
                original_chord=item.get("original_chord", ""),
                new_chord=item.get("new_chord", ""),
            ))
        else:
            # Regular issue (non_chord_tone, etc.)
            prov = item.get("provenance", {})
            issues.append(Issue(
                type=issue_type,
                severity=item.get("severity", ""),
                tick=item.get("tick", 0),
                bar=item.get("bar", 0),
                beat=item.get("beat", 0),
                track=item.get("track", ""),
                pitch=item.get("pitch", 0),
                pitch_name=item.get("pitch_name", ""),
                chord_name=item.get("chord_name", ""),
                chord_tones=item.get("chord_tones", []),
                provenance_source=prov.get("generation_source", ""),
                original_pitch=prov.get("original_pitch", 0),
                description="",
            ))
    return issues


def run_single_test(
    cli_path: str,
    seed: int,
    style: int,
    chord: int,
    blueprint: int,
    work_dir: Path,
) -> TestResult:
    """Run a single generation test and return the result."""
    cmd = [
        cli_path,
        "--analyze",
        "--seed", str(seed),
        "--style", str(style),
        "--chord", str(chord),
        "--blueprint", str(blueprint),
    ]

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

        analysis_file = work_dir / "analysis.json"
        if not analysis_file.exists():
            # Try current directory as fallback
            analysis_file = Path("analysis.json")
        if not analysis_file.exists():
            return TestResult(
                seed=seed, style=style, chord=chord, blueprint=blueprint,
                error="analysis.json not found",
            )

        with open(analysis_file) as f:
            analysis = json.load(f)

        summary = analysis.get("summary", {})
        all_issues = parse_issues(analysis)

        # Filter critical issues (simultaneous clashes or high severity)
        critical = [i for i in all_issues
                   if i.type == "simultaneous_clash" or i.severity == "high"]

        return TestResult(
            seed=seed,
            style=style,
            chord=chord,
            blueprint=blueprint,
            simultaneous_clashes=summary.get("simultaneous_clashes", 0),
            high_severity=summary.get("high_severity", 0),
            medium_severity=summary.get("medium_severity", 0),
            low_severity=summary.get("low_severity", 0),
            total_issues=summary.get("total_issues", 0),
            critical_issues=critical,
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


def run_tests(
    cli_path: str,
    seeds: list[int],
    styles: list[int],
    chords: list[int],
    blueprints: list[int],
    verbose: bool = False,
) -> list[TestResult]:
    """Run tests across all combinations."""
    # Use current working directory (script should be run from project root)
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
    print(f"Running {total} tests...\n")

    for i, (seed, style, chord, blueprint) in enumerate(configs, 1):
        result = run_single_test(cli_path, seed, style, chord, blueprint, work_dir)
        results.append(result)

        # Progress display
        if result.error:
            print(f"[{i:4d}/{total}] seed={seed:3d} style={style:2d} chord={chord:2d} bp={blueprint}: ERROR")
        elif result.has_critical:
            print(f"[{i:4d}/{total}] seed={seed:3d} style={style:2d} chord={chord:2d} bp={blueprint}: "
                  f"\033[31mFAIL\033[0m clashes={result.simultaneous_clashes}")
        elif result.has_warnings:
            print(f"[{i:4d}/{total}] seed={seed:3d} style={style:2d} chord={chord:2d} bp={blueprint}: "
                  f"\033[33mWARN\033[0m high={result.high_severity}")
        elif verbose:
            print(f"[{i:4d}/{total}] seed={seed:3d} style={style:2d} chord={chord:2d} bp={blueprint}: OK")
        else:
            # Inline progress for OK results
            print(f"\r[{i:4d}/{total}] Testing...", end="", flush=True)

    if not verbose:
        print("\r" + " " * 40 + "\r", end="")  # Clear progress line

    return results


def print_issue_detail(issue: Issue, indent: str = "    "):
    """Print detailed information about a single issue."""
    print(f"{indent}Bar {issue.bar}, beat {issue.beat:.1f} (tick {issue.tick})")

    if issue.type == "simultaneous_clash":
        # Show clash details
        print(f"{indent}  \033[31mClash: {issue.interval_name}\033[0m")
        for note in issue.clash_notes:
            prov = note.get("provenance", {})
            src = prov.get("source", "unknown")
            src_file = SOURCE_FILES.get(src, "unknown")
            print(f"{indent}    {note.get('track'):8s} {note.get('name'):4s} "
                  f"<- {src} ({src_file})")
    elif issue.type == "sustained_over_chord_change":
        # Show sustained over chord change
        print(f"{indent}  \033[33mSustained: {issue.track} {issue.pitch_name} "
              f"held over {issue.original_chord} -> {issue.new_chord}\033[0m")
        print(f"{indent}  New chord tones: [{', '.join(issue.chord_tones)}]")
        if issue.provenance_source:
            src_file = SOURCE_FILES.get(issue.provenance_source, "unknown")
            print(f"{indent}  Source: {issue.provenance_source} ({src_file})")
        else:
            print(f"{indent}  Source: \033[31mNO PROVENANCE\033[0m (note origin unknown)")
    else:
        # Non-chord tone or other
        sources = [s.strip() for s in issue.provenance_source.split(",") if s.strip()]
        src_files = [SOURCE_FILES.get(s, "unknown") for s in sources]

        print(f"{indent}  Track: {issue.track}, Pitch: {issue.pitch_name} ({issue.pitch})")
        if issue.chord_name:
            print(f"{indent}  Chord: {issue.chord_name} [{', '.join(issue.chord_tones)}]")
        if sources:
            print(f"{indent}  Source: {', '.join(sources)} -> {', '.join(set(src_files))}")
        if issue.original_pitch and issue.original_pitch != issue.pitch:
            print(f"{indent}  Original pitch: {issue.original_pitch}")


def print_summary(results: list[TestResult]) -> bool:
    """Print test summary with debugging info."""
    total = len(results)
    errors = [r for r in results if r.error]
    critical = [r for r in results if r.has_critical]
    warnings = [r for r in results if r.has_warnings and not r.has_critical]

    print("\n" + "=" * 70)
    print("DISSONANCE CHECK SUMMARY")
    print("=" * 70)
    print(f"Total tests:           {total}")
    print(f"Passed:                {total - len(errors) - len(critical) - len(warnings)}")
    print(f"Warnings (high sev):   {len(warnings)}")
    print(f"\033[31mFailed (clashes):      {len(critical)}\033[0m")
    print(f"Errors:                {len(errors)}")

    # Print critical failures with full details
    if critical:
        print("\n" + "-" * 70)
        print("\033[31mCRITICAL: Simultaneous Clashes Detected\033[0m")
        print("-" * 70)

        for r in critical:
            print(f"\n  \033[1m[seed={r.seed}, style={r.style}, chord={r.chord}, bp={r.blueprint}]\033[0m")
            print(f"  Reproduce: {r.cli_command()}")
            # Show debug command with actual tick from first issue
            if r.critical_issues:
                tick = r.critical_issues[0].tick
                print(f"  Debug:     {r.cli_command()} --dump-collisions-at {tick}")

            if r.critical_issues:
                print(f"  Issues ({len(r.critical_issues)}):")
                for issue in r.critical_issues[:5]:  # Show first 5
                    print_issue_detail(issue)
                if len(r.critical_issues) > 5:
                    print(f"    ... and {len(r.critical_issues) - 5} more")

    # Print warnings summary
    if warnings:
        print("\n" + "-" * 70)
        print("\033[33mWARNINGS: High Severity Non-chord Tones\033[0m")
        print("-" * 70)

        for r in warnings[:10]:  # Show first 10
            print(f"\n  [seed={r.seed}, style={r.style}, chord={r.chord}, bp={r.blueprint}] "
                  f"high={r.high_severity}")
            print(f"  Reproduce: {r.cli_command()}")

        if len(warnings) > 10:
            print(f"\n  ... and {len(warnings) - 10} more warnings")

    # Statistics by source
    source_stats: dict[str, dict] = {}
    for r in results:
        for issue in r.critical_issues:
            if issue.type == "simultaneous_clash":
                # Count each note's source separately
                for note in issue.clash_notes:
                    src = note.get("provenance", {}).get("source", "unknown") or "unknown"
                    if src not in source_stats:
                        source_stats[src] = {"count": 0, "file": SOURCE_FILES.get(src, "unknown")}
                    source_stats[src]["count"] += 1
            else:
                src = issue.provenance_source or "unknown"
                if src not in source_stats:
                    source_stats[src] = {"count": 0, "file": SOURCE_FILES.get(src, "unknown")}
                source_stats[src]["count"] += 1

    if source_stats:
        print("\n" + "-" * 70)
        print("Issues by Generation Source")
        print("-" * 70)
        for src, stats in sorted(source_stats.items(), key=lambda x: -x[1]["count"]):
            print(f"  {src:20s}: {stats['count']:3d} issues -> {stats['file']}")

    # Statistics by blueprint
    bp_stats: dict[int, dict] = {}
    for r in results:
        bp = r.blueprint
        if bp not in bp_stats:
            bp_stats[bp] = {"tests": 0, "clashes": 0, "high": 0}
        bp_stats[bp]["tests"] += 1
        bp_stats[bp]["clashes"] += r.simultaneous_clashes
        bp_stats[bp]["high"] += r.high_severity

    if len(bp_stats) > 1:
        print("\n" + "-" * 70)
        print("Issues by Blueprint")
        print("-" * 70)
        for bp, stats in sorted(bp_stats.items()):
            if stats["clashes"] > 0 or stats["high"] > 0:
                print(f"  Blueprint {bp}: {stats['clashes']} clashes, "
                      f"{stats['high']} high severity (/{stats['tests']} tests)")

    print("\n" + "=" * 70)
    passed = len(critical) == 0 and len(errors) == 0
    if passed:
        print("\033[32mRESULT: PASSED\033[0m")
    else:
        print("\033[31mRESULT: FAILED\033[0m")
    print("=" * 70)

    return passed


def save_json_report(results: list[TestResult], output_path: str):
    """Save detailed JSON report for further analysis."""
    critical = [r for r in results if r.has_critical or r.has_warnings]

    report = {
        "summary": {
            "total_tests": len(results),
            "failed": len([r for r in results if r.has_critical]),
            "warnings": len([r for r in results if r.has_warnings and not r.has_critical]),
            "errors": len([r for r in results if r.error]),
        },
        "failed_cases": [
            {
                "seed": r.seed,
                "style": r.style,
                "chord": r.chord,
                "blueprint": r.blueprint,
                "simultaneous_clashes": r.simultaneous_clashes,
                "high_severity": r.high_severity,
                "reproduce_command": r.cli_command(),
                "issues": [
                    {
                        "type": i.type,
                        "severity": i.severity,
                        "tick": i.tick,
                        "bar": i.bar,
                        "beat": i.beat,
                        "track": i.track,
                        "pitch": i.pitch,
                        "pitch_name": i.pitch_name,
                        "chord_name": i.chord_name,
                        "provenance_source": i.provenance_source,
                        "source_file": SOURCE_FILES.get(i.provenance_source, "unknown"),
                    }
                    for i in r.critical_issues
                ],
            }
            for r in critical
        ],
    }

    with open(output_path, "w") as f:
        json.dump(report, f, indent=2)
    print(f"\nDetailed report saved to: {output_path}")


def main():
    parser = argparse.ArgumentParser(
        description="Check for dissonance issues across multiple configurations",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s --quick              # 10 seeds, style 0, all blueprints
  %(prog)s --medium             # 50 seeds, all styles, all blueprints
  %(prog)s --seeds 100          # Full test with 100 seeds
  %(prog)s --seed-range 1-20    # Test seeds 1-20
  %(prog)s --styles 0,5 --bp 0  # Specific style and blueprint
        """)

    parser.add_argument("--cli", default="./build/bin/midisketch_cli",
                        help="Path to CLI (default: ./build/bin/midisketch_cli)")

    # Preset modes
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--quick", action="store_true",
                      help="Quick: 10 seeds × style 0 × chord 0 × all blueprints")
    mode.add_argument("--medium", action="store_true",
                      help="Medium: 50 seeds × all styles × chord 0 × all blueprints")
    mode.add_argument("--full", action="store_true",
                      help="Full: 100 seeds × all styles × all chords × all blueprints")

    # Custom configuration
    parser.add_argument("--seeds", type=int, default=20,
                        help="Number of seeds (default: 20)")
    parser.add_argument("--seed-start", type=int, default=1,
                        help="Starting seed (default: 1)")
    parser.add_argument("--seed-range", type=str,
                        help="Seed range, e.g., '1-50'")
    parser.add_argument("--styles", type=str, default="0",
                        help="Styles: 'all' or comma-separated (default: 0)")
    parser.add_argument("--chords", type=str, default="0",
                        help="Chords: 'all' or comma-separated (default: 0)")
    parser.add_argument("--bp", "--blueprints", type=str, default="all", dest="blueprints",
                        help="Blueprints: 'all' or comma-separated (default: all)")

    # Output options
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="Show all test results")
    parser.add_argument("-o", "--output", type=str,
                        help="Save JSON report to file")

    args = parser.parse_args()

    # Parse configurations based on mode
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
        # Custom configuration
        if args.seed_range:
            start, end = map(int, args.seed_range.split("-"))
            seeds = list(range(start, end + 1))
        else:
            seeds = list(range(args.seed_start, args.seed_start + args.seeds))

        styles = list(range(13)) if args.styles == "all" else [int(x) for x in args.styles.split(",")]
        chords = list(range(20)) if args.chords == "all" else [int(x) for x in args.chords.split(",")]
        blueprints = list(range(9)) if args.blueprints == "all" else [int(x) for x in args.blueprints.split(",")]

    # Run tests
    results = run_tests(args.cli, seeds, styles, chords, blueprints, args.verbose)

    # Save JSON report if requested
    if args.output:
        save_json_report(results, args.output)

    # Print summary
    passed = print_summary(results)
    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    main()
