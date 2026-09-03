"""Base analyzer class with shared utilities.

Provides common initialization, section estimation, beat position helpers,
and issue creation used by all domain-specific analyzers.
"""

from collections import Counter, defaultdict
from typing import List, Optional

from ..constants import (
    TICKS_PER_BEAT, TICKS_PER_BAR, TRACK_NAMES,
    SECTION_LENGTH_BARS, BEAT_STRENGTH, NON_TRANSPOSED_CHANNELS,
    DEFAULT_VOCAL_RANGE, Severity, Category,
)
from ..models import Note, Issue, Bonus, HookPattern
from ..blueprints import BlueprintProfile
from ..helpers import tick_to_bar


# Role each section name the generator emits plays in the structural checks.
#
# The chorus role is what the energy-contrast, chorus-density and climax checks
# measure, so it carries the sections the core treats as high energy: Chorus,
# MixBreak and Drop. B is the one high-energy name kept out of it: B is the
# pre-chorus, and the tension curve reads it as the run-up *into* the chorus,
# which stops working the moment it is labelled a chorus itself.
# Intro, Interlude, Outro and Chant are the core's transitional sections and
# frame the song rather than carrying it.
#
# Energy level is a second, independent axis. It mirrors isHighEnergySection in
# src/core/section_types.h, which counts B as high energy even though B's
# structural role is the run-up rather than the chorus, so the two axes cannot
# be folded into one field.
HIGH_ENERGY_SECTION_NAMES = {'chorus', 'b', 'mixbreak', 'drop', 'c', 'hook'}

CORE_SECTION_ROLES = {
    'intro': 'instrumental',
    'a': 'verse',
    'b': 'bridge',
    'chorus': 'chorus',
    'bridge': 'bridge',
    'interlude': 'instrumental',
    'outro': 'instrumental',
    'chant': 'instrumental',
    'mixbreak': 'chorus',
    'drop': 'chorus',
    # Short aliases seen in hand-written metadata.
    'c': 'chorus',
    'hook': 'chorus',
    'prechorus': 'bridge',
}

# Notes needed in a bar before its own offset histogram is trusted on its own.
# Below that the surrounding bars are folded in, which keeps sparse bars usable
# without letting a modulated bar borrow the previous key's offset.
MIN_TRANSPOSITION_SAMPLES = 4


def _normalized_section_name(section_type: str) -> str:
    """Section name reduced to the form the lookup tables are keyed by."""
    return section_type.lower().replace('_', '').replace('-', '').replace(' ', '')


