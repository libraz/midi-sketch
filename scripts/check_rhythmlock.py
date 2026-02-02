#!/usr/bin/env python3
"""
check_rhythmlock.py - RhythmLock (Blueprint 1) focused dissonance testing

RhythmLock uses RhythmSync paradigm with Locked riff policy.
Generation order: Motif -> Vocal -> Aux -> Bass -> Chord -> Arpeggio
Coordinate axis: Motif

This script focuses on issues specific to RhythmSync:
  - Motif-driven coordination clashes
  - Locked riff pattern repetition issues
  - Common rhythm alignment problems
  - Cross-track dissonance in shared rhythm contexts
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

# RhythmLock constants
RHYTHMLOCK_BLUEPRINT = 1
RHYTHMSYNC_BLUEPRINTS = [1, 5, 7]  # RhythmLock, IdolHyper, IdolCoolPop

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

SOURCE_FILES = {
    "melody_phrase": "src/track/melody_designer.cpp",
    "hook": "src/track/melody_designer.cpp",
    "bass_pattern": "src/track/bass.cpp",
    "chord_voicing": "src/track/chord_track.cpp",
    "arpeggio": "src/track/arpeggio.cpp",
    "aux": "src/track/aux.cpp",
    "motif": "src/track/motif.cpp",
    "drums": "src/track/drums.cpp",
    "embellishment": "src/core/melody_embellishment.cpp",
    "collision_avoid": "src/core/collision_resolver.cpp",
    "post_process": "src/core/post_processor.cpp",
}

# RhythmSync generation order (Motif is coordinate axis)
RHYTHMSYNC_ORDER = ["motif", "vocal", "aux", "bass", "chord", "arpeggio"]

# Track pairs most likely to clash in RhythmSync (same rhythm = more overlap)
RHYTHMSYNC_RISK_PAIRS = [
    ("motif", "vocal"),    # Motif drives vocal, high overlap risk
    ("motif", "aux"),      # Motif drives aux
    ("motif", "chord"),    # Shared rhythm with chords
    ("bass", "chord"),     # Classic low-register clash
    ("vocal", "chord"),    # Melody vs harmony
    ("motif", "bass"),     # Motif pattern vs bass
]


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
    clash_notes: list[dict] = field(default_factory=list)
    interval_name: str = ""
    interval_semitones: int = 0
    track_pair: tuple[str, str] = ("", "")
    original_chord: str = ""
    new_chord: str = ""


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
        prov = item.get("provenance", {})
        prov_source = prov.get("generation_source", "") or prov.get("source", "")

        if issue_type == "simultaneous_clash":
            notes = item.get("notes", [])
            sources = [n.get("provenance", {}).get("source", "") for n in notes]
            tracks = sorted([n.get("track", "") for n in notes])
            track_pair = (tracks[0], tracks[1]) if len(tracks) >= 2 else ("", "")

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
                interval_semitones=item.get("interval_semitones", 0),
                track_pair=track_pair,
            ))
        elif issue_type == "sustained_over_chord_change":
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
) -> TestResult:
    """Run a single generation test and return the result."""
    cmd = [
        cli_path, "--analyze",
        "--seed", str(seed),
        "--style", str(style),
        "--chord", str(chord),
        "--blueprint", str(blueprint),
    ]

    try:
        result = subprocess.run(
            cmd, capture_output=True, text=True, timeout=60, cwd=work_dir,
        )

        if result.returncode != 0:
            return TestResult(
                seed=seed, style=style, chord=chord, blueprint=blueprint,
                error=f"CLI error: {result.stderr[:200]}",
            )

        std_analysis = work_dir / "analysis.json"
        if not std_analysis.exists():
            return TestResult(
                seed=seed, style=style, chord=chord, blueprint=blueprint,
                error="analysis.json not found",
            )

        with open(std_analysis) as f:
            analysis = json.load(f)

        summary = analysis.get("summary", {})
        all_issues = parse_issues(analysis)
        critical = [i for i in all_issues
                    if i.type == "simultaneous_clash" or i.severity == "high"]

        return TestResult(
            seed=seed, style=style, chord=chord, blueprint=blueprint,
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
        return TestResult(seed=seed, style=style, chord=chord, blueprint=blueprint,
                          error="Timeout (>60s)")
    except Exception as e:
        return TestResult(seed=seed, style=style, chord=chord, blueprint=blueprint,
                          error=str(e)[:200])


def run_tests(
    cli_path: str,
    configs: list[tuple[int, int, int, int]],
    verbose: bool = False,
    parallel: int = 1,
) -> list[TestResult]:
    """Run tests across all configurations."""
    work_dir = Path.cwd()
    total = len(configs)
    label = "RhythmLock" if all(c[3] == RHYTHMLOCK_BLUEPRINT for c in configs) else "RhythmSync"
    print(f"Running {total} {label} tests"
          + (f" with {parallel} parallel workers" if parallel > 1 else "")
          + "...\n")

    if parallel > 1:
        counter = ProgressCounter(total)
        results_dict = {}

        with ThreadPoolExecutor(max_workers=parallel) as executor:
            futures = {
                executor.submit(
                    run_single_test, cli_path, s, st, ch, bp, work_dir
                ): (s, st, ch, bp)
                for s, st, ch, bp in configs
            }

            for future in as_completed(futures):
                config = futures[future]
                result = future.result()
                results_dict[config] = result
                counter.increment(result)

                with counter.lock:
                    i = counter.current
                    bp_name = BLUEPRINT_NAMES.get(config[3], f"bp{config[3]}")
                    tag = f"seed={config[0]:3d} style={config[1]:2d} chord={config[2]:2d} [{bp_name}]"
                    if result.error:
                        print(f"[{i:4d}/{total}] {tag}: ERROR")
                    elif result.has_critical:
                        print(f"[{i:4d}/{total}] {tag}: "
                              f"\033[31mFAIL\033[0m clashes={result.simultaneous_clashes}")
                    elif result.has_warnings:
                        print(f"[{i:4d}/{total}] {tag}: "
                              f"\033[33mWARN\033[0m high={result.high_severity}")
                    elif verbose:
                        print(f"[{i:4d}/{total}] {tag}: OK")
                    else:
                        print(f"\r[{i:4d}/{total}] Testing... (F:{counter.failed} W:{counter.warned} E:{counter.errors})",
                              end="", flush=True)

        results = [results_dict[c] for c in configs]
        if not verbose:
            print("\r" + " " * 60 + "\r", end="")
    else:
        results = []
        for i, (seed, style, chord, blueprint) in enumerate(configs, 1):
            result = run_single_test(cli_path, seed, style, chord, blueprint, work_dir)
            results.append(result)

            bp_name = BLUEPRINT_NAMES.get(blueprint, f"bp{blueprint}")
            tag = f"seed={seed:3d} style={style:2d} chord={chord:2d} [{bp_name}]"
            if result.error:
                print(f"[{i:4d}/{total}] {tag}: ERROR")
            elif result.has_critical:
                print(f"[{i:4d}/{total}] {tag}: "
                      f"\033[31mFAIL\033[0m clashes={result.simultaneous_clashes}")
            elif result.has_warnings:
                print(f"[{i:4d}/{total}] {tag}: "
                      f"\033[33mWARN\033[0m high={result.high_severity}")
            elif verbose:
                print(f"[{i:4d}/{total}] {tag}: OK")
            else:
                print(f"\r[{i:4d}/{total}] Testing...", end="", flush=True)

        if not verbose:
            print("\r" + " " * 40 + "\r", end="")

    return results


def is_rhythmsync_risk_pair(pair: tuple[str, str]) -> bool:
    """Check if a track pair is a known RhythmSync risk pair."""
    return pair in RHYTHMSYNC_RISK_PAIRS or (pair[1], pair[0]) in RHYTHMSYNC_RISK_PAIRS


def analyze_motif_driven_clashes(results: list[TestResult]) -> dict:
    """Analyze clashes involving the motif track (coordinate axis in RhythmSync)."""
    stats = {
        "total_motif_clashes": 0,
        "motif_pairs": defaultdict(int),
        "motif_intervals": defaultdict(int),
        "motif_bars": defaultdict(int),
        "examples": [],
    }

    for r in results:
        for issue in r.all_issues:
            if issue.type != "simultaneous_clash":
                continue
            tracks = [t.strip() for t in issue.track.split(",")]
            if "motif" not in tracks:
                continue

            stats["total_motif_clashes"] += 1
            other = [t for t in tracks if t != "motif"][0] if len(tracks) > 1 else "unknown"
            stats["motif_pairs"][other] += 1
            stats["motif_intervals"][issue.interval_name] += 1
            stats["motif_bars"][issue.bar] += 1

            if len(stats["examples"]) < 5:
                stats["examples"].append({
                    "seed": r.seed, "style": r.style, "chord": r.chord,
                    "blueprint": r.blueprint,
                    "bar": issue.bar, "beat": issue.beat,
                    "interval": issue.interval_name,
                    "other_track": other,
                    "command": r.cli_command(),
                })

    stats["motif_pairs"] = dict(stats["motif_pairs"])
    stats["motif_intervals"] = dict(stats["motif_intervals"])
    stats["motif_bars"] = dict(stats["motif_bars"])
    return stats


def analyze_locked_riff_repetition(results: list[TestResult]) -> dict:
    """Analyze if locked riff patterns cause repeated clashes at similar positions."""
    # Group clashes by bar-within-section position
    position_counts = defaultdict(lambda: {"count": 0, "seeds": set(), "intervals": defaultdict(int)})

    for r in results:
        for issue in r.all_issues:
            if issue.type != "simultaneous_clash":
                continue
            # Bar position within 4-bar phrase (locked riff repeats per section)
            bar_in_phrase = issue.bar % 4
            beat = int(issue.beat)
            pos_key = f"phrase_bar{bar_in_phrase + 1}_beat{beat + 1}"
            position_counts[pos_key]["count"] += 1
            position_counts[pos_key]["seeds"].add(r.seed)
            position_counts[pos_key]["intervals"][issue.interval_name] += 1

    # Identify repetition hotspots (same position across multiple seeds = locked riff issue)
    hotspots = {}
    for pos, data in position_counts.items():
        if len(data["seeds"]) >= 2:  # Same position in 2+ seeds = structural
            hotspots[pos] = {
                "count": data["count"],
                "unique_seeds": len(data["seeds"]),
                "intervals": dict(data["intervals"]),
            }

    return {
        "position_counts": {k: {"count": v["count"], "unique_seeds": len(v["seeds"]),
                                 "intervals": dict(v["intervals"])}
                            for k, v in position_counts.items()},
        "repetition_hotspots": hotspots,
    }


def analyze_rhythmsync_risk_pairs(results: list[TestResult]) -> dict:
    """Analyze RhythmSync-specific risk track pairs."""
    pair_stats = defaultdict(lambda: {
        "count": 0,
        "is_risk_pair": False,
        "intervals": defaultdict(int),
        "severity": defaultdict(int),
        "examples": [],
    })

    for r in results:
        for issue in r.all_issues:
            if issue.type != "simultaneous_clash" or not issue.track_pair[0]:
                continue
            pair_key = f"{issue.track_pair[0]}-{issue.track_pair[1]}"
            stats = pair_stats[pair_key]
            stats["count"] += 1
            stats["is_risk_pair"] = is_rhythmsync_risk_pair(issue.track_pair)
            stats["intervals"][issue.interval_name] += 1
            stats["severity"][issue.severity] += 1
            if len(stats["examples"]) < 3:
                stats["examples"].append({
                    "seed": r.seed, "style": r.style, "chord": r.chord,
                    "bar": issue.bar, "interval": issue.interval_name,
                })

    return {k: {**v, "intervals": dict(v["intervals"]),
                "severity": dict(v["severity"])}
            for k, v in pair_stats.items()}


def analyze_section_distribution(results: list[TestResult]) -> dict:
    """Analyze issue distribution across song sections (Intro/A/B/Chorus/etc)."""
    TICKS_PER_BAR = 1920
    # Approximate section boundaries (varies by form, but 8-bar sections are common)
    section_stats = defaultdict(lambda: {"count": 0, "clashes": 0, "high": 0})

    for r in results:
        for issue in r.all_issues:
            # Rough section estimate: 8 bars per section
            section_idx = issue.bar // 8
            section_key = f"section_{section_idx}"
            section_stats[section_key]["count"] += 1
            if issue.type == "simultaneous_clash":
                section_stats[section_key]["clashes"] += 1
            if issue.severity == "high":
                section_stats[section_key]["high"] += 1

    return dict(section_stats)


def print_rhythmlock_summary(results: list[TestResult], compare_results: Optional[list[TestResult]] = None) -> bool:
    """Print RhythmLock-focused summary with specialized analysis."""
    total = len(results)
    errors = [r for r in results if r.error]
    critical = [r for r in results if r.has_critical]
    warnings = [r for r in results if r.has_warnings and not r.has_critical]

    print("\n" + "=" * 80)
    print("RHYTHMLOCK DISSONANCE CHECK")
    print("Blueprint 1 | Paradigm: RhythmSync | Riff: Locked | Axis: Motif")
    print("=" * 80)

    # Basic counts
    print(f"\n{'Test Results':40s}")
    print("-" * 40)
    print(f"  Total tests:             {total:>6d}")
    print(f"  Passed:                  {total - len(errors) - len(critical) - len(warnings):>6d}")
    print(f"  Warnings (high sev):     {len(warnings):>6d}")
    print(f"  \033[31mFailed (clashes):        {len(critical):>6d}\033[0m")
    print(f"  Errors:                  {len(errors):>6d}")

    # Aggregate issue counts
    total_clashes = sum(r.simultaneous_clashes for r in results)
    total_nct = sum(r.non_chord_tones for r in results)
    total_sustained = sum(r.sustained_over_chord for r in results)
    total_nondiatonic = sum(r.non_diatonic for r in results)

    print(f"\n{'Issue Totals':40s}")
    print("-" * 40)
    print(f"  Simultaneous clashes:    {total_clashes:>6d}")
    print(f"  Non-chord tones:         {total_nct:>6d}")
    print(f"  Sustained over chord:    {total_sustained:>6d}")
    print(f"  Non-diatonic:            {total_nondiatonic:>6d}")

    # --- RhythmSync-specific analyses ---

    # 1. Motif-driven clashes (coordinate axis)
    motif_stats = analyze_motif_driven_clashes(results)
    if motif_stats["total_motif_clashes"] > 0:
        print(f"\n{'[RhythmSync] Motif Axis Clashes':40s}")
        print("-" * 40)
        print(f"  Total motif-involved clashes: {motif_stats['total_motif_clashes']}")
        if motif_stats["motif_pairs"]:
            print(f"  Motif clashes by partner:")
            for partner, count in sorted(motif_stats["motif_pairs"].items(), key=lambda x: -x[1]):
                risk = " \033[33m<-- RhythmSync risk pair\033[0m" if is_rhythmsync_risk_pair(("motif", partner)) else ""
                print(f"    motif-{partner:10s} {count:>4d}{risk}")
        if motif_stats["motif_intervals"]:
            print(f"  Motif clash intervals:")
            for interval, count in sorted(motif_stats["motif_intervals"].items(), key=lambda x: -x[1]):
                print(f"    {interval:20s} {count:>4d}")

    # 2. Locked riff repetition analysis
    riff_stats = analyze_locked_riff_repetition(results)
    hotspots = riff_stats.get("repetition_hotspots", {})
    if hotspots:
        print(f"\n{'[Locked Riff] Repetition Hotspots':40s}")
        print("-" * 40)
        print(f"  Positions with clashes across multiple seeds (structural issues):")
        for pos, data in sorted(hotspots.items(), key=lambda x: -x[1]["count"])[:8]:
            intervals = ", ".join(f"{k}:{v}" for k, v in
                                  sorted(data["intervals"].items(), key=lambda x: -x[1])[:2])
            print(f"    {pos:25s} {data['count']:>3d} hits, {data['unique_seeds']:>2d} seeds  [{intervals}]")
    else:
        print(f"\n  No locked riff repetition hotspots detected.")

    # 3. RhythmSync risk pair analysis
    pair_stats = analyze_rhythmsync_risk_pairs(results)
    risk_pairs = {k: v for k, v in pair_stats.items() if v["is_risk_pair"] and v["count"] > 0}
    other_pairs = {k: v for k, v in pair_stats.items() if not v["is_risk_pair"] and v["count"] > 0}

    if risk_pairs:
        print(f"\n{'[RhythmSync] Risk Pair Clashes':40s}")
        print("-" * 40)
        for pair, stats in sorted(risk_pairs.items(), key=lambda x: -x[1]["count"]):
            intervals = ", ".join(f"{k}:{v}" for k, v in
                                  sorted(stats["intervals"].items(), key=lambda x: -x[1])[:3])
            sev = stats["severity"]
            sev_str = f"H:{sev.get('high', 0)} M:{sev.get('medium', 0)} L:{sev.get('low', 0)}"
            print(f"  \033[33m{pair:20s}\033[0m {stats['count']:>4d}  [{intervals}]  ({sev_str})")

    if other_pairs:
        print(f"\n{'Other Track Pair Clashes':40s}")
        print("-" * 40)
        for pair, stats in sorted(other_pairs.items(), key=lambda x: -x[1]["count"]):
            intervals = ", ".join(f"{k}:{v}" for k, v in
                                  sorted(stats["intervals"].items(), key=lambda x: -x[1])[:3])
            print(f"  {pair:20s} {stats['count']:>4d}  [{intervals}]")

    # 4. Section distribution
    section_stats = analyze_section_distribution(results)
    clash_sections = {k: v for k, v in section_stats.items() if v["clashes"] > 0}
    if clash_sections:
        print(f"\n{'Clashes by Song Section':40s}")
        print("-" * 40)
        for section, stats in sorted(clash_sections.items()):
            bar_label = f"bars {int(section.split('_')[1]) * 8 + 1}-{(int(section.split('_')[1]) + 1) * 8}"
            print(f"  {bar_label:20s} clashes:{stats['clashes']:>3d}  total:{stats['count']:>4d}")

    # 5. Source file analysis
    source_stats: dict[str, int] = defaultdict(int)
    for r in results:
        for issue in r.critical_issues:
            if issue.type == "simultaneous_clash":
                for note in issue.clash_notes:
                    src = note.get("provenance", {}).get("source", "unknown") or "unknown"
                    source_stats[src] += 1
            elif issue.provenance_source:
                source_stats[issue.provenance_source] += 1

    if source_stats:
        print(f"\n{'Critical Issues by Source':40s}")
        print("-" * 40)
        for src, count in sorted(source_stats.items(), key=lambda x: -x[1]):
            src_file = SOURCE_FILES.get(src, "unknown")
            print(f"  {src:20s} {count:>4d}  -> {src_file}")

    # 6. Style breakdown
    style_stats: dict[int, dict] = {}
    for r in results:
        st = r.style
        if st not in style_stats:
            style_stats[st] = {"tests": 0, "clashes": 0, "high": 0, "total": 0}
        style_stats[st]["tests"] += 1
        style_stats[st]["clashes"] += r.simultaneous_clashes
        style_stats[st]["high"] += r.high_severity
        style_stats[st]["total"] += r.total_issues

    problematic_styles = [(st, s) for st, s in style_stats.items() if s["clashes"] > 0 or s["high"] > 0]
    if problematic_styles:
        print(f"\n{'Problematic Styles':40s}")
        print("-" * 40)
        for st, stats in sorted(problematic_styles, key=lambda x: -x[1]["clashes"]):
            print(f"  Style {st:2d}: clashes:{stats['clashes']:>3d} high:{stats['high']:>3d} "
                  f"total:{stats['total']:>4d} /{stats['tests']} tests")

    # 7. Comparison with other blueprints (if available)
    if compare_results:
        print(f"\n{'Blueprint Comparison':40s}")
        print("-" * 40)
        bp_groups: dict[int, list[TestResult]] = defaultdict(list)
        for r in compare_results:
            bp_groups[r.blueprint].append(r)
        for r in results:
            bp_groups[r.blueprint].append(r)

        print(f"  {'Blueprint':20s} {'Tests':>6s} {'Clashes':>8s} {'High':>6s} {'Clash/Test':>10s}")
        for bp in sorted(bp_groups.keys()):
            group = bp_groups[bp]
            bp_name = BLUEPRINT_NAMES.get(bp, f"bp{bp}")
            n_tests = len(group)
            n_clashes = sum(r.simultaneous_clashes for r in group)
            n_high = sum(r.high_severity for r in group)
            rate = n_clashes / n_tests if n_tests > 0 else 0
            marker = " <--" if bp == RHYTHMLOCK_BLUEPRINT else ""
            print(f"  {bp_name:20s} {n_tests:>6d} {n_clashes:>8d} {n_high:>6d} {rate:>10.2f}{marker}")

    # Print critical failures detail
    if critical:
        print("\n" + "-" * 80)
        print("\033[31mCRITICAL FAILURES\033[0m")
        print("-" * 80)

        for r in critical[:10]:
            bp_name = BLUEPRINT_NAMES.get(r.blueprint, f"bp{r.blueprint}")
            print(f"\n  seed={r.seed}, style={r.style}, chord={r.chord} [{bp_name}]")
            print(f"  Reproduce: {r.cli_command()}")
            if r.critical_issues:
                tick = r.critical_issues[0].tick
                print(f"  Debug:     {r.cli_command()} --dump-collisions-at {tick}")
                for issue in r.critical_issues[:3]:
                    print(f"    Bar {issue.bar}, beat {issue.beat:.1f}: {issue.interval_name}")
                    if issue.clash_notes:
                        for note in issue.clash_notes:
                            prov = note.get("provenance", {})
                            src = prov.get("source", "?")
                            print(f"      {note.get('track'):8s} {note.get('name'):4s} <- {src}")
                if len(r.critical_issues) > 3:
                    print(f"    ... and {len(r.critical_issues) - 3} more")

        if len(critical) > 10:
            print(f"\n  ... and {len(critical) - 10} more failing configurations")

    print("\n" + "=" * 80)
    passed = len(critical) == 0 and len(errors) == 0
    if passed:
        print("\033[32mRESULT: PASSED\033[0m")
    else:
        print("\033[31mRESULT: FAILED\033[0m")
    print("=" * 80)

    return passed


def save_json_report(results: list[TestResult], compare_results: Optional[list[TestResult]], output_path: str):
    """Save detailed JSON report."""
    motif_stats = analyze_motif_driven_clashes(results)
    riff_stats = analyze_locked_riff_repetition(results)
    pair_stats = analyze_rhythmsync_risk_pairs(results)

    report = {
        "focus": "RhythmLock (Blueprint 1, RhythmSync paradigm)",
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
        "rhythmsync_analysis": {
            "motif_axis_clashes": motif_stats,
            "locked_riff_repetition": riff_stats,
            "risk_pair_clashes": pair_stats,
        },
        "failed_cases": [
            {
                "seed": r.seed,
                "style": r.style,
                "chord": r.chord,
                "blueprint": r.blueprint,
                "blueprint_name": BLUEPRINT_NAMES.get(r.blueprint, "?"),
                "simultaneous_clashes": r.simultaneous_clashes,
                "high_severity": r.high_severity,
                "total_issues": r.total_issues,
                "reproduce_command": r.cli_command(),
                "issues": [
                    {
                        "type": i.type,
                        "severity": i.severity,
                        "bar": i.bar,
                        "beat": i.beat,
                        "track": i.track,
                        "pitch_name": i.pitch_name,
                        "interval_name": i.interval_name,
                        "track_pair": list(i.track_pair) if i.track_pair[0] else [],
                        "provenance_source": i.provenance_source,
                        "source_file": SOURCE_FILES.get(i.provenance_source, "unknown"),
                    }
                    for i in r.all_issues
                ],
            }
            for r in results if r.has_critical or r.has_warnings
        ],
    }

    # Add comparison data
    if compare_results:
        bp_summary = {}
        all_results = results + compare_results
        bp_groups: dict[int, list[TestResult]] = defaultdict(list)
        for r in all_results:
            bp_groups[r.blueprint].append(r)

        for bp, group in bp_groups.items():
            n = len(group)
            bp_summary[BLUEPRINT_NAMES.get(bp, f"bp{bp}")] = {
                "tests": n,
                "clashes": sum(r.simultaneous_clashes for r in group),
                "high_severity": sum(r.high_severity for r in group),
                "clash_rate": sum(r.simultaneous_clashes for r in group) / n if n > 0 else 0,
            }
        report["blueprint_comparison"] = bp_summary

    with open(output_path, "w") as f:
        json.dump(report, f, indent=2, default=str)
    print(f"\nDetailed report saved to: {output_path}")


def main():
    parser = argparse.ArgumentParser(
        description="RhythmLock-focused dissonance check (Blueprint 1, RhythmSync paradigm)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s --quick                 # 10 seeds, RhythmLock only
  %(prog)s --medium -j 4           # 50 seeds, all styles, 4 workers
  %(prog)s --full                   # 100 seeds, all styles, all chords
  %(prog)s --compare               # Compare RhythmLock with other blueprints
  %(prog)s --all-rhythmsync        # Test all RhythmSync blueprints (1, 5, 7)
  %(prog)s --seed-range 1-20 -v    # Seeds 1-20, verbose
  %(prog)s --styles 0,5 --chords 0,10  # Specific styles and chords
        """)

    parser.add_argument("--cli", default="./build/bin/midisketch_cli",
                        help="Path to CLI (default: ./build/bin/midisketch_cli)")

    # Preset modes
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--quick", action="store_true",
                      help="Quick: 10 seeds x style 0 x chord 0")
    mode.add_argument("--medium", action="store_true",
                      help="Medium: 50 seeds x all styles x chord 0")
    mode.add_argument("--full", action="store_true",
                      help="Full: 100 seeds x all styles x all chords")

    # Seed options
    parser.add_argument("--seeds", type=int, default=20,
                        help="Number of seeds (default: 20)")
    parser.add_argument("--seed-start", type=int, default=1,
                        help="Starting seed (default: 1)")
    parser.add_argument("--seed-range", type=str,
                        help="Seed range, e.g., '1-50'")
    parser.add_argument("--random-seeds", type=int, metavar="N",
                        help="Use N random large seeds")

    # Config options
    parser.add_argument("--styles", type=str, default="all",
                        help="Styles: 'all' or comma-separated (default: all)")
    parser.add_argument("--chords", type=str, default="0",
                        help="Chords: 'all' or comma-separated (default: 0)")

    # RhythmLock-specific options
    parser.add_argument("--all-rhythmsync", action="store_true",
                        help="Test all RhythmSync blueprints (1=RhythmLock, 5=IdolHyper, 7=IdolCoolPop)")
    parser.add_argument("--compare", action="store_true",
                        help="Also run Traditional (bp=0) for comparison")

    # Parallel execution
    parser.add_argument("-j", "--jobs", type=int, default=1,
                        help="Number of parallel workers (default: 1)")

    # Output options
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="Show all test results")
    parser.add_argument("-o", "--output", type=str,
                        help="Save JSON report to file")

    args = parser.parse_args()

    # Parse seeds
    if args.quick:
        seeds = list(range(1, 11))
        styles = [0]
        chords = [0]
    elif args.medium:
        seeds = list(range(1, 51))
        styles = list(range(15))
        chords = [0]
    elif args.full:
        seeds = list(range(1, 101))
        styles = list(range(15))
        chords = list(range(22))
    else:
        if args.random_seeds:
            import random
            random.seed()
            seeds = [random.randint(1, 2**32 - 1) for _ in range(args.random_seeds)]
        elif args.seed_range:
            start, end = map(int, args.seed_range.split("-"))
            seeds = list(range(start, end + 1))
        else:
            seeds = list(range(args.seed_start, args.seed_start + args.seeds))

        styles = list(range(15)) if args.styles == "all" else [int(x) for x in args.styles.split(",")]
        chords = list(range(22)) if args.chords == "all" else [int(x) for x in args.chords.split(",")]

    # Determine blueprints to test
    if args.all_rhythmsync:
        blueprints = RHYTHMSYNC_BLUEPRINTS
    else:
        blueprints = [RHYTHMLOCK_BLUEPRINT]

    # Build configs for main test
    configs = [
        (seed, style, chord, bp)
        for seed in seeds
        for style in styles
        for chord in chords
        for bp in blueprints
    ]

    # Run main tests
    results = run_tests(args.cli, configs, args.verbose, args.jobs)

    # Run comparison tests if requested
    compare_results = None
    if args.compare:
        compare_bps = [0]  # Traditional as baseline
        if not args.all_rhythmsync:
            compare_bps.extend([5, 7])  # Also compare other RhythmSync

        compare_configs = [
            (seed, style, chord, bp)
            for seed in seeds
            for style in styles
            for chord in chords
            for bp in compare_bps
        ]
        print()
        compare_results = run_tests(args.cli, compare_configs, args.verbose, args.jobs)

    # Save report if requested
    if args.output:
        save_json_report(results, compare_results, args.output)

    # Print summary
    passed = print_rhythmlock_summary(results, compare_results)
    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    main()
