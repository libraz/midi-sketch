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
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed
import threading


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

# Track names for display
TRACK_NAMES = ["vocal", "chord", "bass", "motif", "arpeggio", "aux"]

# Interval names for display
INTERVAL_NAMES = {
    0: "unison",
    1: "minor 2nd",
    2: "major 2nd",
    3: "minor 3rd",
    4: "major 3rd",
    5: "perfect 4th",
    6: "tritone",
    7: "perfect 5th",
    8: "minor 6th",
    9: "major 6th",
    10: "minor 7th",
    11: "major 7th",
}

# Section types
SECTION_NAMES = {
    0: "Intro",
    1: "A",
    2: "B",
    3: "Chorus",
    4: "Bridge",
    5: "Outro",
    6: "Interlude",
    7: "Chant",
    8: "MixBreak",
    9: "Drop",
}

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
    interval_semitones: int = 0
    # For sustained_over_chord_change
    original_chord: str = ""
    new_chord: str = ""
    # Track pair (for clash analysis)
    track_pair: tuple[str, str] = ("", "")


@dataclass
class TestResult:
    seed: int
    style: int
    chord: int
    blueprint: int
    simultaneous_clashes: int = 0
    non_chord_tones: int = 0
    sustained_over_chord: int = 0
    non_diatonic: int = 0
    high_severity: int = 0
    medium_severity: int = 0
    low_severity: int = 0
    total_issues: int = 0
    all_issues: list[Issue] = field(default_factory=list)
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
            # Extract track pair
            tracks = sorted([n.get("track", "") for n in notes])
            track_pair = (tracks[0], tracks[1]) if len(tracks) >= 2 else ("", "")

            interval_semitones = item.get("interval_semitones", 0)

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
                interval_semitones=interval_semitones,
                track_pair=track_pair,
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
                provenance_source=prov_source,
                original_pitch=prov.get("original_pitch", 0),
                description=f"held over {item.get('original_chord', '')} -> {item.get('new_chord', '')}",
                original_chord=item.get("original_chord", ""),
                new_chord=item.get("new_chord", ""),
            ))
        elif issue_type == "non_diatonic_note":
            # Non-diatonic note
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
                chord_name="",
                chord_tones=[],
                provenance_source=prov.get("source", ""),
                original_pitch=prov.get("original_pitch", 0),
                description=f"non-diatonic in {item.get('key', 'C major')}",
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


# Thread-safe counter for progress display
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
            elif result.has_critical:
                self.failed += 1
            elif result.has_warnings:
                self.warned += 1


