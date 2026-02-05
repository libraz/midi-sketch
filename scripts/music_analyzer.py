#!/usr/bin/env python3
"""
music_analyzer.py - Unified Music Analysis Tool for midi-sketch

Comprehensive analysis tool for Claude to investigate music quality issues.
Combines functionality from analyze_music.py and comprehensive_music_analysis.py.

Usage:
  # Quick analysis
  python music_analyzer.py output.json
  python music_analyzer.py output.json --quick
  python music_analyzer.py output.json --track Vocal
  python music_analyzer.py output.json --bar-range 15-20
  python music_analyzer.py output.json --category harmonic

  # Generate + analyze
  python music_analyzer.py --generate --seed 42 --style 3 --bp 1

  # Batch testing
  python music_analyzer.py --batch --seeds 20 --bp all
  python music_analyzer.py --batch --quick
  python music_analyzer.py --batch -j 4
"""

import json
import sys
import argparse
import subprocess
from pathlib import Path
from collections import defaultdict, Counter
from dataclasses import dataclass, field
from typing import List, Dict, Optional, Tuple, Set
from enum import Enum
from concurrent.futures import ThreadPoolExecutor, as_completed
import threading

# =============================================================================
# CONSTANTS
# =============================================================================

TICKS_PER_BEAT = 480
TICKS_PER_BAR = 1920

TRACK_NAMES = {
    0: "Vocal",
    1: "Chord",
    2: "Bass",
    3: "Motif",
    4: "Arpeggio",
    5: "Aux",
    9: "Drums",
    15: "SE"
}

TRACK_CHANNELS = {v: k for k, v in TRACK_NAMES.items()}

NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']

BLUEPRINT_NAMES = {
    0: "Traditional",
    1: "RhythmLock",
    2: "StoryPop",
    3: "Ballad",
    4: "IdolStandard",
    5: "IdolHyper",
    6: "IdolKawaii",
    7: "IdolCoolPop",
    8: "IdolEmo",
}

# C major scale pitch classes
C_MAJOR_SCALE = {0, 2, 4, 5, 7, 9, 11}  # C, D, E, F, G, A, B

# Dissonant intervals
DISSONANT_INTERVALS = {
    1: "minor 2nd",
    2: "major 2nd",
    11: "major 7th",
    13: "minor 9th",  # compound
}

# Track ranges (approximate)
TRACK_RANGES = {
    0: (55, 84),   # Vocal: G3-C6
    3: (48, 84),   # Motif: C3-C6
    5: (48, 84),   # Aux: C3-C6
    2: (28, 60),   # Bass: E1-C4
}


class Severity(Enum):
    ERROR = "error"
    WARNING = "warning"
    INFO = "info"


class Category(Enum):
    MELODIC = "melodic"
    HARMONIC = "harmonic"
    RHYTHM = "rhythm"
    STRUCTURE = "structure"


# =============================================================================
# DATA STRUCTURES
# =============================================================================

