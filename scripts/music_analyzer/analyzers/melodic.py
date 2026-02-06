"""Melodic analysis: contour, range, leaps, isolation, singability.

Detects melodic issues including isolated notes, consecutive same-pitch
repetitions, out-of-range pitches, large leaps, monotonous contours,
phrase arc quality, and singability metrics.
"""

from typing import List

from ..constants import (
    TICKS_PER_BEAT,
    TICKS_PER_BAR,
    TRACK_NAMES,
    TRACK_RANGES,
    CONSONANT_LEAPS,
    SINGABILITY_STEP_MAX,
    SINGABILITY_SKIP_MAX,
    Severity,
    Category,
)
from ..helpers import note_name, tick_to_bar
from ..models import Issue
from .base import BaseAnalyzer


class MelodicAnalyzer(BaseAnalyzer):
    """Analyzer for melodic qualities across melodic tracks.

    Checks vocal, motif, and aux tracks for melodic problems such as
    isolated notes, pitch repetition, range violations, awkward leaps,
    monotonous contours, phrase arc shape, and singability ratios.
    """

    def analyze(self) -> List[Issue]:
        """Run all melodic analyses and return collected issues."""
        self._analyze_isolated_notes()
        self._analyze_consecutive_same_pitch()
        self._analyze_melodic_range()
        self._analyze_melodic_leaps()
        self._analyze_melodic_contour()
        self._analyze_melodic_arc()
        self._analyze_singability()
        return self.issues

    # -----------------------------------------------------------------
    # Existing analyses
    # -----------------------------------------------------------------

    def _analyze_isolated_notes(self):
        """Detect isolated notes in melodic tracks."""
        melodic_channels = [0, 3, 5]  # Vocal, Motif, Aux
        isolation_threshold = TICKS_PER_BAR

        for channel in melodic_channels:
            notes = self.notes_by_channel.get(channel, [])
            if len(notes) < 2:
                continue

            track_name = TRACK_NAMES.get(channel, f"Ch{channel}")

            for idx, note in enumerate(notes):
                prev_end = notes[idx - 1].end if idx > 0 else 0
                next_start = (notes[idx + 1].start if idx < len(notes) - 1
                              else note.end + isolation_threshold * 2)

                gap_before = note.start - prev_end
                gap_after = next_start - note.end

                if gap_before >= isolation_threshold and gap_after >= isolation_threshold:
                    self.add_issue(
                        severity=Severity.WARNING,
                        category=Category.MELODIC,
                        subcategory="isolated_note",
                        message=(f"Isolated {note_name(note.pitch)} "
                                 f"(gaps: {gap_before / TICKS_PER_BAR:.1f}/"
                                 f"{gap_after / TICKS_PER_BAR:.1f} bars)"),
                        tick=note.start,
                        track=track_name,
                        details={"pitch": note.pitch,
                                 "gap_before": gap_before,
                                 "gap_after": gap_after},
                    )

    def _analyze_consecutive_same_pitch(self):
        """Detect consecutive same-pitch notes (6+ warning, 8+ error).

        In pop music, 4-5 repeated notes are common for rhythmic delivery
        (e.g., rap-style or syllabic passages). Only flag 6+ as WARNING
        and 8+ as ERROR.
        """
        melodic_channels = [0, 3, 5]
        warn_threshold = 6
        error_threshold = 8

        for channel in melodic_channels:
            notes = self.notes_by_channel.get(channel, [])
            if len(notes) < warn_threshold:
                continue

            track_name = TRACK_NAMES.get(channel, f"Ch{channel}")
            consecutive_count = 1
            current_pitch = notes[0].pitch
            start_tick = notes[0].start

            for idx in range(1, len(notes)):
                gap = notes[idx].start - notes[idx - 1].end
                if notes[idx].pitch == current_pitch and gap < TICKS_PER_BEAT * 2:
                    consecutive_count += 1
                else:
                    if consecutive_count >= warn_threshold:
                        severity = (Severity.ERROR if consecutive_count >= error_threshold
                                    else Severity.WARNING)
                        self.add_issue(
                            severity=severity,
                            category=Category.MELODIC,
                            subcategory="consecutive_same_pitch",
                            message=f"{consecutive_count} consecutive {note_name(current_pitch)}",
                            tick=start_tick,
                            track=track_name,
                            details={"pitch": current_pitch, "count": consecutive_count},
                        )
                    consecutive_count = 1
                    current_pitch = notes[idx].pitch
                    start_tick = notes[idx].start

            # Check last sequence
            if consecutive_count >= warn_threshold:
                severity = (Severity.ERROR if consecutive_count >= error_threshold
                            else Severity.WARNING)
                self.add_issue(
                    severity=severity,
                    category=Category.MELODIC,
                    subcategory="consecutive_same_pitch",
                    message=f"{consecutive_count} consecutive {note_name(current_pitch)}",
                    tick=start_tick,
                    track=track_name,
                    details={"pitch": current_pitch, "count": consecutive_count},
                )

    def _analyze_melodic_range(self):
        """Check if melodic lines stay within appropriate ranges."""
        for channel, (low, high) in TRACK_RANGES.items():
            notes = self.notes_by_channel.get(channel, [])
            track_name = TRACK_NAMES.get(channel, f"Ch{channel}")

            for note in notes:
                if note.pitch < low:
                    self.add_issue(
                        severity=Severity.WARNING,
                        category=Category.MELODIC,
                        subcategory="range_low",
                        message=f"{note_name(note.pitch)} below range (min: {note_name(low)})",
                        tick=note.start,
                        track=track_name,
                        details={"pitch": note.pitch, "expected_low": low},
                    )
                elif note.pitch > high:
                    self.add_issue(
                        severity=Severity.WARNING,
                        category=Category.MELODIC,
                        subcategory="range_high",
                        message=f"{note_name(note.pitch)} above range (max: {note_name(high)})",
                        tick=note.start,
                        track=track_name,
                        details={"pitch": note.pitch, "expected_high": high},
                    )

    def _analyze_melodic_leaps(self):
        """Detect awkward melodic leaps. YOASOBI-aware: resolved/consonant leaps are tolerated."""
        melodic_channels = [0, 3, 5]
        base_threshold = 14  # Raised from 12 for modern J-pop

        for channel in melodic_channels:
            notes = self.notes_by_channel.get(channel, [])
            if len(notes) < 2:
                continue

            track_name = TRACK_NAMES.get(channel, f"Ch{channel}")
            seen_intervals = []

            for idx in range(1, len(notes)):
                gap = notes[idx].start - notes[idx - 1].end
                if gap > TICKS_PER_BEAT * 2:
                    continue  # Different phrase

                interval = abs(notes[idx].pitch - notes[idx - 1].pitch)
                if interval <= base_threshold:
                    seen_intervals.append(interval)
                    continue

                # Check resolution within next 3 notes
                is_resolved = False
                for look_ahead in range(1, min(4, len(notes) - idx)):
                    next_gap = (notes[idx + look_ahead].start -
                                notes[idx + look_ahead - 1].end)
                    if next_gap > TICKS_PER_BEAT * 2:
                        break
                    next_interval = abs(notes[idx + look_ahead].pitch -
                                        notes[idx + look_ahead - 1].pitch)
                    direction_change = (
                        (notes[idx].pitch - notes[idx - 1].pitch) *
                        (notes[idx + look_ahead].pitch -
                         notes[idx + look_ahead - 1].pitch)
                    ) < 0
                    if next_interval <= 4 and direction_change:
                        is_resolved = True
                        break

                interval_class = interval % 12
                is_consonant = interval_class in CONSONANT_LEAPS
                is_pattern = seen_intervals.count(interval) >= 1

                # YOASOBI-aware severity
                if is_resolved and interval <= 24:
                    continue  # Resolved leaps <=2 octaves: OK
                elif is_resolved:
                    severity = Severity.INFO  # Very large but resolved
                elif is_consonant and interval <= 19:
                    severity = Severity.INFO
                elif is_pattern:
                    severity = Severity.INFO
                elif interval > 19:
                    severity = Severity.ERROR
                else:
                    severity = Severity.WARNING

                # Blueprint tolerance
                if self.profile and self.profile.leap_tolerance > 1.0:
                    if severity == Severity.WARNING:
                        severity = Severity.INFO
                    elif severity == Severity.ERROR and interval <= 21:
                        severity = Severity.WARNING
                elif self.profile and self.profile.leap_tolerance < 1.0:
                    if severity == Severity.INFO and not is_resolved:
                        severity = Severity.WARNING

                direction = "up" if notes[idx].pitch > notes[idx - 1].pitch else "down"
                tags = []
                if is_resolved:
                    tags.append("resolved")
                if is_consonant:
                    tags.append("consonant")
                if is_pattern:
                    tags.append("pattern")
                tag_str = f" ({', '.join(tags)})" if tags else ""

                self.add_issue(
                    severity=severity,
                    category=Category.MELODIC,
                    subcategory="large_leap",
                    message=f"Large leap ({interval} semitones {direction}){tag_str}",
                    tick=notes[idx].start,
                    track=track_name,
                    details={"interval": interval, "direction": direction,
                             "resolved": is_resolved, "consonant": is_consonant,
                             "pattern": is_pattern},
                )
                seen_intervals.append(interval)

    def _analyze_melodic_contour(self):
        """Detect monotonous melodic contours (6+ notes same direction)."""
        melodic_channels = [0, 3, 5]
        monotonous_threshold = 6

        for channel in melodic_channels:
            notes = self.notes_by_channel.get(channel, [])
            if len(notes) < monotonous_threshold:
                continue

            track_name = TRACK_NAMES.get(channel, f"Ch{channel}")
            direction_count = 0
            current_direction = 0
            start_tick = notes[0].start

            for idx in range(1, len(notes)):
                gap = notes[idx].start - notes[idx - 1].end
                if gap > TICKS_PER_BEAT * 2:
                    direction_count = 0
                    current_direction = 0
                    start_tick = notes[idx].start
                    continue

                diff = notes[idx].pitch - notes[idx - 1].pitch
                if diff == 0:
                    continue

                direction = 1 if diff > 0 else -1
                if direction == current_direction:
                    direction_count += 1
                else:
                    if direction_count >= monotonous_threshold:
                        dir_name = "ascending" if current_direction > 0 else "descending"
                        self.add_issue(
                            severity=Severity.INFO,
                            category=Category.MELODIC,
                            subcategory="monotonous_contour",
                            message=f"{direction_count} notes continuously {dir_name}",
                            tick=start_tick,
                            track=track_name,
                            details={"count": direction_count,
                                     "direction": dir_name},
                        )
                    direction_count = 1
                    current_direction = direction
                    start_tick = notes[idx - 1].start

            # Check last sequence
            if direction_count >= monotonous_threshold:
                dir_name = "ascending" if current_direction > 0 else "descending"
                self.add_issue(
                    severity=Severity.INFO,
                    category=Category.MELODIC,
                    subcategory="monotonous_contour",
                    message=f"{direction_count} notes continuously {dir_name}",
                    tick=start_tick,
                    track=track_name,
                    details={"count": direction_count,
                             "direction": dir_name},
                )

    # -----------------------------------------------------------------
    # New analyses
    # -----------------------------------------------------------------

    def _analyze_melodic_arc(self):
        """Analyze phrase arc shape for vocal melody.

        Splits vocal notes into phrases (separated by gaps >= 1 beat),
        then checks whether each phrase has a clear rise-peak-fall arc.
        Flags flat phrases and phrases with peaks at boundaries.
        """
        vocal_notes = self.notes_by_channel.get(0, [])
        if len(vocal_notes) < 6:
            return

        # Split into phrases using 1-beat gap threshold
        phrases = []
        current_phrase = [vocal_notes[0]]

        for idx in range(1, len(vocal_notes)):
            gap = vocal_notes[idx].start - vocal_notes[idx - 1].end
            if gap >= TICKS_PER_BEAT:
                if len(current_phrase) >= 6:
                    phrases.append(current_phrase)
                current_phrase = [vocal_notes[idx]]
            else:
                current_phrase.append(vocal_notes[idx])

        # Don't forget the last phrase
        if len(current_phrase) >= 6:
            phrases.append(current_phrase)

        for phrase in phrases:
            pitches = [note.pitch for note in phrase]
            pitch_min = min(pitches)
            pitch_max = max(pitches)
            pitch_range = pitch_max - pitch_min

            # Flat phrase: almost no pitch variation
            if pitch_range < 3:
                self.add_issue(
                    severity=Severity.WARNING,
                    category=Category.MELODIC,
                    subcategory="melodic_arc",
                    message=(f"Flat phrase arc (range: {pitch_range} semitones "
                             f"over {len(phrase)} notes)"),
                    tick=phrase[0].start,
                    track="Vocal",
                    details={"note_count": len(phrase),
                             "pitch_range": pitch_range,
                             "pitch_min": pitch_min,
                             "pitch_max": pitch_max},
                )
                continue

            # Find peak position (highest pitch) as fraction of phrase length
            peak_idx = pitches.index(pitch_max)
            phrase_len = len(phrase)
            peak_position = peak_idx / (phrase_len - 1)  # 0.0 to 1.0

            # Peak at the very first or very last note
            if peak_idx == 0 or peak_idx == phrase_len - 1:
                self.add_issue(
                    severity=Severity.INFO,
                    category=Category.MELODIC,
                    subcategory="melodic_arc",
                    message=(f"Peak at phrase boundary "
                             f"(position: {'start' if peak_idx == 0 else 'end'}, "
                             f"{len(phrase)} notes)"),
                    tick=phrase[peak_idx].start,
                    track="Vocal",
                    details={"note_count": len(phrase),
                             "peak_position": peak_position,
                             "peak_pitch": pitch_max,
                             "pitch_range": pitch_range},
                )
            elif not (0.2 <= peak_position <= 0.8):
                # Peak is near but not at the boundary
                self.add_issue(
                    severity=Severity.INFO,
                    category=Category.MELODIC,
                    subcategory="melodic_arc",
                    message=(f"Peak at phrase boundary "
                             f"(position: {peak_position:.0%}, "
                             f"{len(phrase)} notes)"),
                    tick=phrase[peak_idx].start,
                    track="Vocal",
                    details={"note_count": len(phrase),
                             "peak_position": peak_position,
                             "peak_pitch": pitch_max,
                             "pitch_range": pitch_range},
                )

    def _analyze_singability(self):
        """Analyze step/skip/leap ratio of the vocal melody.

        For consecutive vocal notes within the same phrase (gap < 2 beats),
        classifies each interval as step, skip, or leap and checks whether
        the ratios fall within comfortable singing ranges for pop music.

        Target ratios: step ~60%, skip ~30%, leap ~10%.
        """
        vocal_notes = self.notes_by_channel.get(0, [])
        if len(vocal_notes) < 2:
            return

        step_count = 0
        skip_count = 0
        leap_count = 0

        for idx in range(1, len(vocal_notes)):
            gap = vocal_notes[idx].start - vocal_notes[idx - 1].end
            if gap >= TICKS_PER_BEAT * 2:
                continue  # Different phrase, skip this interval

            interval = abs(vocal_notes[idx].pitch - vocal_notes[idx - 1].pitch)

            if interval <= SINGABILITY_STEP_MAX:
                step_count += 1
            elif interval <= SINGABILITY_SKIP_MAX:
                skip_count += 1
            else:
                leap_count += 1

        total_intervals = step_count + skip_count + leap_count
        if total_intervals == 0:
            return

        step_ratio = step_count / total_intervals
        skip_ratio = skip_count / total_intervals
        leap_ratio = leap_count / total_intervals

        details = {
            "step_count": step_count,
            "skip_count": skip_count,
            "leap_count": leap_count,
            "total_intervals": total_intervals,
            "step_ratio": round(step_ratio, 3),
            "skip_ratio": round(skip_ratio, 3),
            "leap_ratio": round(leap_ratio, 3),
        }

        if step_ratio < 0.4:
            self.add_issue(
                severity=Severity.WARNING,
                category=Category.MELODIC,
                subcategory="singability",
                message=f"Low step ratio ({step_ratio:.0%}), may be hard to sing",
                tick=vocal_notes[0].start,
                track="Vocal",
                details=details,
            )

        if step_ratio > 0.85:
            self.add_issue(
                severity=Severity.INFO,
                category=Category.MELODIC,
                subcategory="singability",
                message=f"Very stepwise melody ({step_ratio:.0%})",
                tick=vocal_notes[0].start,
                track="Vocal",
                details=details,
            )

        if leap_ratio > 0.25:
            self.add_issue(
                severity=Severity.WARNING,
                category=Category.MELODIC,
                subcategory="singability",
                message=f"High leap ratio ({leap_ratio:.0%})",
                tick=vocal_notes[0].start,
                track="Vocal",
                details=details,
            )
