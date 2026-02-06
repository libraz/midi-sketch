"""Bonus scoring analyzer for rhythmic quality.

Awards bonus points for positive rhythmic qualities: rhythmic hooks,
syncopation balance, and section-level rhythm variation.
"""

from collections import Counter
from typing import List

from ..constants import TICKS_PER_BAR, TICKS_PER_BEAT, Category
from ..models import Bonus
from ..helpers import tick_to_bar, quantize_rhythm
from .base import BaseBonusAnalyzer


# Minimum vocal notes for rhythmic hook analysis.
_MIN_VOCAL_NOTES_FOR_HOOKS = 8

# IOI pattern window sizes (number of IOI values per window).
_PATTERN_WINDOW_1BAR = 4
_PATTERN_WINDOW_2BAR = 8

# Rhythmic hook repetition thresholds (top_count / total_windows).
_HOOK_THRESHOLD_FULL = 0.30
_HOOK_THRESHOLD_HIGH = 0.15
_HOOK_THRESHOLD_LOW = 0.08

# Beat position tolerance in ticks for syncopation classification.
_BEAT_TOLERANCE = 60

# Syncopation ideal off-beat ratio range.
_SYNCOPATION_OFF_BEAT_LOW = 0.2
_SYNCOPATION_OFF_BEAT_HIGH = 0.5

# Minimum strong-beat ratio for anchoring.
_SYNCOPATION_ANCHOR_MIN = 0.15

# Section density ratio ideal range (chorus / verse).
_DENSITY_RATIO_LOW = 1.2
_DENSITY_RATIO_HIGH = 2.5

# Melodic channels (excluding drums ch 9 and SE ch 15).
_MELODIC_CHANNELS = {0, 1, 2, 3, 4, 5}


