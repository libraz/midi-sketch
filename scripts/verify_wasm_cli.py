#!/usr/bin/env python3
"""
verify_wasm_cli.py - Verify that WASM and CLI produce identical output.

Runs both the native CLI and WASM module (via Node.js) with the same
parameters and compares the generated events JSON and MIDI binary.

Usage:
    python3 scripts/verify_wasm_cli.py
    python3 scripts/verify_wasm_cli.py --seed 42 --style 3
    python3 scripts/verify_wasm_cli.py --sweep          # Test multiple configs
    python3 scripts/verify_wasm_cli.py --sweep --jobs 4  # Parallel sweep
"""

import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

PROJECT_ROOT = Path(__file__).resolve().parent.parent
CLI_BIN = PROJECT_ROOT / "build" / "bin" / "midisketch_cli"
WASM_HELPER = PROJECT_ROOT / "scripts" / "wasm_helper.mjs"

TRACK_NAMES = ["Vocal", "Chord", "Bass", "Motif", "Arpeggio", "Aux", "Drums", "SE"]


@dataclass
class Config:
    """Config params that both CLI and WASM accept.

    Fields set to None will use each side's createDefaultConfig() defaults.
    """
    seed: int = 1
    style: int = 0
    chord: int = 0
    form: int = 0
    bpm: int = 120
    blueprint: int = 0
    key: int = 0
    vocal_attitude: Optional[int] = None  # None = use style default
    vocal_low: Optional[int] = None
    vocal_high: Optional[int] = None

    def label(self) -> str:
        return (
            f"seed={self.seed} style={self.style} chord={self.chord} "
            f"form={self.form} bpm={self.bpm} bp={self.blueprint} key={self.key}"
        )


@dataclass
class DiffEntry:
    track: str
    field: str
    note_index: int
    cli_value: object
    wasm_value: object


@dataclass
class CompareResult:
    config: Config
    ok: bool = True
    midi_match: Optional[bool] = None
    events_match: Optional[bool] = None
    note_diffs: list[DiffEntry] = field(default_factory=list)
    track_count_diffs: list[str] = field(default_factory=list)
    meta_diffs: list[str] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)


def get_wasm_defaults(style: int) -> dict:
    """Get createDefaultConfig() defaults from WASM for a given style."""
    cmd = [
        "node", str(WASM_HELPER),
        "--style", str(style),
        "--dump-config",
    ]
    try:
        result = subprocess.run(
            cmd, capture_output=True, text=True, timeout=15, cwd=str(PROJECT_ROOT)
        )
        if result.returncode == 0:
            return json.loads(result.stdout)
    except (subprocess.TimeoutExpired, json.JSONDecodeError):
        pass
    return {}


def run_cli(cfg: Config, work_dir: Path) -> Optional[dict]:
    """Run native CLI and return events JSON.

    CLI writes output.mid and output.json to cwd, so we run it inside work_dir.
    All config fields are passed explicitly to override CLI defaults.
    """
    cmd = [
        str(CLI_BIN),
        "--seed", str(cfg.seed),
        "--style", str(cfg.style),
        "--chord", str(cfg.chord),
        "--form", str(cfg.form),
        "--bpm", str(cfg.bpm),
        "--blueprint", str(cfg.blueprint),
        "--key", str(cfg.key),
    ]
    if cfg.vocal_attitude is not None:
        cmd += ["--vocal-attitude", str(cfg.vocal_attitude)]
    if cfg.vocal_low is not None:
        cmd += ["--vocal-low", str(cfg.vocal_low)]
    if cfg.vocal_high is not None:
        cmd += ["--vocal-high", str(cfg.vocal_high)]
    try:
        result = subprocess.run(
            cmd, capture_output=True, text=True, timeout=30, cwd=str(work_dir)
        )
    except subprocess.TimeoutExpired:
        return None

    if result.returncode != 0:
        print(f"  [CLI stderr] {result.stderr[:500]}", file=sys.stderr)
        return None

    json_path = work_dir / "output.json"
    if not json_path.exists():
        return None

    try:
        return json.loads(json_path.read_text())
    except json.JSONDecodeError:
        return None


