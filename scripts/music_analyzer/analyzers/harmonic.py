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
        prev_pitches = None
        consecutive_same_count = 0
        consecutive_same_start = 0

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

            if pitches == prev_pitches:
                if consecutive_same_count == 0:
                    consecutive_same_start = tick
                consecutive_same_count += 1
            else:
                if consecutive_same_count >= 4:
                    self.add_issue(
                        severity=(Severity.WARNING if consecutive_same_count >= 6
                                  else Severity.INFO),
                        category=Category.HARMONIC,
                        subcategory="chord_repetition",
                        message=f"{consecutive_same_count + 1} consecutive same voicing",
                        tick=consecutive_same_start,
                        track="Chord",
                        details={"count": consecutive_same_count + 1,
                                 "pitches": list(prev_pitches) if prev_pitches else []},
                    )
                consecutive_same_count = 0
                prev_pitches = pitches

        if consecutive_same_count >= 4:
            self.add_issue(
                severity=(Severity.WARNING if consecutive_same_count >= 6
                          else Severity.INFO),
                category=Category.HARMONIC,
                subcategory="chord_repetition",
                message=f"{consecutive_same_count + 1} consecutive same voicing",
                tick=consecutive_same_start,
                track="Chord",
                details={"count": consecutive_same_count + 1,
                         "pitches": list(prev_pitches) if prev_pitches else []},
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
            bass_by_bar[bar].append(note.pitch % 12)

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
            roots = bass_by_bar[bar]
            if not roots:
                continue
            root = min(roots)
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
        """Analyze bass note chord degree usage via provenance data.

        Checks whether bass notes predominantly use root (0) and 5th (4)
        degrees. Flags individual non-preferred degree usage and warns
        if the overall ratio of non-preferred degrees exceeds 30%.
        """
        bass_notes = self.notes_by_channel.get(2, [])
        if not bass_notes:
            return

        degree_counts = defaultdict(int)
        total_with_degree = 0
        non_preferred_count = 0

        for note in bass_notes:
            if not note.provenance or 'chord_degree' not in note.provenance:
                continue

            degree = note.provenance['chord_degree']
            if degree < 0:
                continue

            degree_counts[degree] += 1
            total_with_degree += 1

            if degree not in BASS_PREFERRED_DEGREES:
                non_preferred_count += 1
                if degree == 2:  # 3rd degree
                    self.add_issue(
                        severity=Severity.INFO,
                        category=Category.HARMONIC,
                        subcategory="bass_chord_degrees",
                        message=(f"Bass uses 3rd degree at "
                                 f"{note_name(note.pitch)}"),
                        tick=note.start,
                        track="Bass",
                        details={"degree": degree, "pitch": note.pitch},
                    )
                else:
                    self.add_issue(
                        severity=Severity.WARNING,
                        category=Category.HARMONIC,
                        subcategory="bass_chord_degrees",
                        message=(f"Bass uses non-root/5th degree "
                                 f"({degree}) at {note_name(note.pitch)}"),
                        tick=note.start,
                        track="Bass",
                        details={"degree": degree, "pitch": note.pitch},
                    )

        # Overall ratio check
        if total_with_degree > 0:
            ratio = non_preferred_count / total_with_degree
            if ratio > 0.3:
                self.add_issue(
                    severity=Severity.WARNING,
                    category=Category.HARMONIC,
                    subcategory="bass_chord_degrees",
                    message=f"Bass uses non-root/5th degrees {ratio:.0%}",
                    tick=0,
                    track="Bass",
                    details={"non_preferred_ratio": ratio,
                             "non_preferred_count": non_preferred_count,
                             "total_with_degree": total_with_degree,
                             "degree_counts": dict(degree_counts)},
                )

    def _analyze_bass_downbeat_root(self):
        """Check if bass notes on beat 1 use the root degree.

        Bass notes on the downbeat should typically play the root of the
        chord for harmonic stability. Flags non-root bass notes on beat 1
        using provenance chord_degree data. Limited to first 10 issues.
        """
        bass_notes = self.notes_by_channel.get(2, [])
        if not bass_notes:
            return

        issue_count = 0

        for note in bass_notes:
            if issue_count >= 10:
                break

            beat, offset = self.get_beat_position(note.start)
            if beat != 1:
                continue

            if not note.provenance or 'chord_degree' not in note.provenance:
                continue

            degree = note.provenance['chord_degree']
            if degree < 0:
                continue

            if degree != 0:
                self.add_issue(
                    severity=Severity.WARNING,
                    category=Category.HARMONIC,
                    subcategory="bass_downbeat_root",
                    message=(f"Bass on beat 1 is not root "
                             f"(degree={degree})"),
                    tick=note.start,
                    track="Bass",
                    details={"degree": degree, "pitch": note.pitch,
                             "beat": beat},
                )
                issue_count += 1

    def _analyze_bass_contour(self):
        """Analyze bass motion patterns over 4-bar windows.

        Classifies each window as pedal (same pitch >75%), walking
        (stepwise >60%), arpeggiated (intervals 3-5 semitones >50%),
        or random. Warns if >50% of windows are random.
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
                elif 3 <= interval <= 5:
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
        and the interval between them is less than a perfect 5th (7 semitones),
        which creates muddy low-register voicing. Limited to first 10 issues.
        """
        bass_notes = self.notes_by_channel.get(2, [])
        chord_notes = self.notes_by_channel.get(1, [])
        if not bass_notes or not chord_notes:
            return

        # Build a mapping of chord notes by start tick for efficient lookup
        chords_by_time = defaultdict(list)
        for note in chord_notes:
            chords_by_time[note.start].append(note)

        issue_count = 0

        for bass_note in bass_notes:
            if issue_count >= 10:
                break

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
            if interval < 7:
                self.add_issue(
                    severity=Severity.WARNING,
                    category=Category.HARMONIC,
                    subcategory="bass_chord_spacing",
                    message=(f"Bass-chord spacing too close "
                             f"({interval} st)"),
                    tick=bass_note.start,
                    track="Bass/Chord",
                    details={"interval": interval,
                             "bass_pitch": bass_note.pitch,
                             "chord_lowest_pitch": lowest_chord_pitch},
                )
                issue_count += 1
