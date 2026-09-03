"""CLI entry point for the music analyzer.

Supports three modes:
- Single file analysis (default)
- Generate + analyze (--generate)
- Batch testing (--batch)
"""

import sys
import json
import argparse
import subprocess

from .constants import (
    Category,
    Severity,
    STYLE_PRESET_COUNT,
    PRODUCTION_BLUEPRINT_IDS,
    TRACK_NAMES,
)
from .melody_targets import TARGETS_ENV_VAR, set_targets_path
from .models import Note
from .analyzer import MusicAnalyzer
from .formatter import apply_filters, OutputFormatter
from .loader import load_json_output, load_json_metadata
from .runner import run_batch_tests, print_batch_summary


def _batch_values(args):
    """Resolve batch filters, keeping `all` in sync with public IDs."""
    if args.quick:
        return [0], [0], list(PRODUCTION_BLUEPRINT_IDS)
    styles = (
        list(range(STYLE_PRESET_COUNT)) if args.styles == "all"
        else [int(val) for val in args.styles.split(",")]
    )
    chords = (
        list(range(22)) if args.chords == "all"
        else [int(val) for val in args.chords.split(",")]
    )
    blueprints = (
        list(PRODUCTION_BLUEPRINT_IDS) if args.blueprints == "all"
        else [int(val) for val in args.blueprints.split(",")]
    )
    return styles, chords, blueprints