def is_high_energy_section_name(section_type: str) -> bool:
    """Whether the generator counts a section by this name as high energy.

    Mirrors isHighEnergySection in src/core/section_types.h. This is what the
    energy-contrast checks measure against; the structural role a section plays
    is a separate question answered by _normalize_section_type.
    """
    value = _normalized_section_name(section_type)
    if value in CORE_SECTION_ROLES:
        return value in HIGH_ENERGY_SECTION_NAMES
    # Foreign labels: fall back to the structural role, which is the best guess
    # available for a name the generator never produced.
    return BaseAnalyzer._normalize_section_type(section_type) == 'chorus'


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
        self._transposition_by_bar = None  # Lazy computed
        self._transposition_overall = 0

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
                'high_energy': is_high_energy_section_name(raw_type),
                'raw_type': raw_type,
            })

        return sections

    @staticmethod
    def _normalize_section_type(section_type: str) -> str:
        """Map a section name to the role the structural checks reason about.

        Every name the generator can emit resolves through the table; anything
        else (imported MIDI, hand-written metadata) falls back to substring
        matching against the same roles.
        """
        value = _normalized_section_name(section_type)
        role = CORE_SECTION_ROLES.get(value)
        if role is not None:
            return role

        # Ordered: a pre-chorus is a run-up, not a chorus, so it has to be
        # recognized before the plain 'chorus' substring claims it.
        if 'prechorus' in value or 'bridge' in value:
            return 'bridge'
        if 'chorus' in value or 'hook' in value or 'drop' in value:
            return 'chorus'
        if 'verse' in value:
            return 'verse'
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

        # Estimated sections carry no name, so the only evidence of energy is
        # the classification just made.
        for section in sections:
            section['high_energy'] = section['type'] == 'chorus'

        return sections

    @staticmethod
    def is_high_energy(section: dict) -> bool:
        """Whether a section is one the generator drives hard.

        Energy checks (verse-versus-chorus contrast, thin chorus, chorus
        density, guitar dynamics) ask this rather than the structural role, so
        that a pre-chorus counts as part of the loud side without becoming a
        chorus for the checks that reason about song structure.
        """
        if 'high_energy' in section:
            return bool(section['high_energy'])
        return section.get('type') == 'chorus'

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

    @staticmethod
    def _internal_pitch(note) -> Optional[int]:
        """Pitch a note held in the generator's internal space, from provenance.

        Provenance records the pitch the generator started from plus the output
        of every recorded transform, all before the song key is applied when the
        song is exported. Returns None when the note carries no usable
        provenance.
        """
        provenance = note.provenance
        if not isinstance(provenance, dict):
            return None
        pitch = provenance.get('original_pitch')
        if not isinstance(pitch, int) or not 0 <= pitch <= 127:
            return None
        transforms = provenance.get('transforms')
        if isinstance(transforms, list):
            for step in transforms:
                if not isinstance(step, dict):
                    continue
                output = step.get('output')
                # Steps that do not move the pitch write a sentinel instead.
                if isinstance(output, int) and 0 <= output <= 127:
                    pitch = output
        return pitch

    @staticmethod
    def _dominant_offset(counts) -> int:
        """Most frequent offset in a histogram, ties resolved to the smallest."""
        if not counts:
            return 0
        return max(counts.items(), key=lambda item: (item[1], -item[0]))[0]

    def vocal_range(self) -> tuple:
        """Range the vocal was generated against, as (low, high).

        Reads the song's own `vocal_low` / `vocal_high` when the metadata
        carries them and falls back to the generator's documented default. A
        wider window than the one the vocal was written to cannot detect the
        range violations it claims to check.
        """
        low, high = DEFAULT_VOCAL_RANGE
        declared_low = self.metadata.get('vocal_low')
        declared_high = self.metadata.get('vocal_high')
        if isinstance(declared_low, int) and not isinstance(declared_low, bool):
            low = declared_low
        if isinstance(declared_high, int) and not isinstance(declared_high, bool):
            high = declared_high
        if low > high:
            return DEFAULT_VOCAL_RANGE
        return low, high

    def is_transposed_channel(self, channel: int) -> bool:
        """Whether the generator applied the key shift to a channel.

        Uses the flag the generator exports; output written before that flag
        existed falls back to the channels that were untransposed then.
        """
        declared = self.metadata.get('transposed_channels')
        if declared is not None:
            return channel in declared
        return channel not in NON_TRANSPOSED_CHANNELS

    def declared_transposition_at(self, tick: int) -> Optional[int]:
        """Shift from the internal space as the generator declared it.

        Returns None for output that does not carry the key, which is what
        sends the reader to the reconstruction below.
        """
        key = self.metadata.get('key')
        if not isinstance(key, int) or isinstance(key, bool):
            return None
        shift = key
        modulation_tick = self.metadata.get('modulation_tick') or 0
        modulation_semitones = self.metadata.get('modulation_semitones') or 0
        if modulation_tick > 0 and tick >= modulation_tick:
            shift += modulation_semitones
        return shift % 12

    def _build_transposition_map(self):
        """Histogram the sounding-versus-internal pitch offset for every bar."""
        by_bar = defaultdict(Counter)
        overall = Counter()
        for note in self.notes:
            if not self.is_transposed_channel(note.channel):
                continue
            internal = self._internal_pitch(note)
            if internal is None:
                continue
            offset = (note.pitch - internal) % 12
            by_bar[tick_to_bar(note.start)][offset] += 1
            overall[offset] += 1
        self._transposition_by_bar = by_bar
        self._transposition_overall = self._dominant_offset(overall)

    def transposition_at(self, tick: int) -> int:
        """Semitone offset from the internal C major space at ``tick``.

        Chord degrees and the degree tables are expressed in the C major space
        the generator composes in, while exported pitches carry the song key and
        any modulation. When the output declares the key, that is the answer and
        nothing is estimated. Otherwise the offset is reconstructed from the
        notes around ``tick``, which carry their internal pitch in their
        provenance; a modulated passage then resolves to its own offset. Output
        with neither yields 0, i.e. it is read as already being internal.
        """
        declared = self.declared_transposition_at(tick)
        if declared is not None:
            return declared

        if self._transposition_by_bar is None:
            self._build_transposition_map()

        bar = tick_to_bar(tick)
        own = self._transposition_by_bar.get(bar)
        if own and sum(own.values()) >= MIN_TRANSPOSITION_SAMPLES:
            return self._dominant_offset(own)

        counts = Counter(own or {})
        for neighbor in (bar - 1, bar + 1):
            counts.update(self._transposition_by_bar.get(neighbor, {}))
        if not counts:
            return self._transposition_overall
        return self._dominant_offset(counts)

    def internal_pitch_class(self, note) -> int:
        """Pitch class of a sounding note, mapped back to the internal C major space.

        Chord-role checks compare against degree tables written in that space,
        so they must classify the note by this value rather than by its
        transposed pitch class. A note on a channel the key was never applied to
        is already internal.
        """
        if not self.is_transposed_channel(note.channel):
            return note.pitch % 12
        return (note.pitch - self.transposition_at(note.start)) % 12

    def vocal_ceiling_at(self, tick: int) -> Optional[int]:
        """Highest vocal pitch sounding at ``tick``, or None during rests."""
        sounding = [
            note.pitch for note in self.notes_by_channel.get(0, [])
            if note.start <= tick < note.end
        ]
        return max(sounding) if sounding else None

    def get_next_chord_change(self, tick: int) -> Optional[int]:
        """Start of the first exported chord entry that begins after ``tick``.

        Returns None when the harmony is not exported or nothing follows.
        """
        chords = self.metadata.get("chords", [])
        if not isinstance(chords, list):
            return None
        starts = []
        for chord in chords:
            if not isinstance(chord, dict):
                continue
            try:
                start = int(chord.get("tick", chord.get("start_ticks")))
            except (TypeError, ValueError):
                continue
            if start > tick:
                starts.append(start)
        return min(starts) if starts else None

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
