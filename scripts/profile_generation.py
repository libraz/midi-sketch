#!/usr/bin/env python3
"""Profile midi-sketch generation to identify bottlenecks.

Usage:
  python3 scripts/profile_generation.py                    # Quick profile (50 seeds × 9 blueprints)
  python3 scripts/profile_generation.py --seeds 100        # More seeds
  python3 scripts/profile_generation.py --sample           # Attach macOS sample profiler
  python3 scripts/profile_generation.py --sample --duration 10  # Sample for 10 seconds
"""

import argparse
import json
import os
import re
import signal
import subprocess
import sys
import tempfile
import time
from collections import defaultdict
from pathlib import Path
from statistics import mean, median, stdev

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent
CLI_PATH = PROJECT_ROOT / "build" / "bin" / "midisketch_cli"


def time_single_generation(seed: int, blueprint: int, style: int = 0) -> dict:
    """Run a single generation and measure wall-clock time."""
    with tempfile.TemporaryDirectory() as tmpdir:
        json_path = os.path.join(tmpdir, "output.json")
        start = time.perf_counter()
        result = subprocess.run(
            [
                str(CLI_PATH),
                "--seed", str(seed),
                "--blueprint", str(blueprint),
                "--style", str(style),
                "--json",
            ],
            capture_output=True,
            text=True,
            timeout=60,
            cwd=tmpdir,
        )
        elapsed_ms = (time.perf_counter() - start) * 1000

        info = {
            "seed": seed,
            "blueprint": blueprint,
            "style": style,
            "elapsed_ms": elapsed_ms,
            "success": result.returncode == 0,
        }

        # Parse output.json for note counts if available
        if result.returncode == 0 and result.stdout:
            try:
                data = json.loads(result.stdout)
                total_notes = sum(len(t.get("notes", [])) for t in data.get("tracks", []))
                info["total_notes"] = total_notes
                info["duration_ticks"] = data.get("duration_ticks", 0)
                info["bpm"] = data.get("bpm", 0)
                info["num_tracks"] = len(data.get("tracks", []))
            except (json.JSONDecodeError, KeyError):
                pass

        return info


def run_batch_profile(seeds: int, blueprints: list[int], styles: list[int]) -> list[dict]:
    """Run batch generation and collect timing data."""
    results = []
    total = seeds * len(blueprints) * len(styles)
    count = 0

    print(f"Profiling {total} generations ({seeds} seeds × {len(blueprints)} blueprints × {len(styles)} styles)...")
    print()

    for style in styles:
        for bp in blueprints:
            for seed in range(1, seeds + 1):
                count += 1
                info = time_single_generation(seed, bp, style)
                results.append(info)
                if count % 50 == 0 or count == total:
                    print(f"  [{count}/{total}] latest: {info['elapsed_ms']:.1f}ms (bp={bp}, seed={seed})")

    return results


