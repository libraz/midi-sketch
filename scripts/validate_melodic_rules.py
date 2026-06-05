#!/usr/bin/env python3
"""Validate Layer 1 (common prohibition) candidate rules against the corpus.

Membership criterion: a rule may live in the common layer (_common) only if
EVERY reference category satisfies it with a near-zero violation rate. A rule
that any genre systematically violates is genre style, not grammar, and must
be demoted to the per-category (Layer 2) tables.

Usage:
    python3 scripts/validate_melodic_rules.py            # table per ref vocal
    python3 scripts/validate_melodic_rules.py --json     # machine-readable
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import melodic_metrics as mm
from compare_midi_profile import parse_smf

REFERENCE_DIR = Path(__file__).parent.parent / "backup" / "reference"


def median_bpm(tempos: list[tuple[int, int]]) -> float:
    """BPM from tempo events, ignoring the tick-0 placeholder when present."""
    usable = [t for t in tempos if t[1] > 0]
    if len(usable) > 1 and usable[0][0] == 0 and usable[0][1] == 1000000:
        usable = usable[1:]
    if not usable:
        return 120.0
    usecs = sorted(u for _, u in usable)
    return 60_000_000 / usecs[len(usecs) // 2]


def load_vocal_sequences(include_excluded: bool = False) -> list[dict]:
    """All labeled vocal tracks, skyline-filtered, with category attached.

    Tracks flagged `melody_exclude` in track_roles.json (e.g. piano
    arrangements where skyline cannot isolate the melody) are skipped
    unless include_excluded is set.
    """
    roles = json.loads((REFERENCE_DIR / "track_roles.json").read_text())
    categories = {
        fname: cat
        for cat, info in roles.get("_blueprint_categories", {}).items()
        if isinstance(info, dict)
        for fname in info.get("files", [])
    }
    out = []
    for fname, entries in roles.items():
        if fname.startswith("_") or not isinstance(entries, dict):
            continue
        vocal_keys = [
            k for k, v in entries.items()
            if isinstance(v, dict) and v.get("role") == "vocal"
            and (include_excluded or not v.get("melody_exclude"))
        ]
        if not vocal_keys:
            continue
        smf = parse_smf(REFERENCE_DIR / fname)
        for key in vocal_keys:
            trk, ch = key.split("ch")
            trk, ch = int(trk[3:]), int(ch)
            notes = [n for n in smf["notes"] if n.track == trk and n.channel == ch]
            if not notes:
                continue
            seq = mm.skyline(mm.from_objects(notes))
            out.append({
                "file": fname,
                "track": key,
                "category": categories.get(fname, "?"),
                "division": smf["division"],
                "bpm": median_bpm(smf["tempos"]),
                "seq": seq,
            })
    return out


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", action="store_true", help="JSON output")
    args = parser.parse_args()

    rows = []
    for entry in load_vocal_sequences():
        seq, division, bpm = entry["seq"], entry["division"], entry["bpm"]
        profile = mm.melody_profile(seq, division, bpm)
        rows.append({
            "file": entry["file"].replace(".mid", ""),
            "category": entry["category"],
            "notes": profile["notes"],
            "bpm": round(bpm),
            # Layer 1 candidates (violation rates / counts)
            "C1_chains3plus_per100": profile["chains_3plus_per_100_moves"],
            "C2_oct_leap_contrary": profile["leap12_contrary_rate"],
            "C2_oct_leaps": profile["leap12_leaps"],
            "C4_unsingable_rate": profile.get("unsingable_rate"),
            "C5_flat_phrase_ratio": profile["flat_ratio"],
            "C6_leap5_step_recovery": profile["leap5_step_recovery_rate"],
            "C6_leap5_contrary": profile["leap5_contrary_rate"],
            "C7_max_same_pitch_streak": profile["max_streak"],
            # Layer 2 calibration extras
            "step_ratio": profile["step_ratio"],
            "leap_large_ratio": profile["leap_large_ratio"],
            "run_conjunct_ratio": profile["run_conjunct_ratio"],
            "turns_per_100": profile["turns_per_100"],
            "range": profile["range"],
            "climax_position": profile["climax_position"],
            "arch_ratio": profile["arch_ratio"],
            "pitch_cell_consistency": profile["pitch_cell_consistency"],
        })

    if args.json:
        print(json.dumps(rows, indent=1))
        return

    def fmt(v, pct=False):
        if v is None:
            return "   -"
        if pct:
            return f"{100 * v:4.0f}"
        return f"{v:4.1f}" if isinstance(v, float) else f"{v:4d}"

    print(f"{'file':<26} {'cat':<10} {'n':>4} {'bpm':>3} | "
          f"{'C1/100':>6} {'C2ok%':>5}({'n':>2}) {'C4%':>4} {'C5flat%':>7} "
          f"{'C6step%':>7} {'C6ctr%':>6} {'C7run':>5}")
    for r in sorted(rows, key=lambda x: (x["category"], x["file"])):
        print(f"{r['file']:<26} {r['category']:<10} {r['notes']:>4} {r['bpm']:>3} | "
              f"{fmt(r['C1_chains3plus_per100']):>6} "
              f"{fmt(r['C2_oct_leap_contrary'], True):>5}({r['C2_oct_leaps']:>2}) "
              f"{fmt(r['C4_unsingable_rate'], True):>4} "
              f"{fmt(r['C5_flat_phrase_ratio'], True):>7} "
              f"{fmt(r['C6_leap5_step_recovery'], True):>7} "
              f"{fmt(r['C6_leap5_contrary'], True):>6} "
              f"{r['C7_max_same_pitch_streak']:>5}")

    print()
    print(f"{'file':<26} {'cat':<10} | {'step%':>5} {'leap5+%':>7} {'runConj%':>8} "
          f"{'turns':>5} {'range':>5} {'climaxPos':>9} {'arch%':>5} {'pitchCC':>7}")
    for r in sorted(rows, key=lambda x: (x["category"], x["file"])):
        print(f"{r['file']:<26} {r['category']:<10} | "
              f"{fmt(r['step_ratio'], True):>5} "
              f"{fmt(r['leap_large_ratio'], True):>7} "
              f"{fmt(r['run_conjunct_ratio'], True):>8} "
              f"{fmt(r['turns_per_100']):>5} "
              f"{r['range']:>5} "
              f"{fmt(r['climax_position']):>9} "
              f"{fmt(r['arch_ratio'], True):>5} "
              f"{fmt(r['pitch_cell_consistency'], True):>7}")


if __name__ == "__main__":
    main()