def note_name(pitch: int) -> str:
    """Convert MIDI pitch to note name."""
    octave = (pitch // 12) - 1
    return f"{NOTE_NAMES[pitch % 12]}{octave}"


def tick_to_bar_beat(tick: int) -> str:
    """Convert tick to bar:beat format."""
    bar = tick // TICKS_PER_BAR + 1
    beat = (tick % TICKS_PER_BAR) / TICKS_PER_BEAT + 1
    return f"bar{bar}:{beat:.3f}"


def tick_to_bar(tick: int) -> int:
    """Convert tick to bar number (1-indexed)."""
    return tick // TICKS_PER_BAR + 1


@dataclass
class Note:
    """Represents a MIDI note."""
    start: int
    duration: int
    pitch: int
    velocity: int
    channel: int
    provenance: Optional[dict] = None

    @property
    def end(self) -> int:
        return self.start + self.duration

    @property
    def track_name(self) -> str:
        return TRACK_NAMES.get(self.channel, f"Ch{self.channel}")

    @property
    def bar(self) -> int:
        return tick_to_bar(self.start)


@dataclass
class Issue:
    """Represents an analysis issue."""
    severity: Severity
    category: Category
    subcategory: str
    message: str
    tick: int
    track: str = ""
    details: dict = field(default_factory=dict)

    @property
    def bar(self) -> int:
        return tick_to_bar(self.tick)


@dataclass
class QualityScore:
    """Quality scores for music analysis."""
    melodic: float = 100.0
    harmonic: float = 100.0
    rhythm: float = 100.0
    structure: float = 100.0
    overall: float = 100.0
    details: Dict[str, float] = field(default_factory=dict)

    def calculate_overall(self):
        """Calculate weighted overall score."""
        weights = {'melodic': 0.30, 'harmonic': 0.30, 'rhythm': 0.25, 'structure': 0.15}
        self.overall = (
            self.melodic * weights['melodic'] +
            self.harmonic * weights['harmonic'] +
            self.rhythm * weights['rhythm'] +
            self.structure * weights['structure']
        )

    @property
    def grade(self) -> str:
        if self.overall >= 90: return "A"
        if self.overall >= 80: return "B"
        if self.overall >= 70: return "C"
        if self.overall >= 60: return "D"
        return "F"


@dataclass
class HookPattern:
    """Detected hook/motif pattern."""
    start_bar: int
    end_bar: int
    pitches: List[int]
    rhythm: List[int]  # IOIs
    occurrences: List[int]  # start bars of each occurrence
    similarity: float = 0.0


@dataclass
class AnalysisResult:
    """Complete analysis result."""
    notes: List[Note]
    issues: List[Issue]
    score: QualityScore
    hooks: List[HookPattern] = field(default_factory=list)
    energy_curve: List[Tuple[int, float]] = field(default_factory=list)  # (bar, energy)
    metadata: dict = field(default_factory=dict)


@dataclass
class TestResult:
    """Result from batch testing."""
    seed: int
    style: int
    chord: int
    blueprint: int
    score: QualityScore = field(default_factory=QualityScore)
    error_count: int = 0
    warning_count: int = 0
    info_count: int = 0
    error: Optional[str] = None

    def cli_command(self) -> str:
        return (f"./build/bin/midisketch_cli --analyze "
                f"--seed {self.seed} --style {self.style} "
                f"--chord {self.chord} --blueprint {self.blueprint}")


# =============================================================================
# ANALYSIS ENGINE
# =============================================================================

class MusicAnalyzer:
    """Main analysis engine."""

    def __init__(self, notes: List[Note]):
        self.notes = notes
        self.issues: List[Issue] = []
        self.notes_by_channel: Dict[int, List[Note]] = defaultdict(list)

        for note in notes:
            self.notes_by_channel[note.channel].append(note)

        for ch in self.notes_by_channel:
            self.notes_by_channel[ch].sort(key=lambda n: (n.start, n.pitch))

    def analyze_all(self) -> AnalysisResult:
        """Run all analyses."""
        # Melodic analysis
        self._analyze_isolated_notes()
        self._analyze_consecutive_same_pitch()
        self._analyze_melodic_range()
        self._analyze_melodic_leaps()
        self._analyze_melodic_contour()

        # Harmonic analysis
        self._analyze_dissonance()
        self._analyze_chord_voicing()
        self._analyze_bass_line()
        self._analyze_chord_function()

        # Rhythm analysis
        self._analyze_rhythm_patterns()
        self._analyze_note_density()
        self._analyze_rhythmic_monotony()
        self._analyze_beat_alignment()
        self._analyze_syncopation()

        # Structure analysis
        self._analyze_phrase_structure()
        self._analyze_track_balance()

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
        )

    # =========================================================================
    # MELODIC ANALYSIS
    # =========================================================================

    def _analyze_isolated_notes(self):
        """Detect isolated notes in melodic tracks."""
        melodic_channels = [0, 3, 5]  # Vocal, Motif, Aux
        isolation_threshold = TICKS_PER_BAR

        for ch in melodic_channels:
            notes = self.notes_by_channel.get(ch, [])
            if len(notes) < 2:
                continue

            track_name = TRACK_NAMES.get(ch, f"Ch{ch}")

            for i, note in enumerate(notes):
                prev_end = notes[i-1].end if i > 0 else 0
                next_start = notes[i+1].start if i < len(notes) - 1 else note.end + isolation_threshold * 2

                gap_before = note.start - prev_end
                gap_after = next_start - note.end

                if gap_before >= isolation_threshold and gap_after >= isolation_threshold:
                    self.issues.append(Issue(
                        severity=Severity.WARNING,
                        category=Category.MELODIC,
                        subcategory="isolated_note",
                        message=f"Isolated {note_name(note.pitch)} (gaps: {gap_before/TICKS_PER_BAR:.1f}/{gap_after/TICKS_PER_BAR:.1f} bars)",
                        tick=note.start,
                        track=track_name,
                        details={"pitch": note.pitch, "gap_before": gap_before, "gap_after": gap_after}
                    ))

    def _analyze_consecutive_same_pitch(self):
        """Detect consecutive same-pitch notes."""
        melodic_channels = [0, 3, 5]
        threshold = 4  # 4+ is problematic, 6+ is error

        for ch in melodic_channels:
            notes = self.notes_by_channel.get(ch, [])
            if len(notes) < threshold:
                continue

            track_name = TRACK_NAMES.get(ch, f"Ch{ch}")
            consecutive_count = 1
            current_pitch = notes[0].pitch
            start_tick = notes[0].start

            for i in range(1, len(notes)):
                gap = notes[i].start - notes[i-1].end
                if notes[i].pitch == current_pitch and gap < TICKS_PER_BEAT * 2:
                    consecutive_count += 1
                else:
                    if consecutive_count >= threshold:
                        severity = Severity.ERROR if consecutive_count >= 6 else Severity.WARNING
                        self.issues.append(Issue(
                            severity=severity,
                            category=Category.MELODIC,
                            subcategory="consecutive_same_pitch",
                            message=f"{consecutive_count} consecutive {note_name(current_pitch)}",
                            tick=start_tick,
                            track=track_name,
                            details={"pitch": current_pitch, "count": consecutive_count}
                        ))
                    consecutive_count = 1
                    current_pitch = notes[i].pitch
                    start_tick = notes[i].start

            # Check last sequence
            if consecutive_count >= threshold:
                severity = Severity.ERROR if consecutive_count >= 6 else Severity.WARNING
                self.issues.append(Issue(
                    severity=severity,
                    category=Category.MELODIC,
                    subcategory="consecutive_same_pitch",
                    message=f"{consecutive_count} consecutive {note_name(current_pitch)}",
                    tick=start_tick,
                    track=track_name,
                    details={"pitch": current_pitch, "count": consecutive_count}
                ))

    def _analyze_melodic_range(self):
        """Check if melodic lines stay within appropriate ranges."""
        for ch, (low, high) in TRACK_RANGES.items():
            notes = self.notes_by_channel.get(ch, [])
            track_name = TRACK_NAMES.get(ch, f"Ch{ch}")

            for note in notes:
                if note.pitch < low:
                    self.issues.append(Issue(
                        severity=Severity.WARNING,
                        category=Category.MELODIC,
                        subcategory="range_low",
                        message=f"{note_name(note.pitch)} below range (min: {note_name(low)})",
                        tick=note.start,
                        track=track_name,
                        details={"pitch": note.pitch, "expected_low": low}
                    ))
                elif note.pitch > high:
                    self.issues.append(Issue(
                        severity=Severity.WARNING,
                        category=Category.MELODIC,
                        subcategory="range_high",
                        message=f"{note_name(note.pitch)} above range (max: {note_name(high)})",
                        tick=note.start,
                        track=track_name,
                        details={"pitch": note.pitch, "expected_high": high}
                    ))

    def _analyze_melodic_leaps(self):
        """Detect awkward melodic leaps."""
        melodic_channels = [0, 3, 5]
        leap_threshold = 12  # Octave

        for ch in melodic_channels:
            notes = self.notes_by_channel.get(ch, [])
            if len(notes) < 2:
                continue

            track_name = TRACK_NAMES.get(ch, f"Ch{ch}")

            for i in range(1, len(notes)):
                gap = notes[i].start - notes[i-1].end
                if gap > TICKS_PER_BEAT * 2:
                    continue  # Different phrase

                interval = abs(notes[i].pitch - notes[i-1].pitch)
                if interval > leap_threshold:
                    # Check if resolved
                    is_resolved = False
                    if i < len(notes) - 1:
                        next_gap = notes[i+1].start - notes[i].end
                        if next_gap < TICKS_PER_BEAT * 2:
                            next_interval = abs(notes[i+1].pitch - notes[i].pitch)
                            direction_change = (notes[i].pitch - notes[i-1].pitch) * (notes[i+1].pitch - notes[i].pitch) < 0
                            if next_interval <= 4 and direction_change:
                                is_resolved = True

                    if not is_resolved:
                        severity = Severity.WARNING if interval <= 14 else Severity.ERROR
                        direction = "up" if notes[i].pitch > notes[i-1].pitch else "down"
                        self.issues.append(Issue(
                            severity=severity,
                            category=Category.MELODIC,
                            subcategory="large_leap",
                            message=f"Large leap ({interval} semitones {direction})",
                            tick=notes[i].start,
                            track=track_name,
                            details={"interval": interval, "direction": direction}
                        ))

    def _analyze_melodic_contour(self):
        """Detect monotonous melodic contours."""
        melodic_channels = [0, 3, 5]
        monotonous_threshold = 6

        for ch in melodic_channels:
            notes = self.notes_by_channel.get(ch, [])
            if len(notes) < monotonous_threshold:
                continue

            track_name = TRACK_NAMES.get(ch, f"Ch{ch}")
            direction_count = 0
            current_direction = 0
            start_tick = notes[0].start

            for i in range(1, len(notes)):
                gap = notes[i].start - notes[i-1].end
                if gap > TICKS_PER_BEAT * 2:
                    direction_count = 0
                    current_direction = 0
                    start_tick = notes[i].start
                    continue

                diff = notes[i].pitch - notes[i-1].pitch
                if diff == 0:
                    continue

                direction = 1 if diff > 0 else -1
                if direction == current_direction:
                    direction_count += 1
                else:
                    if direction_count >= monotonous_threshold:
                        dir_name = "ascending" if current_direction > 0 else "descending"
                        self.issues.append(Issue(
                            severity=Severity.INFO,
                            category=Category.MELODIC,
                            subcategory="monotonous_contour",
                            message=f"{direction_count} notes continuously {dir_name}",
                            tick=start_tick,
                            track=track_name,
                            details={"count": direction_count, "direction": dir_name}
                        ))
                    direction_count = 1
                    current_direction = direction
                    start_tick = notes[i-1].start

    # =========================================================================
    # HARMONIC ANALYSIS
    # =========================================================================

    def _analyze_dissonance(self):
        """Detect dissonant intervals between simultaneous notes."""
        time_slices = defaultdict(list)
        for note in self.notes:
            if note.channel == 9:  # Skip drums
                continue
            for t in range(note.start, note.end, TICKS_PER_BEAT // 2):
                time_slices[t].append(note)

        checked_pairs = set()

        for tick, active_notes in time_slices.items():
            pitch_info = [(n.pitch, n.channel, n) for n in active_notes if n.start <= tick < n.end]

            for i, (p1, ch1, n1) in enumerate(pitch_info):
                for p2, ch2, n2 in pitch_info[i+1:]:
                    if ch1 == ch2:
                        continue

                    raw_interval = abs(p1 - p2)
                    interval = raw_interval % 12
                    pair_key = (min(n1.start, n2.start), p1, p2, ch1, ch2)

                    if pair_key in checked_pairs:
                        continue
                    checked_pairs.add(pair_key)

                    # Only flag close voicing (within 12 semitones)
                    if raw_interval > 12 and interval in [1, 2]:
                        continue

                    if interval in DISSONANT_INTERVALS:
                        is_bass_collision = (ch1 == 2 or ch2 == 2) and min(p1, p2) < 60
                        severity = Severity.ERROR if is_bass_collision else Severity.WARNING

                        track1 = TRACK_NAMES.get(ch1, f"Ch{ch1}")
                        track2 = TRACK_NAMES.get(ch2, f"Ch{ch2}")
                        self.issues.append(Issue(
                            severity=severity,
                            category=Category.HARMONIC,
                            subcategory="dissonance",
                            message=f"{DISSONANT_INTERVALS[interval]}: {track1} {note_name(p1)} vs {track2} {note_name(p2)}",
                            tick=tick,
                            track=f"{track1}/{track2}",
                            details={
                                "interval": DISSONANT_INTERVALS[interval],
                                "interval_semitones": raw_interval,
                                "track1": track1, "track2": track2,
                                "pitch1": p1, "pitch2": p2,
                            }
                        ))

    def _analyze_chord_voicing(self):
        """Analyze chord track for voicing issues."""
        chord_notes = self.notes_by_channel.get(1, [])
        if not chord_notes:
            return

        # Get vocal ceiling for comparison
        vocal_notes = self.notes_by_channel.get(0, [])
        vocal_ceiling = max((n.pitch for n in vocal_notes), default=84) if vocal_notes else 84

        chords_by_time = defaultdict(list)
        for note in chord_notes:
            chords_by_time[note.start].append(note)

        sorted_ticks = sorted(chords_by_time.keys())
        prev_pitches = None
        consecutive_same_count = 0
        consecutive_same_start = 0

        for tick in sorted_ticks:
            chord = chords_by_time[tick]
            pitches = tuple(sorted([n.pitch for n in chord]))
            voicing_count = len(pitches)

            # Check for thin voicing (1-2 voices)
            if voicing_count == 1:
                self.issues.append(Issue(
                    severity=Severity.WARNING,
                    category=Category.HARMONIC,
                    subcategory="thin_voicing",
                    message=f"Only 1 voice ({note_name(pitches[0])})",
                    tick=tick,
                    track="Chord",
                    details={"voice_count": 1, "pitches": list(pitches)}
                ))
            elif voicing_count == 2:
                self.issues.append(Issue(
                    severity=Severity.INFO,
                    category=Category.HARMONIC,
                    subcategory="thin_voicing",
                    message=f"Only 2 voices ({note_name(pitches[0])}, {note_name(pitches[1])})",
                    tick=tick,
                    track="Chord",
                    details={"voice_count": 2, "pitches": list(pitches)}
                ))

            if voicing_count > 5:
                self.issues.append(Issue(
                    severity=Severity.INFO,
                    category=Category.HARMONIC,
                    subcategory="dense_voicing",
                    message=f"Dense voicing ({voicing_count} voices)",
                    tick=tick,
                    track="Chord",
                    details={"voice_count": voicing_count}
                ))

            if pitches:
                lowest, highest = min(pitches), max(pitches)

                # Check absolute register limits
                if lowest < 48:  # Below C3
                    self.issues.append(Issue(
                        severity=Severity.WARNING,
                        category=Category.HARMONIC,
                        subcategory="chord_register_low",
                        message=f"Low register ({note_name(lowest)})",
                        tick=tick,
                        track="Chord",
                        details={"lowest_pitch": lowest}
                    ))
                if highest > 84:  # Above C6
                    self.issues.append(Issue(
                        severity=Severity.WARNING,
                        category=Category.HARMONIC,
                        subcategory="chord_register_high",
                        message=f"High register ({note_name(highest)})",
                        tick=tick,
                        track="Chord",
                        details={"highest_pitch": highest}
                    ))

                # Check if chord exceeds vocal ceiling
                if highest > vocal_ceiling + 2:  # Allow 2 semitone tolerance
                    self.issues.append(Issue(
                        severity=Severity.WARNING,
                        category=Category.HARMONIC,
                        subcategory="chord_above_vocal",
                        message=f"Chord ({note_name(highest)}) exceeds vocal ceiling ({note_name(vocal_ceiling)})",
                        tick=tick,
                        track="Chord",
                        details={"chord_highest": highest, "vocal_ceiling": vocal_ceiling}
                    ))

            # Check for consecutive same voicing
            if pitches == prev_pitches:
                if consecutive_same_count == 0:
                    consecutive_same_start = tick
                consecutive_same_count += 1
            else:
                if consecutive_same_count >= 4:  # 4+ consecutive same voicings
                    self.issues.append(Issue(
                        severity=Severity.WARNING if consecutive_same_count >= 6 else Severity.INFO,
                        category=Category.HARMONIC,
                        subcategory="chord_repetition",
                        message=f"{consecutive_same_count + 1} consecutive same voicing",
                        tick=consecutive_same_start,
                        track="Chord",
                        details={"count": consecutive_same_count + 1, "pitches": list(prev_pitches) if prev_pitches else []}
                    ))
                consecutive_same_count = 0
                prev_pitches = pitches

        # Check last sequence
        if consecutive_same_count >= 4:
            self.issues.append(Issue(
                severity=Severity.WARNING if consecutive_same_count >= 6 else Severity.INFO,
                category=Category.HARMONIC,
                subcategory="chord_repetition",
                message=f"{consecutive_same_count + 1} consecutive same voicing",
                tick=consecutive_same_start,
                track="Chord",
                details={"count": consecutive_same_count + 1, "pitches": list(prev_pitches) if prev_pitches else []}
            ))

    def _analyze_bass_line(self):
        """Analyze bass line for issues."""
        bass_notes = self.notes_by_channel.get(2, [])
        if len(bass_notes) < 2:
            return

        # Consecutive same notes
        consecutive_count = 1
        current_pitch = bass_notes[0].pitch
        start_tick = bass_notes[0].start

        for i in range(1, len(bass_notes)):
            if bass_notes[i].pitch == current_pitch:
                consecutive_count += 1
            else:
                if consecutive_count >= 8:
                    self.issues.append(Issue(
                        severity=Severity.WARNING,
                        category=Category.HARMONIC,
                        subcategory="bass_monotony",
                        message=f"{consecutive_count} consecutive {note_name(current_pitch)}",
                        tick=start_tick,
                        track="Bass",
                        details={"pitch": current_pitch, "count": consecutive_count}
                    ))
                consecutive_count = 1
                current_pitch = bass_notes[i].pitch
                start_tick = bass_notes[i].start

        # Check last sequence
        if consecutive_count >= 8:
            self.issues.append(Issue(
                severity=Severity.WARNING,
                category=Category.HARMONIC,
                subcategory="bass_monotony",
                message=f"{consecutive_count} consecutive {note_name(current_pitch)}",
                tick=start_tick,
                track="Bass",
                details={"pitch": current_pitch, "count": consecutive_count}
            ))

    def _analyze_chord_function(self):
        """Analyze chord function progression (T/D/S)."""
        # Simplified chord function analysis based on bass notes
        bass_notes = self.notes_by_channel.get(2, [])
        if len(bass_notes) < 4:
            return

        # Group by bar
        bass_by_bar = defaultdict(list)
        for note in bass_notes:
            bar = tick_to_bar(note.start)
            bass_by_bar[bar].append(note.pitch % 12)

        # Determine function: T(C,Am,Em), D(G,Bdim), S(F,Dm)
        def get_function(root: int) -> str:
            if root in [0, 9, 4]:  # C, Am, Em
                return "T"
            elif root in [7, 11]:  # G, Bdim
                return "D"
            elif root in [5, 2]:  # F, Dm
                return "S"
            return "?"

        bars = sorted(bass_by_bar.keys())
        prev_func = None
        for bar in bars:
            roots = bass_by_bar[bar]
            if not roots:
                continue
            root = min(roots)  # Approximate root as lowest
            func = get_function(root)

            # Check for awkward D->S motion (retrograde)
            if prev_func == "D" and func == "S":
                self.issues.append(Issue(
                    severity=Severity.INFO,
                    category=Category.HARMONIC,
                    subcategory="chord_function",
                    message=f"D->S retrograde motion",
                    tick=(bar - 1) * TICKS_PER_BAR,
                    track="Harmony",
                    details={"bar": bar, "motion": "D->S"}
                ))

            prev_func = func

    # =========================================================================
    # RHYTHM ANALYSIS
    # =========================================================================

    def _analyze_rhythm_patterns(self):
        """Analyze rhythm consistency across tracks."""
        sync_channels = [0, 3, 5]  # Vocal, Motif, Aux

        attacks_by_bar_channel = defaultdict(lambda: defaultdict(set))
        for ch in sync_channels:
            for note in self.notes_by_channel.get(ch, []):
                bar = tick_to_bar(note.start)
                beat_pos = note.start % TICKS_PER_BAR
                attacks_by_bar_channel[bar][ch].add(beat_pos)

        for bar in sorted(attacks_by_bar_channel.keys()):
            bar_attacks = attacks_by_bar_channel[bar]
            motif_attacks = bar_attacks.get(3, set())
            vocal_attacks = bar_attacks.get(0, set())

            if motif_attacks and vocal_attacks:
                overlap = len(motif_attacks & vocal_attacks)
                total = len(motif_attacks | vocal_attacks)
                if total > 0:
                    sync_ratio = overlap / total
                    if sync_ratio < 0.2 and len(motif_attacks) > 3 and len(vocal_attacks) > 3:
                        self.issues.append(Issue(
                            severity=Severity.INFO,
                            category=Category.RHYTHM,
                            subcategory="low_sync",
                            message=f"Low Motif/Vocal sync (ratio: {sync_ratio:.2f})",
                            tick=bar * TICKS_PER_BAR,
                            track="Motif/Vocal",
                            details={"bar": bar, "sync_ratio": sync_ratio}
                        ))

    def _analyze_note_density(self):
        """Detect bars with unusually high note density."""
        melodic_channels = [0, 3, 5]

        for ch in melodic_channels:
            notes = self.notes_by_channel.get(ch, [])
            if not notes:
                continue

            track_name = TRACK_NAMES.get(ch, f"Ch{ch}")
            notes_per_bar = defaultdict(int)

            for note in notes:
                bar = tick_to_bar(note.start)
                notes_per_bar[bar] += 1

            if not notes_per_bar:
                continue

            avg_density = sum(notes_per_bar.values()) / len(notes_per_bar)

            for bar, count in notes_per_bar.items():
                if count > avg_density * 3 and count > 16:
                    self.issues.append(Issue(
                        severity=Severity.WARNING,
                        category=Category.RHYTHM,
                        subcategory="high_density",
                        message=f"High density ({count} notes, avg: {avg_density:.1f})",
                        tick=bar * TICKS_PER_BAR,
                        track=track_name,
                        details={"bar": bar, "count": count, "average": avg_density}
                    ))

    def _analyze_rhythmic_monotony(self):
        """Detect repetitive rhythmic patterns."""
        melodic_channels = [0, 3]  # Vocal, Motif

        for ch in melodic_channels:
            notes = self.notes_by_channel.get(ch, [])
            if len(notes) < 8:
                continue

            track_name = TRACK_NAMES.get(ch, f"Ch{ch}")

            # Extract inter-onset intervals
            ioi_list = []
            for i in range(1, len(notes)):
                ioi = notes[i].start - notes[i-1].start
                ioi_list.append(ioi)

            if len(ioi_list) >= 8:
                # Quantize to 16th note grid
                quantized = [round(ioi / (TICKS_PER_BEAT / 4)) for ioi in ioi_list]

                same_ioi_count = 1
                current_ioi = quantized[0]
                for i in range(1, len(quantized)):
                    if quantized[i] == current_ioi:
                        same_ioi_count += 1
                    else:
                        if same_ioi_count >= 8:
                            self.issues.append(Issue(
                                severity=Severity.WARNING,
                                category=Category.RHYTHM,
                                subcategory="rhythmic_monotony",
                                message=f"{same_ioi_count} notes with same spacing",
                                tick=notes[i - same_ioi_count].start,
                                track=track_name,
                                details={"count": same_ioi_count, "ioi": current_ioi}
                            ))
                        same_ioi_count = 1
                        current_ioi = quantized[i]

    def _analyze_beat_alignment(self):
        """Detect notes off the beat grid."""
        melodic_channels = [0, 1, 2, 3, 5]
        grid_resolution = TICKS_PER_BEAT // 4  # 16th note

        for ch in melodic_channels:
            notes = self.notes_by_channel.get(ch, [])
            if not notes:
                continue

            track_name = TRACK_NAMES.get(ch, f"Ch{ch}")
            off_grid_notes = []

            for note in notes:
                remainder = note.start % grid_resolution
                tolerance = 10
                if remainder > tolerance and remainder < grid_resolution - tolerance:
                    off_grid_notes.append(note)

            if len(off_grid_notes) > 5:
                off_grid_ratio = len(off_grid_notes) / len(notes)
                if off_grid_ratio > 0.1:
                    self.issues.append(Issue(
                        severity=Severity.WARNING,
                        category=Category.RHYTHM,
                        subcategory="beat_misalignment",
                        message=f"{len(off_grid_notes)} notes ({off_grid_ratio*100:.1f}%) off 16th grid",
                        tick=off_grid_notes[0].start,
                        track=track_name,
                        details={"off_grid_count": len(off_grid_notes), "total": len(notes)}
                    ))

    def _analyze_syncopation(self):
        """Detect syncopation issues."""
        melodic_channels = [0, 3]

        for ch in melodic_channels:
            notes = self.notes_by_channel.get(ch, [])
            if len(notes) < 4:
                continue

            track_name = TRACK_NAMES.get(ch, f"Ch{ch}")

            beat_positions = defaultdict(int)
            for note in notes:
                beat_in_bar = (note.start % TICKS_PER_BAR) // TICKS_PER_BEAT
                beat_positions[beat_in_bar] += 1

            total = sum(beat_positions.values())
            if total == 0:
                continue

            beat1_ratio = beat_positions[0] / total
            if beat1_ratio < 0.05 and total > 20:
                self.issues.append(Issue(
                    severity=Severity.WARNING,
                    category=Category.RHYTHM,
                    subcategory="weak_downbeat",
                    message=f"Few notes on beat 1 ({beat1_ratio*100:.1f}%)",
                    tick=0,
                    track=track_name,
                    details={"beat1_ratio": beat1_ratio}
                ))

    # =========================================================================
    # STRUCTURE ANALYSIS
    # =========================================================================

    def _analyze_phrase_structure(self):
        """Analyze phrase lengths."""
        vocal_notes = self.notes_by_channel.get(0, [])
        if len(vocal_notes) < 4:
            return

        phrase_gap = TICKS_PER_BEAT
        phrases = []
        current_phrase_start = vocal_notes[0].start
        current_phrase_notes = 1

        for i in range(1, len(vocal_notes)):
            gap = vocal_notes[i].start - vocal_notes[i-1].end
            if gap >= phrase_gap:
                phrase_duration = vocal_notes[i-1].end - current_phrase_start
                phrases.append({
                    'start': current_phrase_start,
                    'duration': phrase_duration,
                    'note_count': current_phrase_notes
                })
                current_phrase_start = vocal_notes[i].start
                current_phrase_notes = 1
            else:
                current_phrase_notes += 1

        for phrase in phrases:
            if phrase['note_count'] <= 2 and phrase['duration'] < TICKS_PER_BEAT * 2:
                self.issues.append(Issue(
                    severity=Severity.INFO,
                    category=Category.STRUCTURE,
                    subcategory="short_phrase",
                    message=f"Very short phrase ({phrase['note_count']} notes)",
                    tick=phrase['start'],
                    track="Vocal",
                    details={"note_count": phrase['note_count']}
                ))
            elif phrase['note_count'] > 20:
                self.issues.append(Issue(
                    severity=Severity.INFO,
                    category=Category.STRUCTURE,
                    subcategory="long_phrase",
                    message=f"Long phrase ({phrase['note_count']} notes)",
                    tick=phrase['start'],
                    track="Vocal",
                    details={"note_count": phrase['note_count']}
                ))

    def _analyze_track_balance(self):
        """Analyze balance between tracks."""
        track_note_counts = {}
        for ch, notes in self.notes_by_channel.items():
            if ch != 9:  # Exclude drums
                track_note_counts[TRACK_NAMES.get(ch, f"Ch{ch}")] = len(notes)

        if not track_note_counts:
            return

        total_notes = sum(track_note_counts.values())
        if total_notes == 0:
            return

        for track, count in track_note_counts.items():
            ratio = count / total_notes
            if ratio > 0.5 and count > 100:
                self.issues.append(Issue(
                    severity=Severity.INFO,
                    category=Category.STRUCTURE,
                    subcategory="track_dominance",
                    message=f"Dominates with {ratio*100:.1f}% of notes ({count})",
                    tick=0,
                    track=track,
                    details={"count": count, "ratio": ratio}
                ))

        for ch in [0, 3, 5]:
            track_name = TRACK_NAMES.get(ch, f"Ch{ch}")
            if track_name not in track_note_counts or track_note_counts[track_name] == 0:
                self.issues.append(Issue(
                    severity=Severity.WARNING,
                    category=Category.STRUCTURE,
                    subcategory="empty_track",
                    message=f"Track is empty",
                    tick=0,
                    track=track_name,
                    details={}
                ))

    # =========================================================================
    # EXTENDED ANALYSIS
    # =========================================================================

    def _detect_hooks(self) -> List[HookPattern]:
        """Detect repeating melodic patterns (hooks)."""
        vocal_notes = self.notes_by_channel.get(0, [])
        if len(vocal_notes) < 8:
            return []

        hooks = []

        # Extract pitch sequences by bar
        pitches_by_bar = defaultdict(list)
        for note in vocal_notes:
            bar = tick_to_bar(note.start)
            pitches_by_bar[bar].append(note.pitch)

        bars = sorted(pitches_by_bar.keys())
        if len(bars) < 8:
            return hooks

        # Look for 4-bar patterns that repeat
        for start in range(len(bars) - 7):
            pattern_bars = bars[start:start+4]
            pattern = tuple(tuple(pitches_by_bar[b]) for b in pattern_bars)

            if not pattern or all(len(p) == 0 for p in pattern):
                continue

            occurrences = [pattern_bars[0]]

            # Search for repetitions
            for check_start in range(start + 4, len(bars) - 3):
                check_bars = bars[check_start:check_start+4]
                check_pattern = tuple(tuple(pitches_by_bar[b]) for b in check_bars)

                if check_pattern == pattern:
                    occurrences.append(check_bars[0])

            if len(occurrences) >= 2:
                flat_pitches = [p for bar_pitches in pattern for p in bar_pitches]
                hooks.append(HookPattern(
                    start_bar=pattern_bars[0],
                    end_bar=pattern_bars[-1],
                    pitches=flat_pitches,
                    rhythm=[],
                    occurrences=occurrences,
                    similarity=1.0,
                ))
                break  # Only report first hook found

        return hooks

    def _calculate_energy_curve(self) -> List[Tuple[int, float]]:
        """Calculate energy curve (velocity + density) per bar."""
        energy_curve = []

        max_bar = max((tick_to_bar(n.start) for n in self.notes), default=0)

        for bar in range(1, max_bar + 1):
            bar_start = (bar - 1) * TICKS_PER_BAR
            bar_end = bar * TICKS_PER_BAR

            bar_notes = [n for n in self.notes if bar_start <= n.start < bar_end and n.channel != 9]
            if not bar_notes:
                energy_curve.append((bar, 0.0))
                continue

            avg_velocity = sum(n.velocity for n in bar_notes) / len(bar_notes)
            density = len(bar_notes)

            # Normalize: velocity (0-127) -> 0-1, density (0-50) -> 0-1
            energy = (avg_velocity / 127) * 0.6 + min(density / 50, 1.0) * 0.4
            energy_curve.append((bar, energy))

        return energy_curve

    # =========================================================================
    # SCORING
    # =========================================================================

    def _calculate_scores(self) -> QualityScore:
        """Calculate quality scores based on issues."""
        score = QualityScore()

        penalties = {
            Category.MELODIC: {
                'consecutive_same_pitch': {Severity.ERROR: 4.0, Severity.WARNING: 1.5, Severity.INFO: 0.3},
                'isolated_note': {Severity.ERROR: 2.0, Severity.WARNING: 1.0, Severity.INFO: 0.3},
                'large_leap': {Severity.ERROR: 2.5, Severity.WARNING: 1.0, Severity.INFO: 0.3},
                'range_low': {Severity.ERROR: 1.5, Severity.WARNING: 0.5, Severity.INFO: 0.1},
                'range_high': {Severity.ERROR: 1.5, Severity.WARNING: 0.5, Severity.INFO: 0.1},
                'monotonous_contour': {Severity.ERROR: 1.0, Severity.WARNING: 0.5, Severity.INFO: 0.2},
            },
            Category.HARMONIC: {
                'dissonance': {Severity.ERROR: 10.0, Severity.WARNING: 5.0, Severity.INFO: 1.0},
                'thin_voicing': {Severity.ERROR: 2.0, Severity.WARNING: 0.8, Severity.INFO: 0.2},
                'dense_voicing': {Severity.ERROR: 0.5, Severity.WARNING: 0.3, Severity.INFO: 0.1},
                'chord_register_low': {Severity.ERROR: 2.0, Severity.WARNING: 1.0, Severity.INFO: 0.2},
                'chord_register_high': {Severity.ERROR: 1.5, Severity.WARNING: 0.8, Severity.INFO: 0.2},
                'chord_above_vocal': {Severity.ERROR: 2.0, Severity.WARNING: 1.0, Severity.INFO: 0.3},
                'chord_repetition': {Severity.ERROR: 1.5, Severity.WARNING: 0.8, Severity.INFO: 0.2},
                'bass_monotony': {Severity.ERROR: 2.0, Severity.WARNING: 1.0, Severity.INFO: 0.3},
                'chord_function': {Severity.ERROR: 1.0, Severity.WARNING: 0.5, Severity.INFO: 0.2},
            },
            Category.RHYTHM: {
                'low_sync': {Severity.ERROR: 1.5, Severity.WARNING: 0.8, Severity.INFO: 0.2},
                'high_density': {Severity.ERROR: 2.0, Severity.WARNING: 1.0, Severity.INFO: 0.3},
                'rhythmic_monotony': {Severity.ERROR: 2.0, Severity.WARNING: 1.0, Severity.INFO: 0.3},
                'beat_misalignment': {Severity.ERROR: 3.0, Severity.WARNING: 1.5, Severity.INFO: 0.3},
                'weak_downbeat': {Severity.ERROR: 2.0, Severity.WARNING: 1.0, Severity.INFO: 0.3},
            },
            Category.STRUCTURE: {
                'short_phrase': {Severity.ERROR: 1.5, Severity.WARNING: 0.8, Severity.INFO: 0.2},
                'long_phrase': {Severity.ERROR: 0.5, Severity.WARNING: 0.3, Severity.INFO: 0.1},
                'track_dominance': {Severity.ERROR: 1.0, Severity.WARNING: 0.5, Severity.INFO: 0.1},
                'empty_track': {Severity.ERROR: 3.0, Severity.WARNING: 1.5, Severity.INFO: 0.3},
            }
        }

        category_penalties = defaultdict(float)
        subcategory_counts = defaultdict(int)

        for issue in self.issues:
            cat = issue.category
            subcat = issue.subcategory
            sev = issue.severity

            if cat in penalties and subcat in penalties[cat]:
                penalty = penalties[cat][subcat].get(sev, 0.5)
                category_penalties[cat] += penalty
                subcategory_counts[f"{cat.value}.{subcat}"] += 1

        total_notes = len(self.notes)
        note_factor = max(1, total_notes / 500)

        score.melodic = max(0, 100 - category_penalties[Category.MELODIC] / note_factor * 5)
        score.harmonic = max(0, 100 - category_penalties[Category.HARMONIC] / note_factor * 8)
        score.rhythm = max(0, 100 - category_penalties[Category.RHYTHM] / note_factor * 10)
        score.structure = max(0, 100 - category_penalties[Category.STRUCTURE] / note_factor * 15)

        score.details = {
            'melodic_penalty': category_penalties[Category.MELODIC],
            'harmonic_penalty': category_penalties[Category.HARMONIC],
            'rhythm_penalty': category_penalties[Category.RHYTHM],
            'structure_penalty': category_penalties[Category.STRUCTURE],
            'total_notes': total_notes,
            'note_factor': note_factor,
        }
        for key, count in subcategory_counts.items():
            score.details[f"count_{key}"] = count

        score.calculate_overall()
        return score


# =============================================================================
# FILTERING
# =============================================================================

def apply_filters(result: AnalysisResult, filters: dict) -> AnalysisResult:
    """Apply filters to analysis result."""
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

        # Category filter
        if filters.get('category'):
            if issue.category.value != filters['category'].lower():
                include = False

        # Severity filter
        if filters.get('severity'):
            sev_map = {'error': Severity.ERROR, 'warning': Severity.WARNING, 'info': Severity.INFO}
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
    """Format analysis results for output."""

    @staticmethod
    def format_quick(result: AnalysisResult, filepath: str) -> str:
        """Format quick summary output."""
        lines = []
        s = result.score
        lines.append(f"=== MUSIC ANALYSIS: {filepath} ===")
        lines.append(f"Score: {s.overall:.1f} ({s.grade}) | Melodic: {s.melodic:.0f} | Harmonic: {s.harmonic:.0f} | Rhythm: {s.rhythm:.0f} | Structure: {s.structure:.0f}")
        lines.append("")

        errors = [i for i in result.issues if i.severity == Severity.ERROR]
        warnings = [i for i in result.issues if i.severity == Severity.WARNING]

        if errors:
            lines.append(f"ERRORS ({len(errors)}):")
            for issue in errors[:10]:
                lines.append(f"  [!!] {issue.track}: {issue.message} at {tick_to_bar_beat(issue.tick)}")
            if len(errors) > 10:
                lines.append(f"  ... ({len(errors) - 10} more)")
            lines.append("")

        if warnings:
            lines.append(f"WARNINGS ({len(warnings)}):")
            for issue in warnings[:10]:
                lines.append(f"  [!] {issue.track}: {issue.message} at {tick_to_bar_beat(issue.tick)}")
            if len(warnings) > 10:
                lines.append(f"  ... ({len(warnings) - 10} more)")

        return "\n".join(lines)

    @staticmethod
    def format_track(result: AnalysisResult, track_name: str) -> str:
        """Format track-specific analysis."""
        lines = []
        lines.append(f"=== {track_name.upper()} TRACK ANALYSIS ===")

        # Get track channel
        channel = TRACK_CHANNELS.get(track_name, -1)

        # Find notes for this track
        track_notes = [n for n in result.notes if n.track_name.lower() == track_name.lower()]
        if not track_notes:
            lines.append(f"No notes found for track: {track_name}")
            return "\n".join(lines)

        pitches = [n.pitch for n in track_notes]
        velocities = [n.velocity for n in track_notes]

        lines.append(f"Notes: {len(track_notes)} | Range: {note_name(min(pitches))}-{note_name(max(pitches))} | Avg velocity: {sum(velocities)/len(velocities):.0f}")
        lines.append("")

        # Filter issues for this track
        track_issues = [i for i in result.issues if track_name.lower() in i.track.lower()]
        if track_issues:
            lines.append(f"Issues ({len(track_issues)}):")
            for issue in track_issues[:20]:
                marker = "!!" if issue.severity == Severity.ERROR else "!" if issue.severity == Severity.WARNING else "-"
                lines.append(f"  [{marker}] bar {tick_to_bar(issue.tick)}: {issue.message}")
            if len(track_issues) > 20:
                lines.append(f"  ... ({len(track_issues) - 20} more)")
        else:
            lines.append("No issues found.")

        # Hook analysis for vocal
        if track_name.lower() == "vocal" and result.hooks:
            lines.append("")
            lines.append("Hook Analysis:")
            for i, hook in enumerate(result.hooks, 1):
                occ_str = ", ".join(str(b) for b in hook.occurrences)
                lines.append(f"  Hook {i}: bars {hook.start_bar}-{hook.end_bar} repeats at bars [{occ_str}]")

        return "\n".join(lines)

    @staticmethod
    def format_full(result: AnalysisResult, filepath: str) -> str:
        """Format full analysis report."""
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
        lines.append(f"    Melodic:   {s.melodic:5.1f} {score_bar(s.melodic)}")
        lines.append(f"    Harmonic:  {s.harmonic:5.1f} {score_bar(s.harmonic)}")
        lines.append(f"    Rhythm:    {s.rhythm:5.1f} {score_bar(s.rhythm)}")
        lines.append(f"    Structure: {s.structure:5.1f} {score_bar(s.structure)}")

        # Issue summary
        error_count = sum(1 for i in result.issues if i.severity == Severity.ERROR)
        warning_count = sum(1 for i in result.issues if i.severity == Severity.WARNING)
        info_count = sum(1 for i in result.issues if i.severity == Severity.INFO)

        lines.append("")
        lines.append(f"Summary: {error_count} errors, {warning_count} warnings, {info_count} info")

        # Group by category
        by_category = defaultdict(lambda: defaultdict(list))
        for issue in result.issues:
            by_category[issue.category.value][issue.subcategory].append(issue)

        for category in ["melodic", "harmonic", "rhythm", "structure"]:
            subcategories = by_category.get(category, {})
            if not subcategories:
                continue

            lines.append("")
            lines.append("=" * 40)
            lines.append(f"  {category.upper()} ISSUES")
            lines.append("=" * 40)

            for subcategory, subissues in sorted(subcategories.items()):
                lines.append("")
                lines.append(f"--- {subcategory.replace('_', ' ').title()} ({len(subissues)}) ---")
                for issue in subissues[:15]:
                    marker = "!!" if issue.severity == Severity.ERROR else "!" if issue.severity == Severity.WARNING else "-"
                    lines.append(f"  [{marker}] {issue.track}: {issue.message} at {tick_to_bar_beat(issue.tick)}")
                if len(subissues) > 15:
                    lines.append(f"  ... and {len(subissues) - 15} more")

        # Hook analysis
        if result.hooks:
            lines.append("")
            lines.append("=" * 40)
            lines.append("  HOOK DETECTION")
            lines.append("=" * 40)
            for i, hook in enumerate(result.hooks, 1):
                occ_str = ", ".join(str(b) for b in hook.occurrences)
                lines.append(f"  Hook {i}: bars {hook.start_bar}-{hook.end_bar}")
                lines.append(f"    Occurrences: [{occ_str}]")
                lines.append(f"    Pitches: {[note_name(p) for p in hook.pitches[:8]]}...")

        lines.append("")
        lines.append("=" * 80)

        return "\n".join(lines)

    @staticmethod
    def format_json(result: AnalysisResult, filepath: str) -> str:
        """Format as JSON."""
        output = {
            'file': filepath,
            'total_notes': len(result.notes),
            'scores': {
                'overall': round(result.score.overall, 1),
                'grade': result.score.grade,
                'melodic': round(result.score.melodic, 1),
                'harmonic': round(result.score.harmonic, 1),
                'rhythm': round(result.score.rhythm, 1),
                'structure': round(result.score.structure, 1),
            },
            'issue_summary': {
                'errors': sum(1 for i in result.issues if i.severity == Severity.ERROR),
                'warnings': sum(1 for i in result.issues if i.severity == Severity.WARNING),
                'info': sum(1 for i in result.issues if i.severity == Severity.INFO),
            },
            'issues': [
                {
                    'severity': i.severity.value,
                    'category': i.category.value,
                    'subcategory': i.subcategory,
                    'track': i.track,
                    'message': i.message,
                    'bar': tick_to_bar(i.tick),
                    'tick': i.tick,
                }
                for i in result.issues
            ],
            'hooks': [
                {
                    'start_bar': h.start_bar,
                    'end_bar': h.end_bar,
                    'occurrences': h.occurrences,
                }
                for h in result.hooks
            ],
        }
        return json.dumps(output, indent=2)

    @staticmethod
    def format_score_only(result: AnalysisResult) -> str:
        """Format score only (one line)."""
        s = result.score
        return f"{s.overall:.1f} ({s.grade}) M:{s.melodic:.0f} H:{s.harmonic:.0f} R:{s.rhythm:.0f} S:{s.structure:.0f}"


# =============================================================================
# FILE LOADING
# =============================================================================

def load_json_output(filepath: str) -> List[Note]:
    """Load notes from JSON output."""
    with open(filepath, 'r') as f:
        data = json.load(f)

    notes = []
    for track in data.get('tracks', []):
        channel = track.get('channel', 0)
        for note_data in track.get('notes', []):
            notes.append(Note(
                start=note_data.get('start_ticks', note_data.get('start', 0)),
                duration=note_data.get('duration_ticks', note_data.get('duration', 0)),
                pitch=note_data['pitch'],
                velocity=note_data.get('velocity', 100),
                channel=channel,
                provenance=note_data.get('provenance')
            ))

    return notes


# =============================================================================
# BATCH RUNNER
# =============================================================================

class ProgressCounter:
    """Thread-safe progress counter."""
    def __init__(self, total: int):
        self.total = total
        self.current = 0
        self.lock = threading.Lock()
        self.passed = 0
        self.failed = 0
        self.errors = 0

    def increment(self, result: TestResult):
        with self.lock:
            self.current += 1
            if result.error:
                self.errors += 1
            elif result.error_count > 0:
                self.failed += 1
            else:
                self.passed += 1


def run_single_test(
    cli_path: str,
    seed: int,
    style: int,
    chord: int,
    blueprint: int,
    work_dir: Path,
) -> TestResult:
    """Run a single generation and analysis."""
    cmd = [
        cli_path,
        "--analyze",
        "--json",
        "--seed", str(seed),
        "--style", str(style),
        "--chord", str(chord),
        "--blueprint", str(blueprint),
    ]

    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=60,
            cwd=work_dir,
        )

        if result.returncode != 0:
            return TestResult(
                seed=seed, style=style, chord=chord, blueprint=blueprint,
                error=f"CLI error: {result.stderr[:200]}",
            )

        # Load output.json
        output_file = work_dir / "output.json"
        if not output_file.exists():
            return TestResult(
                seed=seed, style=style, chord=chord, blueprint=blueprint,
                error="output.json not found",
            )

        notes = load_json_output(str(output_file))
        analyzer = MusicAnalyzer(notes)
        analysis = analyzer.analyze_all()

        error_count = sum(1 for i in analysis.issues if i.severity == Severity.ERROR)
        warning_count = sum(1 for i in analysis.issues if i.severity == Severity.WARNING)
        info_count = sum(1 for i in analysis.issues if i.severity == Severity.INFO)

        return TestResult(
            seed=seed,
            style=style,
            chord=chord,
            blueprint=blueprint,
            score=analysis.score,
            error_count=error_count,
            warning_count=warning_count,
            info_count=info_count,
        )

    except subprocess.TimeoutExpired:
        return TestResult(
            seed=seed, style=style, chord=chord, blueprint=blueprint,
            error="Timeout (>60s)",
        )
    except Exception as e:
        return TestResult(
            seed=seed, style=style, chord=chord, blueprint=blueprint,
            error=str(e)[:200],
        )


