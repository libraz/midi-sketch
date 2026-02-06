"""Rhythm analysis: density, monotony, syncopation, grid alignment, variety.

Detects rhythmic issues including high note density, repetitive patterns,
beat alignment problems, weak downbeats, backbeat strength, velocity emphasis,
rhythm variety, beat content, and grid strictness by blueprint paradigm.
"""

from collections import defaultdict
from typing import List

from ..constants import TICKS_PER_BEAT, TICKS_PER_BAR, TRACK_NAMES, Severity, Category
from ..helpers import tick_to_bar, _ioi_entropy
from ..models import Issue
from .base import BaseAnalyzer


class RhythmAnalyzer(BaseAnalyzer):
    """Analyzer for rhythmic qualities across all tracks.

    Checks melodic and drum tracks for rhythmic problems such as
    excessive density, monotonous spacing, off-grid timing, weak
    downbeats, missing backbeat, incorrect velocity emphasis,
    low rhythmic variety, thin beat content, and grid strictness
    violations per blueprint paradigm.
    """

    def analyze(self) -> List[Issue]:
        """Run all rhythm analyses and return collected issues."""
        self._analyze_rhythm_patterns()
        self._analyze_note_density()
        self._analyze_rhythmic_monotony()
        self._analyze_beat_alignment()
        self._analyze_syncopation()
        self._analyze_strong_beat_emphasis()
        self._analyze_backbeat()
        self._analyze_rhythm_variety()
        self._analyze_beat_content()
        self._analyze_grid_by_blueprint()
        return self.issues

    # -----------------------------------------------------------------
    # Existing analyses
    # -----------------------------------------------------------------

    def _analyze_rhythm_patterns(self):
        """Analyze rhythm consistency across tracks (Motif/Vocal sync ratio)."""
        sync_channels = [0, 3, 5]
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
                    if (sync_ratio < 0.2
                            and len(motif_attacks) > 3
                            and len(vocal_attacks) > 3):
                        self.add_issue(
                            severity=Severity.INFO,
                            category=Category.RHYTHM,
                            subcategory="low_sync",
                            message=f"Low Motif/Vocal sync (ratio: {sync_ratio:.2f})",
                            tick=bar * TICKS_PER_BAR,
                            track="Motif/Vocal",
                            details={"bar": bar, "sync_ratio": sync_ratio},
                        )

    def _analyze_note_density(self):
        """Detect bars with unusually high note density (>3x avg AND >16)."""
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
                    self.add_issue(
                        severity=Severity.WARNING,
                        category=Category.RHYTHM,
                        subcategory="high_density",
                        message=f"High density ({count} notes, avg: {avg_density:.1f})",
                        tick=bar * TICKS_PER_BAR,
                        track=track_name,
                        details={"bar": bar, "count": count, "average": avg_density},
                    )

    def _analyze_rhythmic_monotony(self):
        """Detect repetitive rhythmic patterns (8+ same IOI)."""
        melodic_channels = [0, 3]
        for ch in melodic_channels:
            notes = self.notes_by_channel.get(ch, [])
            if len(notes) < 8:
                continue
            track_name = TRACK_NAMES.get(ch, f"Ch{ch}")
            ioi_list = []
            for idx in range(1, len(notes)):
                ioi = notes[idx].start - notes[idx - 1].start
                ioi_list.append(ioi)
            if len(ioi_list) >= 8:
                quantized = [round(ioi / (TICKS_PER_BEAT / 4)) for ioi in ioi_list]
                same_ioi_count = 1
                current_ioi = quantized[0]
                for idx in range(1, len(quantized)):
                    if quantized[idx] == current_ioi:
                        same_ioi_count += 1
                    else:
                        if same_ioi_count >= 8:
                            self.add_issue(
                                severity=Severity.WARNING,
                                category=Category.RHYTHM,
                                subcategory="rhythmic_monotony",
                                message=f"{same_ioi_count} notes with same spacing",
                                tick=notes[idx - same_ioi_count].start,
                                track=track_name,
                                details={
                                    "count": same_ioi_count,
                                    "ioi": current_ioi,
                                },
                            )
                        same_ioi_count = 1
                        current_ioi = quantized[idx]

    def _analyze_beat_alignment(self):
        """Detect notes off the 16th-note beat grid."""
        melodic_channels = [0, 1, 2, 3, 5]
        grid_resolution = TICKS_PER_BEAT // 4
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
                    self.add_issue(
                        severity=Severity.WARNING,
                        category=Category.RHYTHM,
                        subcategory="beat_misalignment",
                        message=(
                            f"{len(off_grid_notes)} notes "
                            f"({off_grid_ratio * 100:.1f}%) off 16th grid"
                        ),
                        tick=off_grid_notes[0].start,
                        track=track_name,
                        details={
                            "off_grid_count": len(off_grid_notes),
                            "total": len(notes),
                        },
                    )

    def _analyze_syncopation(self):
        """Detect weak beat 1 (few notes on downbeat)."""
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
                self.add_issue(
                    severity=Severity.WARNING,
                    category=Category.RHYTHM,
                    subcategory="weak_downbeat",
                    message=f"Few notes on beat 1 ({beat1_ratio * 100:.1f}%)",
                    tick=0,
                    track=track_name,
                    details={"beat1_ratio": beat1_ratio},
                )

    def _analyze_strong_beat_emphasis(self):
        """Check velocity emphasis on strong beats (1,3) vs weak beats (2,4)."""
        melodic_channels = [0, 3, 5]
        for ch in melodic_channels:
            notes = self.notes_by_channel.get(ch, [])
            if len(notes) < 8:
                continue
            strong_vels = []
            weak_vels = []
            for note in notes:
                beat = (note.start % TICKS_PER_BAR) // TICKS_PER_BEAT
                if beat in (0, 2):
                    strong_vels.append(note.velocity)
                else:
                    weak_vels.append(note.velocity)
            if strong_vels and weak_vels:
                avg_strong = sum(strong_vels) / len(strong_vels)
                avg_weak = sum(weak_vels) / len(weak_vels)
                if avg_weak > avg_strong * 1.1:
                    track_name = TRACK_NAMES.get(ch, f"Ch{ch}")
                    sensitivity = (
                        self.profile.velocity_sensitivity
                        if self.profile else 1.0
                    )
                    if sensitivity > 1.0 or avg_weak > avg_strong * 1.2:
                        self.add_issue(
                            severity=Severity.INFO,
                            category=Category.RHYTHM,
                            subcategory="weak_emphasis",
                            message=(
                                f"Weak beats louder than strong "
                                f"(strong: {avg_strong:.0f}, weak: {avg_weak:.0f})"
                            ),
                            tick=0,
                            track=track_name,
                            details={
                                "avg_strong": avg_strong,
                                "avg_weak": avg_weak,
                            },
                        )

    def _analyze_backbeat(self):
        """Check if snare (38) is on beats 2 and 4."""
        drums = self.notes_by_channel.get(9, [])
        if not drums:
            return
        snare_notes = [n for n in drums if n.pitch == 38]
        if not snare_notes:
            return
        on_backbeat = sum(
            1 for n in snare_notes
            if (n.start % TICKS_PER_BAR) // TICKS_PER_BEAT in (1, 3)
        )
        ratio = on_backbeat / len(snare_notes)
        if ratio < 0.5:
            self.add_issue(
                severity=Severity.INFO,
                category=Category.RHYTHM,
                subcategory="backbeat",
                message=f"Weak backbeat (only {ratio:.0%} snares on beats 2/4)",
                tick=0,
                track="Drums",
                details={"backbeat_ratio": ratio},
            )

    def _analyze_rhythm_variety(self):
        """Analyze IOI entropy for rhythmic variety."""
        for ch in [0, 3]:
            notes = self.notes_by_channel.get(ch, [])
            if len(notes) < 12:
                continue
            track_name = TRACK_NAMES.get(ch, f"Ch{ch}")
            ioi_list = []
            for idx in range(1, len(notes)):
                ioi = notes[idx].start - notes[idx - 1].start
                quantized = round(ioi / (TICKS_PER_BEAT / 4))
                ioi_list.append(quantized)
            if not ioi_list:
                continue
            entropy = _ioi_entropy(ioi_list)
            if entropy < 1.0:
                self.add_issue(
                    severity=Severity.INFO,
                    category=Category.RHYTHM,
                    subcategory="rhythm_variety",
                    message=f"Low rhythmic variety (entropy: {entropy:.2f})",
                    tick=0,
                    track=track_name,
                    details={"entropy": entropy},
                )
            elif entropy > 4.0:
                self.add_issue(
                    severity=Severity.INFO,
                    category=Category.RHYTHM,
                    subcategory="rhythm_variety",
                    message=f"Very diverse rhythm (entropy: {entropy:.2f})",
                    tick=0,
                    track=track_name,
                    details={"entropy": entropy},
                )

    # -----------------------------------------------------------------
    # New analyses
    # -----------------------------------------------------------------

    def _analyze_beat_content(self):
        """Detect bars where beat 1 has fewer than 2 active tracks.

        For each bar, counts how many melodic tracks (channels 0-5,
        excluding drums ch 9) have a note attacking within a 16th-note
        tolerance of the downbeat. Skips the first 2 bars (pickup/intro).
        Only reports if more than 25% of bars have thin downbeats.
        """
        melodic_channels = [0, 1, 2, 3, 4, 5]
        sixteenth = TICKS_PER_BEAT // 4

        # Collect which channels attack on beat 1 for each bar
        beat1_tracks_per_bar = defaultdict(set)
        max_bar = 0
        for ch in melodic_channels:
            for note in self.notes_by_channel.get(ch, []):
                bar = tick_to_bar(note.start)
                if bar > max_bar:
                    max_bar = bar
                pos_in_bar = note.start % TICKS_PER_BAR
                if pos_in_bar < sixteenth:
                    beat1_tracks_per_bar[bar].add(ch)

        if max_bar <= 2:
            return

        thin_bars = []
        total_bars = 0
        for bar in range(3, max_bar + 1):
            total_bars += 1
            track_count = len(beat1_tracks_per_bar.get(bar, set()))
            if track_count < 2:
                thin_bars.append((bar, track_count))

        if total_bars == 0:
            return

        thin_ratio = len(thin_bars) / total_bars
        if thin_ratio > 0.25:
            for bar, count in thin_bars:
                self.add_issue(
                    severity=Severity.INFO,
                    category=Category.RHYTHM,
                    subcategory="beat_content",
                    message=f"Thin downbeat (only {count} tracks)",
                    tick=(bar - 1) * TICKS_PER_BAR,
                    track="All",
                    details={
                        "bar": bar,
                        "active_tracks": count,
                        "thin_ratio": thin_ratio,
                    },
                )

    def _analyze_grid_by_blueprint(self):
        """Check grid alignment strictness based on blueprint paradigm.

        RhythmSync paradigm requires >95% of notes on the 16th-note grid.
        Other paradigms require >70%. Reports a WARNING if alignment falls
        below the paradigm-specific threshold.
        """
        if not self.profile:
            return

        melodic_channels = [0, 3, 5]
        grid_resolution = TICKS_PER_BEAT // 4
        tolerance = 10

        total_notes = 0
        on_grid_notes = 0
        for ch in melodic_channels:
            for note in self.notes_by_channel.get(ch, []):
                total_notes += 1
                remainder = note.start % grid_resolution
                if remainder <= tolerance or remainder >= grid_resolution - tolerance:
                    on_grid_notes += 1

        if total_notes == 0:
            return

        ratio = on_grid_notes / total_notes
        paradigm = self.profile.paradigm

        if paradigm == "RhythmSync":
            threshold = 0.95
        else:
            threshold = 0.70

        if ratio < threshold:
            self.add_issue(
                severity=Severity.WARNING,
                category=Category.RHYTHM,
                subcategory="grid_strictness",
                message=(
                    f"Grid alignment below {paradigm} standard "
                    f"({ratio:.0%} vs {threshold:.0%})"
                ),
                tick=0,
                track="All",
                details={
                    "paradigm": paradigm,
                    "on_grid_ratio": ratio,
                    "threshold": threshold,
                    "total_notes": total_notes,
                    "on_grid_notes": on_grid_notes,
                },
            )