class BonusRhythmAnalyzer(BaseBonusAnalyzer):
    """Awards bonus points for positive rhythmic qualities.

    Checks:
        1. Rhythmic hooks: repeated rhythmic patterns in vocal track.
        2. Syncopation quality: balanced off-beat accents with on-beat anchors.
        3. Section rhythm variation: appropriate density changes across sections.
    """

    def analyze(self) -> List[Bonus]:
        """Run all rhythmic bonus checks.

        Returns:
            List of Bonus objects awarded.
        """
        self._score_rhythmic_hooks()
        self._score_syncopation_quality()
        self._score_section_rhythm_variation()

        return self.bonuses

    # ------------------------------------------------------------------
    # Check 1: Rhythmic Hooks (max +4)
    # ------------------------------------------------------------------

    def _score_rhythmic_hooks(self) -> None:
        """Score rhythmic hook quality based on repeated IOI patterns.

        Takes vocal (ch 0) notes, computes inter-onset intervals, quantizes
        them to a 16th-note grid, and extracts 1-bar and 2-bar sliding-window
        patterns. Scores based on how often the most common pattern repeats
        relative to the total number of windows.
        """
        max_score = 4.0
        vocal_notes = self.notes_by_channel.get(0, [])

        if len(vocal_notes) < _MIN_VOCAL_NOTES_FOR_HOOKS:
            return

        sorted_notes = sorted(vocal_notes, key=lambda note: note.start)
        iois = [
            sorted_notes[idx + 1].start - sorted_notes[idx].start
            for idx in range(len(sorted_notes) - 1)
        ]

        if not iois:
            return

        quantized_iois = quantize_rhythm(iois)
        best_score = 0.0

        # Try both 1-bar (4 IOI) and 2-bar (8 IOI) window sizes.
        for window_size in (_PATTERN_WINDOW_1BAR, _PATTERN_WINDOW_2BAR):
            score = self._evaluate_pattern_repetition(
                quantized_iois, window_size
            )
            best_score = max(best_score, score)

        raw_score = best_score * max_score

        # Apply blueprint groove weight if available.
        weight = self._get_groove_weight()
        raw_score *= weight

        if raw_score > 0:
            self.add_bonus(
                Category.RHYTHM,
                "rhythmic_hooks",
                raw_score,
                max_score,
                description=f"best_pattern_score={best_score:.2f}",
            )

    def _evaluate_pattern_repetition(
        self, quantized_iois: List[int], window_size: int
    ) -> float:
        """Evaluate pattern repetition for a given window size.

        Uses a sliding window to extract all patterns of the given size,
        counts occurrences via Counter, and returns a normalized score.

        Args:
            quantized_iois: Quantized IOI values.
            window_size: Number of IOI values per pattern window.

        Returns:
            Score in [0.0, 1.0] based on repetition frequency.
        """
        if len(quantized_iois) < window_size:
            return 0.0

        # Extract all sliding-window patterns as tuples for hashing.
        patterns = [
            tuple(quantized_iois[idx:idx + window_size])
            for idx in range(len(quantized_iois) - window_size + 1)
        ]

        if not patterns:
            return 0.0

        counter = Counter(patterns)
        top_count = counter.most_common(1)[0][1]
        total_windows = len(patterns)

        # A single occurrence is never a "hook" -- require at least 2 repeats.
        if top_count < 2:
            return 0.0

        ratio = top_count / total_windows

        if ratio >= _HOOK_THRESHOLD_FULL:
            return 1.0
        elif ratio >= _HOOK_THRESHOLD_HIGH:
            return 0.75
        elif ratio >= _HOOK_THRESHOLD_LOW:
            return 0.5
        else:
            return 0.25

    # ------------------------------------------------------------------
    # Check 2: Syncopation Quality (max +3)
    # ------------------------------------------------------------------

    def _score_syncopation_quality(self) -> None:
        """Score syncopation quality based on beat-position distribution.

        Good syncopation balances off-beat accents with on-beat anchors.
        Classifies each melodic note as strong-beat, weak-beat, or off-beat
        based on its position within the bar, then evaluates the ratios.
        """
        max_score = 3.0
        melodic_notes = self._get_melodic_notes()

        if not melodic_notes:
            return

        strong_count = 0
        weak_count = 0
        off_beat_count = 0

        # Strong beats: beat 1 (pos 0) and beat 3 (pos TICKS_PER_BEAT*2).
        strong_positions = [0, TICKS_PER_BEAT * 2]
        # Weak beats: beat 2 (pos TICKS_PER_BEAT) and beat 4 (pos TICKS_PER_BEAT*3).
        weak_positions = [TICKS_PER_BEAT, TICKS_PER_BEAT * 3]

        for note in melodic_notes:
            pos_in_bar = note.start % TICKS_PER_BAR
            classified = False

            for strong_pos in strong_positions:
                if abs(pos_in_bar - strong_pos) <= _BEAT_TOLERANCE:
                    strong_count += 1
                    classified = True
                    break

            if not classified:
                for weak_pos in weak_positions:
                    if abs(pos_in_bar - weak_pos) <= _BEAT_TOLERANCE:
                        weak_count += 1
                        classified = True
                        break

            if not classified:
                off_beat_count += 1

        total_count = strong_count + weak_count + off_beat_count
        if total_count == 0:
            return

        off_beat_ratio = off_beat_count / total_count
        strong_beat_ratio = strong_count / total_count
        anchored = strong_beat_ratio >= _SYNCOPATION_ANCHOR_MIN

        # Ideal off-beat ratio: 0.2 - 0.5 with sufficient anchoring.
        if (_SYNCOPATION_OFF_BEAT_LOW <= off_beat_ratio
                <= _SYNCOPATION_OFF_BEAT_HIGH and anchored):
            score = max_score
        elif anchored and off_beat_ratio > 0:
            # Partial credit: some syncopation present with anchoring.
            if off_beat_ratio < _SYNCOPATION_OFF_BEAT_LOW:
                # Too little syncopation.
                fraction = off_beat_ratio / _SYNCOPATION_OFF_BEAT_LOW
                score = max_score * fraction
            else:
                # Too much syncopation (chaotic).
                overshoot = off_beat_ratio - _SYNCOPATION_OFF_BEAT_HIGH
                score = max(0.0, max_score * (1.0 - overshoot * 2.0))
        elif off_beat_ratio > 0:
            # Some syncopation but not enough anchoring.
            score = max_score * 0.3
        else:
            score = 0.0

        # Apply blueprint groove weight if available.
        weight = self._get_groove_weight()
        score *= weight

        if score > 0:
            self.add_bonus(
                Category.RHYTHM,
                "syncopation_quality",
                score,
                max_score,
                description=(
                    f"off_beat={off_beat_ratio:.2f} "
                    f"strong={strong_beat_ratio:.2f} "
                    f"anchored={anchored}"
                ),
            )

    def _get_melodic_notes(self) -> list:
        """Collect all melodic notes (channels 0-5, excluding drums).

        Returns:
            List of Note objects from melodic channels.
        """
        melodic_notes = []
        for channel in _MELODIC_CHANNELS:
            melodic_notes.extend(self.notes_by_channel.get(channel, []))
        return melodic_notes

    # ------------------------------------------------------------------
    # Check 3: Section Rhythm Variation (max +3)
    # ------------------------------------------------------------------

    def _score_section_rhythm_variation(self) -> None:
        """Score rhythm variation across song sections.

        Calculates melodic note density per section, compares verse and
        chorus densities, and awards points when the chorus is appropriately
        denser than the verse (ratio 1.2-2.5).
        """
        max_score = 3.0
        sections = self.sections

        if not sections:
            return

        # Calculate melodic note density per section.
        section_densities = []
        for section in sections:
            density = self._compute_section_melodic_density(section)
            section_densities.append({
                'type': section['type'],
                'density': density,
            })

        # Separate verse and chorus densities.
        verse_densities = [
            entry['density'] for entry in section_densities
            if entry['type'] == 'verse' and entry['density'] > 0
        ]
        chorus_densities = [
            entry['density'] for entry in section_densities
            if entry['type'] == 'chorus' and entry['density'] > 0
        ]

        if verse_densities and chorus_densities:
            avg_verse = sum(verse_densities) / len(verse_densities)
            avg_chorus = sum(chorus_densities) / len(chorus_densities)

            if avg_verse > 0:
                ratio = avg_chorus / avg_verse
                score = self._score_density_ratio(ratio, max_score)
            else:
                # Verse has no notes but chorus does: some variation exists.
                score = 1.0
        else:
            # No clear verse/chorus distinction. Award partial credit for
            # any density variation across sections.
            all_densities = [
                entry['density'] for entry in section_densities
                if entry['density'] > 0
            ]
            if len(all_densities) >= 2:
                min_density = min(all_densities)
                max_density = max(all_densities)
                if min_density > 0 and max_density > min_density:
                    score = 1.0
                else:
                    score = 0.0
            else:
                score = 0.0

        if score > 0:
            self.add_bonus(
                Category.RHYTHM,
                "section_rhythm_variation",
                score,
                max_score,
                description=self._format_variation_description(
                    verse_densities, chorus_densities
                ),
            )

    def _compute_section_melodic_density(self, section: dict) -> float:
        """Compute melodic note density for a section (notes per bar).

        Counts all melodic channel notes within the section's bar range
        and divides by the number of bars.

        Args:
            section: Section dict with 'start_bar' and 'end_bar'.

        Returns:
            Notes per bar for all melodic channels in this section.
        """
        start_tick = (section['start_bar'] - 1) * TICKS_PER_BAR
        end_tick = section['end_bar'] * TICKS_PER_BAR
        num_bars = max(1, section['end_bar'] - section['start_bar'] + 1)

        note_count = 0
        for channel in _MELODIC_CHANNELS:
            for note in self.notes_by_channel.get(channel, []):
                if start_tick <= note.start < end_tick:
                    note_count += 1

        return note_count / num_bars

    def _score_density_ratio(self, ratio: float, max_score: float) -> float:
        """Score a verse-to-chorus density ratio.

        Ideal range is 1.2 - 2.5. Scores scale down outside this range.

        Args:
            ratio: chorus_density / verse_density.
            max_score: Maximum achievable score.

        Returns:
            Score in [0.0, max_score].
        """
        if _DENSITY_RATIO_LOW <= ratio <= _DENSITY_RATIO_HIGH:
            return max_score
        elif ratio < _DENSITY_RATIO_LOW:
            if ratio <= 1.0:
                # Chorus is not denser than verse at all.
                return 0.0
            # Partial credit for slight density increase.
            fraction = (ratio - 1.0) / (_DENSITY_RATIO_LOW - 1.0)
            return max_score * fraction
        else:
            # Ratio above ideal range (chorus is overwhelmingly dense).
            overshoot = ratio - _DENSITY_RATIO_HIGH
            return max(0.0, max_score * (1.0 - overshoot / _DENSITY_RATIO_HIGH))

    def _format_variation_description(
        self,
        verse_densities: List[float],
        chorus_densities: List[float],
    ) -> str:
        """Format a human-readable description of section density variation.

        Args:
            verse_densities: Density values for verse sections.
            chorus_densities: Density values for chorus sections.

        Returns:
            Formatted description string.
        """
        if verse_densities and chorus_densities:
            avg_verse = sum(verse_densities) / len(verse_densities)
            avg_chorus = sum(chorus_densities) / len(chorus_densities)
            ratio = avg_chorus / avg_verse if avg_verse > 0 else 0.0
            return (
                f"verse_density={avg_verse:.2f} "
                f"chorus_density={avg_chorus:.2f} "
                f"ratio={ratio:.2f}"
            )
        return "no_verse_chorus_distinction"

    # ------------------------------------------------------------------
    # Shared helpers
    # ------------------------------------------------------------------

    def _get_groove_weight(self) -> float:
        """Get the groove bonus weight from the blueprint profile.

        Returns:
            Weight multiplier (1.0 if no profile set).
        """
        if self.profile and hasattr(self.profile, 'groove_bonus_weight'):
            return self.profile.groove_bonus_weight
        return 1.0
