"""Bonus scoring analyzer for melodic quality.

Awards bonus points for positive melodic qualities: hook catchiness,
earworm potential, melodic sequences, and phrase balance.
"""

from collections import defaultdict
from typing import List, Tuple

from ..constants import TICKS_PER_BAR, TICKS_PER_BEAT, Category
from ..models import Bonus, HookPattern
from ..helpers import (
    tick_to_bar,
    contour_direction_changes,
    quantize_rhythm,
    _pattern_similarity,
)
from .base import BaseBonusAnalyzer


# Minimum number of vocal notes required for meaningful analysis.
_MIN_VOCAL_NOTES = 8

# Gap threshold (in ticks) for phrase boundary detection.
_PHRASE_GAP_TICKS = TICKS_PER_BEAT  # 1 beat = 480 ticks

# Pattern matching thresholds.
_APPROXIMATE_MATCH_SIMILARITY = 0.7
_HOOK_PATTERN_BARS = (2, 4)

# Earworm optimal ranges.
_EARWORM_EXACT_RATE_LOW = 0.15
_EARWORM_EXACT_RATE_HIGH = 0.40
_EARWORM_RANGE_LOW = 5
_EARWORM_RANGE_HIGH = 12

# Melodic sequence detection parameters.
_SEQUENCE_WINDOW_MIN = 4
_SEQUENCE_WINDOW_MAX = 8
_SEQUENCE_MAX_BONUS = 3


def _extract_intervals(pitches: List[int]) -> List[int]:
    """Compute consecutive pitch intervals from a pitch list.

    Args:
        pitches: List of MIDI pitch values.

    Returns:
        List of signed intervals (pitch[i+1] - pitch[i]).
    """
    return [pitches[idx + 1] - pitches[idx] for idx in range(len(pitches) - 1)]


def _group_notes_by_bar(notes) -> dict:
    """Group notes by their bar number.

    Args:
        notes: List of Note objects.

    Returns:
        Dict mapping bar number to list of notes in that bar.
    """
    bars = defaultdict(list)
    for note in notes:
        bars[note.bar].append(note)
    return bars


def _extract_bar_pitch_pattern(notes) -> List[int]:
    """Extract sorted pitch pattern from notes in a bar.

    Args:
        notes: List of Note objects in a single bar.

    Returns:
        Sorted list of pitches.
    """
    return sorted(note.pitch for note in notes)


def _intervals_match_transposed(intervals_a: List[int],
                                intervals_b: List[int]) -> bool:
    """Check if two interval sequences are identical (transposition-invariant).

    Two pitch patterns with the same interval sequence are the same melody
    at a different pitch level.

    Args:
        intervals_a: First interval sequence.
        intervals_b: Second interval sequence.

    Returns:
        True if the interval sequences are identical.
    """
    if len(intervals_a) != len(intervals_b):
        return False
    return intervals_a == intervals_b