def run_single_test(
    cli_path: str,
    seed: int,
    style: int,
    chord: int,
    blueprint: int,
    work_dir: Path,
    output_dir: Path,
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

    # Use unique output file per test to avoid race conditions
    analysis_file = output_dir / f"analysis_{seed}_{style}_{chord}_{blueprint}.json"

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

        # Read from the standard analysis.json location
        std_analysis = work_dir / "analysis.json"
        if std_analysis.exists():
            # Copy to unique file for this test
            with open(std_analysis) as f:
                analysis = json.load(f)
        else:
            return TestResult(
                seed=seed, style=style, chord=chord, blueprint=blueprint,
                error="analysis.json not found",
            )

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
            non_chord_tones=summary.get("non_chord_tones", 0),
            sustained_over_chord=summary.get("sustained_over_chord_change", 0),
            non_diatonic=summary.get("non_diatonic_notes", 0),
            high_severity=summary.get("high_severity", 0),
            medium_severity=summary.get("medium_severity", 0),
            low_severity=summary.get("low_severity", 0),
            total_issues=summary.get("total_issues", 0),
            all_issues=all_issues,
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
    parallel: int = 1,
) -> list[TestResult]:
    """Run tests across all combinations."""
    work_dir = Path.cwd()
    output_dir = work_dir / ".dissonance_check"
    output_dir.mkdir(exist_ok=True)

    results = []

    configs = [
        (seed, style, chord, blueprint)
        for seed in seeds
        for style in styles
        for chord in chords
        for blueprint in blueprints
    ]

    total = len(configs)
    print(f"Running {total} tests" + (f" with {parallel} parallel workers" if parallel > 1 else "") + "...\n")

    if parallel > 1:
        # Parallel execution
        counter = ProgressCounter(total)
        results_dict = {}

        with ThreadPoolExecutor(max_workers=parallel) as executor:
            futures = {
                executor.submit(
                    run_single_test, cli_path, seed, style, chord, blueprint, work_dir, output_dir
                ): (seed, style, chord, blueprint)
                for seed, style, chord, blueprint in configs
            }

            for future in as_completed(futures):
                config = futures[future]
                result = future.result()
                results_dict[config] = result
                counter.increment(result)

                # Progress display
                with counter.lock:
                    i = counter.current
                    if result.error:
                        print(f"[{i:4d}/{total}] seed={config[0]:3d} style={config[1]:2d} chord={config[2]:2d} bp={config[3]}: ERROR")
                    elif result.has_critical:
                        print(f"[{i:4d}/{total}] seed={config[0]:3d} style={config[1]:2d} chord={config[2]:2d} bp={config[3]}: "
                              f"\033[31mFAIL\033[0m clashes={result.simultaneous_clashes}")
                    elif result.has_warnings:
                        print(f"[{i:4d}/{total}] seed={config[0]:3d} style={config[1]:2d} chord={config[2]:2d} bp={config[3]}: "
                              f"\033[33mWARN\033[0m high={result.high_severity}")
                    elif verbose:
                        print(f"[{i:4d}/{total}] seed={config[0]:3d} style={config[1]:2d} chord={config[2]:2d} bp={config[3]}: OK")
                    else:
                        print(f"\r[{i:4d}/{total}] Testing... (F:{counter.failed} W:{counter.warned} E:{counter.errors})", end="", flush=True)

        # Sort results by config order
        results = [results_dict[config] for config in configs]

        if not verbose:
            print("\r" + " " * 60 + "\r", end="")
    else:
        # Sequential execution (original behavior)
        for i, (seed, style, chord, blueprint) in enumerate(configs, 1):
            result = run_single_test(cli_path, seed, style, chord, blueprint, work_dir, output_dir)
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
            print("\r" + " " * 40 + "\r", end="")

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
    elif issue.type == "non_diatonic_note":
        # Non-diatonic note
        print(f"{indent}  \033[33mNon-diatonic: {issue.track} {issue.pitch_name}\033[0m")
        print(f"{indent}  {issue.description}")
        if issue.provenance_source:
            src_file = SOURCE_FILES.get(issue.provenance_source, "unknown")
            print(f"{indent}  Source: {issue.provenance_source} ({src_file})")
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


def analyze_track_pairs(results: list[TestResult]) -> dict:
    """Analyze which track pairs have the most clashes."""
    pair_stats = defaultdict(lambda: {"count": 0, "intervals": defaultdict(int), "examples": []})

    for r in results:
        for issue in r.all_issues:
            if issue.type == "simultaneous_clash" and issue.track_pair[0]:
                pair_key = f"{issue.track_pair[0]}-{issue.track_pair[1]}"
                pair_stats[pair_key]["count"] += 1
                pair_stats[pair_key]["intervals"][issue.interval_name] += 1
                if len(pair_stats[pair_key]["examples"]) < 3:
                    pair_stats[pair_key]["examples"].append({
                        "seed": r.seed, "style": r.style, "chord": r.chord,
                        "blueprint": r.blueprint, "bar": issue.bar, "beat": issue.beat,
                    })

    return dict(pair_stats)


