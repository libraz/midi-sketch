"""Harmonic analysis: dissonance, voicing, bass line, chord function, resolution.

Detects harmonic issues including dissonant intervals between simultaneous
notes, chord voicing problems, bass line quality, chord function progressions,
dissonance resolution, harmonic rhythm stagnation, bass chord degree usage,
bass downbeat root analysis, bass contour patterns, beat-position-aware
dissonance severity, and bass-chord spacing.
"""

from collections import defaultdict
from typing import List

from ..constants import (
    TICKS_PER_BEAT, TICKS_PER_BAR, TRACK_NAMES, TRACK_CHANNELS,
    DISSONANT_INTERVALS, BASS_PREFERRED_DEGREES, BASS_ACCEPTABLE_DEGREES,
    DEGREE_TO_ROOT_PC, DEGREE_TO_CHORD_TONES,
    GUITAR_CHANNEL, GUITAR_BASS_MUD_THRESHOLD, GUITAR_STRUM_MIN_VOICES,
    Severity, Category,
)
from ..helpers import note_name, tick_to_bar
from .base import BaseAnalyzer


class HarmonicAnalyzer(BaseAnalyzer):
    """Analyzer for harmonic qualities across all pitched tracks.

    Checks for dissonant intervals between simultaneous notes, chord voicing
    issues, bass line monotony and sync, chord function progressions,
    dissonance resolution, harmonic rhythm stagnation, bass chord degree
    usage, bass downbeat root fidelity, bass contour quality,
    beat-position-weighted dissonance severity, and bass-chord spacing.
    """

    def analyze(self) -> List["Issue"]:
        """Run all harmonic analyses and return collected issues."""
        self._analyze_dissonance()
        self._analyze_chord_voicing()
        self._analyze_bass_line()
        self._analyze_chord_function()
        self._analyze_dissonance_resolution()
        self._analyze_harmonic_rhythm()
        self._analyze_bass_chord_degrees()
        self._analyze_bass_downbeat_root()
        self._analyze_bass_contour()
        self._analyze_dissonance_by_beat()
        self._analyze_bass_chord_spacing()
        self._analyze_guitar_voicing()
        self._analyze_guitar_bass_spacing()
        self._analyze_chord_duration_consistency()
        return self.issues

    # -----------------------------------------------------------------
    # Existing analyses
    # -----------------------------------------------------------------

    def _analyze_dissonance(self):
        """Detect dissonant intervals between simultaneous notes across channels."""
        time_slices = defaultdict(list)
        for note in self.notes:
            if note.channel == 9:  # Skip drums
                continue
            for tick in range(note.start, note.end, TICKS_PER_BEAT // 2):
                time_slices[tick].append(note)

        checked_pairs = set()

        for tick, active_notes in time_slices.items():
            pitch_info = [
                (note.pitch, note.channel, note)
                for note in active_notes
                if note.start <= tick < note.end
            ]

            for idx, (pitch_a, channel_a, note_a) in enumerate(pitch_info):
                for pitch_b, channel_b, note_b in pitch_info[idx + 1:]:
                    if channel_a == channel_b:
                        continue

                    raw_interval = abs(pitch_a - pitch_b)
                    interval = raw_interval % 12
                    pair_key = (min(note_a.start, note_b.start),
                                pitch_a, pitch_b, channel_a, channel_b)

                    if pair_key in checked_pairs:
                        continue
                    checked_pairs.add(pair_key)

                    # Only flag close voicing (within 12 semitones)
                    if raw_interval > 12 and interval in [1, 2]:
                        continue

                    if interval in DISSONANT_INTERVALS:
                        # Major 7th: wider voicings (24+ semitones) are less harsh
                        if interval == 11:
                            if raw_interval >= 36:
                                continue  # 3+ octaves: not perceptually dissonant
                            elif raw_interval > 23:
                                severity = Severity.INFO  # 2-3 octaves: notable but acceptable
                            else:
                                is_bass_collision = (
                                    (channel_a == 2 or channel_b == 2)
                                    and min(pitch_a, pitch_b) < 60
                                )
                                severity = (Severity.ERROR if is_bass_collision
                                            else Severity.WARNING)
                        else:
                            is_bass_collision = (
                                (channel_a == 2 or channel_b == 2)
                                and min(pitch_a, pitch_b) < 60
                            )
                            severity = (Severity.ERROR if is_bass_collision
                                        else Severity.WARNING)

                        track_a = TRACK_NAMES.get(channel_a, f"Ch{channel_a}")
                        track_b = TRACK_NAMES.get(channel_b, f"Ch{channel_b}")
                        self.add_issue(
                            severity=severity,
                            category=Category.HARMONIC,
                            subcategory="dissonance",
                            message=(f"{DISSONANT_INTERVALS[interval]}: "
                                     f"{track_a} {note_name(pitch_a)} vs "
                                     f"{track_b} {note_name(pitch_b)}"),
                            tick=tick,
                            track=f"{track_a}/{track_b}",
                            details={
                                "interval": DISSONANT_INTERVALS[interval],
                                "interval_semitones": raw_interval,
                                "track1": track_a,
                                "track2": track_b,
                                "pitch1": pitch_a,
                                "pitch2": pitch_b,
                            },
                        )

    def _analyze_chord_voicing(self):
        """Analyze chord track for voicing issues.

        Checks for thin/dense voicing, low/high register, chord above vocal
        ceiling, and consecutive same voicing repetition.
        """
        chord_notes = self.notes_by_channel.get(1, [])
        if not chord_notes:
            return

        # Get vocal ceiling for comparison
        vocal_notes = self.notes_by_channel.get(0, [])
        vocal_ceiling = (max((note.pitch for note in vocal_notes), default=84)
                         if vocal_notes else 84)

        chords_by_time = defaultdict(list)
        for note in chord_notes:
            chords_by_time[note.start].append(note)

        sorted_ticks = sorted(chords_by_time.keys())

        # --- Per-onset voicing analysis (thin/dense/register/ceiling) ---
        for tick in sorted_ticks:
            chord = chords_by_time[tick]
            pitches = tuple(sorted([note.pitch for note in chord]))
            voicing_count = len(pitches)

            # Check for thin voicing (1-2 voices)
            if voicing_count == 1:
                self.add_issue(
                    severity=Severity.WARNING,
                    category=Category.HARMONIC,
                    subcategory="thin_voicing",
                    message=f"Only 1 voice ({note_name(pitches[0])})",
                    tick=tick,
                    track="Chord",
                    details={"voice_count": 1, "pitches": list(pitches)},
                )
            elif voicing_count == 2:
                self.add_issue(
                    severity=Severity.INFO,
                    category=Category.HARMONIC,
                    subcategory="thin_voicing",
                    message=(f"Only 2 voices ({note_name(pitches[0])}, "
                             f"{note_name(pitches[1])})"),
                    tick=tick,
                    track="Chord",
                    details={"voice_count": 2, "pitches": list(pitches)},
                )

            if voicing_count > 5:
                self.add_issue(
                    severity=Severity.INFO,
                    category=Category.HARMONIC,
                    subcategory="dense_voicing",
                    message=f"Dense voicing ({voicing_count} voices)",
                    tick=tick,
                    track="Chord",
                    details={"voice_count": voicing_count},
                )

            if pitches:
                lowest, highest = min(pitches), max(pitches)

                if lowest < 48:  # Below C3
                    self.add_issue(
                        severity=Severity.WARNING,
                        category=Category.HARMONIC,
                        subcategory="chord_register_low",
                        message=f"Low register ({note_name(lowest)})",
                        tick=tick,
                        track="Chord",
                        details={"lowest_pitch": lowest},
                    )
                if highest > 84:  # Above C6
                    self.add_issue(
                        severity=Severity.WARNING,
                        category=Category.HARMONIC,
                        subcategory="chord_register_high",
                        message=f"High register ({note_name(highest)})",
                        tick=tick,
                        track="Chord",
                        details={"highest_pitch": highest},
                    )

                if highest > vocal_ceiling + 2:
                    self.add_issue(
                        severity=Severity.WARNING,
                        category=Category.HARMONIC,
                        subcategory="chord_above_vocal",
                        message=(f"Chord ({note_name(highest)}) exceeds "
                                 f"vocal ceiling ({note_name(vocal_ceiling)})"),
                        tick=tick,
                        track="Chord",
                        details={"chord_highest": highest,
                                 "vocal_ceiling": vocal_ceiling},
                    )

        # --- Bar-level voicing repetition detection ---
        chords_by_bar = defaultdict(set)
        for note in chord_notes:
            bar_idx = note.start // TICKS_PER_BAR
            chords_by_bar[bar_idx].add(note.pitch)

        sorted_bars = sorted(chords_by_bar.keys())
        prev_bar_pitches = None
        prev_bar_idx = None
        consecutive_same_count = 0
        consecutive_same_start = 0

        for bar_idx in sorted_bars:
            bar_pitches = tuple(sorted(chords_by_bar[bar_idx]))

            if bar_pitches == prev_bar_pitches:
                if consecutive_same_count == 0:
                    # Start tick is the first bar of the repetition
                    consecutive_same_start = prev_bar_idx * TICKS_PER_BAR
                consecutive_same_count += 1
            else:
                if consecutive_same_count >= 3:
                    self.add_issue(
                        severity=(Severity.WARNING if consecutive_same_count >= 5
                                  else Severity.INFO),
                        category=Category.HARMONIC,
                        subcategory="chord_repetition",
                        message=(f"{consecutive_same_count + 1} consecutive bars "
                                 f"same voicing"),
                        tick=consecutive_same_start,
                        track="Chord",
                        details={"count": consecutive_same_count + 1,
                                 "pitches": list(prev_bar_pitches)
                                 if prev_bar_pitches else []},
                    )
                consecutive_same_count = 0
                prev_bar_pitches = bar_pitches
            prev_bar_idx = bar_idx

        # Final flush
        if consecutive_same_count >= 3:
            self.add_issue(
                severity=(Severity.WARNING if consecutive_same_count >= 5
                          else Severity.INFO),
                category=Category.HARMONIC,
                subcategory="chord_repetition",
                message=(f"{consecutive_same_count + 1} consecutive bars "
                         f"same voicing"),
                tick=consecutive_same_start,
                track="Chord",
                details={"count": consecutive_same_count + 1,
                         "pitches": list(prev_bar_pitches)
                         if prev_bar_pitches else []},
            )

    def _analyze_bass_line(self):
        """Analyze bass line for monotony, kick sync, empty bars, and stepwise rate."""
        bass_notes = self.notes_by_channel.get(2, [])
        if len(bass_notes) < 2:
            return

        # Consecutive same pitch detection
        consecutive_count = 1
        current_pitch = bass_notes[0].pitch
        start_tick = bass_notes[0].start

        for idx in range(1, len(bass_notes)):
            if bass_notes[idx].pitch == current_pitch:
                consecutive_count += 1
            else:
                if consecutive_count >= 8:
                    self.add_issue(
                        severity=Severity.WARNING,
                        category=Category.HARMONIC,
                        subcategory="bass_monotony",
                        message=f"{consecutive_count} consecutive {note_name(current_pitch)}",
                        tick=start_tick,
                        track="Bass",
                        details={"pitch": current_pitch, "count": consecutive_count},
                    )
                consecutive_count = 1
                current_pitch = bass_notes[idx].pitch
                start_tick = bass_notes[idx].start

        if consecutive_count >= 8:
            self.add_issue(
                severity=Severity.WARNING,
                category=Category.HARMONIC,
                subcategory="bass_monotony",
                message=f"{consecutive_count} consecutive {note_name(current_pitch)}",
                tick=start_tick,
                track="Bass",
                details={"pitch": current_pitch, "count": consecutive_count},
            )

        # Bass-kick sync
        drums = self.notes_by_channel.get(9, [])
        kick_notes = [note for note in drums if note.pitch == 36]
        if kick_notes and bass_notes:
            bass_attacks = set()
            for note in bass_notes:
                quantized = (round(note.start / (TICKS_PER_BEAT // 4))
                             * (TICKS_PER_BEAT // 4))
                bass_attacks.add(quantized)

            kick_attacks = set()
            for note in kick_notes:
                quantized = (round(note.start / (TICKS_PER_BEAT // 4))
                             * (TICKS_PER_BEAT // 4))
                kick_attacks.add(quantized)

            if kick_attacks:
                sync_count = len(kick_attacks & bass_attacks)
                sync_ratio = sync_count / len(kick_attacks)
                if sync_ratio < 0.5:
                    self.add_issue(
                        severity=Severity.INFO,
                        category=Category.HARMONIC,
                        subcategory="bass_kick_sync",
                        message=f"Low bass-kick sync ({sync_ratio:.0%})",
                        tick=0,
                        track="Bass",
                        details={"sync_ratio": sync_ratio,
                                 "sync_count": sync_count,
                                 "kick_count": len(kick_attacks)},
                    )

        # Empty bars (bars 3 to max-1)
        max_bar = max((tick_to_bar(note.start) for note in self.notes), default=0)
        if max_bar > 4:
            bass_bars = set(tick_to_bar(note.start) for note in bass_notes)
            empty_bars = []
            for bar in range(3, max_bar - 1):
                if bar not in bass_bars:
                    empty_bars.append(bar)
            if empty_bars:
                self.add_issue(
                    severity=Severity.INFO,
                    category=Category.HARMONIC,
                    subcategory="bass_empty_bars",
                    message=f"{len(empty_bars)} bars with no bass notes",
                    tick=(empty_bars[0] - 1) * TICKS_PER_BAR,
                    track="Bass",
                    details={"empty_bars": empty_bars[:10],
                             "count": len(empty_bars)},
                )

        # Stepwise rate
        if len(bass_notes) >= 4:
            step_count = 0
            total_moves = 0
            for idx in range(1, len(bass_notes)):
                interval_val = abs(bass_notes[idx].pitch - bass_notes[idx - 1].pitch)
                if interval_val == 0:
                    continue
                total_moves += 1
                if interval_val <= 2:
                    step_count += 1

            if total_moves > 4:
                stepwise_rate = step_count / total_moves
                if stepwise_rate < 0.3:
                    self.add_issue(
                        severity=Severity.INFO,
                        category=Category.HARMONIC,
                        subcategory="bass_stepwise_rate",
                        message=f"Bass too jumpy (stepwise: {stepwise_rate:.0%})",
                        tick=0,
                        track="Bass",
                        details={"stepwise_rate": stepwise_rate,
                                 "step_count": step_count,
                                 "total_moves": total_moves},
                    )
                elif stepwise_rate > 0.9:
                    self.add_issue(
                        severity=Severity.INFO,
                        category=Category.HARMONIC,
                        subcategory="bass_stepwise_rate",
                        message=f"Bass too scalar (stepwise: {stepwise_rate:.0%})",
                        tick=0,
                        track="Bass",
                        details={"stepwise_rate": stepwise_rate,
                                 "step_count": step_count,
                                 "total_moves": total_moves},
                    )

    def _analyze_chord_function(self):
        """Analyze chord function progression (T/D/S), flag D->S retrograde."""
        bass_notes = self.notes_by_channel.get(2, [])
        if len(bass_notes) < 4:
            return

        bass_by_bar = defaultdict(list)
        for note in bass_notes:
            bar = tick_to_bar(note.start)
            bass_by_bar[bar].append(note)

        def get_function(root: int) -> str:
            if root in [0, 9, 4]:
                return "T"
            elif root in [7, 11]:
                return "D"
            elif root in [5, 2]:
                return "S"
            return "?"

        bars = sorted(bass_by_bar.keys())
        prev_func = None
        for bar in bars:
            bar_notes = bass_by_bar[bar]
            if not bar_notes:
                continue
            # Use the pitch class of the note closest to beat 1
            first_note = min(bar_notes, key=lambda n: n.start)
            root = first_note.pitch % 12
            func = get_function(root)

            if prev_func == "D" and func == "S":
                self.add_issue(
                    severity=Severity.INFO,
                    category=Category.HARMONIC,
                    subcategory="chord_function",
                    message="D->S retrograde motion",
                    tick=(bar - 1) * TICKS_PER_BAR,
                    track="Harmony",
                    details={"bar": bar, "motion": "D->S"},
                )

            prev_func = func

    def _analyze_dissonance_resolution(self):
        """Check if dissonances resolve stepwise within 2 beats."""
        dissonance_issues = [
            issue for issue in self.issues if issue.subcategory == "dissonance"
        ]
        resolved_ticks = set()

        for issue in dissonance_issues:
            tick = issue.tick
            details = issue.details
            pitch_a = details.get('pitch1', 0)
            pitch_b = details.get('pitch2', 0)
            track_a_name = details.get('track1', '')
            track_b_name = details.get('track2', '')

            resolve_window = tick + TICKS_PER_BEAT * 2

            for track_name, pitch in [(track_a_name, pitch_a), (track_b_name, pitch_b)]:
                channel = TRACK_CHANNELS.get(track_name, -1)
                notes = self.notes_by_channel.get(channel, [])
                for note in notes:
                    if tick < note.start <= resolve_window:
                        if abs(note.pitch - pitch) in (1, 2):
                            resolved_ticks.add(tick)
                            break

        for issue in self.issues:
            if (issue.subcategory == "dissonance"
                    and issue.tick in resolved_ticks
                    and issue.severity == Severity.WARNING):
                issue.severity = Severity.INFO
                issue.message += " (resolved)"

    def _analyze_harmonic_rhythm(self):
        """Detect stagnant harmony (no chord change for too long)."""
        chord_notes = self.notes_by_channel.get(1, [])
        if not chord_notes:
            return

        chords_by_bar = defaultdict(list)
        for note in chord_notes:
            bar = tick_to_bar(note.start)
            chords_by_bar[bar].append(note.pitch % 12)

        bars = sorted(chords_by_bar.keys())
        if len(bars) < 3:
            return

        same_count = 1
        prev_roots = set(chords_by_bar[bars[0]])
        same_start = bars[0]

        for idx in range(1, len(bars)):
            curr_roots = set(chords_by_bar[bars[idx]])
            if curr_roots == prev_roots:
                same_count += 1
            else:
                if same_count > 3:
                    self.add_issue(
                        severity=Severity.INFO,
                        category=Category.HARMONIC,
                        subcategory="harmonic_stagnation",
                        message=f"Same harmony for {same_count} bars",
                        tick=(same_start - 1) * TICKS_PER_BAR,
                        track="Chord",
                        details={"bar_count": same_count},
                    )
                same_count = 1
                same_start = bars[idx]
            prev_roots = curr_roots

        if same_count > 3:
            self.add_issue(
                severity=Severity.INFO,
                category=Category.HARMONIC,
                subcategory="harmonic_stagnation",
                message=f"Same harmony for {same_count} bars",
                tick=(same_start - 1) * TICKS_PER_BAR,
                track="Chord",
                details={"bar_count": same_count},
            )

    # -----------------------------------------------------------------
    # New analyses
    # -----------------------------------------------------------------

    def _analyze_bass_chord_degrees(self):
        """Analyze whether bass notes use chord tones (root, 3rd, 5th).

        Uses provenance chord_degree (the active chord's scale degree: 0=I,
        3=IV, etc.) to derive the expected chord tones, then checks if the
        bass pitch is a chord tone. Issues a single aggregate warning if the
        ratio of non-chord-tone bass notes is too high.
        """
        bass_notes = self.notes_by_channel.get(2, [])
        if not bass_notes:
            return

        total_checked = 0
        root_count = 0
        fifth_count = 0
        third_count = 0
        non_chord_tone_count = 0

        for note in bass_notes:
            if not note.provenance or 'chord_degree' not in note.provenance:
                continue

            degree = note.provenance['chord_degree']
            if degree < 0 or degree not in DEGREE_TO_ROOT_PC:
                continue

            total_checked += 1
            bass_pc = note.pitch % 12
            root_pc = DEGREE_TO_ROOT_PC[degree]
            chord_tones = DEGREE_TO_CHORD_TONES.get(degree, set())

            if bass_pc == root_pc:
                root_count += 1
            elif bass_pc in chord_tones:
                # Identify 3rd vs 5th
                # 5th is 7 semitones above root (or tritone for vii)
                fifth_pc = (root_pc + 7) % 12
                if degree == 6:
                    fifth_pc = (root_pc + 6) % 12  # diminished 5th for vii
                if bass_pc == fifth_pc:
                    fifth_count += 1
                else:
                    third_count += 1
            else:
                non_chord_tone_count += 1

        if total_checked == 0:
            return

        non_chord_tone_ratio = non_chord_tone_count / total_checked
        root_fifth_ratio = (root_count + fifth_count) / total_checked

        if non_chord_tone_ratio > 0.3:
            self.add_issue(
                severity=Severity.WARNING,
                category=Category.HARMONIC,
                subcategory="bass_chord_degrees",
                message=(f"Bass uses non-chord-tones "
                         f"{non_chord_tone_ratio:.0%} of the time"),
                tick=0,
                track="Bass",
                details={"non_chord_tone_ratio": non_chord_tone_ratio,
                         "root_ratio": root_count / total_checked,
                         "fifth_ratio": fifth_count / total_checked,
                         "third_ratio": third_count / total_checked,
                         "total_checked": total_checked},
            )
        elif root_fifth_ratio < 0.5:
            self.add_issue(
                severity=Severity.INFO,
                category=Category.HARMONIC,
                subcategory="bass_chord_degrees",
                message=(f"Bass root+5th usage low "
                         f"({root_fifth_ratio:.0%})"),
                tick=0,
                track="Bass",
                details={"root_fifth_ratio": root_fifth_ratio,
                         "root_ratio": root_count / total_checked,
                         "fifth_ratio": fifth_count / total_checked,
                         "third_ratio": third_count / total_checked,
                         "total_checked": total_checked},
            )

    def _analyze_bass_downbeat_root(self):
        """Check if bass notes on beat 1 play the chord root.

        Bass notes on the downbeat should typically play the root of the
        active chord for harmonic stability. Compares actual bass pitch
        against the chord root derived from provenance chord_degree.
        Issues a single aggregate warning based on the non-root ratio.
        """
        bass_notes = self.notes_by_channel.get(2, [])
        if not bass_notes:
            return

        total_beat1 = 0
        root_count = 0
        fifth_count = 0

        for note in bass_notes:
            beat, offset = self.get_beat_position(note.start)
            if beat != 1:
                continue

            if not note.provenance or 'chord_degree' not in note.provenance:
                continue

            degree = note.provenance['chord_degree']
            if degree < 0 or degree not in DEGREE_TO_ROOT_PC:
                continue

            total_beat1 += 1
            bass_pc = note.pitch % 12
            root_pc = DEGREE_TO_ROOT_PC[degree]
            fifth_pc = (root_pc + 7) % 12
            if degree == 6:
                fifth_pc = (root_pc + 6) % 12

            if bass_pc == root_pc:
                root_count += 1
            elif bass_pc == fifth_pc:
                fifth_count += 1

        if total_beat1 == 0:
            return

        non_root_ratio = 1.0 - root_count / total_beat1
        non_root_fifth_ratio = 1.0 - (root_count + fifth_count) / total_beat1

        if non_root_fifth_ratio > 0.6:
            self.add_issue(
                severity=Severity.WARNING,
                category=Category.HARMONIC,
                subcategory="bass_downbeat_root",
                message=(f"Bass on beat 1 is not root/5th "
                         f"{non_root_fifth_ratio:.0%} of the time"),
                tick=0,
                track="Bass",
                details={"non_root_fifth_ratio": non_root_fifth_ratio,
                         "root_ratio": root_count / total_beat1,
                         "fifth_ratio": fifth_count / total_beat1,
                         "total_beat1": total_beat1},
            )
        elif non_root_ratio > 0.5:
            self.add_issue(
                severity=Severity.INFO,
                category=Category.HARMONIC,
                subcategory="bass_downbeat_root",
                message=(f"Bass on beat 1 is not root "
                         f"{non_root_ratio:.0%} of the time"),
                tick=0,
                track="Bass",
                details={"non_root_ratio": non_root_ratio,
                         "root_ratio": root_count / total_beat1,
                         "fifth_ratio": fifth_count / total_beat1,
                         "total_beat1": total_beat1},
            )

    def _analyze_bass_contour(self):
        """Analyze bass motion patterns over 4-bar windows.

        Classifies each window as pedal (same pitch >75%), walking
        (stepwise >60%), arpeggiated (intervals 3-7 semitones or octave
        >50%), or random. Warns if >50% of windows are random.
        """
        bass_notes = self.notes_by_channel.get(2, [])
        if len(bass_notes) < 4:
            return

        max_bar = max(tick_to_bar(note.start) for note in bass_notes)
        window_size = 4
        window_types = []

        for window_start in range(1, max_bar + 1, window_size):
            window_end = window_start + window_size - 1
            window_notes = [
                note for note in bass_notes
                if window_start <= tick_to_bar(note.start) <= window_end
            ]
            if len(window_notes) < 2:
                continue

            pitches = [note.pitch for note in window_notes]
            total_notes = len(pitches)

            # Count same pitch occurrences (for pedal detection)
            most_common_count = max(
                pitches.count(pitch) for pitch in set(pitches)
            )
            same_ratio = most_common_count / total_notes

            if same_ratio > 0.75:
                window_types.append("pedal")
                continue

            # Classify intervals between consecutive notes
            stepwise_count = 0
            arpeggiated_count = 0
            total_intervals = 0

            for idx in range(1, len(window_notes)):
                interval = abs(window_notes[idx].pitch - window_notes[idx - 1].pitch)
                if interval == 0:
                    continue
                total_intervals += 1
                if interval <= 2:
                    stepwise_count += 1
                elif 3 <= interval <= 7 or interval == 12:
                    # Include P4(5), P5(7), and octave(12) as structured motion
                    arpeggiated_count += 1

            if total_intervals == 0:
                window_types.append("pedal")
                continue

            stepwise_ratio = stepwise_count / total_intervals
            arpeggiated_ratio = arpeggiated_count / total_intervals

            if stepwise_ratio > 0.6:
                window_types.append("walking")
            elif arpeggiated_ratio > 0.5:
                window_types.append("arpeggiated")
            else:
                window_types.append("random")

        if not window_types:
            return

        random_count = window_types.count("random")
        random_ratio = random_count / len(window_types)

        if random_ratio > 0.5:
            self.add_issue(
                severity=Severity.WARNING,
                category=Category.HARMONIC,
                subcategory="bass_contour",
                message=f"Bass contour is mostly random ({random_ratio:.0%})",
                tick=0,
                track="Bass",
                details={"random_ratio": random_ratio,
                         "random_count": random_count,
                         "total_windows": len(window_types),
                         "window_types": window_types},
            )

    def _analyze_dissonance_by_beat(self):
        """Adjust dissonance severity based on beat position.

        Dissonances on beat 1 (strong downbeat) are upgraded from WARNING
        to ERROR. Dissonances on beats 2 or 4 (weak beats, passing tone
        tolerance) are downgraded from ERROR to WARNING. Modifies existing
        dissonance issues in place.
        """
        for issue in self.issues:
            if issue.subcategory != "dissonance":
                continue

            beat, offset = self.get_beat_position(issue.tick)

            if beat == 1 and issue.severity == Severity.WARNING:
                issue.severity = Severity.ERROR
            elif beat in (2, 4) and issue.severity == Severity.ERROR:
                issue.severity = Severity.WARNING

    def _analyze_bass_chord_spacing(self):
        """Check interval between bass and lowest chord note.

        Flags cases where both bass and chord notes are below C4 (MIDI 60)
        and the interval between them is less than a perfect 4th (5 semitones),
        which creates muddy low-register voicing. Unisons (interval 0) are
        excluded as they represent intentional doubling. Issues a single
        aggregate warning based on the close-spacing ratio.
        """
        bass_notes = self.notes_by_channel.get(2, [])
        chord_notes = self.notes_by_channel.get(1, [])
        if not bass_notes or not chord_notes:
            return

        total_checked = 0
        close_count = 0

        for bass_note in bass_notes:
            # Find chord notes sounding at the bass note's start tick
            sounding_chord_notes = []
            for chord_note in chord_notes:
                if chord_note.start <= bass_note.start < chord_note.end:
                    sounding_chord_notes.append(chord_note)

            if not sounding_chord_notes:
                continue

            lowest_chord_pitch = min(note.pitch for note in sounding_chord_notes)

            # Both must be below C4 (60)
            if bass_note.pitch >= 60 or lowest_chord_pitch >= 60:
                continue

            interval = abs(bass_note.pitch - lowest_chord_pitch)

            # Skip unisons (interval 0) — intentional doubling is fine
            if interval == 0:
                continue

            total_checked += 1
            if interval < 5:  # Less than perfect 4th
                close_count += 1

        if total_checked == 0:
            return

        close_ratio = close_count / total_checked
        if close_ratio > 0.5:
            self.add_issue(
                severity=Severity.WARNING,
                category=Category.HARMONIC,
                subcategory="bass_chord_spacing",
                message=(f"Bass-chord spacing too close "
                         f"{close_ratio:.0%} of the time"),
                tick=0,
                track="Bass/Chord",
                details={"close_ratio": close_ratio,
                         "close_count": close_count,
                         "total_checked": total_checked},
            )
        elif close_ratio > 0.3:
            self.add_issue(
                severity=Severity.INFO,
                category=Category.HARMONIC,
                subcategory="bass_chord_spacing",
                message=(f"Bass-chord spacing somewhat close "
                         f"{close_ratio:.0%} of the time"),
                tick=0,
                track="Bass/Chord",
                details={"close_ratio": close_ratio,
                         "close_count": close_count,
                         "total_checked": total_checked},
            )

    # -----------------------------------------------------------------
    # Guitar analyses
    # -----------------------------------------------------------------

    def _analyze_guitar_voicing(self):
        """Analyze guitar track for voicing issues.

        Checks for thin strum voicing (<3 voices), guitar above vocal
        ceiling, and consecutive bar-level voicing repetition.
        PowerChord style (mostly 2-note onsets, no 3+ note onsets) is
        excluded from thin voicing checks.
        """
        guitar_notes = self.notes_by_channel.get(GUITAR_CHANNEL, [])
        if not guitar_notes:
            return

        # Get vocal ceiling for comparison
        vocal_notes = self.notes_by_channel.get(0, [])
        vocal_ceiling = (max((note.pitch for note in vocal_notes), default=84)
                         if vocal_notes else 84)

        # Group notes by onset
        onsets = defaultdict(list)
        for note in guitar_notes:
            onsets[note.start].append(note)

        sorted_ticks = sorted(onsets.keys())
        total_onsets = len(sorted_ticks)
        if total_onsets == 0:
            return

        # Classify style: count multi-note onsets
        multi_note_onsets = 0
        three_plus_onsets = 0
        thin_strum_count = 0
        above_vocal_count = 0

        for tick in sorted_ticks:
            chord = onsets[tick]
            pitches = sorted(set(note.pitch for note in chord))
            voice_count = len(pitches)

            if voice_count >= 2:
                multi_note_onsets += 1
            if voice_count >= 3:
                three_plus_onsets += 1

            # Thin voicing check (will be gated by strum detection below)
            if voice_count >= 2 and voice_count < GUITAR_STRUM_MIN_VOICES:
                thin_strum_count += 1

            # Above vocal ceiling check
            if pitches and max(pitches) > vocal_ceiling + 2:
                above_vocal_count += 1

        multi_ratio = multi_note_onsets / total_onsets if total_onsets > 0 else 0

        # --- Thin voicing (strum style only) ---
        # Detect strum style: >30% multi-note onsets
        is_strum = multi_ratio > 0.3
        # Exclude PowerChord: mostly 2-note onsets with no 3+ note onsets
        is_power_chord = (multi_note_onsets > 0
                          and three_plus_onsets == 0)

        if is_strum and not is_power_chord and multi_note_onsets > 0:
            thin_ratio = thin_strum_count / multi_note_onsets
            if thin_ratio > 0.5:
                self.add_issue(
                    severity=Severity.WARNING,
                    category=Category.HARMONIC,
                    subcategory="guitar_thin_voicing",
                    message=(f"Guitar strum voicing thin "
                             f"({thin_ratio:.0%} of strums < {GUITAR_STRUM_MIN_VOICES} voices)"),
                    tick=0,
                    track="Guitar",
                    details={"thin_ratio": thin_ratio,
                             "thin_count": thin_strum_count,
                             "multi_note_onsets": multi_note_onsets},
                )

        # --- Above vocal ceiling ---
        if total_onsets > 0:
            above_ratio = above_vocal_count / total_onsets
            if above_ratio > 0.15:
                self.add_issue(
                    severity=Severity.WARNING,
                    category=Category.HARMONIC,
                    subcategory="guitar_above_vocal",
                    message=(f"Guitar exceeds vocal ceiling "
                             f"({above_ratio:.0%} of onsets)"),
                    tick=0,
                    track="Guitar",
                    details={"above_ratio": above_ratio,
                             "above_count": above_vocal_count,
                             "vocal_ceiling": vocal_ceiling},
                )
            elif above_ratio > 0.05:
                self.add_issue(
                    severity=Severity.INFO,
                    category=Category.HARMONIC,
                    subcategory="guitar_above_vocal",
                    message=(f"Guitar occasionally exceeds vocal ceiling "
                             f"({above_ratio:.0%} of onsets)"),
                    tick=0,
                    track="Guitar",
                    details={"above_ratio": above_ratio,
                             "above_count": above_vocal_count,
                             "vocal_ceiling": vocal_ceiling},
                )

        # --- Bar-level voicing repetition ---
        guitar_by_bar = defaultdict(set)
        for note in guitar_notes:
            bar_idx = note.start // TICKS_PER_BAR
            guitar_by_bar[bar_idx].add(note.pitch)

        sorted_bars = sorted(guitar_by_bar.keys())
        prev_bar_pitches = None
        prev_bar_idx = None
        consecutive_same_count = 0
        consecutive_same_start = 0

        for bar_idx in sorted_bars:
            bar_pitches = tuple(sorted(guitar_by_bar[bar_idx]))

            if bar_pitches == prev_bar_pitches:
                if consecutive_same_count == 0:
                    consecutive_same_start = prev_bar_idx * TICKS_PER_BAR
                consecutive_same_count += 1
            else:
                if consecutive_same_count >= 6:
                    self.add_issue(
                        severity=(Severity.WARNING if consecutive_same_count >= 9
                                  else Severity.INFO),
                        category=Category.HARMONIC,
                        subcategory="guitar_voicing_repetition",
                        message=(f"{consecutive_same_count + 1} consecutive bars "
                                 f"same guitar voicing"),
                        tick=consecutive_same_start,
                        track="Guitar",
                        details={"count": consecutive_same_count + 1,
                                 "pitches": list(prev_bar_pitches)
                                 if prev_bar_pitches else []},
                    )
                consecutive_same_count = 0
                prev_bar_pitches = bar_pitches
            prev_bar_idx = bar_idx

        # Final flush
        if consecutive_same_count >= 6:
            self.add_issue(
                severity=(Severity.WARNING if consecutive_same_count >= 9
                          else Severity.INFO),
                category=Category.HARMONIC,
                subcategory="guitar_voicing_repetition",
                message=(f"{consecutive_same_count + 1} consecutive bars "
                         f"same guitar voicing"),
                tick=consecutive_same_start,
                track="Guitar",
                details={"count": consecutive_same_count + 1,
                         "pitches": list(prev_bar_pitches)
                         if prev_bar_pitches else []},
            )

    def _analyze_guitar_bass_spacing(self):
        """Check low-register guitar-bass muddiness.

        Flags guitar notes below E3 (52) that are within a P5 (7 semitones)
        of concurrent bass notes. Unisons are excluded as intentional doubling.
        """
        guitar_notes = self.notes_by_channel.get(GUITAR_CHANNEL, [])
        bass_notes = self.notes_by_channel.get(2, [])
        if not guitar_notes or not bass_notes:
            return

        total_checked = 0
        muddy_count = 0

        for guitar_note in guitar_notes:
            if guitar_note.pitch >= GUITAR_BASS_MUD_THRESHOLD:
                continue

            # Find bass notes sounding at this guitar note's start
            sounding_bass = [
                bn for bn in bass_notes
                if bn.start <= guitar_note.start < bn.end
            ]
            if not sounding_bass:
                continue

            total_checked += 1
            for bass_note in sounding_bass:
                interval = abs(guitar_note.pitch - bass_note.pitch)
                if 0 < interval < 7:  # Exclude unisons, flag < P5
                    muddy_count += 1
                    break

        if total_checked == 0:
            return

        muddy_ratio = muddy_count / total_checked
        if muddy_ratio > 0.4:
            self.add_issue(
                severity=Severity.WARNING,
                category=Category.HARMONIC,
                subcategory="guitar_bass_mud",
                message=(f"Guitar-bass low register muddiness "
                         f"({muddy_ratio:.0%} of low guitar notes)"),
                tick=0,
                track="Guitar/Bass",
                details={"muddy_ratio": muddy_ratio,
                         "muddy_count": muddy_count,
                         "total_checked": total_checked},
            )
        elif muddy_ratio > 0.2:
            self.add_issue(
                severity=Severity.INFO,
                category=Category.HARMONIC,
                subcategory="guitar_bass_mud",
                message=(f"Guitar-bass low register somewhat muddy "
                         f"({muddy_ratio:.0%} of low guitar notes)"),
                tick=0,
                track="Guitar/Bass",
                details={"muddy_ratio": muddy_ratio,
                         "muddy_count": muddy_count,
                         "total_checked": total_checked},
            )

    def _analyze_chord_duration_consistency(self):
        """Check duration consistency within chord onsets.

        Groups chord track (channel 1) notes by start_ticks and checks
        if notes within the same onset have wildly different durations.
        A max/min ratio > 3.0 is flagged as ERROR, > 2.0 as WARNING.
        If more than 30% of onsets are inconsistent, an aggregate
        WARNING is also issued.
        """
        chord_notes = self.notes_by_channel.get(1, [])
        if not chord_notes:
            return

        onsets = defaultdict(list)
        for note in chord_notes:
            onsets[note.start].append(note)

        inconsistent_count = 0
        total_checked = 0

        for tick in sorted(onsets.keys()):
            notes = onsets[tick]
            if len(notes) < 2:
                continue

            durations = [n.duration for n in notes]
            min_dur = min(durations)
            max_dur = max(durations)

            if min_dur <= 0:
                continue

            total_checked += 1
            ratio = max_dur / min_dur

            if ratio > 3.0:
                inconsistent_count += 1
                self.add_issue(
                    severity=Severity.ERROR,
                    category=Category.HARMONIC,
                    subcategory="chord_duration_mismatch",
                    message=(f"Chord voices have {ratio:.1f}:1 duration ratio "
                             f"(max={max_dur}, min={min_dur})"),
                    tick=tick,
                    track="Chord",
                    details={
                        "ratio": ratio,
                        "max_duration": max_dur,
                        "min_duration": min_dur,
                        "voice_count": len(notes),
                    },
                )
            elif ratio > 2.0:
                inconsistent_count += 1
                self.add_issue(
                    severity=Severity.WARNING,
                    category=Category.HARMONIC,
                    subcategory="chord_duration_mismatch",
                    message=(f"Chord voices have {ratio:.1f}:1 duration ratio "
                             f"(max={max_dur}, min={min_dur})"),
                    tick=tick,
                    track="Chord",
                    details={
                        "ratio": ratio,
                        "max_duration": max_dur,
                        "min_duration": min_dur,
                        "voice_count": len(notes),
                    },
                )

        if total_checked > 0:
            incon_ratio = inconsistent_count / total_checked
            if incon_ratio > 0.3:
                agg_severity = Severity.ERROR if incon_ratio > 0.6 else Severity.WARNING
                self.add_issue(
                    severity=agg_severity,
                    category=Category.HARMONIC,
                    subcategory="chord_duration_mismatch",
                    message=(f"Chord duration inconsistency widespread "
                             f"({inconsistent_count}/{total_checked} onsets)"),
                    tick=0,
                    track="Chord",
                    details={
                        "inconsistent_count": inconsistent_count,
                        "total_checked": total_checked,
                        "ratio": incon_ratio,
                    },
                )
