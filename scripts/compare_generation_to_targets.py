#!/usr/bin/env python3
"""Compare generated output against reference target profiles.

For each blueprint category in a target-profile file, generates songs with a
representative blueprint across multiple seeds, profiles each named track, and
reports per-role metrics that fall outside the reference min-max range.

The profiles are derived from a commercial reference corpus that cannot be
redistributed, so the file is supplied by the caller and is not part of the
repository. Point --targets at your own copy.

Usage:
    python3 scripts/compare_generation_to_targets.py --targets PATH
    python3 scripts/compare_generation_to_targets.py --targets PATH --seeds 10
    python3 scripts/compare_generation_to_targets.py --targets PATH --category rhythmsync
    python3 scripts/compare_generation_to_targets.py --targets PATH --json
"""

from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from collections import defaultdict
from pathlib import Path
from statistics import median

import melodic_metrics as mm
from reference_motif_report import Note, profile_track

ROOT = Path(__file__).resolve().parent.parent
CLI = ROOT / "build" / "bin" / "midisketch_cli"

# Metrics compared against reference ranges. lead_overtake is checked
# upper-bound only (lower interference than references is fine).
COMPARED_METRICS = (
    "notes_per_bar",
    "repeat_cell_consistency",
    "short_pulse_ratio",
    "lead_overtake_ratio",
)
# Quantization makes this measurement useful for diagnosis, but it is not a
# musical quality boundary.  Report it without emitting LOW/HIGH verdicts.
ARTIFACT_METRICS = ("eighth_grid_ratio",)
LOWER_IS_FINE = {"lead_overtake_ratio"}
SKIP_TRACKS = {"Drums", "SE"}

# Backward-compatible name for callers of this script; the canonical list
# lives in melodic_metrics with the analyzer's measurement definitions.
MELODY_COMPARED = mm.MELODY_STYLE_METRICS


def category_blueprints(target: dict) -> list[int]:
    """Return every configured blueprint for a reference category."""
    return sorted({int(blueprint) for blueprint in target.get("blueprints", [])})


def melody_profile_from_json(data: dict) -> dict | None:
    """Layer 2 melody profile of the generated Vocal track (skyline)."""
    vocal = next((t for t in data.get("tracks", []) if t["name"] == "Vocal"), None)
    if not vocal or not vocal["notes"]:
        return None
    division = int(data.get("division", 480))
    bpm = float(data.get("bpm") or 120)
    seq = mm.skyline(sorted(
        (int(n["start_ticks"]), int(n["duration_ticks"]), int(n["pitch"]))
        for n in vocal["notes"]
    ))
    return mm.melody_profile(seq, division, bpm)


