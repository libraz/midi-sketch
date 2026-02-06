"""Output formatting for music analysis results.

Provides filtering and multiple output format options: quick summary,
track-specific detail, full report, JSON, and score-only one-liner.
"""

import json
from collections import defaultdict

from .constants import Severity, Category
from .models import AnalysisResult
from .helpers import note_name, tick_to_bar, tick_to_bar_beat


# =============================================================================
# FILTERING
# =============================================================================

def apply_filters(result: AnalysisResult, filters: dict) -> AnalysisResult:
    """Apply filters to analysis result.

    Supports filtering by track, bar range, category (including arrangement),
    and minimum severity level.

    Args:
        result: The analysis result to filter.
        filters: Dictionary with optional keys:
            - 'track': Track name substring filter.
            - 'bar_start' / 'bar_end': Bar range filter (inclusive).
            - 'category': Category value string
              (melodic, harmonic, rhythm, arrangement, structure).
            - 'severity': Minimum severity level (error, warning, info).

    Returns:
        The same AnalysisResult with issues list filtered in place.
    """
    filtered_issues = []

    for issue in result.issues:
        include = True

        # Track filter
        if filters.get('track'):
            if filters['track'].lower() not in issue.track.lower():
                include = False

        # Bar range filter
        if filters.get('bar_start') and filters.get('bar_end'):
            bar = tick_to_bar(issue.tick)
            if bar < filters['bar_start'] or bar > filters['bar_end']:
                include = False

        # Category filter (supports arrangement)
        if filters.get('category'):
            if issue.category.value != filters['category'].lower():
                include = False

        # Severity filter
        if filters.get('severity'):
            sev_map = {
                'error': Severity.ERROR,
                'warning': Severity.WARNING,
                'info': Severity.INFO,
            }
            min_sev = sev_map.get(filters['severity'].lower())
            if min_sev:
                sev_order = [Severity.ERROR, Severity.WARNING, Severity.INFO]
                if sev_order.index(issue.severity) > sev_order.index(min_sev):
                    include = False

        if include:
            filtered_issues.append(issue)

    result.issues = filtered_issues
    return result


# =============================================================================
# OUTPUT FORMATTERS
# =============================================================================