def run_wasm(cfg: Config, midi_path: Path) -> Optional[dict]:
    """Run WASM via Node.js helper and return events JSON.

    All config fields are passed explicitly to match CLI invocation.
    """
    cmd = [
        "node",
        str(WASM_HELPER),
        "--seed", str(cfg.seed),
        "--style", str(cfg.style),
        "--chord", str(cfg.chord),
        "--form", str(cfg.form),
        "--bpm", str(cfg.bpm),
        "--blueprint", str(cfg.blueprint),
        "--key", str(cfg.key),
        "--midi", str(midi_path),
    ]
    if cfg.vocal_attitude is not None:
        cmd += ["--vocal-attitude", str(cfg.vocal_attitude)]
    if cfg.vocal_low is not None:
        cmd += ["--vocal-low", str(cfg.vocal_low)]
    if cfg.vocal_high is not None:
        cmd += ["--vocal-high", str(cfg.vocal_high)]
    try:
        result = subprocess.run(
            cmd, capture_output=True, text=True, timeout=30, cwd=str(PROJECT_ROOT)
        )
    except subprocess.TimeoutExpired:
        return None

    if result.returncode != 0:
        print(f"  [WASM stderr] {result.stderr[:500]}", file=sys.stderr)
        return None

    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError:
        return None


def md5(path: Path) -> str:
    return hashlib.md5(path.read_bytes()).hexdigest()


def compare_notes(track_name: str, cli_notes: list, wasm_notes: list) -> list[DiffEntry]:
    """Compare note arrays and return diffs."""
    diffs = []
    max_len = max(len(cli_notes), len(wasm_notes))
    for i in range(max_len):
        if i >= len(cli_notes):
            diffs.append(DiffEntry(track_name, "MISSING_IN_CLI", i, None, wasm_notes[i]))
            continue
        if i >= len(wasm_notes):
            diffs.append(DiffEntry(track_name, "MISSING_IN_WASM", i, cli_notes[i], None))
            continue

        cn = cli_notes[i]
        wn = wasm_notes[i]
        for key in ("pitch", "velocity", "start_ticks", "duration_ticks"):
            cv = cn.get(key)
            wv = wn.get(key)
            if cv != wv:
                diffs.append(DiffEntry(track_name, key, i, cv, wv))
    return diffs


