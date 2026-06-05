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

import melodic_metrics as mm
from reference_motif_report import (
    labeled_tracks,
    load_notes,
    load_track_roles,
    profile_track,
)
from validate_melodic_rules import load_vocal_sequences

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

# Layer 2 (genre coloring): melodic metrics aggregated per category.
# Ranges are derived from labeled vocal tracks (melody_exclude respected).
MELODY_METRICS = (
    "same_ratio",
    "step_ratio",
    "leap_small_ratio",
    "leap_large_ratio",
    "leap5_step_recovery_rate",
    "leap5_contrary_rate",
    "turns_per_100",
    "run_conjunct_ratio",
    "run_note_ratio",
    "flat_ratio",
    "arch_ratio",
    "range",
    "climax_position",
    "max_streak",
    "pitch_cell_consistency",
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


def collect_melody_profiles() -> dict[str, list[dict]]:
    """Melody profiles of labeled vocal tracks, grouped by category."""
    grouped: dict[str, list[dict]] = {}
    for entry in load_vocal_sequences():
        profile = mm.melody_profile(entry["seq"], entry["division"], entry["bpm"])
        profile["_source"] = entry["file"]
        grouped.setdefault(entry["category"], []).append(profile)
    return grouped


def aggregate_melody(profiles: list[dict]) -> dict:
    """min/median/max per melody metric (Layer 2 genre coloring)."""
    out: dict = {"n": len(profiles), "sources": sorted(p["_source"] for p in profiles)}
    for metric in MELODY_METRICS:
        values = [p[metric] for p in profiles if p.get(metric) is not None]
        if values:
            out[metric] = {
                "min": round(min(values), 3),
                "med": round(median(values), 3),
                "max": round(max(values), 3),
            }
    return out


def build_common_melody_rules(grouped: dict[str, list[dict]]) -> dict:
    """Layer 1 (common prohibition) rules with corpus-derived bounds.

    Membership criterion: every category satisfies the rule with near-zero
    violation (see scripts/validate_melodic_rules.py). Bounds are derived
    from the worst corpus value plus documented slack — never hand-written.
    Rules any genre systematically violates (flat phrases, leap recovery,
    same-pitch streaks) live in the per-category melody ranges instead.
    """
    pooled = [p for profiles in grouped.values() for p in profiles]

    chain_values = [p["chains_3plus_per_100_moves"] for p in pooled]
    # C2 rate is noisy when a file has very few octave leaps; require 3+.
    contrary_values = [
        p["leap12_contrary_rate"] for p in pooled
        if p.get("leap12_contrary_rate") is not None and p["leap12_leaps"] >= 3
    ]
    unsingable_values = [p["unsingable_rate"] for p in pooled
                         if p.get("unsingable_rate") is not None]

    return {
        "_derivation": (
            "Bounds derived from the full labeled-vocal corpus "
            "(all categories pooled; melody_exclude respected). "
            "Validation table: scripts/validate_melodic_rules.py"
        ),
        "same_direction_leap_chains": {
            "metric": "chains_3plus_per_100_moves",
            "direction": "max",
            "bound": round(max(1.0, max(chain_values) * 1.5), 3),
            "corpus_worst": round(max(chain_values), 3),
            "severity": "warning",
            "description": "3+ consecutive same-direction leaps (>=3st) "
                           "outline an arpeggio, not a melodic line",
        },
        "octave_leap_contrary": {
            "metric": "leap12_contrary_rate",
            "direction": "min",
            "bound": round(min(contrary_values) * 0.9, 3) if contrary_values else None,
            "corpus_worst": round(min(contrary_values), 3) if contrary_values else None,
            "min_samples": 3,
            "severity": "warning",
            "description": "Leaps beyond an octave are followed by contrary "
                           "motion or pitch repeat",
        },
        "unsingable_moves": {
            "metric": "unsingable_rate",
            "direction": "max",
            "bound": round(max(unsingable_values), 3) if unsingable_values else 0.0,
            "corpus_worst": round(max(unsingable_values), 3) if unsingable_values else 0.0,
            "severity": "error",
            "description": "Moves wider than an octave faster than 150ms do "
                           "not occur even at 240 BPM in the corpus",
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dry-run", action="store_true", help="print summary without writing")
    args = parser.parse_args()

    categories, grouped = collect_profiles()
    melody_grouped = collect_melody_profiles()
    result: dict = {
        "_generated_by": "scripts/build_reference_targets.py",
        "_source": "track_roles.json labels (confidence high/medium, ms_role set, Drums excluded)",
        "_melody_source": (
            "Labeled vocal tracks, skyline-filtered, melody_exclude respected. "
            "Layer 1 = melody_common_rules (all genres), Layer 2 = per-category "
            "melody ranges (genre coloring). shoushitsu is a 240 BPM extreme "
            "reference and widens rhythmsync ranges deliberately. Ballad melody "
            "rests on a single reference (secret_base) and is provisional: "
            "sparkle/yoru piano arrangements fail skyline extraction "
            "(measured range 65/59 st = accompaniment pollution)."
        ),
        "melody_common_rules": build_common_melody_rules(melody_grouped),
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
        if cat in melody_grouped:
            result["categories"][cat]["melody"] = aggregate_melody(melody_grouped[cat])

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

    print("\n=== melody (Layer 2 genre coloring, labeled vocals) ===")
    print(f"  {'cat':10s} {'n':>2s} {'step%':>15s} {'leap5+%':>15s} "
          f"{'runConj%':>15s} {'arch%':>15s} {'maxStreak':>12s}")
    for cat, data in result["categories"].items():
        mel = data.get("melody")
        if not mel:
            continue
        def mrng(metric: str, pct: bool = True) -> str:
            m = mel.get(metric)
            if not m:
                return "-"
            k = 100 if pct else 1
            return f"{m['min'] * k:3.0f}-{m['med'] * k:3.0f}-{m['max'] * k:3.0f}"
        print(f"  {cat:10s} {mel['n']:2d} {mrng('step_ratio'):>15s} "
              f"{mrng('leap_large_ratio'):>15s} {mrng('run_conjunct_ratio'):>15s} "
              f"{mrng('arch_ratio'):>15s} {mrng('max_streak', False):>12s}")

    common = result["melody_common_rules"]
    print("\n=== melody common rules (Layer 1) ===")
    for name, rule in common.items():
        if name.startswith("_"):
            continue
        print(f"  {name:30s} {rule['metric']:30s} "
              f"{rule['direction']}={rule['bound']} (corpus worst {rule['corpus_worst']}) "
              f"[{rule['severity']}]")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