def analyze_intervals(results: list[TestResult]) -> dict:
    """Analyze distribution of dissonant intervals."""
    interval_stats = defaultdict(lambda: {"count": 0, "severity": defaultdict(int), "track_pairs": defaultdict(int)})

    for r in results:
        for issue in r.all_issues:
            if issue.type == "simultaneous_clash":
                interval = issue.interval_name or "unknown"
                interval_stats[interval]["count"] += 1
                interval_stats[interval]["severity"][issue.severity] += 1
                if issue.track_pair[0]:
                    pair_key = f"{issue.track_pair[0]}-{issue.track_pair[1]}"
                    interval_stats[interval]["track_pairs"][pair_key] += 1

    return dict(interval_stats)


def analyze_bar_distribution(results: list[TestResult]) -> dict:
    """Analyze which bar positions have the most issues."""
    bar_stats = defaultdict(lambda: {"count": 0, "types": defaultdict(int)})

    for r in results:
        for issue in r.all_issues:
            # Normalize to bar within 4-bar phrase
            bar_in_phrase = issue.bar % 4
            beat_key = f"bar{bar_in_phrase + 1}_beat{int(issue.beat)}"
            bar_stats[beat_key]["count"] += 1
            bar_stats[beat_key]["types"][issue.type] += 1

    return dict(bar_stats)


def analyze_by_issue_type(results: list[TestResult]) -> dict:
    """Analyze issues grouped by type."""
    type_stats = defaultdict(lambda: {
        "count": 0, "high": 0, "medium": 0, "low": 0,
        "by_track": defaultdict(int), "by_source": defaultdict(int)
    })

    for r in results:
        for issue in r.all_issues:
            stats = type_stats[issue.type]
            stats["count"] += 1
            if issue.severity == "high":
                stats["high"] += 1
            elif issue.severity == "medium":
                stats["medium"] += 1
            else:
                stats["low"] += 1

            # Track distribution
            if issue.track:
                for track in issue.track.split(", "):
                    stats["by_track"][track.strip()] += 1

            # Source distribution
            if issue.provenance_source:
                for src in issue.provenance_source.split(", "):
                    if src.strip():
                        stats["by_source"][src.strip()] += 1

    return dict(type_stats)