def compare(cfg: Config) -> CompareResult:
    """Run CLI and WASM, compare outputs.

    To ensure both sides use identical configs, we first get WASM defaults
    for the style, then fill in any None fields in cfg with those defaults.
    Both CLI and WASM receive all parameters explicitly.
    """
    res = CompareResult(config=cfg)

    # Fill in unset fields from WASM defaults for this style
    defaults = get_wasm_defaults(cfg.style)
    if cfg.vocal_attitude is None:
        cfg.vocal_attitude = defaults.get("vocalAttitude", 0)
    if cfg.vocal_low is None:
        cfg.vocal_low = defaults.get("vocalLow", 60)
    if cfg.vocal_high is None:
        cfg.vocal_high = defaults.get("vocalHigh", 79)

    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)
        cli_dir = tmp / "cli"
        cli_dir.mkdir()
        wasm_midi = tmp / "wasm.mid"

        cli_events = run_cli(cfg, cli_dir)
        if cli_events is None:
            res.ok = False
            res.errors.append("CLI execution failed")
            return res

        wasm_events = run_wasm(cfg, wasm_midi)
        if wasm_events is None:
            res.ok = False
            res.errors.append("WASM execution failed")
            return res

        # Compare MIDI binary
        cli_midi = cli_dir / "output.mid"
        if cli_midi.exists() and wasm_midi.exists():
            res.midi_match = md5(cli_midi) == md5(wasm_midi)
        else:
            res.midi_match = None
            if not cli_midi.exists():
                res.errors.append("CLI MIDI file not generated")
            if not wasm_midi.exists():
                res.errors.append("WASM MIDI file not generated")

        # Compare metadata
        for key in ("bpm", "division", "duration_ticks"):
            cv = cli_events.get(key)
            wv = wasm_events.get(key)
            if cv != wv:
                res.meta_diffs.append(f"{key}: CLI={cv} WASM={wv}")

        # Compare tracks
        cli_tracks = cli_events.get("tracks", [])
        wasm_tracks = wasm_events.get("tracks", [])

        if len(cli_tracks) != len(wasm_tracks):
            res.track_count_diffs.append(
                f"track count: CLI={len(cli_tracks)} WASM={len(wasm_tracks)}"
            )

        min_tracks = min(len(cli_tracks), len(wasm_tracks))
        for t in range(min_tracks):
            ct = cli_tracks[t]
            wt = wasm_tracks[t]
            track_name = ct.get("name", f"Track{t}")

            # Compare note counts
            cli_notes = ct.get("notes", [])
            wasm_notes = wt.get("notes", [])

            if len(cli_notes) != len(wasm_notes):
                res.track_count_diffs.append(
                    f"{track_name} note count: CLI={len(cli_notes)} WASM={len(wasm_notes)}"
                )

            # Compare individual notes
            diffs = compare_notes(track_name, cli_notes, wasm_notes)
            res.note_diffs.extend(diffs)

        # Compare sections
        cli_sections = cli_events.get("sections", [])
        wasm_sections = wasm_events.get("sections", [])
        if len(cli_sections) != len(wasm_sections):
            res.meta_diffs.append(
                f"section count: CLI={len(cli_sections)} WASM={len(wasm_sections)}"
            )
        else:
            for i, (cs, ws) in enumerate(zip(cli_sections, wasm_sections)):
                for key in ("name", "type", "startTick", "bars"):
                    if cs.get(key) != ws.get(key):
                        res.meta_diffs.append(
                            f"section[{i}].{key}: CLI={cs.get(key)} WASM={ws.get(key)}"
                        )

        # MIDI binary difference alone is expected (SMF1 vs SMF2 format, timestamps).
        # Only flag as failure if events (notes/sections/metadata) differ.
        has_event_diff = (
            res.meta_diffs
            or res.track_count_diffs
            or res.note_diffs
        )
        if has_event_diff:
            res.ok = False
            res.events_match = False
        else:
            res.events_match = True

    return res


def print_result(res: CompareResult, verbose: bool = False):
    """Print comparison result."""
    status = "OK" if res.ok else "DIFF"
    print(f"[{status}] {res.config.label()}")

    if res.errors:
        for e in res.errors:
            print(f"  ERROR: {e}")
        return

    if res.midi_match is False:
        print("  MIDI binary: DIFFERENT")
    elif res.midi_match is True and verbose:
        print("  MIDI binary: identical")

    if res.meta_diffs:
        print("  Metadata diffs:")
        for d in res.meta_diffs:
            print(f"    - {d}")

    if res.track_count_diffs:
        print("  Track/note count diffs:")
        for d in res.track_count_diffs:
            print(f"    - {d}")

    if res.note_diffs:
        # Group by track
        by_track: dict[str, list[DiffEntry]] = {}
        for d in res.note_diffs:
            by_track.setdefault(d.track, []).append(d)

        for track, diffs in by_track.items():
            shown = diffs[:10]
            print(f"  {track}: {len(diffs)} note diffs (showing first {len(shown)}):")
            for d in shown:
                print(f"    note[{d.note_index}].{d.field}: CLI={d.cli_value} WASM={d.wasm_value}")
            if len(diffs) > 10:
                print(f"    ... and {len(diffs) - 10} more")


def sweep_configs() -> list[Config]:
    """Generate a set of configs to test."""
    configs = []
    seeds = [1, 42, 100, 999]
    styles = [0, 3, 5, 8, 12]
    blueprints = [0, 1, 2, 3]

    # Basic sweep: fixed seed across styles
    for style in styles:
        configs.append(Config(seed=1, style=style))

    # Seed sweep on default style
    for seed in seeds:
        configs.append(Config(seed=seed))

    # Blueprint sweep
    for bp in blueprints:
        configs.append(Config(seed=1, blueprint=bp))

    # Key sweep
    for key in [0, 3, 7]:
        configs.append(Config(seed=1, key=key))

    # Cross: style x seed (sample)
    for style in [0, 5]:
        for seed in [42, 999]:
            configs.append(Config(seed=seed, style=style))

    # Deduplicate
    seen = set()
    unique = []
    for c in configs:
        k = (c.seed, c.style, c.chord, c.form, c.bpm, c.blueprint, c.key)
        if k not in seen:
            seen.add(k)
            unique.append(c)

    return unique