def generate_profiles(blueprint: int, seed: int, workdir: Path) -> tuple[list, dict | None]:
    subprocess.run(
        [str(CLI), "--blueprint", str(blueprint), "--seed", str(seed), "--json"],
        cwd=workdir, capture_output=True, check=True,
    )
    data = json.loads((workdir / "output.json").read_text())
    division = int(data.get("division", 480))
    by_name: dict[str, list[Note]] = {}
    for track in data["tracks"]:
        notes = [
            Note(track=track["channel"], channel=track["channel"],
                 pitch=int(n["pitch"]), velocity=int(n.get("velocity", 80)),
                 start=int(n["start_ticks"]), duration=int(n["duration_ticks"]))
            for n in track["notes"]
        ]
        if notes:
            by_name[track["name"]] = notes
    lead = by_name.get("Vocal", [])
    profiles = []
    for name, notes in by_name.items():
        if name in SKIP_TRACKS:
            continue
        profiles.append(profile_track(f"bp{blueprint}/seed{seed}", name, notes, division,
                                      None if name == "Vocal" else lead,
                                      role=name.lower(), ms_role=name))
    return profiles, melody_profile_from_json(data)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--targets", type=Path, required=True,
                        help="target-profile JSON produced from your reference corpus")
    parser.add_argument("--seeds", type=int, default=5, help="seeds per blueprint")
    parser.add_argument("--category", type=str, default=None)
    parser.add_argument("--json", action="store_true", dest="as_json")
    args = parser.parse_args()

    if not args.targets.is_file():
        print(f"target-profile file not found: {args.targets}")
        return 1

    targets_full = json.loads(args.targets.read_text())
    targets = targets_full["categories"]
    common_rules = targets_full.get("melody_common_rules", {})
    report: dict = {}
    for cat, target in targets.items():
        if args.category and cat != args.category:
            continue
        blueprints = category_blueprints(target)
        if not blueprints:
            continue
        by_role: dict[str, list] = defaultdict(list)
        melody_profiles: list[dict] = []
        with tempfile.TemporaryDirectory() as tmp:
            for blueprint in blueprints:
                for seed in range(1, args.seeds + 1):
                    profiles, mel = generate_profiles(blueprint, seed, Path(tmp))
                    for p in profiles:
                        by_role[p.ms_role].append(p)
                    if mel:
                        melody_profiles.append(mel)

        cat_report: dict = {"blueprints": blueprints, "roles": {}}
        for role, profiles in sorted(by_role.items()):
            ref = target["roles"].get(role)
            role_report: dict = {"n_gen": len(profiles), "metrics": {}, "in_reference": ref is not None}
            for metric in (*COMPARED_METRICS, *ARTIFACT_METRICS):
                values = [getattr(p, metric) for p in profiles if getattr(p, metric) is not None]
                if not values:
                    continue
                gen_med = round(median(values), 3)
                entry: dict = {"gen_med": gen_med}
                if metric in ARTIFACT_METRICS:
                    entry["artifact"] = True
                if ref and metric in ref:
                    lo, hi = ref[metric]["min"], ref[metric]["max"]
                    entry["ref"] = [lo, ref[metric]["med"], hi]
                    if metric in ARTIFACT_METRICS:
                        pass
                    elif gen_med < lo and metric not in LOWER_IS_FINE:
                        entry["verdict"] = "LOW"
                    elif gen_med > hi:
                        entry["verdict"] = "HIGH"
                    else:
                        entry["verdict"] = "ok"
                role_report["metrics"][metric] = entry
            cat_report["roles"][role] = role_report

        # Layer 2: melody coloring vs per-category ranges (median over seeds)
        ref_mel = target.get("melody")
        if melody_profiles and ref_mel:
            mel_report: dict = {"n_gen": len(melody_profiles), "metrics": {}}
            for metric in MELODY_COMPARED:
                values = [p[metric] for p in melody_profiles if p.get(metric) is not None]
                if not values:
                    continue
                gen_med = round(median(values), 3)
                entry = {"gen_med": gen_med}
                if metric in ref_mel:
                    lo, hi = ref_mel[metric]["min"], ref_mel[metric]["max"]
                    entry["ref"] = [lo, ref_mel[metric]["med"], hi]
                    entry["verdict"] = "LOW" if gen_med < lo else "HIGH" if gen_med > hi else "ok"
                mel_report["metrics"][metric] = entry
            cat_report["melody"] = mel_report

        # Layer 1: common prohibitions (worst seed must stay within bound)
        if melody_profiles and common_rules:
            violations: dict = {}
            for rule_name, rule in common_rules.items():
                if rule_name.startswith("_") or rule.get("bound") is None:
                    continue
                values = [p[rule["metric"]] for p in melody_profiles
                          if p.get(rule["metric"]) is not None]
                if rule.get("min_samples"):
                    values = [p[rule["metric"]] for p in melody_profiles
                              if p.get(rule["metric"]) is not None
                              and p.get("leap12_leaps", 0) >= rule["min_samples"]]
                if not values:
                    continue
                worst = max(values) if rule["direction"] == "max" else min(values)
                ok = worst <= rule["bound"] if rule["direction"] == "max" \
                    else worst >= rule["bound"]
                if not ok:
                    violations[rule_name] = {
                        "worst": round(worst, 4), "bound": rule["bound"],
                        "severity": rule["severity"],
                    }
            cat_report["melody_common_violations"] = violations
        report[cat] = cat_report

    if args.as_json:
        print(json.dumps(report, indent=2))
        return 0

    for cat, cat_report in report.items():
        print(f"=== {cat} (blueprints {cat_report['blueprints']}, "
              f"{args.seeds} seeds each) ===")
        for role, rr in cat_report["roles"].items():
            if not rr["in_reference"]:
                print(f"  {role:9s} (no reference data)")
                continue
            issues = []
            for metric, entry in rr["metrics"].items():
                verdict = entry.get("verdict")
                if verdict in ("LOW", "HIGH"):
                    lo, med, hi = entry["ref"]
                    issues.append(f"{metric}={entry['gen_med']} {verdict} (ref {lo}-{med}-{hi})")
            status = "; ".join(issues) if issues else "ok"
            print(f"  {role:9s} {status}")
        mel = cat_report.get("melody")
        if mel:
            issues = []
            for metric, entry in mel["metrics"].items():
                if entry.get("verdict") in ("LOW", "HIGH"):
                    lo, med, hi = entry["ref"]
                    issues.append(f"{metric}={entry['gen_med']} {entry['verdict']} "
                                  f"(ref {lo}-{med}-{hi})")
            status = "; ".join(issues) if issues else "ok"
            print(f"  {'melody':9s} {status}")
        violations = cat_report.get("melody_common_violations")
        if violations:
            for rule, v in violations.items():
                print(f"  {'COMMON!':9s} {rule}: worst={v['worst']} vs bound={v['bound']} "
                      f"[{v['severity']}]")
        elif violations == {}:
            print(f"  {'common':9s} ok (no Layer 1 violations)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
