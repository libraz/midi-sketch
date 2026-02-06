"""Base analyzer class with shared utilities.

Provides common initialization, section estimation, beat position helpers,
and issue creation used by all domain-specific analyzers.
"""

from collections import defaultdict
from typing import List, Optional

from ..constants import (
    TICKS_PER_BEAT, TICKS_PER_BAR, TRACK_NAMES,
    SECTION_LENGTH_BARS, BEAT_STRENGTH,
    Severity, Category,
)
from ..models import Note, Issue
from ..blueprints import BlueprintProfile
from ..helpers import tick_to_bar


class BaseAnalyzer:
    """Common base for all domain-specific analyzers.

    Attributes:
        notes: All notes sorted by (start, channel).
        notes_by_channel: Notes grouped by MIDI channel.
        profile: Optional blueprint profile for context-aware analysis.
        metadata: Song metadata (bpm, sections, etc.).
        issues: Collected analysis issues.
    """

    def __init__(
        self,
        notes: List[Note],
        notes_by_channel: dict,
        profile: Optional[BlueprintProfile] = None,
        metadata: Optional[dict] = None,
    ):
        self.notes = notes
        self.notes_by_channel = notes_by_channel
        self.profile = profile
        self.metadata = metadata or {}
        self.issues: List[Issue] = []
        self._sections = None  # Lazy computed

    @property
    def sections(self):
        """Estimated song sections (lazy computed)."""
        if self._sections is None:
            self._sections = self._estimate_sections()
        return self._sections

    def analyze(self) -> List[Issue]:
        """Run all analyses for this domain. Override in subclasses."""
        raise NotImplementedError

    def _estimate_sections(self) -> list:
        """Estimate sections as 8-bar groups with type classification."""
        max_bar = max(
            (tick_to_bar(n.start) for n in self.notes), default=0
        ) if self.notes else 0
        sections = []
        vocal = self.notes_by_channel.get(0, [])

        for start_bar in range(1, max_bar + 1, SECTION_LENGTH_BARS):
            end_bar = min(start_bar + SECTION_LENGTH_BARS - 1, max_bar)
            st = (start_bar - 1) * TICKS_PER_BAR
            et = end_bar * TICKS_PER_BAR
            sec_notes = [n for n in vocal if st <= n.start < et]
            density = len(sec_notes) / max(1, end_bar - start_bar + 1)
            avg_p = (
                sum(n.pitch for n in sec_notes) / len(sec_notes)
                if sec_notes else 0
            )
            avg_v = (
                sum(n.velocity for n in sec_notes) / len(sec_notes)
                if sec_notes else 0
            )
            sections.append({
                'start_bar': start_bar,
                'end_bar': end_bar,
                'density': density,
                'avg_pitch': avg_p,
                'avg_velocity': avg_v,
                'note_count': len(sec_notes),
                'type': 'verse',
            })

        if len(sections) >= 3:
            energies = sorted(
                range(len(sections)),
                key=lambda i: (sections[i]['density'] * 0.4 +
                               sections[i]['avg_velocity'] / 127 * 0.6),
                reverse=True,
            )
            for rank, idx in enumerate(energies):
                if rank < len(sections) // 3:
                    sections[idx]['type'] = 'chorus'
                elif rank >= len(sections) * 2 // 3:
                    sections[idx]['type'] = 'verse'
                else:
                    sections[idx]['type'] = 'bridge'

        return sections

    def get_beat_position(self, tick: int) -> tuple:
        """Return (beat_number, offset_in_beat) for a tick.

        Beat number is 1-indexed (1-4 in 4/4 time).
        """
        pos_in_bar = tick % TICKS_PER_BAR
        beat = pos_in_bar // TICKS_PER_BEAT + 1
        offset = pos_in_bar % TICKS_PER_BEAT
        return beat, offset

    def get_beat_strength(self, tick: int) -> float:
        """Return beat strength (0.0-1.0) for a tick position."""
        beat, _ = self.get_beat_position(tick)
        return BEAT_STRENGTH.get(beat, 0.4)

    def get_chord_degree_at(self, tick: int) -> int:
        """Get chord degree from provenance data at a tick.

        Searches for the closest note with provenance chord_degree info.
        Returns -1 if not found.
        """
        best_degree = -1
        best_dist = float('inf')
        for note in self.notes:
            if note.provenance and 'chord_degree' in note.provenance:
                dist = abs(note.start - tick)
                if dist < best_dist:
                    best_dist = dist
                    best_degree = note.provenance['chord_degree']
                    if dist == 0:
                        break
        return best_degree

    def add_issue(
        self,
        severity: Severity,
        category: Category,
        subcategory: str,
        message: str,
        tick: int,
        track: str = "",
        details: Optional[dict] = None,
    ):
        """Convenience helper to create and append an Issue."""
        self.issues.append(Issue(
            severity=severity,
            category=category,
            subcategory=subcategory,
            message=message,
            tick=tick,
            track=track,
            details=details or {},
        ))