def main():
    parser = argparse.ArgumentParser(description="Verify WASM and CLI output match")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--style", type=int, default=0)
    parser.add_argument("--chord", type=int, default=0)
    parser.add_argument("--form", type=int, default=0)
    parser.add_argument("--bpm", type=int, default=120)
    parser.add_argument("--blueprint", type=int, default=0)
    parser.add_argument("--key", type=int, default=0)
    parser.add_argument("--vocal-attitude", type=int, default=None)
    parser.add_argument("--vocal-low", type=int, default=None)
    parser.add_argument("--vocal-high", type=int, default=None)
    parser.add_argument("--sweep", action="store_true", help="Run multiple configs")
    parser.add_argument("--jobs", "-j", type=int, default=1, help="Parallel jobs for sweep")
    parser.add_argument("--verbose", "-v", action="store_true")
    args = parser.parse_args()

    # Verify prerequisites
    if not CLI_BIN.exists():
        print(f"ERROR: CLI binary not found at {CLI_BIN}", file=sys.stderr)
        print("Run: make build", file=sys.stderr)
        sys.exit(1)

    if not WASM_HELPER.exists():
        print(f"ERROR: WASM helper not found at {WASM_HELPER}", file=sys.stderr)
        sys.exit(1)

    if not (PROJECT_ROOT / "dist" / "index.mjs").exists():
        print("ERROR: WASM dist not found. Run: npm run build", file=sys.stderr)
        sys.exit(1)

    if args.sweep:
        configs = sweep_configs()
        print(f"Running {len(configs)} configurations (jobs={args.jobs})...\n")

        total_ok = 0
        total_fail = 0
        results = []

        if args.jobs > 1:
            with ThreadPoolExecutor(max_workers=args.jobs) as executor:
                futures = {executor.submit(compare, c): c for c in configs}
                for future in as_completed(futures):
                    res = future.result()
                    results.append(res)
                    print_result(res, args.verbose)
                    if res.ok:
                        total_ok += 1
                    else:
                        total_fail += 1
        else:
            for cfg in configs:
                res = compare(cfg)
                results.append(res)
                print_result(res, args.verbose)
                if res.ok:
                    total_ok += 1
                else:
                    total_fail += 1

        print(f"\n{'='*60}")
        print(f"Total: {len(configs)}  OK: {total_ok}  DIFF: {total_fail}")

        if total_fail > 0:
            # Summary of divergent fields
            field_counts: dict[str, int] = {}
            for r in results:
                if not r.ok:
                    for d in r.note_diffs:
                        key = f"{d.track}.{d.field}"
                        field_counts[key] = field_counts.get(key, 0) + 1
                    for m in r.meta_diffs:
                        field_counts[f"meta:{m.split(':')[0]}"] = (
                            field_counts.get(f"meta:{m.split(':')[0]}", 0) + 1
                        )

            if field_counts:
                print("\nMost common divergences:")
                for k, v in sorted(field_counts.items(), key=lambda x: -x[1])[:15]:
                    print(f"  {k}: {v} occurrences")

        sys.exit(1 if total_fail > 0 else 0)

    else:
        cfg = Config(
            seed=args.seed,
            style=args.style,
            chord=args.chord,
            form=args.form,
            bpm=args.bpm,
            blueprint=args.blueprint,
            key=args.key,
            vocal_attitude=args.vocal_attitude,
            vocal_low=args.vocal_low,
            vocal_high=args.vocal_high,
        )
        res = compare(cfg)
        print_result(res, verbose=True)
        sys.exit(0 if res.ok else 1)


if __name__ == "__main__":
    main()