def print_timing_report(results: list[dict]):
    """Print detailed timing analysis."""
    times = [r["elapsed_ms"] for r in results if r["success"]]
    if not times:
        print("No successful generations to analyze.")
        return

    print()
    print("=" * 70)
    print("GENERATION TIMING REPORT")
    print("=" * 70)

    # Overall stats
    print(f"\n  Total generations:  {len(times)}")
    print(f"  Mean:               {mean(times):.2f} ms")
    print(f"  Median:             {median(times):.2f} ms")
    print(f"  Std Dev:            {stdev(times):.2f} ms" if len(times) > 1 else "")
    print(f"  Min:                {min(times):.2f} ms")
    print(f"  Max:                {max(times):.2f} ms")
    print(f"  P95:                {sorted(times)[int(len(times) * 0.95)]:.2f} ms")
    print(f"  P99:                {sorted(times)[int(len(times) * 0.99)]:.2f} ms")

    # By blueprint
    bp_times = defaultdict(list)
    for r in results:
        if r["success"]:
            bp_times[r["blueprint"]].append(r["elapsed_ms"])

    BP_NAMES = {
        0: "Traditional", 1: "RhythmLock", 2: "StoryPop", 3: "Ballad",
        4: "IdolStandard", 5: "IdolHyper", 6: "IdolKawaii",
        7: "IdolCoolPop", 8: "IdolEmo",
    }

    print(f"\n{'Blueprint':<20} {'Mean':>8} {'Med':>8} {'Max':>8} {'P95':>8} {'Count':>6}")
    print("-" * 60)
    for bp in sorted(bp_times.keys()):
        t = bp_times[bp]
        name = BP_NAMES.get(bp, f"BP{bp}")
        p95 = sorted(t)[int(len(t) * 0.95)] if len(t) >= 20 else max(t)
        print(f"  {name:<18} {mean(t):>7.1f} {median(t):>7.1f} {max(t):>7.1f} {p95:>7.1f} {len(t):>6}")

    # By style
    style_times = defaultdict(list)
    for r in results:
        if r["success"]:
            style_times[r["style"]].append(r["elapsed_ms"])

    if len(style_times) > 1:
        print(f"\n{'Style':>8} {'Mean':>8} {'Med':>8} {'Max':>8} {'Count':>6}")
        print("-" * 42)
        for s in sorted(style_times.keys()):
            t = style_times[s]
            print(f"  {s:>6} {mean(t):>7.1f} {median(t):>7.1f} {max(t):>7.1f} {len(t):>6}")

    # Correlation: notes vs time
    with_notes = [(r["elapsed_ms"], r.get("total_notes", 0)) for r in results if r["success"] and "total_notes" in r]
    if with_notes:
        times_arr, notes_arr = zip(*with_notes)
        print(f"\n  Note count range:   {min(notes_arr)} - {max(notes_arr)}")
        print(f"  Avg notes:          {mean(notes_arr):.0f}")

        # Simple correlation check
        if len(with_notes) > 10:
            n = len(with_notes)
            mean_t = mean(times_arr)
            mean_n = mean(notes_arr)
            cov = sum((t - mean_t) * (nn - mean_n) for t, nn in with_notes) / n
            std_t = stdev(times_arr)
            std_n = stdev(notes_arr)
            if std_t > 0 and std_n > 0:
                corr = cov / (std_t * std_n)
                print(f"  Time-Notes corr:    {corr:.3f}")

    # Slowest generations
    sorted_results = sorted(results, key=lambda r: r["elapsed_ms"], reverse=True)
    print(f"\n  Top 10 slowest generations:")
    for r in sorted_results[:10]:
        notes = r.get("total_notes", "?")
        print(f"    {r['elapsed_ms']:>7.1f}ms  bp={r['blueprint']} seed={r['seed']} style={r['style']} notes={notes}")

    print()