def print_comprehensive_summary(results: list[TestResult], filters: dict) -> bool:
    """Print comprehensive test summary with detailed analysis."""
    total = len(results)
    errors = [r for r in results if r.error]
    critical = [r for r in results if r.has_critical]
    warnings = [r for r in results if r.has_warnings and not r.has_critical]

    print("\n" + "=" * 80)
    print("DISSONANCE CHECK SUMMARY")
    print("=" * 80)

    # Basic counts
    print(f"\n{'Test Results':40s}")
    print("-" * 40)
    print(f"  Total tests:             {total:>6d}")
    print(f"  Passed:                  {total - len(errors) - len(critical) - len(warnings):>6d}")
    print(f"  Warnings (high sev):     {len(warnings):>6d}")
    print(f"  \033[31mFailed (clashes):        {len(critical):>6d}\033[0m")
    print(f"  Errors:                  {len(errors):>6d}")

    # Issue type breakdown
    type_stats = analyze_by_issue_type(results)
    if type_stats:
        print(f"\n{'Issue Type Breakdown':40s}")
        print("-" * 40)
        for issue_type, stats in sorted(type_stats.items(), key=lambda x: -x[1]["count"]):
            type_name = issue_type.replace("_", " ").title()
            sev_str = f"H:{stats['high']} M:{stats['medium']} L:{stats['low']}"
            print(f"  {type_name:25s} {stats['count']:>5d}  ({sev_str})")

    # Track pair analysis
    pair_stats = analyze_track_pairs(results)
    if pair_stats:
        print(f"\n{'Track Pair Clashes':40s}")
        print("-" * 40)
        for pair, stats in sorted(pair_stats.items(), key=lambda x: -x[1]["count"])[:10]:
            intervals = ", ".join(f"{k}:{v}" for k, v in
                                  sorted(stats["intervals"].items(), key=lambda x: -x[1])[:3])
            print(f"  {pair:20s} {stats['count']:>4d}  [{intervals}]")

    # Interval distribution
    interval_stats = analyze_intervals(results)
    if interval_stats:
        print(f"\n{'Interval Distribution':40s}")
        print("-" * 40)
        for interval, stats in sorted(interval_stats.items(), key=lambda x: -x[1]["count"]):
            sev_str = f"H:{stats['severity'].get('high', 0)} M:{stats['severity'].get('medium', 0)}"
            print(f"  {interval:20s} {stats['count']:>4d}  ({sev_str})")

    # Bar position analysis
    bar_stats = analyze_bar_distribution(results)
    if bar_stats:
        print(f"\n{'Bar Position Distribution (top 8)':40s}")
        print("-" * 40)
        for pos, stats in sorted(bar_stats.items(), key=lambda x: -x[1]["count"])[:8]:
            types = ", ".join(f"{k.split('_')[0][:5]}:{v}" for k, v in
                             sorted(stats["types"].items(), key=lambda x: -x[1])[:2])
            print(f"  {pos:20s} {stats['count']:>4d}  [{types}]")

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
        print(f"\n{'Critical Issues by Generation Source':40s}")
        print("-" * 40)
        for src, stats in sorted(source_stats.items(), key=lambda x: -x[1]["count"]):
            print(f"  {src:20s} {stats['count']:>4d} -> {stats['file']}")

    # Statistics by blueprint
    bp_stats: dict[int, dict] = {}
    for r in results:
        bp = r.blueprint
        if bp not in bp_stats:
            bp_stats[bp] = {"tests": 0, "clashes": 0, "high": 0, "total": 0}
        bp_stats[bp]["tests"] += 1
        bp_stats[bp]["clashes"] += r.simultaneous_clashes
        bp_stats[bp]["high"] += r.high_severity
        bp_stats[bp]["total"] += r.total_issues

    if len(bp_stats) > 1:
        print(f"\n{'Issues by Blueprint':40s}")
        print("-" * 40)
        for bp, stats in sorted(bp_stats.items()):
            bp_name = BLUEPRINT_NAMES.get(bp, f"Blueprint{bp}")
            if stats["clashes"] > 0 or stats["high"] > 0:
                print(f"  {bp_name:20s} clashes:{stats['clashes']:>3d} high:{stats['high']:>3d} total:{stats['total']:>4d}")

    # Statistics by style
    style_stats: dict[int, dict] = {}
    for r in results:
        st = r.style
        if st not in style_stats:
            style_stats[st] = {"tests": 0, "clashes": 0, "high": 0}
        style_stats[st]["tests"] += 1
        style_stats[st]["clashes"] += r.simultaneous_clashes
        style_stats[st]["high"] += r.high_severity

    if len(style_stats) > 1:
        problematic_styles = [(st, stats) for st, stats in style_stats.items()
                              if stats["clashes"] > 0 or stats["high"] > 0]
        if problematic_styles:
            print(f"\n{'Issues by Style (problematic only)':40s}")
            print("-" * 40)
            for st, stats in sorted(problematic_styles, key=lambda x: -x[1]["clashes"]):
                print(f"  Style {st:2d}: clashes:{stats['clashes']:>3d} high:{stats['high']:>3d} /{stats['tests']} tests")

    # Statistics by chord progression
    chord_stats: dict[int, dict] = {}
    for r in results:
        ch = r.chord
        if ch not in chord_stats:
            chord_stats[ch] = {"tests": 0, "clashes": 0, "high": 0}
        chord_stats[ch]["tests"] += 1
        chord_stats[ch]["clashes"] += r.simultaneous_clashes
        chord_stats[ch]["high"] += r.high_severity

    if len(chord_stats) > 1:
        problematic_chords = [(ch, stats) for ch, stats in chord_stats.items()
                              if stats["clashes"] > 0 or stats["high"] > 0]
        if problematic_chords:
            print(f"\n{'Issues by Chord Progression (problematic only)':40s}")
            print("-" * 40)
            for ch, stats in sorted(problematic_chords, key=lambda x: -x[1]["clashes"])[:10]:
                print(f"  Chord {ch:2d}: clashes:{stats['clashes']:>3d} high:{stats['high']:>3d} /{stats['tests']} tests")

    # Print critical failures with full details
    if critical:
        print("\n" + "-" * 80)
        print("\033[31mCRITICAL: Simultaneous Clashes Detected\033[0m")
        print("-" * 80)

        # Group by similar patterns
        pattern_groups = defaultdict(list)
        for r in critical:
            # Create pattern key from track pairs and intervals
            patterns = set()
            for issue in r.critical_issues:
                if issue.type == "simultaneous_clash":
                    patterns.add(f"{issue.track_pair[0]}-{issue.track_pair[1]}:{issue.interval_name}")
            pattern_key = frozenset(patterns)
            pattern_groups[pattern_key].append(r)

        shown = 0
        max_show = 10
        for pattern, group in sorted(pattern_groups.items(), key=lambda x: -len(x[1])):
            if shown >= max_show:
                remaining = sum(len(g) for _, g in list(pattern_groups.items())[max_show:])
                print(f"\n  ... and {remaining} more similar failures")
                break

            print(f"\n  Pattern ({len(group)} occurrences): {', '.join(sorted(pattern))}")
            # Show first example from group
            r = group[0]
            print(f"  Example: seed={r.seed}, style={r.style}, chord={r.chord}, bp={r.blueprint}")
            print(f"  Reproduce: {r.cli_command()}")
            if r.critical_issues:
                tick = r.critical_issues[0].tick
                print(f"  Debug:     {r.cli_command()} --dump-collisions-at {tick}")
                print(f"  Issues ({len(r.critical_issues)}):")
                for issue in r.critical_issues[:3]:
                    print_issue_detail(issue)
                if len(r.critical_issues) > 3:
                    print(f"      ... and {len(r.critical_issues) - 3} more")
            shown += 1

    # Print warnings summary (condensed)
    if warnings and not filters.get("no_warnings"):
        print("\n" + "-" * 80)
        print("\033[33mWARNINGS: High Severity Issues\033[0m")
        print("-" * 80)

        print(f"  {len(warnings)} configurations with high-severity issues")
        print(f"  To see details, run with specific seed/style/chord/blueprint")

    print("\n" + "=" * 80)
    passed = len(critical) == 0 and len(errors) == 0
    if passed:
        print("\033[32mRESULT: PASSED\033[0m")
    else:
        print("\033[31mRESULT: FAILED\033[0m")
    print("=" * 80)

    return passed


