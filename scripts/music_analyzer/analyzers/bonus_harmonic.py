"""Bonus scoring analyzer for harmonic quality.

Awards bonus points for positive harmonic qualities: tension-resolution arcs,
voice leading smoothness, and harmonic vocabulary richness.
"""

from collections import defaultdict
from typing import Dict, List, Optional, Tuple

from ..constants import (
    TICKS_PER_BAR,
    TICKS_PER_BEAT,
    Category,
    DEGREE_TO_ROOT_PC,
    CHORD_FUNCTION_MAP,
)
from ..models import Bonus
from ..helpers import tick_to_bar
from .base import BaseBonusAnalyzer


# Harmonic function tension values.
_FUNCTION_TENSION = {'T': 0.0, 'S': 0.5, 'D': 1.0}

# Voice leading thresholds (in semitones).
_VOICE_LEADING_EXCELLENT = 3  # Average movement <= 3 is excellent
_VOICE_LEADING_CUTOFF = 7     # Average movement >= 7 earns no bonus

# Harmonic variety thresholds.
_MIN_DISTINCT_ROOTS = 4       # Need at least 4 distinct root PCs
_MIN_DISTINCT_VOICINGS = 6    # Need at least 6 distinct voicing fingerprints


def _get_chord_function(degree: int) -> Optional[str]:
    """Map a chord degree to its harmonic function (T, S, or D).

    Converts the degree to its root pitch class via DEGREE_TO_ROOT_PC,
    then looks up the function in CHORD_FUNCTION_MAP.

    Args:
        degree: Chord degree (0-6), or -1 if unknown.

    Returns:
        Harmonic function string ('T', 'S', 'D') or None if unknown.
    """
    if degree < 0 or degree > 6:
        return None
    root_pc = DEGREE_TO_ROOT_PC.get(degree)
    if root_pc is None:
        return None
    return CHORD_FUNCTION_MAP.get(root_pc)


def _tension_value(degree: int) -> Optional[float]:
    """Get the numeric tension value for a chord degree.

    Args:
        degree: Chord degree (0-6).

    Returns:
        Tension value (0.0 for tonic, 0.5 for subdominant, 1.0 for dominant)
        or None if degree is invalid.
    """
    func = _get_chord_function(degree)
    if func is None:
        return None
    return _FUNCTION_TENSION.get(func, 0.0)