def run_batch_tests(
    cli_path: str,
    seeds: List[int],
    styles: List[int],
    chords: List[int],
    blueprints: List[int],
    parallel: int = 1,
    verbose: bool = False,
) -> List[TestResult]:
    """Run batch tests."""
    work_dir = Path.cwd()
    results = []

    configs = [
        (seed, style, chord, blueprint)
        for seed in seeds
        for style in styles
        for chord in chords
        for blueprint in blueprints
    ]

    total = len(configs)
    print(f"Running {total} tests" + (f" with {parallel} workers" if parallel > 1 else "") + "...")
    print()

    if parallel > 1:
        counter = ProgressCounter(total)
        results_dict = {}

        with ThreadPoolExecutor(max_workers=parallel) as executor:
            futures = {
                executor.submit(
                    run_single_test, cli_path, seed, style, chord, blueprint, work_dir
                ): (seed, style, chord, blueprint)
                for seed, style, chord, blueprint in configs
            }

            for future in as_completed(futures):
                config = futures[future]
                result = future.result()
                results_dict[config] = result
                counter.increment(result)

                with counter.lock:
                    i = counter.current
                    if result.error:
                        print(f"[{i:4d}/{total}] seed={config[0]:3d} bp={config[3]}: ERROR")
                    elif result.error_count > 0:
                        print(f"[{i:4d}/{total}] seed={config[0]:3d} bp={config[3]}: "
                              f"\033[31mFAIL\033[0m score={result.score.overall:.1f} errors={result.error_count}")
                    elif verbose:
                        print(f"[{i:4d}/{total}] seed={config[0]:3d} bp={config[3]}: "
                              f"OK score={result.score.overall:.1f}")
                    else:
                        print(f"\r[{i:4d}/{total}] Testing... (P:{counter.passed} F:{counter.failed} E:{counter.errors})", end="", flush=True)

        results = [results_dict[config] for config in configs]
        if not verbose:
            print("\r" + " " * 60 + "\r", end="")
    else:
        for i, (seed, style, chord, blueprint) in enumerate(configs, 1):
            result = run_single_test(cli_path, seed, style, chord, blueprint, work_dir)
            results.append(result)

            if result.error:
                print(f"[{i:4d}/{total}] seed={seed:3d} bp={blueprint}: ERROR")
            elif result.error_count > 0:
                print(f"[{i:4d}/{total}] seed={seed:3d} bp={blueprint}: "
                      f"\033[31mFAIL\033[0m score={result.score.overall:.1f} errors={result.error_count}")
            elif verbose:
                print(f"[{i:4d}/{total}] seed={seed:3d} bp={blueprint}: "
                      f"OK score={result.score.overall:.1f}")
            else:
                print(f"\r[{i:4d}/{total}] Testing...", end="", flush=True)

        if not verbose:
            print("\r" + " " * 40 + "\r", end="")

    return results


