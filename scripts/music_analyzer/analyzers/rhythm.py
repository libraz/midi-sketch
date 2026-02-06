"""Rhythm analysis: density, monotony, syncopation, grid alignment, variety.

Detects rhythmic issues including high note density, repetitive patterns,
beat alignment problems, weak downbeats, backbeat strength, velocity emphasis,
rhythm variety, beat content, and grid strictness by blueprint paradigm.
"""

from collections import defaultdict
from typing import List

from ..constants import (TICKS_PER_BEAT, TICKS_PER_BAR, TRACK_NAMES, GUITAR_CHANNEL,
                         Severity, Category, VOCAL_STYLE_ULTRA_VOCALOID)
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
        self._analyze_drive_metrics()
        self._analyze_guitar_rhythm_consistency()
        self._analyze_guitar_fingerpick_monotony()
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
                            tick=(bar - 1) * TICKS_PER_BAR,
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
                        tick=(bar - 1) * TICKS_PER_BAR,
                        track=track_name,
                        details={"bar": bar, "count": count, "average": avg_density},
                    )

    def _analyze_rhythmic_monotony(self):
        """Detect repetitive rhythmic patterns (12+ same IOI).

        In pop music, 8-note runs of equal spacing (e.g., 8th notes)
        are common. Only flag 12+ consecutive same-spacing notes as
        rhythmically monotonous.

        UltraVocaloid: Machine-gun 32nd note runs are intentional,
        so the threshold is raised to 64 to avoid false positives.
        """
        melodic_channels = [0, 3]
        vocal_style = self.metadata.get('vocal_style')
        is_ultra = (vocal_style == VOCAL_STYLE_ULTRA_VOCALOID)
        monotony_threshold = 64 if is_ultra else 12
        for ch in melodic_channels:
            notes = self.notes_by_channel.get(ch, [])
            if len(notes) < monotony_threshold:
                continue
            track_name = TRACK_NAMES.get(ch, f"Ch{ch}")
            ioi_list = []
            for idx in range(1, len(notes)):
                ioi = notes[idx].start - notes[idx - 1].start
                ioi_list.append(ioi)
            if len(ioi_list) >= monotony_threshold:
                quantized = [round(ioi / (TICKS_PER_BEAT / 4)) for ioi in ioi_list]
                same_ioi_count = 1
                current_ioi = quantized[0]
                for idx in range(1, len(quantized)):
                    if quantized[idx] == current_ioi:
                        same_ioi_count += 1
                    else:
                        if same_ioi_count >= monotony_threshold:
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
                # Final flush: check last run
                if same_ioi_count >= monotony_threshold:
                    self.add_issue(
                        severity=Severity.WARNING,
                        category=Category.RHYTHM,
                        subcategory="rhythmic_monotony",
                        message=f"{same_ioi_count} notes with same spacing",
                        tick=notes[len(quantized) - same_ioi_count].start,
                        track=track_name,
                        details={
                            "count": same_ioi_count,
                            "ioi": current_ioi,
                        },
                    )

    def _analyze_beat_alignment(self):
        """Detect notes off the beat grid.

        Checks against both straight 16th grid and shuffle/swing grid
        (2/3 position within each 8th note). Uses a tolerance of 30
        ticks to allow for intentional humanization offsets.

        UltraVocaloid: Skipped. Machine-gun 32nd-note runs are
        intentional and don't benefit from grid alignment analysis.
        """
        vocal_style = self.metadata.get('vocal_style')
        if vocal_style == VOCAL_STYLE_ULTRA_VOCALOID:
            return

        melodic_channels = [0, 1, 2, 3, 5, 6]
        grid_unit = TICKS_PER_BEAT // 4
        tolerance = 30
        # Shuffle position: 2/3 of an 8th note = 160 ticks from beat
        # Within each 16th cell, shuffle falls at ~67% (80 ticks into 120)
        shuffle_offset = TICKS_PER_BEAT // 6  # 80 ticks (triplet 16th)

        for ch in melodic_channels:
            notes = self.notes_by_channel.get(ch, [])
            if not notes:
                continue
            track_name = TRACK_NAMES.get(ch, f"Ch{ch}")
            off_grid_notes = []
            for note in notes:
                remainder = note.start % grid_unit
                # Check straight 16th grid (near 0 or near grid_unit)
                on_straight = (remainder <= tolerance or
                               remainder >= grid_unit - tolerance)
                # Check shuffle grid (near shuffle_offset)
                on_shuffle = abs(remainder - shuffle_offset) <= tolerance
                if not on_straight and not on_shuffle:
                    off_grid_notes.append(note)
            if len(off_grid_notes) > 5:
                off_grid_ratio = len(off_grid_notes) / len(notes)
                if off_grid_ratio > 0.2:
                    self.add_issue(
                        severity=Severity.WARNING,
                        category=Category.RHYTHM,
                        subcategory="beat_misalignment",
                        message=(
                            f"{len(off_grid_notes)} notes "
                            f"({off_grid_ratio * 100:.1f}%) off grid"
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

    def _analyze_drive_metrics(self):
        """Detect lack of forward momentum in uptempo songs (BPM >= 140).

        For high-BPM songs (typically RhythmSync blueprints), checks whether
        bass, chord, and kick drum provide sufficient rhythmic drive in the
        chorus. Flags issues when bass has no syncopation or 8th notes, chord
        is pad-like with mostly long notes, or kick is too sparse or lacks
        offbeat patterns.
        """
        bpm = self.metadata.get('bpm') or 120
        if bpm < 140:
            return

        # Find the first Chorus section tick range
        chorus_start = None
        chorus_end = None

        # Try metadata sections first
        meta_sections = self.metadata.get('sections', [])
        for sec in meta_sections:
            sec_type = sec.get('type', '').lower()
            if sec_type == 'chorus':
                chorus_start = sec.get('start_ticks', sec.get('start_tick'))
                chorus_end = sec.get('end_ticks', sec.get('end_tick'))
                # Convert bar-based sections if tick values are missing
                if chorus_start is None and 'start_bar' in sec:
                    chorus_start = (sec['start_bar'] - 1) * TICKS_PER_BAR
                if chorus_end is None and 'end_bar' in sec:
                    chorus_end = sec['end_bar'] * TICKS_PER_BAR
                break

        # Fall back to estimated sections
        if chorus_start is None:
            for sec in self.sections:
                if sec.get('type') == 'chorus':
                    chorus_start = (sec['start_bar'] - 1) * TICKS_PER_BAR
                    chorus_end = sec['end_bar'] * TICKS_PER_BAR
                    break

        if chorus_start is None or chorus_end is None:
            return

        chorus_bars = max(1, (chorus_end - chorus_start) / TICKS_PER_BAR)

        # --- Bass (channel 2) drive check ---
        bass_notes = [
            note for note in self.notes_by_channel.get(2, [])
            if chorus_start <= note.start < chorus_end
        ]
        if bass_notes:
            bass_syncopated = sum(
                1 for note in bass_notes
                if note.start % TICKS_PER_BEAT != 0
            )
            bass_syncopation_rate = bass_syncopated / len(bass_notes)

            eighth_note_max = TICKS_PER_BEAT // 2 + 10
            bass_eighth = sum(
                1 for note in bass_notes
                if note.duration <= eighth_note_max
            )
            bass_eighth_ratio = bass_eighth / len(bass_notes)

            if bass_syncopation_rate < 0.05 and bass_eighth_ratio < 0.10:
                self.add_issue(
                    severity=Severity.WARNING,
                    category=Category.RHYTHM,
                    subcategory="drive_deficit",
                    message=(
                        "Bass lacks drive "
                        "(no syncopation, no 8th notes)"
                    ),
                    tick=chorus_start,
                    track="Bass",
                    details={
                        "syncopation_rate": bass_syncopation_rate,
                        "eighth_note_ratio": bass_eighth_ratio,
                        "bpm": bpm,
                    },
                )
            elif bass_syncopation_rate < 0.10:
                self.add_issue(
                    severity=Severity.INFO,
                    category=Category.RHYTHM,
                    subcategory="drive_deficit",
                    message=(
                        f"Bass has low syncopation "
                        f"({bass_syncopation_rate:.0%})"
                    ),
                    tick=chorus_start,
                    track="Bass",
                    details={
                        "syncopation_rate": bass_syncopation_rate,
                        "bpm": bpm,
                    },
                )

        # --- Chord (channel 1) drive check ---
        chord_notes = [
            note for note in self.notes_by_channel.get(1, [])
            if chorus_start <= note.start < chorus_end
        ]
        if chord_notes:
            long_notes = sum(
                1 for note in chord_notes
                if note.duration > TICKS_PER_BEAT
            )
            long_ratio = long_notes / len(chord_notes)

            if long_ratio > 0.80:
                self.add_issue(
                    severity=Severity.INFO,
                    category=Category.RHYTHM,
                    subcategory="drive_deficit",
                    message=(
                        f"Chord is pad-like "
                        f"(rhythmic activity low, {long_ratio:.0%} long notes)"
                    ),
                    tick=chorus_start,
                    track="Chord",
                    details={
                        "long_note_ratio": long_ratio,
                        "bpm": bpm,
                    },
                )

        # --- Drums (channel 9) kick drive check ---
        drum_notes = [
            note for note in self.notes_by_channel.get(9, [])
            if chorus_start <= note.start < chorus_end
        ]
        if drum_notes:
            kick_notes = [
                note for note in drum_notes if note.pitch == 36
            ]
            if kick_notes:
                kick_density = len(kick_notes) / chorus_bars

                if kick_density < 2.0:
                    self.add_issue(
                        severity=Severity.WARNING,
                        category=Category.RHYTHM,
                        subcategory="drive_deficit",
                        message=(
                            f"Kick too sparse for uptempo song "
                            f"({kick_density:.1f}/bar)"
                        ),
                        tick=chorus_start,
                        track="Drums",
                        details={
                            "kick_per_bar": kick_density,
                            "bpm": bpm,
                        },
                    )
                elif kick_density < 3.0:
                    self.add_issue(
                        severity=Severity.INFO,
                        category=Category.RHYTHM,
                        subcategory="drive_deficit",
                        message=(
                            f"Kick density low for uptempo "
                            f"({kick_density:.1f}/bar)"
                        ),
                        tick=chorus_start,
                        track="Drums",
                        details={
                            "kick_per_bar": kick_density,
                            "bpm": bpm,
                        },
                    )

                kick_syncopated = sum(
                    1 for note in kick_notes
                    if note.start % TICKS_PER_BEAT != 0
                )
                kick_syncopation_rate = kick_syncopated / len(kick_notes)

                if kick_syncopation_rate < 0.10 and bpm >= 150:
                    self.add_issue(
                        severity=Severity.INFO,
                        category=Category.RHYTHM,
                        subcategory="drive_deficit",
                        message=(
                            f"Kick lacks offbeat drive "
                            f"({kick_syncopation_rate:.0%} syncopation)"
                        ),
                        tick=chorus_start,
                        track="Drums",
                        details={
                            "kick_syncopation_rate": kick_syncopation_rate,
                            "bpm": bpm,
                        },
                    )

    # -----------------------------------------------------------------
    # Guitar analyses
    # -----------------------------------------------------------------

    def _analyze_guitar_rhythm_consistency(self):
        """Check guitar attack pattern consistency within sections.

        Quantizes bar-level attack positions to 8th-note grid and checks
        whether a dominant pattern exists within each section. Random
        attack patterns indicate generation issues.
        """
        guitar_notes = self.notes_by_channel.get(GUITAR_CHANNEL, [])
        if len(guitar_notes) < 8:
            return

        eighth = TICKS_PER_BEAT // 2

        inconsistent_sections = 0
        total_sections = 0

        for sec in self.sections:
            st = (sec['start_bar'] - 1) * TICKS_PER_BAR
            et = sec['end_bar'] * TICKS_PER_BAR
            sec_notes = [n for n in guitar_notes if st <= n.start < et]
            if len(sec_notes) < 4:
                continue

            # Group notes by bar and quantize attack positions to 8th grid
            bar_patterns = defaultdict(list)
            for note in sec_notes:
                bar = note.start // TICKS_PER_BAR
                pos_in_bar = note.start % TICKS_PER_BAR
                quantized = round(pos_in_bar / eighth)
                bar_patterns[bar].append(quantized)

            if len(bar_patterns) < 2:
                continue

            total_sections += 1

            # Normalize patterns to tuples for comparison
            pattern_strs = []
            for bar in sorted(bar_patterns.keys()):
                pattern_strs.append(tuple(sorted(bar_patterns[bar])))

            # Count most common pattern
            pattern_counts = defaultdict(int)
            for pat in pattern_strs:
                pattern_counts[pat] += 1

            most_common_count = max(pattern_counts.values())
            consistency = most_common_count / len(pattern_strs)

            if consistency < 0.3:
                inconsistent_sections += 1

        if total_sections == 0:
            return

        if inconsistent_sections > 0:
            ratio = inconsistent_sections / total_sections
            severity = Severity.WARNING if ratio > 0.5 else Severity.INFO
            self.add_issue(
                severity=severity,
                category=Category.RHYTHM,
                subcategory="guitar_rhythm_inconsistency",
                message=(f"Guitar has inconsistent rhythm patterns "
                         f"({inconsistent_sections}/{total_sections} sections)"),
                tick=0,
                track="Guitar",
                details={"inconsistent_sections": inconsistent_sections,
                         "total_sections": total_sections,
                         "ratio": ratio},
            )

    def _analyze_guitar_fingerpick_monotony(self):
        """Detect repetitive fingerpick patterns over 8+ consecutive bars.

        Only runs on fingerpick-style tracks (>70% single-note onsets).
        Compares bar-level pitch sequences for consecutive identical bars.
        """
        guitar_notes = self.notes_by_channel.get(GUITAR_CHANNEL, [])
        if len(guitar_notes) < 16:
            return

        # Detect fingerpick style: >70% single-note onsets
        onsets = defaultdict(list)
        for note in guitar_notes:
            onsets[note.start].append(note)

        total_onsets = len(onsets)
        if total_onsets == 0:
            return

        single_note_onsets = sum(
            1 for notes in onsets.values() if len(notes) == 1
        )
        single_ratio = single_note_onsets / total_onsets

        if single_ratio < 0.7:
            return  # Not fingerpick style

        # Group pitch sequences by bar
        bar_sequences = defaultdict(list)
        for note in guitar_notes:
            bar = note.start // TICKS_PER_BAR
            bar_sequences[bar].append(note.pitch)

        sorted_bars = sorted(bar_sequences.keys())
        if len(sorted_bars) < 8:
            return

        # Find consecutive identical bar patterns
        prev_seq = None
        consecutive_count = 0
        consecutive_start = 0

        for bar in sorted_bars:
            seq = tuple(bar_sequences[bar])
            if seq == prev_seq:
                consecutive_count += 1
            else:
                if consecutive_count >= 8:
                    self.add_issue(
                        severity=Severity.INFO,
                        category=Category.RHYTHM,
                        subcategory="guitar_fingerpick_monotony",
                        message=(f"Identical fingerpick pattern for "
                                 f"{consecutive_count + 1} bars"),
                        tick=consecutive_start * TICKS_PER_BAR,
                        track="Guitar",
                        details={"bar_count": consecutive_count + 1},
                    )
                consecutive_count = 1
                consecutive_start = bar
                prev_seq = seq

        # Final flush
        if consecutive_count >= 8:
            self.add_issue(
                severity=Severity.INFO,
                category=Category.RHYTHM,
                subcategory="guitar_fingerpick_monotony",
                message=(f"Identical fingerpick pattern for "
                         f"{consecutive_count + 1} bars"),
                tick=consecutive_start * TICKS_PER_BAR,
                track="Guitar",
                details={"bar_count": consecutive_count + 1},
            )
