#!/usr/bin/env python3
"""Attribute vocal melodic leaps to their provenance (raw generation vs transforms).

For every consecutive vocal note pair, compares the RAW interval (using
provenance.original_pitch, i.e. the pitch chosen by the melody generator before
any transform) against the FINAL interval (output pitch). Leaps that exist in
the final melody but not in the raw melody were *manufactured* by a transform
(chord_tone_snap, collision_avoid, range_clamp, or an unrecorded pass); leaps
already present in the raw melody are attributed to the generating source
(melody_phrase, hook, ...).

Usage:
  python3 scripts/leap_attribution.py [--blueprints 0,1,3,4] [--seeds 10] [--json]

Requires ./build/bin/midisketch_cli.
"""

import argparse
import collections
import json
import subprocess
import sys
import tempfile
from pathlib import Path

CLI = Path(__file__).resolve().parent.parent / "build" / "bin" / "midisketch_cli"

STEP_MAX = 2          # 1-2 semitones: step
LEAP_SMALL_MAX = 4    # 3-4 semitones: small leap
# >= 5: large leap

LEAP_CLASSES = ("leap_small", "leap_large")


def classify(interval):
    """Same classes as scripts/melodic_metrics.py interval_distribution()."""
    if interval == 0:
        return "same"
    if interval <= STEP_MAX:
        return "step"
    if interval <= LEAP_SMALL_MAX:
        return "leap_small"
    return "leap_large"


def note_raw_pitch(note):
    prov = note.get("provenance") or {}
    orig = prov.get("original_pitch")
    return orig if orig is not None else note["pitch"]


def note_transform_types(note):
    prov = note.get("provenance") or {}
    types = [t["type"] for t in prov.get("transforms", [])]
    if not types and prov.get("original_pitch") is not None and prov["original_pitch"] != note["pitch"]:
        types = ["unrecorded"]
    return types


def analyze_song(data):
    """Returns per-song counters."""
    vocal = next((t for t in data["tracks"] if t["name"] == "Vocal"), None)
    counters = {
        "pairs": 0,
        "final": collections.Counter(),       # same / step / leap_small / leap_large
        "raw": collections.Counter(),
        "manufactured_pairs": collections.Counter(),  # final class -> unique pair count
        "manufactured": collections.Counter(),  # transform type -> count (may double-count pairs)
        "healed": 0,                             # raw leap -> final same/step
        "raw_leap_sources": collections.Counter(),  # source of 2nd note for raw leaps kept in final
        "max_step_streak": 0,
        "turns": 0,
    }
    if vocal is None or len(vocal["notes"]) < 2:
        return counters

    notes = sorted(vocal["notes"], key=lambda n: n["start_ticks"])
    streak = 0
    prev_dir = 0
    for a, b in zip(notes, notes[1:]):
        fi = abs(b["pitch"] - a["pitch"])
        ri = abs(note_raw_pitch(b) - note_raw_pitch(a))
        fc, rc = classify(fi), classify(ri)
        counters["pairs"] += 1
        counters["final"][fc] += 1
        counters["raw"][rc] += 1

        # Direction turns (final melody)
        d = (b["pitch"] > a["pitch"]) - (b["pitch"] < a["pitch"])
        if d != 0:
            if prev_dir != 0 and d != prev_dir:
                counters["turns"] += 1
            prev_dir = d

        # Conjunct streaks (final melody, moving steps only)
        if 1 <= fi <= STEP_MAX:
            streak += 1
            counters["max_step_streak"] = max(counters["max_step_streak"], streak)
        else:
            streak = 0

        f_leap = fc in LEAP_CLASSES
        r_leap = rc in LEAP_CLASSES
        if f_leap and not r_leap:
            # A transform manufactured this leap. Blame transforms on either
            # endpoint (both can contribute); fall back to "unknown".
            counters["manufactured_pairs"][fc] += 1
            types = set(note_transform_types(a)) | set(note_transform_types(b))
            if not types:
                types = {"unknown"}
            for t in types:
                counters["manufactured"][t] += 1
        elif not f_leap and r_leap:
            counters["healed"] += 1
        elif f_leap and r_leap:
            src = (b.get("provenance") or {}).get("source", "unknown")
            counters["raw_leap_sources"][src] += 1
    return counters