def print_batch_summary(results: List[TestResult]) -> bool:
    """Print batch test summary."""
    total = len(results)
    errors = [r for r in results if r.error]
    failed = [r for r in results if r.error_count > 0]
    passed = [r for r in results if not r.error and r.error_count == 0]

    print()
    print("=" * 80)
    print("BATCH TEST SUMMARY")
    print("=" * 80)

    print(f"\n{'Results':40s}")
    print("-" * 40)
    print(f"  Total tests:             {total:>6d}")
    print(f"  \033[32mPassed:                  {len(passed):>6d}\033[0m")
    print(f"  \033[31mFailed:                  {len(failed):>6d}\033[0m")
    print(f"  Errors:                  {len(errors):>6d}")

    # Score statistics
    valid_results = [r for r in results if not r.error]
    if valid_results:
        scores = [r.score.overall for r in valid_results]
        avg_score = sum(scores) / len(scores)
        min_score = min(scores)
        max_score = max(scores)

        print(f"\n{'Score Statistics':40s}")
        print("-" * 40)
        print(f"  Average:     {avg_score:6.1f}")
        print(f"  Min:         {min_score:6.1f}")
        print(f"  Max:         {max_score:6.1f}")

        # Grade distribution
        grades = Counter(r.score.grade for r in valid_results)
        print(f"\n{'Grade Distribution':40s}")
        print("-" * 40)
        for grade in ['A', 'B', 'C', 'D', 'F']:
            count = grades.get(grade, 0)
            pct = count / len(valid_results) * 100
            bar = '#' * int(pct / 5)
            print(f"  {grade}: {count:4d} ({pct:5.1f}%) {bar}")

    # Blueprint breakdown
    bp_stats = defaultdict(lambda: {'count': 0, 'score_sum': 0, 'errors': 0, 'failed': 0})
    for r in results:
        bp = r.blueprint
        bp_stats[bp]['count'] += 1
        if r.error:
            bp_stats[bp]['errors'] += 1
        elif r.error_count > 0:
            bp_stats[bp]['failed'] += 1
        else:
            bp_stats[bp]['score_sum'] += r.score.overall

    if len(bp_stats) > 1:
        print(f"\n{'Results by Blueprint':40s}")
        print("-" * 40)
        for bp in sorted(bp_stats.keys()):
            stats = bp_stats[bp]
            bp_name = BLUEPRINT_NAMES.get(bp, f"BP{bp}")
            passed_count = stats['count'] - stats['errors'] - stats['failed']
            avg = stats['score_sum'] / passed_count if passed_count > 0 else 0
            print(f"  {bp_name:15s} passed:{passed_count:3d} failed:{stats['failed']:3d} avg:{avg:5.1f}")

    # Worst cases
    if failed:
        print(f"\n{'Worst Cases (lowest scores)':40s}")
        print("-" * 40)
        worst = sorted(failed, key=lambda r: r.score.overall)[:5]
        for r in worst:
            bp_name = BLUEPRINT_NAMES.get(r.blueprint, f"BP{r.blueprint}")
            print(f"  seed={r.seed:3d} style={r.style:2d} {bp_name:12s} score={r.score.overall:5.1f} errors={r.error_count}")
            print(f"    {r.cli_command()}")

    print()
    print("=" * 80)
    is_passed = len(failed) == 0 and len(errors) == 0
    if is_passed:
        print("\033[32mRESULT: PASSED\033[0m")
    else:
        print("\033[31mRESULT: FAILED\033[0m")
    print("=" * 80)

    return is_passed


