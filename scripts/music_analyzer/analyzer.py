"""MusicAnalyzer - Orchestrator for domain-specific music analyzers.

Delegates analysis to specialized analyzers (melodic, vocal, harmonic,
rhythm, arrangement, structure), then runs hook detection, energy curve
calculation, and blueprint-aware scoring.
"""

from collections import defaultdict
from typing import List, Tuple

from .constants import (
    TICKS_PER_BEAT, TICKS_PER_BAR, TRACK_NAMES,
    Severity, Category,
)
from .models import Note, Issue, QualityScore, HookPattern, AnalysisResult
from .blueprints import BLUEPRINT_PROFILES
from .helpers import tick_to_bar
from .analyzers import (
    MelodicAnalyzer, VocalAnalyzer, HarmonicAnalyzer,
    RhythmAnalyzer, ArrangementAnalyzer, StructureAnalyzer,
)


class MusicAnalyzer:
    """Comprehensive music analysis engine with blueprint-aware scoring.

    Orchestrates domain-specific analyzers and combines their results
    with hook detection, energy curve, and quality scoring.
    """

    def __init__(self, notes: List[Note], blueprint: int = None, metadata: dict = None):
        self.notes = sorted(notes, key=lambda n: (n.start, n.channel))
        self.notes_by_channel = defaultdict(list)
        for note in self.notes:
            self.notes_by_channel[note.channel].append(note)
        self.issues: List[Issue] = []
        self.profile = BLUEPRINT_PROFILES.get(blueprint) if blueprint is not None else None
        self.metadata = metadata or {}

    def analyze_all(self) -> AnalysisResult:
        """Run all analyses and return a complete result.

        Delegates to domain analyzers, then runs hook detection,
        energy curve, and scoring.
        """
        common_args = dict(
            notes=self.notes,
            notes_by_channel=self.notes_by_channel,
            profile=self.profile,
            metadata=self.metadata,
        )

        analyzers = [
            MelodicAnalyzer(**common_args),
            VocalAnalyzer(**common_args),
            HarmonicAnalyzer(**common_args),
            RhythmAnalyzer(**common_args),
            ArrangementAnalyzer(**common_args),
            StructureAnalyzer(**common_args),
        ]

        for analyzer in analyzers:
            self.issues.extend(analyzer.analyze())

        # Extended analysis
        hooks = self._detect_hooks()
        energy_curve = self._calculate_energy_curve()

        # Calculate scores
        score = self._calculate_scores()

        return AnalysisResult(
            notes=self.notes,
            issues=self.issues,
            score=score,
            hooks=hooks,
            energy_curve=energy_curve,
            metadata=self.metadata,
        )

    # =========================================================================
    # EXTENDED ANALYSIS
    # =========================================================================

    def _detect_hooks(self) -> List[HookPattern]:
        """Detect repeating melodic patterns (hooks)."""
        vocal_notes = self.notes_by_channel.get(0, [])
        if len(vocal_notes) < 8:
            return []

        hooks = []

        pitches_by_bar = defaultdict(list)
        for note in vocal_notes:
            bar = tick_to_bar(note.start)
            pitches_by_bar[bar].append(note.pitch)

        bars = sorted(pitches_by_bar.keys())
        if len(bars) < 8:
            return hooks

        for start in range(len(bars) - 7):
            pattern_bars = bars[start:start + 4]
            pattern = tuple(tuple(pitches_by_bar[b]) for b in pattern_bars)

            if not pattern or all(len(p) == 0 for p in pattern):
                continue

            occurrences = [pattern_bars[0]]

            for check_start in range(start + 4, len(bars) - 3):
                check_bars = bars[check_start:check_start + 4]
                check_pattern = tuple(
                    tuple(pitches_by_bar[b]) for b in check_bars
                )

                if check_pattern == pattern:
                    occurrences.append(check_bars[0])

            if len(occurrences) >= 2:
                flat_pitches = [
                    p for bar_pitches in pattern for p in bar_pitches
                ]
                hooks.append(HookPattern(
                    start_bar=pattern_bars[0],
                    end_bar=pattern_bars[-1],
                    pitches=flat_pitches,
                    rhythm=[],
                    occurrences=occurrences,
                    similarity=1.0,
                ))
                break

        return hooks

    def _calculate_energy_curve(self) -> List[Tuple[int, float]]:
        """Calculate energy curve (velocity + density) per bar."""
        energy_curve = []

        max_bar = max((tick_to_bar(n.start) for n in self.notes), default=0)

        for bar in range(1, max_bar + 1):
            bar_start = (bar - 1) * TICKS_PER_BAR
            bar_end = bar * TICKS_PER_BAR

            bar_notes = [
                n for n in self.notes
                if bar_start <= n.start < bar_end and n.channel != 9
            ]
            if not bar_notes:
                energy_curve.append((bar, 0.0))
                continue

            avg_velocity = sum(n.velocity for n in bar_notes) / len(bar_notes)
            density = len(bar_notes)

            energy = ((avg_velocity / 127) * 0.6 +
                      min(density / 50, 1.0) * 0.4)
            energy_curve.append((bar, energy))

        return energy_curve

    # =========================================================================
    # SCORING
    # =========================================================================

    # Default penalty for new subcategories not explicitly listed
    _DEFAULT_PENALTY = {
        Severity.ERROR: 1.0, Severity.WARNING: 0.5, Severity.INFO: 0.1,
    }

    def _calculate_scores(self) -> QualityScore:
        """Calculate quality scores based on issues with blueprint-aware weighting."""
        score = QualityScore()

        penalties = {
            Category.MELODIC: {
                'consecutive_same_pitch': {
                    Severity.ERROR: 4.0, Severity.WARNING: 1.5, Severity.INFO: 0.3,
                },
                'isolated_note': {
                    Severity.ERROR: 2.0, Severity.WARNING: 1.0, Severity.INFO: 0.3,
                },
                'large_leap': {
                    Severity.ERROR: 2.5, Severity.WARNING: 1.0, Severity.INFO: 0.2,
                },
                'range_low': {
                    Severity.ERROR: 1.5, Severity.WARNING: 0.5, Severity.INFO: 0.1,
                },
                'range_high': {
                    Severity.ERROR: 1.5, Severity.WARNING: 0.5, Severity.INFO: 0.1,
                },
                'monotonous_contour': {
                    Severity.ERROR: 1.0, Severity.WARNING: 0.5, Severity.INFO: 0.2,
                },
                'breathability': {
                    Severity.ERROR: 1.0, Severity.WARNING: 0.5, Severity.INFO: 0.1,
                },
                'tessitura': {
                    Severity.ERROR: 0.5, Severity.WARNING: 0.3, Severity.INFO: 0.1,
                },
                'climax_position': {
                    Severity.ERROR: 0.5, Severity.WARNING: 0.3, Severity.INFO: 0.1,
                },
                'interval_distribution': {
                    Severity.ERROR: 0.5, Severity.WARNING: 0.3, Severity.INFO: 0.1,
                },
                'melodic_arc': {
                    Severity.ERROR: 1.0, Severity.WARNING: 0.5, Severity.INFO: 0.1,
                },
                'singability': {
                    Severity.ERROR: 1.0, Severity.WARNING: 0.5, Severity.INFO: 0.1,
                },
                'climax_placement': {
                    Severity.ERROR: 1.0, Severity.WARNING: 0.5, Severity.INFO: 0.1,
                },
                'repetition_balance': {
                    Severity.ERROR: 0.5, Severity.WARNING: 0.3, Severity.INFO: 0.1,
                },
            },
            Category.HARMONIC: {
                'dissonance': {
                    Severity.ERROR: 10.0, Severity.WARNING: 5.0, Severity.INFO: 1.0,
                },
                'thin_voicing': {
                    Severity.ERROR: 2.0, Severity.WARNING: 0.8, Severity.INFO: 0.2,
                },
                'dense_voicing': {
                    Severity.ERROR: 0.5, Severity.WARNING: 0.3, Severity.INFO: 0.1,
                },
                'chord_register_low': {
                    Severity.ERROR: 2.0, Severity.WARNING: 1.0, Severity.INFO: 0.2,
                },
                'chord_register_high': {
                    Severity.ERROR: 1.5, Severity.WARNING: 0.8, Severity.INFO: 0.2,
                },
                'chord_above_vocal': {
                    Severity.ERROR: 2.0, Severity.WARNING: 1.0, Severity.INFO: 0.3,
                },
                'chord_repetition': {
                    Severity.ERROR: 1.5, Severity.WARNING: 0.8, Severity.INFO: 0.2,
                },
                'bass_monotony': {
                    Severity.ERROR: 2.0, Severity.WARNING: 1.0, Severity.INFO: 0.3,
                },
                'chord_function': {
                    Severity.ERROR: 1.0, Severity.WARNING: 0.5, Severity.INFO: 0.2,
                },
                'harmonic_stagnation': {
                    Severity.ERROR: 0.5, Severity.WARNING: 0.3, Severity.INFO: 0.1,
                },
                'bass_chord_degrees': {
                    Severity.ERROR: 1.5, Severity.WARNING: 0.8, Severity.INFO: 0.2,
                },
                'bass_downbeat_root': {
                    Severity.ERROR: 2.0, Severity.WARNING: 1.0, Severity.INFO: 0.2,
                },
                'bass_contour': {
                    Severity.ERROR: 1.5, Severity.WARNING: 0.8, Severity.INFO: 0.2,
                },
                'bass_chord_spacing': {
                    Severity.ERROR: 2.0, Severity.WARNING: 1.0, Severity.INFO: 0.2,
                },
                'bass_kick_sync': {
                    Severity.ERROR: 1.0, Severity.WARNING: 0.5, Severity.INFO: 0.1,
                },
                'bass_empty_bars': {
                    Severity.ERROR: 1.0, Severity.WARNING: 0.5, Severity.INFO: 0.1,
                },
                'bass_stepwise_rate': {
                    Severity.ERROR: 0.5, Severity.WARNING: 0.3, Severity.INFO: 0.1,
                },
            },
            Category.RHYTHM: {
                'low_sync': {
                    Severity.ERROR: 1.5, Severity.WARNING: 0.8, Severity.INFO: 0.2,
                },
                'high_density': {
                    Severity.ERROR: 2.0, Severity.WARNING: 1.0, Severity.INFO: 0.3,
                },
                'rhythmic_monotony': {
                    Severity.ERROR: 2.0, Severity.WARNING: 1.0, Severity.INFO: 0.3,
                },
                'beat_misalignment': {
                    Severity.ERROR: 3.0, Severity.WARNING: 1.5, Severity.INFO: 0.3,
                },
                'weak_downbeat': {
                    Severity.ERROR: 2.0, Severity.WARNING: 1.0, Severity.INFO: 0.3,
                },
                'weak_emphasis': {
                    Severity.ERROR: 0.5, Severity.WARNING: 0.3, Severity.INFO: 0.1,
                },
                'backbeat': {
                    Severity.ERROR: 0.5, Severity.WARNING: 0.3, Severity.INFO: 0.1,
                },
                'rhythm_variety': {
                    Severity.ERROR: 0.5, Severity.WARNING: 0.3, Severity.INFO: 0.1,
                },
                'beat_content': {
                    Severity.ERROR: 0.5, Severity.WARNING: 0.3, Severity.INFO: 0.1,
                },
                'grid_strictness': {
                    Severity.ERROR: 2.0, Severity.WARNING: 1.0, Severity.INFO: 0.3,
                },
                'drive_deficit': {
                    Severity.ERROR: 2.0, Severity.WARNING: 1.0, Severity.INFO: 0.2,
                },
            },
            Category.ARRANGEMENT: {
                'register_overlap': {
                    Severity.ERROR: 3.0, Severity.WARNING: 1.5, Severity.INFO: 0.3,
                },
                'track_separation': {
                    Severity.ERROR: 2.0, Severity.WARNING: 1.0, Severity.INFO: 0.3,
                },
                'motif_degradation': {
                    Severity.ERROR: 2.0, Severity.WARNING: 1.0, Severity.INFO: 0.3,
                },
                'motif_vocal_clash': {
                    Severity.ERROR: 2.0, Severity.WARNING: 1.0, Severity.INFO: 0.3,
                },
                'motif_density_balance': {
                    Severity.ERROR: 0.5, Severity.WARNING: 0.3, Severity.INFO: 0.1,
                },
                'rhythm_sync': {
                    Severity.ERROR: 3.0, Severity.WARNING: 1.5, Severity.INFO: 0.3,
                },
                'submelody_vocal_crossing': {
                    Severity.ERROR: 1.5, Severity.WARNING: 0.8, Severity.INFO: 0.2,
                },
                'motif_contour_preservation': {
                    Severity.ERROR: 1.5, Severity.WARNING: 0.8, Severity.INFO: 0.2,
                },
                'motif_rhythm_preservation': {
                    Severity.ERROR: 1.5, Severity.WARNING: 0.8, Severity.INFO: 0.2,
                },
                'blueprint_paradigm': {
                    Severity.ERROR: 1.5, Severity.WARNING: 0.8, Severity.INFO: 0.2,
                },
            },
            Category.STRUCTURE: {
                'short_phrase': {
                    Severity.ERROR: 1.5, Severity.WARNING: 0.8, Severity.INFO: 0.2,
                },
                'long_phrase': {
                    Severity.ERROR: 0.5, Severity.WARNING: 0.3, Severity.INFO: 0.1,
                },
                'track_dominance': {
                    Severity.ERROR: 1.0, Severity.WARNING: 0.5, Severity.INFO: 0.1,
                },
                'empty_track': {
                    Severity.ERROR: 3.0, Severity.WARNING: 1.5, Severity.INFO: 0.3,
                },
                'energy_contrast': {
                    Severity.ERROR: 0.5, Severity.WARNING: 0.3, Severity.INFO: 0.1,
                },
                'section_density': {
                    Severity.ERROR: 0.5, Severity.WARNING: 0.3, Severity.INFO: 0.1,
                },
                'chorus_density_inversion': {
                    Severity.ERROR: 1.5, Severity.WARNING: 0.8, Severity.INFO: 0.2,
                },
                'section_contrast': {
                    Severity.ERROR: 0.5, Severity.WARNING: 0.3, Severity.INFO: 0.1,
                },
            },
        }

        category_penalties = defaultdict(float)
        subcategory_penalties = defaultdict(float)
        subcategory_counts = defaultdict(int)

        # Cap per subcategory: max penalty = single_penalty × 5
        # Prevents a single subcategory from destroying the entire category score
        _SUBCATEGORY_CAP_MULTIPLIER = 5

        for issue in self.issues:
            cat = issue.category
            subcat = issue.subcategory
            sev = issue.severity

            if cat in penalties and subcat in penalties[cat]:
                penalty = penalties[cat][subcat].get(sev, 0.5)
            elif cat in penalties:
                # New subcategory: use default penalty
                penalty = self._DEFAULT_PENALTY.get(sev, 0.5)
            else:
                continue

            subcat_key = f"{cat.value}.{subcat}"
            max_penalty = penalty * _SUBCATEGORY_CAP_MULTIPLIER
            if subcategory_penalties[subcat_key] < max_penalty:
                effective = min(penalty, max_penalty - subcategory_penalties[subcat_key])
                category_penalties[cat] += effective
                subcategory_penalties[subcat_key] += effective
            subcategory_counts[subcat_key] += 1

        total_notes = len(self.notes)
        note_factor = max(1, total_notes / 500)

        score.melodic = max(
            0, 100 - category_penalties[Category.MELODIC] / note_factor * 5
        )
        score.harmonic = max(
            0, 100 - category_penalties[Category.HARMONIC] / note_factor * 8
        )
        score.rhythm = max(
            0, 100 - category_penalties[Category.RHYTHM] / note_factor * 10
        )
        score.arrangement = max(
            0, 100 - category_penalties[Category.ARRANGEMENT] / note_factor * 8
        )
        score.structure = max(
            0, 100 - category_penalties[Category.STRUCTURE] / note_factor * 15
        )

        score.details = {
            'melodic_penalty': category_penalties[Category.MELODIC],
            'harmonic_penalty': category_penalties[Category.HARMONIC],
            'rhythm_penalty': category_penalties[Category.RHYTHM],
            'arrangement_penalty': category_penalties[Category.ARRANGEMENT],
            'structure_penalty': category_penalties[Category.STRUCTURE],
            'total_notes': total_notes,
            'note_factor': note_factor,
        }
        for key, count in subcategory_counts.items():
            score.details[f"count_{key}"] = count

        if self.profile:
            weights = (
                self.profile.weight_melodic,
                self.profile.weight_harmonic,
                self.profile.weight_rhythm,
                self.profile.weight_arrangement,
                self.profile.weight_structure,
            )
            score.calculate_overall(weights)
        else:
            score.calculate_overall()

        return score
