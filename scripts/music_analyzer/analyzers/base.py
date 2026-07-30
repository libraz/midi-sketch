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
from ..models import Note, Issue, Bonus, HookPattern
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
        """Song sections from metadata when available, otherwise estimates."""
        if self._sections is None:
            self._sections = self._metadata_sections() or self._estimate_sections()
        return self._sections

    def analyze(self) -> List[Issue]:
        """Run all analyses for this domain. Override in subclasses."""
        raise NotImplementedError

    def _metadata_sections(self) -> list:
        """Return normalized sections from generation metadata if present."""
        raw_sections = self.metadata.get('sections', [])
        if not raw_sections:
            return []

        sections = []
        vocal = self.notes_by_channel.get(0, [])
        for idx, raw in enumerate(raw_sections):
            start_tick = raw.get('start_ticks', raw.get('startTick'))
            end_tick = raw.get('end_ticks', raw.get('endTick'))

            if start_tick is None:
                start_bar = raw.get('start_bar', raw.get('startBar', idx * SECTION_LENGTH_BARS + 1))
                start_tick = (max(1, int(start_bar)) - 1) * TICKS_PER_BAR
            if end_tick is None:
                if 'end_bar' in raw:
                    end_tick = int(raw['end_bar']) * TICKS_PER_BAR
                elif 'bars' in raw:
                    end_tick = int(start_tick) + int(raw['bars']) * TICKS_PER_BAR
                else:
                    end_tick = int(start_tick) + SECTION_LENGTH_BARS * TICKS_PER_BAR

            start_tick = int(start_tick)
            end_tick = int(end_tick)
            if end_tick <= start_tick:
                continue

            sec_notes = [n for n in vocal if start_tick <= n.start < end_tick]
            bars = max(1, (end_tick - start_tick) / TICKS_PER_BAR)
            raw_type = str(raw.get('type', raw.get('name', 'verse')))
            section_type = self._normalize_section_type(raw_type)
            # Analyzer section bars are always 1-indexed and inclusive.
            # Generated metadata also contains the core's 0-indexed
            # ``start_bar`` field, so derive both boundaries from the
            # authoritative tick range instead of forwarding raw bar values.
            start_bar = tick_to_bar(start_tick)
            end_bar = tick_to_bar(end_tick - 1)

            sections.append({
                'start_bar': int(start_bar),
                'end_bar': int(end_bar),
                'start_ticks': start_tick,
                'end_ticks': end_tick,
                'density': len(sec_notes) / bars,
                'avg_pitch': (
                    sum(n.pitch for n in sec_notes) / len(sec_notes)
                    if sec_notes else 0
                ),
                'avg_velocity': (
                    sum(n.velocity for n in sec_notes) / len(sec_notes)
                    if sec_notes else 0
                ),
                'note_count': len(sec_notes),
                'type': section_type,
                'raw_type': raw_type,
            })

        return sections

    @staticmethod
    def _normalize_section_type(section_type: str) -> str:
        """Map generated section names to roles used by structural checks."""
        value = section_type.lower().replace('_', '').replace('-', '').replace(' ', '')
        if 'chorus' in value or value in {'c', 'hook', 'drop'}:
            return 'chorus'
        if ('bridge' in value or 'interlude' in value
                or value in {'b', 'prechorus'}):
            return 'bridge'
        if value == 'a' or 'verse' in value:
            return 'verse'
        if any(marker in value for marker in ('intro', 'outro', 'mixbreak', 'chant')):
            return 'instrumental'
        # Unknown labels must not dilute the verse baseline used by the
        # verse-vs-chorus energy check.
        return 'instrumental'

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

        Prefers the generator's exported chord timeline. Falls back to the
        nearest note provenance for legacy JSON without that timeline.
        """
        chord = self.chord_at(tick)
        if chord is not None:
            try:
                return int(chord["degree"])
            except (KeyError, TypeError, ValueError):
                pass
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

    def vocal_ceiling_at(self, tick: int) -> Optional[int]:
        """Highest vocal pitch sounding at ``tick``, or None during rests."""
        sounding = [
            note.pitch for note in self.notes_by_channel.get(0, [])
            if note.start <= tick < note.end
        ]
        return max(sounding) if sounding else None

    def chord_at(self, tick: int) -> Optional[dict]:
        """Return the exported chord timeline entry sounding at ``tick``."""
        chords = self.metadata.get("chords", [])
        if not isinstance(chords, list):
            return None
        for chord in chords:
            if not isinstance(chord, dict):
                continue
            try:
                start = int(chord.get("tick", chord.get("start_ticks")))
                end = int(chord.get("endTick", chord.get("end_ticks")))
            except (TypeError, ValueError):
                continue
            if start <= tick < end:
                return chord
        return None

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


class BaseBonusAnalyzer(BaseAnalyzer):
    """Base class for bonus scoring analyzers.

    Extends BaseAnalyzer with bonus tracking, hook patterns,
    and energy curve data for positive quality evaluation.
    """

    def __init__(
        self,
        notes: List[Note],
        notes_by_channel: dict,
        profile: Optional[BlueprintProfile] = None,
        metadata: Optional[dict] = None,
        hooks: Optional[List[HookPattern]] = None,
        energy_curve: Optional[list] = None,
    ):
        super().__init__(notes, notes_by_channel, profile, metadata)
        self.hooks = hooks or []
        self.energy_curve = energy_curve or []
        self.bonuses: List[Bonus] = []

    def analyze(self) -> List[Bonus]:
        """Run bonus analysis. Override in subclasses."""
        raise NotImplementedError

    def add_bonus(
        self,
        category: Category,
        name: str,
        score: float,
        max_score: float,
        description: str = "",
    ):
        """Add a bonus with clamping to [0, max_score]."""
        clamped = max(0.0, min(score, max_score))
        if clamped > 0:
            self.bonuses.append(Bonus(
                category=category,
                name=name,
                score=round(clamped, 2),
                max_score=max_score,
                description=description,
            ))
