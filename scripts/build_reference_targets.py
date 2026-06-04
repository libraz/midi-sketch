#!/usr/bin/env python3
"""Build per-blueprint-category target profiles from labeled reference MIDIs.

Reads backup/reference/track_roles.json (labels + _blueprint_categories),
profiles every labeled track with confidence high/medium and a non-null
ms_role, then aggregates min/median/max per (category, ms_role) and writes
backup/reference/target_profiles.json.

Usage:
    python3 scripts/build_reference_targets.py            # write + summary table
    python3 scripts/build_reference_targets.py --dry-run  # summary table only
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from statistics import median

from reference_motif_report import (
    labeled_tracks,
    load_notes,
    load_track_roles,
    profile_track,
)

REFERENCE_DIR = Path(__file__).resolve().parent.parent / "backup" / "reference"

METRICS = (
    "notes_per_bar",
    "short_pulse_ratio",
    "eighth_grid_ratio",
    "sixteenth_grid_ratio",
    "repeat_cell_consistency",
    "pitch_class_focus",
    "avg_pitch",
    "lead_overlap_ratio",
    "lead_overtake_ratio",
)


def collect_profiles() -> tuple[dict, dict[str, list]]:
    """Profile labeled reference tracks, grouped by (category, ms_role)."""
    roles_data = json.loads((REFERENCE_DIR / "track_roles.json").read_text())
    categories = roles_data["_blueprint_categories"]
    grouped: dict[str, list] = {}
    for cat, info in categories.items():
        if cat.startswith("_"):
            continue
        for fname in info["files"]:
            path = REFERENCE_DIR / fname
            roles = load_track_roles(path)
            if not roles:
                continue
            division, notes, _, _ = load_notes(path)
            file_entry = roles_data.get(fname, {})
            for label, role, ms_role, track_notes, lead in labeled_tracks(notes, roles, None):
                if not ms_role or ms_role == "Drums":
                    continue
                key = label.split(":")[0]
                confidence = file_entry.get(key, {}).get("confidence", "low")
                if confidence == "low":
                    continue
                profile = profile_track(fname, label, track_notes, division, lead,
                                        role=role, ms_role=ms_role)
                grouped.setdefault(cat, []).append(profile)
    return categories, grouped


def aggregate(profiles: list) -> dict:
    """min/median/max per metric across a list of TrackProfile."""
    out: dict = {"n": len(profiles), "sources": sorted({p.source for p in profiles})}
    for metric in METRICS:
        values = [getattr(p, metric) for p in profiles if getattr(p, metric) is not None]
        if values:
            out[metric] = {
                "min": round(min(values), 3),
                "med": round(median(values), 3),
                "max": round(max(values), 3),
            }
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dry-run", action="store_true", help="print summary without writing")
    args = parser.parse_args()

    categories, grouped = collect_profiles()
    result: dict = {
        "_generated_by": "scripts/build_reference_targets.py",
        "_source": "track_roles.json labels (confidence high/medium, ms_role set, Drums excluded)",
        "categories": {},
    }
    for cat, profiles in grouped.items():
        by_role: dict[str, list] = {}
        for p in profiles:
            by_role.setdefault(p.ms_role, []).append(p)
        result["categories"][cat] = {
            "blueprints": categories[cat]["blueprints"],
            "roles": {role: aggregate(ps) for role, ps in sorted(by_role.items())},
        }

    if not args.dry_run:
        out_path = REFERENCE_DIR / "target_profiles.json"
        out_path.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n")
        print(f"wrote {out_path}\n")

    for cat, data in result["categories"].items():
        print(f"=== {cat} (blueprints {data['blueprints']}) ===")
        print(f"  {'role':9s} {'n':>2s} {'notes/bar':>16s} {'cell_consist':>16s} "
              f"{'short_pulse':>16s} {'overtake':>13s}")
        for role, agg in data["roles"].items():
            def rng(metric: str) -> str:
                m = agg.get(metric)
                return f"{m['min']:4.2f}-{m['med']:4.2f}-{m['max']:4.2f}" if m else "       -"
            print(f"  {role:9s} {agg['n']:2d} {rng('notes_per_bar'):>16s} "
                  f"{rng('repeat_cell_consistency'):>16s} {rng('short_pulse_ratio'):>16s} "
                  f"{rng('lead_overtake_ratio'):>13s}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