def main():
    """CLI entry point for the music analyzer.

    Supports three modes:
    - Single file analysis (default)
    - Generate + analyze (--generate)
    - Batch testing (--batch)
    """
    parser = argparse.ArgumentParser(
        description="Unified Music Analysis Tool for midi-sketch",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s output.json                    # Basic analysis
  %(prog)s output.json --quick            # Summary only
  %(prog)s output.json --track Vocal      # Track-specific
  %(prog)s output.json --bar-range 15-20  # Bar range filter
  %(prog)s output.json --category harmonic # Category filter

  %(prog)s --generate --seed 42 --bp 1    # Generate + analyze
  %(prog)s --batch --seeds 20              # Batch test (all blueprints)
  %(prog)s --batch --quick -j 4           # Quick batch, 4 workers
        """,
    )

    # Input file (positional, optional if --generate or --batch)
    parser.add_argument(
        "input_file", nargs="?",
        help="JSON output file to analyze",
    )

    # Generate mode
    gen_group = parser.add_argument_group("Generate Mode")
    gen_group.add_argument(
        "--generate", "-g", action="store_true",
        help="Generate MIDI then analyze",
    )
    gen_group.add_argument(
        "--seed", type=int, default=0,
        help="Random seed (default: 0 = random)",
    )
    gen_group.add_argument(
        "--style", type=int, default=0,
        help="Style preset (default: 0)",
    )
    gen_group.add_argument(
        "--chord", type=int, default=0,
        help="Chord progression (default: 0)",
    )
    gen_group.add_argument(
        "--bp", "--blueprint", type=int, default=0, dest="blueprint",
        help="Blueprint (default: 0)",
    )

    # Batch mode
    batch_group = parser.add_argument_group("Batch Mode")
    batch_group.add_argument(
        "--batch", "-b", action="store_true",
        help="Run batch tests",
    )
    batch_group.add_argument(
        "--seeds", type=int, default=10,
        help="Number of seeds for batch (default: 10)",
    )
    batch_group.add_argument(
        "--seed-start", type=int, default=1,
        help="Starting seed (default: 1)",
    )
    batch_group.add_argument(
        "--styles", type=str, default="0",
        help="Styles: 'all' or comma-separated (default: 0)",
    )
    batch_group.add_argument(
        "--chords", type=str, default="0",
        help="Chords: 'all' or comma-separated (default: 0)",
    )
    batch_group.add_argument(
        "--blueprints", type=str, default="all",
        help="Blueprints: 'all' or comma-separated (default: all)",
    )
    batch_group.add_argument(
        "-j", "--jobs", type=int, default=1,
        help="Parallel workers (default: 1)",
    )

    # Filter options
    filter_group = parser.add_argument_group("Filters")
    filter_group.add_argument(
        "--track", type=str,
        help=("Filter by track name, matched as a case-insensitive substring "
              "so cross-track issues such as Guitar/Bass match either name "
              f"({', '.join(TRACK_NAMES.values())})"),
    )
    filter_group.add_argument(
        "--bar-range", type=str,
        help="Filter by bar range (e.g., 15-20)",
    )
    filter_group.add_argument(
        "--category", type=str,
        choices=[category.value for category in Category],
        help="Filter by category",
    )
    filter_group.add_argument(
        "--severity", type=str,
        choices=["error", "warning", "info"],
        help="Minimum severity to show",
    )
    filter_group.add_argument(
        "--blueprint-single", type=int, default=None,
        help="Blueprint ID for single file analysis scoring",
    )

    # Output options
    output_group = parser.add_argument_group("Output Options")
    output_group.add_argument(
        "--quick", "-q", action="store_true",
        help="Quick summary only",
    )
    output_group.add_argument(
        "--json", action="store_true",
        help="JSON output",
    )
    output_group.add_argument(
        "--score-only", action="store_true",
        help="Score only (one line)",
    )
    output_group.add_argument(
        "-v", "--verbose", action="store_true",
        help="Verbose output",
    )

    # CLI path
    parser.add_argument(
        "--cli", default="./build/bin/midisketch_cli",
        help="Path to CLI (default: ./build/bin/midisketch_cli)",
    )
    parser.add_argument(
        "--melody-targets", default=None,
        help=("Path to the melody discipline target profile, overriding "
              f"{TARGETS_ENV_VAR} and the default location. When the profile "
              "cannot be read the melody discipline layers are reported as "
              "inactive instead of being evaluated."),
    )

    args = parser.parse_args()

    if args.melody_targets:
        set_targets_path(args.melody_targets)

    # Build filters
    filters = {
        'track': args.track,
        'category': args.category,
        'severity': args.severity,
    }
    if args.bar_range:
        parts = args.bar_range.split('-')
        if len(parts) == 2:
            filters['bar_start'] = int(parts[0])
            filters['bar_end'] = int(parts[1])

    # Batch mode
    if args.batch:
        seeds = list(range(args.seed_start, args.seed_start + args.seeds))
        styles, chords, blueprints = _batch_values(args)

        batch_results = run_batch_tests(
            args.cli, seeds, styles, chords, blueprints,
            args.jobs, args.verbose,
        )
        batch_passed = print_batch_summary(batch_results)
        sys.exit(0 if batch_passed else 1)

    # Generate mode
    if args.generate:
        cmd = [
            args.cli,
            "--analyze",
            "--json",
            "--seed", str(args.seed),
            "--style", str(args.style),
            "--chord", str(args.chord),
            "--blueprint", str(args.blueprint),
        ]
        gen_result = subprocess.run(cmd, capture_output=True, text=True)
        if gen_result.returncode != 0:
            print(f"Error: {gen_result.stderr}", file=sys.stderr)
            sys.exit(1)
        input_file = "output.json"
    else:
        input_file = args.input_file

    if not input_file:
        parser.print_help()
        sys.exit(1)

    # Load notes
    try:
        notes = load_json_output(input_file)
    except json.JSONDecodeError as exc:
        print(f"Error: Invalid JSON: {exc}", file=sys.stderr)
        sys.exit(1)
    except OSError as exc:
        print(f"Error: Cannot read {input_file}: {exc}", file=sys.stderr)
        sys.exit(1)
    except (TypeError, ValueError) as exc:
        print(f"Error: Invalid analyzer input {input_file}: {exc}", file=sys.stderr)
        sys.exit(1)

    # Determine blueprint and metadata for analysis
    metadata = load_json_metadata(input_file)
    if args.generate:
        bp_value = args.blueprint
    else:
        bp_value = metadata.get('blueprint')
        if bp_value is None:
            bp_value = args.blueprint_single
    analyzer = MusicAnalyzer(notes, blueprint=bp_value, metadata=metadata)

    result = analyzer.analyze_all()
    error_count = sum(
        1 for idx in result.issues if idx.severity == Severity.ERROR
    )

    # Filters affect only presentation, never the analysis status.
    display_result = apply_filters(result, filters)

    # Format output
    if args.score_only:
        print(OutputFormatter.format_score_only(display_result))
    elif args.json:
        print(OutputFormatter.format_json(display_result, input_file))
    elif args.track:
        print(OutputFormatter.format_track(display_result, args.track))
    elif args.quick:
        print(OutputFormatter.format_quick(display_result, input_file))
    else:
        print(OutputFormatter.format_full(display_result, input_file))

    # Exit code based on errors
    sys.exit(1 if error_count > 0 else 0)