def save_json_report(results: list[TestResult], output_path: str):
    """Save detailed JSON report for further analysis."""
    critical = [r for r in results if r.has_critical or r.has_warnings]

    # Comprehensive statistics
    type_stats = analyze_by_issue_type(results)
    pair_stats = analyze_track_pairs(results)
    interval_stats = analyze_intervals(results)
    bar_stats = analyze_bar_distribution(results)

    report = {
        "summary": {
            "total_tests": len(results),
            "failed": len([r for r in results if r.has_critical]),
            "warnings": len([r for r in results if r.has_warnings and not r.has_critical]),
            "errors": len([r for r in results if r.error]),
            "total_clashes": sum(r.simultaneous_clashes for r in results),
            "total_non_chord_tones": sum(r.non_chord_tones for r in results),
            "total_sustained_over_chord": sum(r.sustained_over_chord for r in results),
            "total_non_diatonic": sum(r.non_diatonic for r in results),
        },
        "analysis": {
            "by_issue_type": {k: dict(v) for k, v in type_stats.items()},
            "by_track_pair": pair_stats,
            "by_interval": interval_stats,
            "by_bar_position": bar_stats,
        },
        "failed_cases": [
            {
                "seed": r.seed,
                "style": r.style,
                "chord": r.chord,
                "blueprint": r.blueprint,
                "simultaneous_clashes": r.simultaneous_clashes,
                "non_chord_tones": r.non_chord_tones,
                "sustained_over_chord": r.sustained_over_chord,
                "non_diatonic": r.non_diatonic,
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
                        "interval_name": i.interval_name,
                        "track_pair": list(i.track_pair) if i.track_pair[0] else [],
                        "provenance_source": i.provenance_source,
                        "source_file": SOURCE_FILES.get(i.provenance_source, "unknown"),
                    }
                    for i in r.all_issues
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
        description="Comprehensive dissonance check across multiple configurations",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s --quick              # 10 seeds, style 0, all blueprints
  %(prog)s --medium             # 50 seeds, all styles, all blueprints
  %(prog)s --full               # 100 seeds, all styles, all chords, all blueprints
  %(prog)s --seeds 100          # Full test with 100 seeds
  %(prog)s --seed-range 1-20    # Test seeds 1-20
  %(prog)s --styles 0,5 --bp 0  # Specific style and blueprint
  %(prog)s -j 4                 # Run with 4 parallel workers

Filters:
  %(prog)s --track vocal        # Only show issues involving vocal track
  %(prog)s --interval "minor 2nd" # Only show minor 2nd clashes
  %(prog)s --severity high      # Only show high severity issues
        """)

    parser.add_argument("--cli", default="./build/bin/midisketch_cli",
                        help="Path to CLI (default: ./build/bin/midisketch_cli)")

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
    parser.add_argument("--styles", type=str, default="0",
                        help="Styles: 'all' or comma-separated (default: 0)")
    parser.add_argument("--chords", type=str, default="0",
                        help="Chords: 'all' or comma-separated (default: 0)")
    parser.add_argument("--bp", "--blueprints", type=str, default="all", dest="blueprints",
                        help="Blueprints: 'all' or comma-separated (default: all)")

    # Parallel execution
    parser.add_argument("-j", "--jobs", type=int, default=1,
                        help="Number of parallel workers (default: 1)")

    # Filter options
    parser.add_argument("--track", type=str,
                        help="Filter by track name (vocal, chord, bass, etc.)")
    parser.add_argument("--interval", type=str,
                        help="Filter by interval name (e.g., 'minor 2nd', 'tritone')")
    parser.add_argument("--severity", type=str, choices=["high", "medium", "low"],
                        help="Filter by severity level")
    parser.add_argument("--type", type=str,
                        choices=["simultaneous_clash", "non_chord_tone", "sustained_over_chord_change", "non_diatonic_note"],
                        help="Filter by issue type")
    parser.add_argument("--no-warnings", action="store_true",
                        help="Suppress warning details in output")

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

    # Build filters dict
    filters = {
        "track": args.track,
        "interval": args.interval,
        "severity": args.severity,
        "type": args.type,
        "no_warnings": args.no_warnings,
    }

    # Run tests
    results = run_tests(args.cli, seeds, styles, chords, blueprints, args.verbose, args.jobs)

    # Apply filters to results for display
    if any(filters.values()):
        for r in results:
            filtered_issues = []
            for issue in r.all_issues:
                include = True
                if filters["track"] and filters["track"].lower() not in issue.track.lower():
                    include = False
                if filters["interval"] and filters["interval"].lower() != issue.interval_name.lower():
                    include = False
                if filters["severity"] and filters["severity"] != issue.severity:
                    include = False
                if filters["type"] and filters["type"] != issue.type:
                    include = False
                if include:
                    filtered_issues.append(issue)
            r.critical_issues = [i for i in filtered_issues
                                if i.type == "simultaneous_clash" or i.severity == "high"]

    # Save JSON report if requested
    if args.output:
        save_json_report(results, args.output)

    # Print comprehensive summary
    passed = print_comprehensive_summary(results, filters)
    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    main()