class BonusMelodicAnalyzer(BaseBonusAnalyzer):
    """Awards bonus points for positive melodic qualities.

    Checks:
        1. Hook quality: pitch contour interest, rhythmic catchiness, repetition
        2. Earworm potential: balance of repetition and variation
        3. Melodic sequences: same interval pattern at different pitch levels
        4. Phrase balance: consecutive phrases have similar lengths
    """

    def analyze(self) -> List[Bonus]:
        """Run all melodic bonus checks.

        Returns:
            List of Bonus objects awarded.
        """
        vocal_notes = self.notes_by_channel.get(0, [])
        if len(vocal_notes) < _MIN_VOCAL_NOTES:
            return self.bonuses

        self._score_hook_quality(vocal_notes)
        self._score_earworm_potential(vocal_notes)
        self._score_melodic_sequences(vocal_notes)
        self._score_phrase_balance(vocal_notes)

        return self.bonuses

    # ------------------------------------------------------------------
    # Check 1: Hook Quality (max +6)
    # ------------------------------------------------------------------

    def _score_hook_quality(self, vocal_notes) -> None:
        """Score hook quality based on contour, rhythm, and repetition.

        Extracts 2-bar and 4-bar pitch patterns from the vocal track and
        evaluates contour interest, rhythmic catchiness, and repetition count
        using transposition-invariant (interval-based) matching.

        Args:
            vocal_notes: Sorted list of vocal Note objects.
        """
        max_score = 6.0
        best_contour = 0.0
        best_rhythm = 0.0
        best_repetition = 0.0

        # Extract patterns at 2-bar and 4-bar granularity.
        for bar_span in _HOOK_PATTERN_BARS:
            patterns = self._extract_bar_span_patterns(vocal_notes, bar_span)
            if not patterns:
                continue

            for pitches, iois in patterns:
                if len(pitches) < 3:
                    continue

                # Contour interest: direction changes normalized by note count.
                changes = contour_direction_changes(pitches)
                num_notes = len(pitches)
                # Ratio of direction changes per note transition.
                contour_ratio = changes / max(1, num_notes - 1)
                # Sweet spot: 0.3-0.7 changes per note transition.
                if 0.3 <= contour_ratio <= 0.7:
                    contour_score = 1.0
                elif contour_ratio < 0.3:
                    # Too flat/monotone.
                    contour_score = contour_ratio / 0.3
                else:
                    # Too jagged.
                    contour_score = max(0.0, 1.0 - (contour_ratio - 0.7) / 0.3)

                # Rhythmic catchiness: number of distinct IOI values.
                if iois:
                    quantized = quantize_rhythm(iois)
                    distinct_iois = len(set(quantized))
                    # 2-4 distinct values is ideal.
                    if 2 <= distinct_iois <= 4:
                        rhythm_score = 1.0
                    elif distinct_iois == 1:
                        rhythm_score = 0.3
                    elif distinct_iois == 5:
                        rhythm_score = 0.7
                    else:
                        rhythm_score = max(0.2, 1.0 - abs(distinct_iois - 3) * 0.15)
                else:
                    rhythm_score = 0.0

                # Repetition: count approximate matches across the track.
                intervals = _extract_intervals(pitches)
                match_count = self._count_interval_matches(
                    intervals, vocal_notes, bar_span
                )

                # Also factor in pre-detected hooks if available.
                hook_matches = self._count_hook_occurrences(pitches)
                total_matches = max(match_count, hook_matches)

                if total_matches >= 3:
                    repetition_score = 1.0
                elif total_matches == 2:
                    repetition_score = 0.7
                elif total_matches == 1:
                    repetition_score = 0.3
                else:
                    repetition_score = 0.0

                best_contour = max(best_contour, contour_score)
                best_rhythm = max(best_rhythm, rhythm_score)
                best_repetition = max(best_repetition, repetition_score)

        raw_score = (
            best_contour * 0.4
            + best_rhythm * 0.3
            + best_repetition * 0.3
        ) * max_score

        # Apply blueprint weight if available.
        weight = self._get_hook_weight()
        raw_score *= weight

        if raw_score > 0:
            self.add_bonus(
                Category.MELODIC,
                "hook_quality",
                raw_score,
                max_score,
                description=(
                    f"contour={best_contour:.2f} "
                    f"rhythm={best_rhythm:.2f} "
                    f"repetition={best_repetition:.2f}"
                ),
            )

    def _extract_bar_span_patterns(
        self, vocal_notes, bar_span: int
    ) -> List[Tuple[List[int], List[int]]]:
        """Extract pitch and IOI patterns at a given bar-span granularity.

        Args:
            vocal_notes: Sorted list of vocal Note objects.
            bar_span: Number of bars per pattern (e.g., 2 or 4).

        Returns:
            List of (pitches, iois) tuples for each bar-span window.
        """
        if not vocal_notes:
            return []

        max_bar = max(note.bar for note in vocal_notes)
        patterns = []

        for start_bar in range(1, max_bar + 1, bar_span):
            end_bar = start_bar + bar_span
            start_tick = (start_bar - 1) * TICKS_PER_BAR
            end_tick = (end_bar - 1) * TICKS_PER_BAR

            span_notes = [
                note for note in vocal_notes
                if start_tick <= note.start < end_tick
            ]
            if len(span_notes) < 2:
                continue

            span_notes.sort(key=lambda note: note.start)
            pitches = [note.pitch for note in span_notes]
            iois = [
                span_notes[idx + 1].start - span_notes[idx].start
                for idx in range(len(span_notes) - 1)
            ]
            patterns.append((pitches, iois))

        return patterns

    def _count_interval_matches(
        self,
        target_intervals: List[int],
        vocal_notes,
        bar_span: int,
    ) -> int:
        """Count how many bar-span windows have matching interval patterns.

        Uses transposition-invariant matching (compares interval sequences).

        Args:
            target_intervals: Interval sequence to search for.
            vocal_notes: All vocal notes.
            bar_span: Bar span used for pattern extraction.

        Returns:
            Number of matching windows (including the original).
        """
        if not target_intervals:
            return 0

        patterns = self._extract_bar_span_patterns(vocal_notes, bar_span)
        match_count = 0

        for pitches, _ in patterns:
            if len(pitches) < 2:
                continue
            candidate_intervals = _extract_intervals(pitches)
            if _intervals_match_transposed(target_intervals, candidate_intervals):
                match_count += 1
            elif _pattern_similarity(
                target_intervals, candidate_intervals
            ) >= _APPROXIMATE_MATCH_SIMILARITY:
                match_count += 1

        return match_count

    def _count_hook_occurrences(self, pitches: List[int]) -> int:
        """Count hook occurrences that match the given pitch pattern.

        Uses pre-detected hook patterns from self.hooks for additional
        repetition data.

        Args:
            pitches: Pitch pattern to match against hooks.

        Returns:
            Maximum occurrence count among matching hooks.
        """
        if not self.hooks or not pitches:
            return 0

        target_intervals = _extract_intervals(pitches)
        best_count = 0

        for hook in self.hooks:
            if not hook.pitches or len(hook.pitches) < 2:
                continue
            hook_intervals = _extract_intervals(hook.pitches)
            if _intervals_match_transposed(target_intervals, hook_intervals):
                best_count = max(best_count, len(hook.occurrences))
            elif _pattern_similarity(
                target_intervals, hook_intervals
            ) >= _APPROXIMATE_MATCH_SIMILARITY:
                best_count = max(best_count, len(hook.occurrences))

        return best_count

    # ------------------------------------------------------------------
    # Check 2: Earworm Potential (max +4)
    # ------------------------------------------------------------------

    def _score_earworm_potential(self, vocal_notes) -> None:
        """Score earworm potential based on repetition/variation balance.

        Groups vocal notes by bar, computes bar-level pitch patterns, and
        evaluates the golden ratio between exact repetition and variation.

        Args:
            vocal_notes: Sorted list of vocal Note objects.
        """
        max_score = 4.0
        bars = _group_notes_by_bar(vocal_notes)

        if len(bars) < 4:
            return

        # Extract bar-level interval patterns for transposition-invariant matching.
        bar_numbers = sorted(bars.keys())
        bar_patterns = {}
        for bar_num in bar_numbers:
            bar_notes = sorted(bars[bar_num], key=lambda note: note.start)
            if len(bar_notes) >= 2:
                bar_patterns[bar_num] = _extract_intervals(
                    [note.pitch for note in bar_notes]
                )

        if len(bar_patterns) < 4:
            return

        pattern_list = list(bar_patterns.values())
        total_bars = len(pattern_list)
        exact_matches = 0
        approx_matches = 0

        # Compare each pair of bar patterns.
        for idx_a in range(total_bars):
            for idx_b in range(idx_a + 1, total_bars):
                if _intervals_match_transposed(
                    pattern_list[idx_a], pattern_list[idx_b]
                ):
                    exact_matches += 1
                elif _pattern_similarity(
                    pattern_list[idx_a], pattern_list[idx_b]
                ) >= _APPROXIMATE_MATCH_SIMILARITY:
                    approx_matches += 1

        total_pairs = total_bars * (total_bars - 1) / 2
        if total_pairs == 0:
            return

        exact_rate = exact_matches / total_pairs
        approx_rate = (exact_matches + approx_matches) / total_pairs

        # Golden ratio: exact_match_rate in 0.15-0.40 is ideal.
        if _EARWORM_EXACT_RATE_LOW <= exact_rate <= _EARWORM_EXACT_RATE_HIGH:
            repetition_score = 1.0
        elif exact_rate < _EARWORM_EXACT_RATE_LOW:
            repetition_score = exact_rate / _EARWORM_EXACT_RATE_LOW
        else:
            # Too repetitive.
            overshoot = exact_rate - _EARWORM_EXACT_RATE_HIGH
            repetition_score = max(0.0, 1.0 - overshoot * 3.0)

        # Approximate matches add a softer contribution.
        variation_score = min(1.0, approx_rate * 2.0) if approx_rate > 0 else 0.0

        # Phrase range analysis on repeated patterns.
        range_score = self._evaluate_phrase_ranges(bars, bar_patterns)

        raw_score = (
            repetition_score * 0.4
            + variation_score * 0.3
            + range_score * 0.3
        ) * max_score

        # Apply blueprint weight if available.
        weight = self._get_hook_weight()
        raw_score *= weight

        if raw_score > 0:
            self.add_bonus(
                Category.MELODIC,
                "earworm_potential",
                raw_score,
                max_score,
                description=(
                    f"exact_rate={exact_rate:.2f} "
                    f"approx_rate={approx_rate:.2f} "
                    f"range={range_score:.2f}"
                ),
            )

    def _evaluate_phrase_ranges(self, bars: dict, bar_patterns: dict) -> float:
        """Evaluate pitch range of repeated phrases.

        Optimal range for earworm phrases is 5-12 semitones.

        Args:
            bars: Dict mapping bar number to notes.
            bar_patterns: Dict mapping bar number to interval patterns.

        Returns:
            Score in [0.0, 1.0] based on phrase ranges.
        """
        if not bar_patterns:
            return 0.0

        # Find bars that have at least one approximate match.
        pattern_list = list(bar_patterns.items())
        repeated_bars = set()

        for idx_a in range(len(pattern_list)):
            for idx_b in range(idx_a + 1, len(pattern_list)):
                bar_a, pat_a = pattern_list[idx_a]
                bar_b, pat_b = pattern_list[idx_b]
                if (_intervals_match_transposed(pat_a, pat_b)
                        or _pattern_similarity(pat_a, pat_b)
                        >= _APPROXIMATE_MATCH_SIMILARITY):
                    repeated_bars.add(bar_a)
                    repeated_bars.add(bar_b)

        if not repeated_bars:
            return 0.0

        # Compute pitch range for repeated bars.
        ranges = []
        for bar_num in repeated_bars:
            if bar_num in bars:
                pitches = [note.pitch for note in bars[bar_num]]
                if len(pitches) >= 2:
                    ranges.append(max(pitches) - min(pitches))

        if not ranges:
            return 0.0

        avg_range = sum(ranges) / len(ranges)

        # 5-12 semitones is optimal.
        if _EARWORM_RANGE_LOW <= avg_range <= _EARWORM_RANGE_HIGH:
            return 1.0
        elif avg_range < _EARWORM_RANGE_LOW:
            return avg_range / _EARWORM_RANGE_LOW
        else:
            overshoot = avg_range - _EARWORM_RANGE_HIGH
            return max(0.0, 1.0 - overshoot / 12.0)

    # ------------------------------------------------------------------
    # Check 3: Melodic Sequences (max +3)
    # ------------------------------------------------------------------

    def _score_melodic_sequences(self, vocal_notes) -> None:
        """Detect melodic sequences: same interval pattern at different pitches.

        A melodic sequence is a melodic fragment that repeats at a different
        pitch level, preserving the interval structure. Common in classical
        and pop music as a development technique.

        Args:
            vocal_notes: Sorted list of vocal Note objects.
        """
        max_score = 3.0
        sorted_notes = sorted(vocal_notes, key=lambda note: note.start)
        pitches = [note.pitch for note in sorted_notes]

        if len(pitches) < _SEQUENCE_WINDOW_MIN + 2:
            return

        all_intervals = _extract_intervals(pitches)
        if len(all_intervals) < _SEQUENCE_WINDOW_MIN:
            return

        detected_sequences = 0

        # Track which starting positions have already been matched to avoid
        # counting overlapping sequences multiple times.
        matched_positions = set()

        # Try window sizes from largest to smallest for stronger matches first.
        for window_size in range(
            min(_SEQUENCE_WINDOW_MAX, len(all_intervals)),
            _SEQUENCE_WINDOW_MIN - 1,
            -1,
        ):
            for idx_start in range(len(all_intervals) - window_size + 1):
                if idx_start in matched_positions:
                    continue

                target = all_intervals[idx_start:idx_start + window_size]

                # Search for the same interval pattern at a different position,
                # starting from a different pitch (to exclude exact repeats).
                for idx_candidate in range(
                    idx_start + window_size,
                    len(all_intervals) - window_size + 1,
                ):
                    if idx_candidate in matched_positions:
                        continue

                    candidate = all_intervals[
                        idx_candidate:idx_candidate + window_size
                    ]

                    # Must have the same intervals but start on different pitch.
                    if (target == candidate
                            and pitches[idx_start]
                            != pitches[idx_candidate]):
                        detected_sequences += 1
                        matched_positions.add(idx_start)
                        matched_positions.add(idx_candidate)
                        break  # Move to next target window.

                if detected_sequences >= _SEQUENCE_MAX_BONUS:
                    break
            if detected_sequences >= _SEQUENCE_MAX_BONUS:
                break

        if detected_sequences > 0:
            score = min(detected_sequences, _SEQUENCE_MAX_BONUS) * (
                max_score / _SEQUENCE_MAX_BONUS
            )
            self.add_bonus(
                Category.MELODIC,
                "melodic_sequences",
                score,
                max_score,
                description=f"detected {detected_sequences} sequence(s)",
            )

    # ------------------------------------------------------------------
    # Check 4: Phrase Balance (max +2)
    # ------------------------------------------------------------------

    def _score_phrase_balance(self, vocal_notes) -> None:
        """Score phrase symmetry based on consecutive phrase length ratios.

        Detects phrases via gaps >= 1 beat in vocal notes, then compares
        consecutive phrase lengths. Good phrase balance means phrases have
        similar durations (ratio >= 0.5, ideally 0.7-1.0).

        Args:
            vocal_notes: Sorted list of vocal Note objects.
        """
        max_score = 2.0
        phrases = self._detect_phrases(vocal_notes)

        if len(phrases) < 2:
            return

        # Compute length ratios between consecutive phrases.
        ratios = []
        for idx in range(len(phrases) - 1):
            len_a = phrases[idx]
            len_b = phrases[idx + 1]
            if len_a > 0 and len_b > 0:
                ratio = min(len_a, len_b) / max(len_a, len_b)
                ratios.append(ratio)

        if not ratios:
            return

        avg_ratio = sum(ratios) / len(ratios)

        # Score based on average ratio. >= 0.7 is ideal, >= 0.5 is acceptable.
        if avg_ratio >= 0.7:
            score = max_score
        elif avg_ratio >= 0.5:
            # Linear interpolation between 0.5 (half score) and 0.7 (full).
            fraction = (avg_ratio - 0.5) / 0.2
            score = max_score * (0.5 + 0.5 * fraction)
        else:
            # Below 0.5: partial credit.
            score = max_score * avg_ratio

        if score > 0:
            self.add_bonus(
                Category.MELODIC,
                "phrase_balance",
                score,
                max_score,
                description=(
                    f"avg_ratio={avg_ratio:.2f} "
                    f"phrases={len(phrases)}"
                ),
            )

    def _detect_phrases(self, vocal_notes) -> List[int]:
        """Detect phrase lengths based on inter-note gaps.

        A phrase boundary is detected when the gap between consecutive notes
        is >= 1 beat (480 ticks).

        Args:
            vocal_notes: List of vocal Note objects.

        Returns:
            List of phrase lengths in ticks (start of first note to end
            of last note in each phrase).
        """
        sorted_notes = sorted(vocal_notes, key=lambda note: note.start)
        if not sorted_notes:
            return []

        phrases = []
        phrase_start = sorted_notes[0].start
        phrase_end = sorted_notes[0].end

        for idx in range(1, len(sorted_notes)):
            gap = sorted_notes[idx].start - sorted_notes[idx - 1].end
            if gap >= _PHRASE_GAP_TICKS:
                # End current phrase, start new one.
                phrase_length = phrase_end - phrase_start
                if phrase_length > 0:
                    phrases.append(phrase_length)
                phrase_start = sorted_notes[idx].start
                phrase_end = sorted_notes[idx].end
            else:
                phrase_end = max(phrase_end, sorted_notes[idx].end)

        # Final phrase.
        final_length = phrase_end - phrase_start
        if final_length > 0:
            phrases.append(final_length)

        return phrases

    # ------------------------------------------------------------------
    # Shared helpers
    # ------------------------------------------------------------------

    def _get_hook_weight(self) -> float:
        """Get the hook bonus weight from the blueprint profile.

        Returns:
            Weight multiplier (1.0 if no profile set).
        """
        if self.profile and hasattr(self.profile, 'hook_bonus_weight'):
            return self.profile.hook_bonus_weight
        return 1.0