# =============================================================================
# CLI MAIN
# =============================================================================

def main():
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
        """)

    # Input file (positional, optional if --generate or --batch)
    parser.add_argument("input_file", nargs="?", help="JSON output file to analyze")

    # Generate mode
    gen_group = parser.add_argument_group("Generate Mode")
    gen_group.add_argument("--generate", "-g", action="store_true",
                           help="Generate MIDI then analyze")
    gen_group.add_argument("--seed", type=int, default=0,
                           help="Random seed (default: 0 = random)")
    gen_group.add_argument("--style", type=int, default=0,
                           help="Style preset (default: 0)")
    gen_group.add_argument("--chord", type=int, default=0,
                           help="Chord progression (default: 0)")
    gen_group.add_argument("--bp", "--blueprint", type=int, default=0, dest="blueprint",
                           help="Blueprint (default: 0)")

    # Batch mode
    batch_group = parser.add_argument_group("Batch Mode")
    batch_group.add_argument("--batch", "-b", action="store_true",
                             help="Run batch tests")
    batch_group.add_argument("--seeds", type=int, default=10,
                             help="Number of seeds for batch (default: 10)")
    batch_group.add_argument("--seed-start", type=int, default=1,
                             help="Starting seed (default: 1)")
    batch_group.add_argument("--styles", type=str, default="0",
                             help="Styles: 'all' or comma-separated (default: 0)")
    batch_group.add_argument("--chords", type=str, default="0",
                             help="Chords: 'all' or comma-separated (default: 0)")
    batch_group.add_argument("--blueprints", type=str, default="all",
                             help="Blueprints: 'all' or comma-separated (default: all)")
    batch_group.add_argument("-j", "--jobs", type=int, default=1,
                             help="Parallel workers (default: 1)")

    # Filter options
    filter_group = parser.add_argument_group("Filters")
    filter_group.add_argument("--track", type=str,
                              help="Filter by track (Vocal, Chord, Bass, Motif, Aux)")
    filter_group.add_argument("--bar-range", type=str,
                              help="Filter by bar range (e.g., 15-20)")
    filter_group.add_argument("--category", type=str,
                              choices=["melodic", "harmonic", "rhythm", "structure"],
                              help="Filter by category")
    filter_group.add_argument("--severity", type=str,
                              choices=["error", "warning", "info"],
                              help="Minimum severity to show")

    # Output options
    output_group = parser.add_argument_group("Output Options")
    output_group.add_argument("--quick", "-q", action="store_true",
                              help="Quick summary only")
    output_group.add_argument("--json", action="store_true",
                              help="JSON output")
    output_group.add_argument("--score-only", action="store_true",
                              help="Score only (one line)")
    output_group.add_argument("-v", "--verbose", action="store_true",
                              help="Verbose output")

    # CLI path
    parser.add_argument("--cli", default="./build/bin/midisketch_cli",
                        help="Path to CLI (default: ./build/bin/midisketch_cli)")

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
            styles = list(range(15)) if args.styles == "all" else [int(x) for x in args.styles.split(",")]
            chords = list(range(22)) if args.chords == "all" else [int(x) for x in args.chords.split(",")]
            blueprints = list(range(9)) if args.blueprints == "all" else [int(x) for x in args.blueprints.split(",")]

        results = run_batch_tests(args.cli, seeds, styles, chords, blueprints, args.jobs, args.verbose)
        passed = print_batch_summary(results)
        sys.exit(0 if passed else 1)

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
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"Error: {result.stderr}", file=sys.stderr)
            sys.exit(1)
        input_file = "output.json"
    else:
        input_file = args.input_file

    if not input_file:
        parser.print_help()
        sys.exit(1)

    # Load and analyze
    try:
        notes = load_json_output(input_file)
    except FileNotFoundError:
        print(f"Error: File not found: {input_file}", file=sys.stderr)
        sys.exit(1)
    except json.JSONDecodeError as e:
        print(f"Error: Invalid JSON: {e}", file=sys.stderr)
        sys.exit(1)

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
    error_count = sum(1 for i in result.issues if i.severity == Severity.ERROR)
    sys.exit(1 if error_count > 0 else 0)


if __name__ == "__main__":
    main()
