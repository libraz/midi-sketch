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

from .constants import Severity
from .models import Note
from .analyzer import MusicAnalyzer
from .formatter import apply_filters, OutputFormatter
from .loader import load_json_output, load_json_metadata
from .runner import run_batch_tests, print_batch_summary


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
  %(prog)s --batch --seeds 20 --bp all    # Batch test
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
        help="Filter by track (Vocal, Chord, Bass, Motif, Aux)",
    )
    filter_group.add_argument(
        "--bar-range", type=str,
        help="Filter by bar range (e.g., 15-20)",
    )
    filter_group.add_argument(
        "--category", type=str,
        choices=["melodic", "harmonic", "rhythm", "arrangement", "structure"],
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

    args = parser.parse_args()

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

        if args.quick:
            # Quick batch: 10 seeds x bp all
            styles = [0]
            chords = [0]
            blueprints = list(range(9))
        else:
            styles = (
                list(range(15)) if args.styles == "all"
                else [int(val) for val in args.styles.split(",")]
            )
            chords = (
                list(range(22)) if args.chords == "all"
                else [int(val) for val in args.chords.split(",")]
            )
            blueprints = (
                list(range(9)) if args.blueprints == "all"
                else [int(val) for val in args.blueprints.split(",")]
            )

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
    except FileNotFoundError:
        print(f"Error: File not found: {input_file}", file=sys.stderr)
        sys.exit(1)
    except json.JSONDecodeError as exc:
        print(f"Error: Invalid JSON: {exc}", file=sys.stderr)
        sys.exit(1)

    # Determine blueprint and metadata for single-file analysis
    if not args.batch and not args.generate:
        metadata = load_json_metadata(input_file)
        bp_value = metadata.get('blueprint', args.blueprint_single)
        analyzer = MusicAnalyzer(notes, blueprint=bp_value, metadata=metadata)
    else:
        analyzer = MusicAnalyzer(notes)

    result = analyzer.analyze_all()

    # Apply filters
    result = apply_filters(result, filters)

    # Format output
    if args.score_only:
        print(OutputFormatter.format_score_only(result))
    elif args.json:
        print(OutputFormatter.format_json(result, input_file))
    elif args.track:
        print(OutputFormatter.format_track(result, args.track))
    elif args.quick:
        print(OutputFormatter.format_quick(result, input_file))
    else:
        print(OutputFormatter.format_full(result, input_file))

    # Exit code based on errors
    error_count = sum(
        1 for idx in result.issues if idx.severity == Severity.ERROR
    )
    sys.exit(1 if error_count > 0 else 0)