def merge(total, c):
    total["pairs"] += c["pairs"]
    for k in ("final", "raw", "manufactured_pairs", "manufactured", "raw_leap_sources"):
        total[k].update(c[k])
    total["healed"] += c["healed"]
    total["turns"] += c["turns"]
    total["max_step_streaks"].append(c["max_step_streak"])


def run_one(blueprint, seed, workdir):
    out = workdir / "output.json"
    if out.exists():
        out.unlink()
    cmd = [str(CLI), "--seed", str(seed), "--blueprint", str(blueprint), "--json"]
    r = subprocess.run(cmd, cwd=workdir, capture_output=True, text=True)
    if r.returncode != 0 or not out.exists():
        print(f"  WARN bp={blueprint} seed={seed}: generation failed", file=sys.stderr)
        return None
    with open(out) as f:
        return json.load(f)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--blueprints", default="0,1,3,4")
    ap.add_argument("--seeds", type=int, default=10)
    ap.add_argument("--json", action="store_true", dest="json_out")
    args = ap.parse_args()

    blueprints = [int(b) for b in args.blueprints.split(",")]
    report = {}

    with tempfile.TemporaryDirectory() as td:
        workdir = Path(td)
        for bp in blueprints:
            total = {
                "pairs": 0,
                "final": collections.Counter(),
                "raw": collections.Counter(),
                "manufactured_pairs": collections.Counter(),
                "manufactured": collections.Counter(),
                "healed": 0,
                "raw_leap_sources": collections.Counter(),
                "turns": 0,
                "max_step_streaks": [],
            }
            for seed in range(1, args.seeds + 1):
                data = run_one(bp, seed, workdir)
                if data is None:
                    continue
                merge(total, analyze_song(data))
            report[bp] = total

    if args.json_out:
        out = {}
        for bp, t in report.items():
            out[bp] = {
                "pairs": t["pairs"],
                "final": dict(t["final"]),
                "raw": dict(t["raw"]),
                "manufactured_pairs": dict(t["manufactured_pairs"]),
                "manufactured_by": dict(t["manufactured"]),
                "healed": t["healed"],
                "raw_leap_sources": dict(t["raw_leap_sources"]),
                "turns": t["turns"],
                "max_step_streaks": t["max_step_streaks"],
            }
        print(json.dumps(out, indent=2))
        return

    for bp, t in report.items():
        p = max(t["pairs"], 1)
        f, r = t["final"], t["raw"]
        mp = t["manufactured_pairs"]
        manufactured_total = sum(mp.values())
        final_leaps = f["leap_small"] + f["leap_large"]
        print(f"\n=== Blueprint {bp} ({t['pairs']} intervals, {len(t['max_step_streaks'])} songs) ===")
        print(f"  RAW   : same {r['same']/p:5.1%}  step {r['step']/p:5.1%}  leap_small {r['leap_small']/p:5.1%}  leap_large {r['leap_large']/p:5.1%}")
        print(f"  FINAL : same {f['same']/p:5.1%}  step {f['step']/p:5.1%}  leap_small {f['leap_small']/p:5.1%}  leap_large {f['leap_large']/p:5.1%}")
        print(f"  Leaps manufactured by transforms: {manufactured_total}"
              f" ({manufactured_total/max(final_leaps,1):.1%} of final leaps)"
              f"  [small {mp['leap_small']}, large {mp['leap_large']}]")
        print(f"  Blame breakdown (a pair may have several transform types):")
        for ttype, n in t["manufactured"].most_common():
            print(f"    {ttype:20s} {n:5d}")
        print(f"  Leaps healed by transforms (raw leap -> final same/step): {t['healed']}")
        print(f"  Raw leaps surviving to final, by source:")
        for src, n in t["raw_leap_sources"].most_common():
            print(f"    {src:20s} {n:5d}")
        streaks = t["max_step_streaks"]
        if streaks:
            print(f"  Max conjunct streak per song: min {min(streaks)} med {sorted(streaks)[len(streaks)//2]} max {max(streaks)}")
        print(f"  Direction turns: {t['turns']} ({t['turns']/p*100:.0f} per 100 intervals)")


if __name__ == "__main__":
    main()
