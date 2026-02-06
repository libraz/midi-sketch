"""Bonus scoring analyzer for structural quality.

Awards bonus points for positive structural qualities: energy arc
progression, dynamic range across sections, and arrangement fullness
in chorus sections.
"""

from typing import List

from ..constants import TICKS_PER_BAR, Category
from ..models import Bonus
from .base import BaseBonusAnalyzer


# Minimum energy range (max - min) to award dynamic range bonus.
_ENERGY_RANGE_THRESHOLD = 0.3

# Peak position ideal range (40%-80% of song duration).
_PEAK_POSITION_LOW = 0.40
_PEAK_POSITION_HIGH = 0.80

# Velocity range thresholds for dynamic range scoring.
_VELOCITY_RANGE_STRONG = 25
_VELOCITY_RANGE_MODERATE = 15
_VELOCITY_RANGE_MILD = 8

# Active track count thresholds for arrangement fullness.
_FULLNESS_RICH = 5
_FULLNESS_GOOD = 4
_FULLNESS_FAIR = 3

# SE channel excluded from arrangement analysis.
_SE_CHANNEL = 15


class BonusStructureAnalyzer(BaseBonusAnalyzer):
    """Awards bonus points for positive structural qualities.

    Checks:
        1. Energy arc: good energy progression with peak in the right spot.
        2. Dynamic range: velocity contrast between sections.
        3. Arrangement fullness: active track count in chorus sections.
    """

    def analyze(self) -> List[Bonus]:
        """Run all structural bonus checks.

        Returns:
            List of Bonus objects awarded.
        """
        self._score_energy_arc()
        self._score_dynamic_range()
        self._score_arrangement_fullness()

        return self.bonuses

    # ------------------------------------------------------------------
    # Check 1: Energy Arc (max +4)
    # ------------------------------------------------------------------

    def _score_energy_arc(self) -> None:
        """Score energy arc quality based on energy progression.

        Evaluates the energy curve across the song in three parts:
        - Peak position: highest energy in the 40-80% position (+2)
        - Energy progression: middle third avg > first third avg (+1)
        - Dynamic range: max - min energy >= 0.3 (+1)
        """
        max_score = 4.0

        if not self.energy_curve:
            return

        total_points = len(self.energy_curve)
        if total_points == 0:
            return

        energies = [energy for _, energy in self.energy_curve]
        bars = [bar for bar, _ in self.energy_curve]

        # Find peak position as a fraction of song duration.
        max_energy = max(energies)
        min_energy = min(energies)
        peak_idx = energies.index(max_energy)
        peak_position = peak_idx / max(1, total_points - 1) if total_points > 1 else 0.5

        # Divide into three equal parts.
        third_size = max(1, total_points // 3)
        first_third = energies[:third_size]
        middle_third = energies[third_size:third_size * 2]
        last_third = energies[third_size * 2:]

        avg_first = sum(first_third) / len(first_third) if first_third else 0.0
        avg_middle = sum(middle_third) / len(middle_third) if middle_third else 0.0

        score = 0.0

        # Peak position: highest energy should be in the 40-80% position (+2).
        if _PEAK_POSITION_LOW <= peak_position <= _PEAK_POSITION_HIGH:
            score += 2.0

        # Energy progression: middle third avg > first third avg (+1).
        if avg_middle > avg_first:
            score += 1.0

        # Dynamic range: max - min >= 0.3 (+1).
        energy_range = max_energy - min_energy
        if energy_range >= _ENERGY_RANGE_THRESHOLD:
            score += 1.0

        if score > 0:
            self.add_bonus(
                Category.STRUCTURE,
                "energy_arc",
                score,
                max_score,
                description=(
                    f"peak_pos={peak_position:.2f} "
                    f"mid_avg={avg_middle:.2f} "
                    f"first_avg={avg_first:.2f} "
                    f"range={energy_range:.2f}"
                ),
            )

    # ------------------------------------------------------------------
    # Check 2: Dynamic Range (max +3)
    # ------------------------------------------------------------------

    def _score_dynamic_range(self) -> None:
        """Score dynamic range based on velocity contrast between sections.

        Uses section-level average velocity to measure contrast.
        Higher contrast (velocity range) earns more points.
        """
        max_score = 3.0
        sections = self.sections

        if not sections:
            return

        # Collect average velocities from sections that have notes.
        avg_velocities = [
            section['avg_velocity']
            for section in sections
            if section['note_count'] > 0
        ]

        if len(avg_velocities) < 2:
            return

        velocity_range = max(avg_velocities) - min(avg_velocities)

        if velocity_range >= _VELOCITY_RANGE_STRONG:
            score = 3.0
        elif velocity_range >= _VELOCITY_RANGE_MODERATE:
            score = 2.0
        elif velocity_range >= _VELOCITY_RANGE_MILD:
            score = 1.0
        else:
            score = 0.0

        # Apply blueprint dynamics weight if available.
        weight = self._get_dynamics_weight()
        score *= weight

        if score > 0:
            self.add_bonus(
                Category.STRUCTURE,
                "dynamic_range",
                score,
                max_score,
                description=(
                    f"velocity_range={velocity_range:.1f} "
                    f"sections={len(avg_velocities)}"
                ),
            )

    # ------------------------------------------------------------------
    # Check 3: Arrangement Fullness (max +3)
    # ------------------------------------------------------------------

    def _score_arrangement_fullness(self) -> None:
        """Score arrangement fullness based on active tracks in chorus.

        Counts distinct MIDI channels with notes during chorus sections
        (excluding SE channel 15). Falls back to the densest section
        if no chorus sections are detected.
        """
        max_score = 3.0
        sections = self.sections

        if not sections:
            return

        # Find chorus sections.
        chorus_sections = [
            section for section in sections
            if section['type'] == 'chorus'
        ]

        # Fall back to densest section if no chorus detected.
        if not chorus_sections:
            chorus_sections = [
                max(sections, key=lambda sec: sec['density'])
            ]

        # Count active channels per chorus section.
        active_counts = []
        for section in chorus_sections:
            active_channels = self._count_active_channels(section)
            active_counts.append(active_channels)

        if not active_counts:
            return

        avg_active = sum(active_counts) / len(active_counts)

        if avg_active >= _FULLNESS_RICH:
            score = 3.0
        elif avg_active >= _FULLNESS_GOOD:
            score = 2.0
        elif avg_active >= _FULLNESS_FAIR:
            score = 1.0
        else:
            score = 0.0

        if score > 0:
            self.add_bonus(
                Category.STRUCTURE,
                "arrangement_fullness",
                score,
                max_score,
                description=(
                    f"avg_active_tracks={avg_active:.1f} "
                    f"chorus_sections={len(chorus_sections)}"
                ),
            )

    # ------------------------------------------------------------------
    # Private helpers
    # ------------------------------------------------------------------

    def _count_active_channels(self, section: dict) -> int:
        """Count distinct channels with notes in a section.

        Excludes SE channel (15) from the count.

        Args:
            section: Section dict with 'start_bar' and 'end_bar'.

        Returns:
            Number of distinct active channels in the section.
        """
        start_tick = (section['start_bar'] - 1) * TICKS_PER_BAR
        end_tick = section['end_bar'] * TICKS_PER_BAR

        active_channels = set()
        for note in self.notes:
            if note.channel == _SE_CHANNEL:
                continue
            if start_tick <= note.start < end_tick:
                active_channels.add(note.channel)

        return len(active_channels)

    def _get_dynamics_weight(self) -> float:
        """Get the dynamics bonus weight from the blueprint profile.

        Returns:
            Weight multiplier (1.0 if no profile set).
        """
        if self.profile and hasattr(self.profile, 'dynamics_bonus_weight'):
            return self.profile.dynamics_bonus_weight
        return 1.0