class OutputFormatter:
    """Format analysis results for output.

    Provides multiple output modes: quick summary, track-specific detail,
    full report, JSON, and score-only one-liner.
    """

    @staticmethod
    def format_quick(result: AnalysisResult, filepath: str) -> str:
        """Format quick summary output with arrangement score."""
        lines = []
        s = result.score
        lines.append(f"=== MUSIC ANALYSIS: {filepath} ===")

        # Build score line with bonus indicators
        bonus_parts = []
        if s.melodic_bonus > 0:
            bonus_parts.append(f"M:{s.melodic:.0f}(+{s.melodic_bonus:.0f})")
        else:
            bonus_parts.append(f"M:{s.melodic:.0f}")
        if s.harmonic_bonus > 0:
            bonus_parts.append(f"H:{s.harmonic:.0f}(+{s.harmonic_bonus:.0f})")
        else:
            bonus_parts.append(f"H:{s.harmonic:.0f}")
        if s.rhythm_bonus > 0:
            bonus_parts.append(f"R:{s.rhythm:.0f}(+{s.rhythm_bonus:.0f})")
        else:
            bonus_parts.append(f"R:{s.rhythm:.0f}")
        bonus_parts.append(f"A:{s.arrangement:.0f}")
        if s.structure_bonus > 0:
            bonus_parts.append(f"S:{s.structure:.0f}(+{s.structure_bonus:.0f})")
        else:
            bonus_parts.append(f"S:{s.structure:.0f}")

        bonus_tag = f" +B:{s.total_bonus:.1f}" if s.total_bonus > 0 else ""
        lines.append(
            f"Score: {s.overall:.1f} ({s.grade}){bonus_tag} | "
            + " ".join(bonus_parts)
        )
        lines.append("")

        errors = [idx for idx in result.issues if idx.severity == Severity.ERROR]
        warnings = [idx for idx in result.issues if idx.severity == Severity.WARNING]

        if errors:
            lines.append(f"ERRORS ({len(errors)}):")
            for issue in errors[:10]:
                lines.append(
                    f"  [!!] {issue.track}: {issue.message} "
                    f"at {tick_to_bar_beat(issue.tick)}"
                )
            if len(errors) > 10:
                lines.append(f"  ... ({len(errors) - 10} more)")
            lines.append("")

        if warnings:
            lines.append(f"WARNINGS ({len(warnings)}):")
            for issue in warnings[:10]:
                lines.append(
                    f"  [!] {issue.track}: {issue.message} "
                    f"at {tick_to_bar_beat(issue.tick)}"
                )
            if len(warnings) > 10:
                lines.append(f"  ... ({len(warnings) - 10} more)")

        return "\n".join(lines)

    @staticmethod
    def format_track(result: AnalysisResult, track_name: str) -> str:
        """Format track-specific analysis."""
        lines = []
        lines.append(f"=== {track_name.upper()} TRACK ANALYSIS ===")

        # Find notes for this track
        track_notes = [
            note for note in result.notes
            if note.track_name.lower() == track_name.lower()
        ]
        if not track_notes:
            lines.append(f"No notes found for track: {track_name}")
            return "\n".join(lines)

        pitches = [note.pitch for note in track_notes]
        velocities = [note.velocity for note in track_notes]

        lines.append(
            f"Notes: {len(track_notes)} | "
            f"Range: {note_name(min(pitches))}-{note_name(max(pitches))} | "
            f"Avg velocity: {sum(velocities) / len(velocities):.0f}"
        )
        lines.append("")

        # Filter issues for this track
        track_issues = [
            idx for idx in result.issues
            if track_name.lower() in idx.track.lower()
        ]
        if track_issues:
            lines.append(f"Issues ({len(track_issues)}):")
            for issue in track_issues[:20]:
                if issue.severity == Severity.ERROR:
                    marker = "!!"
                elif issue.severity == Severity.WARNING:
                    marker = "!"
                else:
                    marker = "-"
                lines.append(
                    f"  [{marker}] bar {tick_to_bar(issue.tick)}: {issue.message}"
                )
            if len(track_issues) > 20:
                lines.append(f"  ... ({len(track_issues) - 20} more)")
        else:
            lines.append("No issues found.")

        # Hook analysis for vocal
        if track_name.lower() == "vocal" and result.hooks:
            lines.append("")
            lines.append("Hook Analysis:")
            for idx, hook in enumerate(result.hooks, 1):
                occ_str = ", ".join(str(bar) for bar in hook.occurrences)
                lines.append(
                    f"  Hook {idx}: bars {hook.start_bar}-{hook.end_bar} "
                    f"repeats at bars [{occ_str}]"
                )

        return "\n".join(lines)

    @staticmethod
    def format_full(result: AnalysisResult, filepath: str) -> str:
        """Format full analysis report with arrangement score."""
        lines = []
        lines.append("=" * 80)
        lines.append("COMPREHENSIVE MUSIC ANALYSIS REPORT")
        lines.append(f"File: {filepath}")
        lines.append("=" * 80)

        # Score summary
        s = result.score
        lines.append("")
        lines.append(f"  OVERALL SCORE: {s.overall:5.1f} ({s.grade})")

        def score_bar(val: float) -> str:
            filled = int(val / 5)
            return "[" + "#" * filled + "-" * (20 - filled) + "]"

        lines.append(f"  {score_bar(s.overall)}")
        lines.append("")
        lines.append("  Category Scores:")

        def bonus_tag(val: float) -> str:
            return f" (+{val:.1f})" if val > 0 else ""

        lines.append(
            f"    Melodic:     {s.melodic:5.1f} {score_bar(s.melodic)}"
            f"{bonus_tag(s.melodic_bonus)}"
        )
        lines.append(
            f"    Harmonic:    {s.harmonic:5.1f} {score_bar(s.harmonic)}"
            f"{bonus_tag(s.harmonic_bonus)}"
        )
        lines.append(
            f"    Rhythm:      {s.rhythm:5.1f} {score_bar(s.rhythm)}"
            f"{bonus_tag(s.rhythm_bonus)}"
        )
        lines.append(
            f"    Arrangement: {s.arrangement:5.1f} {score_bar(s.arrangement)}"
        )
        lines.append(
            f"    Structure:   {s.structure:5.1f} {score_bar(s.structure)}"
            f"{bonus_tag(s.structure_bonus)}"
        )

        # Issue summary
        error_count = sum(
            1 for idx in result.issues if idx.severity == Severity.ERROR
        )
        warning_count = sum(
            1 for idx in result.issues if idx.severity == Severity.WARNING
        )
        info_count = sum(
            1 for idx in result.issues if idx.severity == Severity.INFO
        )

        lines.append("")
        lines.append(
            f"Summary: {error_count} errors, "
            f"{warning_count} warnings, {info_count} info"
        )

        # Group by category (includes arrangement)
        by_category = defaultdict(lambda: defaultdict(list))
        for issue in result.issues:
            by_category[issue.category.value][issue.subcategory].append(issue)

        for category in ["melodic", "harmonic", "rhythm", "arrangement", "structure"]:
            subcategories = by_category.get(category, {})
            if not subcategories:
                continue

            lines.append("")
            lines.append("=" * 40)
            lines.append(f"  {category.upper()} ISSUES")
            lines.append("=" * 40)

            for subcategory, subissues in sorted(subcategories.items()):
                lines.append("")
                lines.append(
                    f"--- {subcategory.replace('_', ' ').title()} "
                    f"({len(subissues)}) ---"
                )
                for issue in subissues[:15]:
                    if issue.severity == Severity.ERROR:
                        marker = "!!"
                    elif issue.severity == Severity.WARNING:
                        marker = "!"
                    else:
                        marker = "-"
                    lines.append(
                        f"  [{marker}] {issue.track}: {issue.message} "
                        f"at {tick_to_bar_beat(issue.tick)}"
                    )
                if len(subissues) > 15:
                    lines.append(f"  ... and {len(subissues) - 15} more")

        # Quality bonuses
        if result.bonuses:
            lines.append("")
            lines.append("=" * 40)
            lines.append("  QUALITY BONUSES")
            lines.append("=" * 40)

            # Group by category
            bonus_by_cat = defaultdict(list)
            for bonus in result.bonuses:
                bonus_by_cat[bonus.category.value].append(bonus)

            for cat_name in ["melodic", "harmonic", "rhythm", "structure"]:
                cat_bonuses = bonus_by_cat.get(cat_name, [])
                if not cat_bonuses:
                    continue
                cat_total = sum(bonus.score for bonus in cat_bonuses)
                lines.append(f"")
                lines.append(
                    f"  {cat_name.title()} Bonuses (+{cat_total:.1f}):"
                )
                for bonus in cat_bonuses:
                    lines.append(
                        f"    +{bonus.score:.1f}/{bonus.max_score:.0f} "
                        f"{bonus.name}: {bonus.description}"
                    )

        # Hook analysis
        if result.hooks:
            lines.append("")
            lines.append("=" * 40)
            lines.append("  HOOK DETECTION")
            lines.append("=" * 40)
            for idx, hook in enumerate(result.hooks, 1):
                occ_str = ", ".join(str(bar) for bar in hook.occurrences)
                lines.append(f"  Hook {idx}: bars {hook.start_bar}-{hook.end_bar}")
                lines.append(f"    Occurrences: [{occ_str}]")
                lines.append(
                    f"    Pitches: "
                    f"{[note_name(pitch) for pitch in hook.pitches[:8]]}..."
                )

        lines.append("")
        lines.append("=" * 80)

        return "\n".join(lines)

    @staticmethod
    def format_json(result: AnalysisResult, filepath: str) -> str:
        """Format as JSON with arrangement score."""
        output = {
            'file': filepath,
            'total_notes': len(result.notes),
            'scores': {
                'overall': round(result.score.overall, 1),
                'grade': result.score.grade,
                'melodic': round(result.score.melodic, 1),
                'harmonic': round(result.score.harmonic, 1),
                'rhythm': round(result.score.rhythm, 1),
                'arrangement': round(result.score.arrangement, 1),
                'structure': round(result.score.structure, 1),
            },
            'issue_summary': {
                'errors': sum(
                    1 for idx in result.issues
                    if idx.severity == Severity.ERROR
                ),
                'warnings': sum(
                    1 for idx in result.issues
                    if idx.severity == Severity.WARNING
                ),
                'info': sum(
                    1 for idx in result.issues
                    if idx.severity == Severity.INFO
                ),
            },
            'issues': [
                {
                    'severity': idx.severity.value,
                    'category': idx.category.value,
                    'subcategory': idx.subcategory,
                    'track': idx.track,
                    'message': idx.message,
                    'bar': tick_to_bar(idx.tick),
                    'tick': idx.tick,
                }
                for idx in result.issues
            ],
            'bonuses': {
                'total': round(result.score.total_bonus, 2),
                'melodic': round(result.score.melodic_bonus, 2),
                'harmonic': round(result.score.harmonic_bonus, 2),
                'rhythm': round(result.score.rhythm_bonus, 2),
                'structure': round(result.score.structure_bonus, 2),
                'items': [
                    {
                        'category': bonus.category.value,
                        'name': bonus.name,
                        'score': round(bonus.score, 2),
                        'max_score': bonus.max_score,
                        'description': bonus.description,
                    }
                    for bonus in result.bonuses
                ],
            },
            'hooks': [
                {
                    'start_bar': hook.start_bar,
                    'end_bar': hook.end_bar,
                    'occurrences': hook.occurrences,
                }
                for hook in result.hooks
            ],
        }
        return json.dumps(output, indent=2)

    @staticmethod
    def format_score_only(result: AnalysisResult) -> str:
        """Format score only (one line) with arrangement score."""
        s = result.score
        bonus_tag = f" +B:{s.total_bonus:.1f}" if s.total_bonus > 0 else ""
        return (
            f"{s.overall:.1f} ({s.grade}){bonus_tag} "
            f"M:{s.melodic:.0f} H:{s.harmonic:.0f} R:{s.rhythm:.0f} "
            f"A:{s.arrangement:.0f} S:{s.structure:.0f}"
        )
