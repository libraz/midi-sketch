#!/usr/bin/env python3
"""Compare generated output against reference target profiles.

For each blueprint category in backup/reference/target_profiles.json,
generates songs with a representative blueprint across multiple seeds,
profiles each named track, and reports per-role metrics that fall outside
the reference min-max range.

Usage:
    python3 scripts/compare_generation_to_targets.py                # all categories
    python3 scripts/compare_generation_to_targets.py --seeds 10
    python3 scripts/compare_generation_to_targets.py --category rhythmsync
    python3 scripts/compare_generation_to_targets.py --json         # machine-readable
"""

from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from collections import defaultdict
from pathlib import Path
from statistics import median

from reference_motif_report import Note, profile_track

ROOT = Path(__file__).resolve().parent.parent
CLI = ROOT / "build" / "bin" / "midisketch_cli"
TARGETS = ROOT / "backup" / "reference" / "target_profiles.json"

# Metrics compared against reference ranges. lead_overtake is checked
# upper-bound only (lower interference than references is fine).
COMPARED_METRICS = (
    "notes_per_bar",
    "repeat_cell_consistency",
    "short_pulse_ratio",
    "eighth_grid_ratio",
    "lead_overtake_ratio",
)
LOWER_IS_FINE = {"lead_overtake_ratio"}
SKIP_TRACKS = {"Drums", "SE"}


def generate_profiles(blueprint: int, seed: int, workdir: Path) -> list:
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
    return profiles


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seeds", type=int, default=5, help="seeds per blueprint")
    parser.add_argument("--category", type=str, default=None)
    parser.add_argument("--json", action="store_true", dest="as_json")
    args = parser.parse_args()

    targets = json.loads(TARGETS.read_text())["categories"]
    report: dict = {}
    for cat, target in targets.items():
        if args.category and cat != args.category:
            continue
        blueprint = target["blueprints"][0]
        by_role: dict[str, list] = defaultdict(list)
        with tempfile.TemporaryDirectory() as tmp:
            for seed in range(1, args.seeds + 1):
                for p in generate_profiles(blueprint, seed, Path(tmp)):
                    by_role[p.ms_role].append(p)

        cat_report: dict = {"blueprint": blueprint, "roles": {}}
        for role, profiles in sorted(by_role.items()):
            ref = target["roles"].get(role)
            role_report: dict = {"n_gen": len(profiles), "metrics": {}, "in_reference": ref is not None}
            for metric in COMPARED_METRICS:
                values = [getattr(p, metric) for p in profiles if getattr(p, metric) is not None]
                if not values:
                    continue
                gen_med = round(median(values), 3)
                entry: dict = {"gen_med": gen_med}
                if ref and metric in ref:
                    lo, hi = ref[metric]["min"], ref[metric]["max"]
                    entry["ref"] = [lo, ref[metric]["med"], hi]
                    if gen_med < lo and metric not in LOWER_IS_FINE:
                        entry["verdict"] = "LOW"
                    elif gen_med > hi:
                        entry["verdict"] = "HIGH"
                    else:
                        entry["verdict"] = "ok"
                role_report["metrics"][metric] = entry
            cat_report["roles"][role] = role_report
        report[cat] = cat_report

    if args.as_json:
        print(json.dumps(report, indent=2))
        return 0

    for cat, cat_report in report.items():
        print(f"=== {cat} (blueprint {cat_report['blueprint']}, {args.seeds} seeds) ===")
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
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