def run_with_sample_profiler(seeds: int, blueprints: list[int], duration: int):
    """Run generation in a subprocess and attach macOS sample profiler."""
    print(f"Running sample profiler for {duration}s during batch generation...")

    # Create a shell script that loops generating MIDI
    loop_script = f"""#!/bin/bash
while true; do
  for bp in {' '.join(str(b) for b in blueprints)}; do
    for seed in $(seq 1 {seeds}); do
      "{CLI_PATH}" --seed $seed --blueprint $bp --json > /dev/null 2>&1
    done
  done
done
"""
    with tempfile.NamedTemporaryFile(mode="w", suffix=".sh", delete=False) as f:
        f.write(loop_script)
        loop_path = f.name
    os.chmod(loop_path, 0o755)

    try:
        # Start the generation loop
        proc = subprocess.Popen(
            ["/bin/bash", loop_path],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

        # Wait a moment for CLI to start
        time.sleep(0.5)

        # Find the actual midisketch_cli processes
        sample_output = tempfile.mktemp(suffix=".txt")

        print(f"  Loop PID: {proc.pid}")
        print(f"  Sampling midisketch_cli for {duration}s (interval: 1ms)...")
        print(f"  Output: {sample_output}")

        # Sample the CLI process by name
        sample_proc = subprocess.run(
            ["sample", "midisketch_cli", str(duration), "1", "-file", sample_output, "-wait", "-mayDie"],
            capture_output=True,
            text=True,
            timeout=duration + 30,
        )

        print(f"  Sample profiler finished.")
        return sample_output

    finally:
        # Kill the generation loop
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
        os.unlink(loop_path)


def parse_sample_output(filepath: str):
    """Parse macOS sample output and extract hot functions."""
    if not os.path.exists(filepath):
        print(f"Sample output not found: {filepath}")
        return

    with open(filepath) as f:
        content = f.read()

    print()
    print("=" * 70)
    print("SAMPLE PROFILER RESULTS")
    print("=" * 70)

    # Extract the heaviest stack traces
    # Look for lines with sample counts
    lines = content.split("\n")
    func_counts = defaultdict(int)
    in_call_graph = False

    for line in lines:
        # Sample output format: "  N funcname (in binary) + offset [path]"
        match = re.search(r"^\s*(\d+)\s+(.+?)\s+\(in", line)
        if match:
            count = int(match.group(1))
            func = match.group(2).strip()
            # Skip system/stdlib functions
            if not any(skip in func for skip in ["__pthread", "dyld", "_start", "__libc", "mach_msg"]):
                func_counts[func] += count

    if func_counts:
        total = sum(func_counts.values())
        sorted_funcs = sorted(func_counts.items(), key=lambda x: x[1], reverse=True)

        print(f"\n  Total samples: {total}")
        print(f"\n  {'Function':<60} {'Samples':>8} {'%':>6}")
        print("  " + "-" * 76)
        for func, count in sorted_funcs[:30]:
            pct = count / total * 100
            print(f"  {func:<60} {count:>8} {pct:>5.1f}%")
    else:
        print("\n  Could not parse sample output. Raw output saved at:", filepath)
        # Print the raw heaviest frames section
        heavy_start = content.find("Heaviest stack")
        if heavy_start >= 0:
            print(content[heavy_start:heavy_start + 2000])
        else:
            # Print last portion
            print(content[-3000:])

    print(f"\n  Full sample output: {filepath}")


def main():
    parser = argparse.ArgumentParser(description="Profile midi-sketch generation")
    parser.add_argument("--seeds", type=int, default=50, help="Number of seeds per blueprint (default: 50)")
    parser.add_argument("--blueprints", type=str, default="0-8", help="Blueprint range (default: 0-8)")
    parser.add_argument("--styles", type=str, default="0", help="Style IDs (comma-separated, default: 0)")
    parser.add_argument("--sample", action="store_true", help="Attach macOS sample profiler")
    parser.add_argument("--duration", type=int, default=5, help="Sample profiler duration in seconds (default: 5)")
    parser.add_argument("--json-out", type=str, help="Save raw timing data as JSON")

    args = parser.parse_args()

    if not CLI_PATH.exists():
        print(f"CLI not found at {CLI_PATH}. Run 'make build' first.")
        sys.exit(1)

    # Parse blueprint range
    bp_match = re.match(r"(\d+)-(\d+)", args.blueprints)
    if bp_match:
        blueprints = list(range(int(bp_match.group(1)), int(bp_match.group(2)) + 1))
    else:
        blueprints = [int(x) for x in args.blueprints.split(",")]

    styles = [int(x) for x in args.styles.split(",")]

    # Run sample profiler if requested
    sample_file = None
    if args.sample:
        sample_file = run_with_sample_profiler(args.seeds, blueprints, args.duration)

    # Run timing profile
    results = run_batch_profile(args.seeds, blueprints, styles)
    print_timing_report(results)

    # Parse sample output if available
    if sample_file:
        parse_sample_output(sample_file)

    # Save raw data
    if args.json_out:
        with open(args.json_out, "w") as f:
            json.dump(results, f, indent=2)
        print(f"Raw timing data saved to {args.json_out}")


if __name__ == "__main__":
    main()