class BonusHarmonicAnalyzer(BaseBonusAnalyzer):
    """Awards bonus points for positive harmonic qualities.

    Checks:
        1. Tension-resolution arc: harmonic tension rises before chorus,
           resolves with V->I cadence.
        2. Voice leading quality: smooth chord-to-chord transitions.
        3. Harmonic variety: richness of harmonic vocabulary.
    """

    def analyze(self) -> List[Bonus]:
        """Run all harmonic bonus checks.

        Returns:
            List of Bonus objects awarded.
        """
        self._score_tension_resolution()
        self._score_voice_leading()
        self._score_harmonic_variety()

        return self.bonuses

    # ------------------------------------------------------------------
    # Check 1: Tension-Resolution Arc (max +5)
    # ------------------------------------------------------------------

    def _score_tension_resolution(self) -> None:
        """Score tension-resolution arc across the song structure.

        Evaluates three components:
        (a) Tension increase before chorus sections (+2)
        (b) V->I cadence detection at section boundaries (+2)
        (c) Overall tension arc is not flat (+1)
        """
        max_score = 5.0
        sections = self.sections

        if len(sections) < 2:
            return

        # Compute average tension per section.
        section_tensions = []
        for section in sections:
            tension = self._compute_section_tension(section)
            section_tensions.append(tension)

        if not any(val is not None for val in section_tensions):
            return

        # (a) Tension increase before chorus: pre-chorus tension > verse tension.
        pre_chorus_score = self._evaluate_pre_chorus_tension(
            sections, section_tensions
        )

        # (b) V->I cadence detection at section boundaries.
        cadence_score = self._evaluate_cadences(sections)

        # (c) Overall tension arc is not flat.
        arc_score = self._evaluate_tension_arc(section_tensions)

        raw_score = pre_chorus_score + cadence_score + arc_score

        # Apply blueprint weight if available.
        weight = self._get_tension_weight()
        raw_score *= weight

        if raw_score > 0:
            self.add_bonus(
                Category.HARMONIC,
                "tension_resolution",
                raw_score,
                max_score,
                description=(
                    f"pre_chorus={pre_chorus_score:.2f} "
                    f"cadence={cadence_score:.2f} "
                    f"arc={arc_score:.2f}"
                ),
            )

    def _compute_section_tension(self, section: dict) -> Optional[float]:
        """Compute average harmonic tension for a section.

        Samples chord degree at beat 1 of each bar in the section and
        averages the tension values.

        Args:
            section: Section dict with 'start_bar' and 'end_bar'.

        Returns:
            Average tension value (0.0-1.0) or None if no data.
        """
        tensions = []
        for bar_num in range(section['start_bar'], section['end_bar'] + 1):
            # Beat 1 tick for this bar.
            beat_one_tick = (bar_num - 1) * TICKS_PER_BAR
            degree = self._get_chord_degree_near_tick(beat_one_tick)
            if degree >= 0:
                tension = _tension_value(degree)
                if tension is not None:
                    tensions.append(tension)

        if not tensions:
            return None
        return sum(tensions) / len(tensions)

    def _get_chord_degree_near_tick(self, tick: int) -> int:
        """Get chord degree from provenance at or near a tick.

        Searches all notes for the closest provenance chord_degree within
        a half-bar window of the target tick.

        Args:
            tick: Target tick position.

        Returns:
            Chord degree (0-6) or -1 if not found.
        """
        best_degree = -1
        best_dist = TICKS_PER_BAR // 2  # Max search window: half a bar.
        for note in self.notes:
            if note.provenance and 'chord_degree' in note.provenance:
                dist = abs(note.start - tick)
                if dist < best_dist:
                    best_dist = dist
                    best_degree = note.provenance['chord_degree']
                    if dist == 0:
                        break
        return best_degree

    def _evaluate_pre_chorus_tension(
        self,
        sections: list,
        section_tensions: list,
    ) -> float:
        """Evaluate whether tension rises before chorus sections.

        Compares tension of sections immediately preceding chorus sections
        with the average verse tension. Higher pre-chorus tension earns
        bonus points.

        Args:
            sections: List of section dicts.
            section_tensions: Parallel list of tension values (or None).

        Returns:
            Score in [0.0, 2.0].
        """
        # Collect verse and pre-chorus tensions.
        verse_tensions = []
        pre_chorus_tensions = []

        for idx, section in enumerate(sections):
            tension = section_tensions[idx]
            if tension is None:
                continue

            if section['type'] == 'verse':
                verse_tensions.append(tension)

            # Check if the next section is a chorus.
            if idx + 1 < len(sections) and sections[idx + 1]['type'] == 'chorus':
                pre_chorus_tensions.append(tension)

        if not verse_tensions or not pre_chorus_tensions:
            return 0.0

        avg_verse = sum(verse_tensions) / len(verse_tensions)
        avg_pre_chorus = sum(pre_chorus_tensions) / len(pre_chorus_tensions)

        # Award points if pre-chorus tension exceeds verse tension.
        if avg_pre_chorus > avg_verse:
            diff = avg_pre_chorus - avg_verse
            # Full 2 points for a difference >= 0.3, scaled below that.
            return min(2.0, diff / 0.3 * 2.0)

        return 0.0

    def _evaluate_cadences(self, sections: list) -> float:
        """Detect V->I cadences at section boundaries.

        Looks for degree 4 (V chord) near the end of one section and
        degree 0 (I chord) near the start of the next section.

        Args:
            sections: List of section dicts.

        Returns:
            Score in [0.0, 2.0].
        """
        cadence_count = 0
        boundary_count = 0

        for idx in range(len(sections) - 1):
            boundary_count += 1

            # End of current section: last bar, beat 1.
            end_bar = sections[idx]['end_bar']
            end_tick = (end_bar - 1) * TICKS_PER_BAR
            end_degree = self._get_chord_degree_near_tick(end_tick)

            # Start of next section: first bar, beat 1.
            start_bar = sections[idx + 1]['start_bar']
            start_tick = (start_bar - 1) * TICKS_PER_BAR
            start_degree = self._get_chord_degree_near_tick(start_tick)

            # V -> I cadence: degree 4 -> degree 0.
            if end_degree == 4 and start_degree == 0:
                cadence_count += 1

        if boundary_count == 0:
            return 0.0

        # At least one cadence earns 1 point, two or more earns full 2 points.
        if cadence_count >= 2:
            return 2.0
        elif cadence_count == 1:
            return 1.0
        return 0.0

    def _evaluate_tension_arc(
        self, section_tensions: list
    ) -> float:
        """Evaluate whether the overall tension arc is non-flat.

        A non-flat tension arc means the song has harmonic movement --
        not all sections have the same tension level.

        Args:
            section_tensions: List of tension values (some may be None).

        Returns:
            Score in [0.0, 1.0].
        """
        valid_tensions = [val for val in section_tensions if val is not None]

        if len(valid_tensions) < 2:
            return 0.0

        min_tension = min(valid_tensions)
        max_tension = max(valid_tensions)
        tension_range = max_tension - min_tension

        # Full point for range >= 0.4, scaled below that.
        if tension_range >= 0.4:
            return 1.0
        elif tension_range > 0.0:
            return tension_range / 0.4
        return 0.0

    # ------------------------------------------------------------------
    # Check 2: Voice Leading Quality (max +3)
    # ------------------------------------------------------------------

    def _score_voice_leading(self) -> None:
        """Score voice leading quality based on chord-to-chord transitions.

        Groups chord track notes (channel 1) by bar onset, then measures
        the average pitch movement per voice between consecutive onsets.
        Good voice leading keeps movement small (stepwise or close).
        """
        max_score = 3.0
        chord_notes = self.notes_by_channel.get(1, [])

        if not chord_notes:
            return

        # Group chord notes by bar onset (beat 1 of each bar).
        bar_voicings = self._group_chord_notes_by_bar_onset(chord_notes)

        if len(bar_voicings) < 2:
            return

        # Calculate average pitch movement per voice across consecutive bars.
        sorted_bars = sorted(bar_voicings.keys())
        total_movement = 0.0
        transition_count = 0

        for idx in range(len(sorted_bars) - 1):
            bar_a = sorted_bars[idx]
            bar_b = sorted_bars[idx + 1]
            pitches_a = sorted(bar_voicings[bar_a])
            pitches_b = sorted(bar_voicings[bar_b])

            # Match voices by position (lowest to lowest, etc.).
            voice_count = min(len(pitches_a), len(pitches_b))
            if voice_count == 0:
                continue

            movement = sum(
                abs(pitches_b[voice_idx] - pitches_a[voice_idx])
                for voice_idx in range(voice_count)
            ) / voice_count

            total_movement += movement
            transition_count += 1

        if transition_count == 0:
            return

        avg_movement = total_movement / transition_count

        # Score: 3 points for avg <= 3, linearly scaled to 0 at avg >= 7.
        if avg_movement <= _VOICE_LEADING_EXCELLENT:
            score = max_score
        elif avg_movement >= _VOICE_LEADING_CUTOFF:
            score = 0.0
        else:
            # Linear interpolation between excellent and cutoff.
            fraction = (avg_movement - _VOICE_LEADING_EXCELLENT) / (
                _VOICE_LEADING_CUTOFF - _VOICE_LEADING_EXCELLENT
            )
            score = max_score * (1.0 - fraction)

        if score > 0:
            self.add_bonus(
                Category.HARMONIC,
                "voice_leading",
                score,
                max_score,
                description=(
                    f"avg_movement={avg_movement:.2f}st "
                    f"transitions={transition_count}"
                ),
            )

    def _group_chord_notes_by_bar_onset(
        self, chord_notes
    ) -> Dict[int, List[int]]:
        """Group chord note pitches by the bar they fall on at beat 1.

        Only considers notes that start on or near beat 1 of a bar
        (within a quarter-beat tolerance).

        Args:
            chord_notes: List of chord track Note objects.

        Returns:
            Dict mapping bar number to list of pitches at that bar onset.
        """
        tolerance = TICKS_PER_BEAT // 4  # 120 ticks
        bar_voicings: Dict[int, List[int]] = defaultdict(list)

        for note in chord_notes:
            bar_num = tick_to_bar(note.start)
            beat_one_tick = (bar_num - 1) * TICKS_PER_BAR
            offset = note.start - beat_one_tick

            if offset <= tolerance:
                bar_voicings[bar_num].append(note.pitch)

        return dict(bar_voicings)

    # ------------------------------------------------------------------
    # Check 3: Harmonic Variety (max +2)
    # ------------------------------------------------------------------

    def _score_harmonic_variety(self) -> None:
        """Score harmonic vocabulary richness.

        Evaluates two components:
        (a) Distinct root pitch classes used across the song (+1)
        (b) Distinct voicing fingerprints in the chord track (+1)
        """
        max_score = 2.0
        root_score = self._evaluate_root_variety()
        voicing_score = self._evaluate_voicing_variety()

        raw_score = root_score + voicing_score

        if raw_score > 0:
            self.add_bonus(
                Category.HARMONIC,
                "harmonic_variety",
                raw_score,
                max_score,
                description=(
                    f"root={root_score:.2f} "
                    f"voicing={voicing_score:.2f}"
                ),
            )

    def _evaluate_root_variety(self) -> float:
        """Evaluate the number of distinct root pitch classes used.

        Collects chord degrees at beat 1 of each bar and maps them to
        root pitch classes.

        Returns:
            Score in [0.0, 1.0]. Full point for 4+ distinct roots.
        """
        if not self.notes:
            return 0.0

        max_bar = max(tick_to_bar(note.start) for note in self.notes)
        root_pcs = set()

        for bar_num in range(1, max_bar + 1):
            beat_one_tick = (bar_num - 1) * TICKS_PER_BAR
            degree = self._get_chord_degree_near_tick(beat_one_tick)
            if degree >= 0:
                root_pc = DEGREE_TO_ROOT_PC.get(degree)
                if root_pc is not None:
                    root_pcs.add(root_pc)

        distinct_count = len(root_pcs)

        if distinct_count >= _MIN_DISTINCT_ROOTS:
            return 1.0
        elif distinct_count > 0:
            return distinct_count / _MIN_DISTINCT_ROOTS
        return 0.0

    def _evaluate_voicing_variety(self) -> float:
        """Evaluate the number of distinct chord voicing fingerprints.

        A voicing fingerprint is the sorted tuple of pitch classes from
        chord notes at each bar onset. More distinct voicings means
        richer harmonic vocabulary.

        Returns:
            Score in [0.0, 1.0]. Full point for 6+ distinct voicings.
        """
        chord_notes = self.notes_by_channel.get(1, [])
        if not chord_notes:
            return 0.0

        bar_voicings = self._group_chord_notes_by_bar_onset(chord_notes)
        fingerprints = set()

        for pitches in bar_voicings.values():
            if pitches:
                # Convert to pitch classes and create sorted fingerprint.
                pitch_classes = tuple(sorted(set(pitch % 12 for pitch in pitches)))
                fingerprints.add(pitch_classes)

        distinct_count = len(fingerprints)

        if distinct_count >= _MIN_DISTINCT_VOICINGS:
            return 1.0
        elif distinct_count > 0:
            return distinct_count / _MIN_DISTINCT_VOICINGS
        return 0.0

    # ------------------------------------------------------------------
    # Shared helpers
    # ------------------------------------------------------------------

    def _get_tension_weight(self) -> float:
        """Get the tension bonus weight from the blueprint profile.

        Returns:
            Weight multiplier (1.0 if no profile set).
        """
        if self.profile and hasattr(self.profile, 'tension_bonus_weight'):
            return self.profile.tension_bonus_weight
        return 1.0
