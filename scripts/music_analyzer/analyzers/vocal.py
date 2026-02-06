"""Vocal-specific music analysis.

Analyzes vocal track (channel 0) for breathability, tessitura,
climax placement, interval distribution, section contrast,
and phrase repetition balance.
"""

from collections import Counter
from typing import List

from ..constants import TICKS_PER_BEAT, TICKS_PER_BAR, Severity, Category
from ..helpers import note_name, tick_to_bar
from ..models import Issue
from .base import BaseAnalyzer


class VocalAnalyzer(BaseAnalyzer):
    """Analyzer for vocal track musical quality.

    Checks phrase breathability, tessitura comfort, climax positioning,
    interval distribution for singability, verse-chorus contrast,
    climax-in-chorus verification, and phrase repetition balance.
    """

    def analyze(self) -> List[Issue]:
        """Run all vocal analyses and return collected issues."""
        self._analyze_vocal_breathability()
        self._analyze_vocal_tessitura()
        self._analyze_vocal_climax()
        self._analyze_vocal_interval_distribution()
        self._analyze_vocal_section_contrast()
        self._analyze_climax_placement()
        self._analyze_repetition_balance()
        return self.issues

    def _analyze_vocal_breathability(self):
        """Check phrase lengths against human breathing limits."""
        vocal = self.notes_by_channel.get(0, [])
        if len(vocal) < 4:
            return

        phrase_gap = TICKS_PER_BEAT  # 1 beat gap = new phrase
        current_start = vocal[0].start
        current_end = vocal[0].end

        for idx in range(1, len(vocal)):
            if vocal[idx].start - vocal[idx - 1].end >= phrase_gap:
                phrase_beats = (current_end - current_start) / TICKS_PER_BEAT
                if phrase_beats > 12:
                    sev = Severity.WARNING if phrase_beats > 16 else Severity.INFO
                    self.add_issue(
                        severity=sev,
                        category=Category.MELODIC,
                        subcategory="breathability",
                        message=f"Long phrase ({phrase_beats:.1f} beats, needs breath)",
                        tick=current_start,
                        track="Vocal",
                        details={"phrase_beats": phrase_beats},
                    )
                current_start = vocal[idx].start
            current_end = vocal[idx].end

        # Check last phrase
        phrase_beats = (current_end - current_start) / TICKS_PER_BEAT
        if phrase_beats > 12:
            sev = Severity.WARNING if phrase_beats > 16 else Severity.INFO
            self.add_issue(
                severity=sev,
                category=Category.MELODIC,
                subcategory="breathability",
                message=f"Long phrase ({phrase_beats:.1f} beats, needs breath)",
                tick=current_start,
                track="Vocal",
                details={"phrase_beats": phrase_beats},
            )

    def _analyze_vocal_tessitura(self):
        """Analyze where most vocal notes concentrate (tessitura)."""
        vocal = self.notes_by_channel.get(0, [])
        if len(vocal) < 8:
            return

        pitches = sorted(n.pitch for n in vocal)
        q1_val = pitches[len(pitches) // 4]
        median = pitches[len(pitches) // 2]
        q3_val = pitches[3 * len(pitches) // 4]

        # Comfortable tessitura: centered around A3(57)-E5(76)
        if median < 55 or median > 79:
            self.add_issue(
                severity=Severity.INFO,
                category=Category.MELODIC,
                subcategory="tessitura",
                message=f"Tessitura center ({note_name(median)}) outside comfortable range",
                tick=0,
                track="Vocal",
                details={"median": median, "q1": q1_val, "q3": q3_val},
            )

        # Check for bimodal distribution (notes at extremes)
        iqr = q3_val - q1_val
        if iqr > 19:  # > octave+P5 spread in middle 50%
            self.add_issue(
                severity=Severity.INFO,
                category=Category.MELODIC,
                subcategory="tessitura",
                message=f"Wide tessitura spread (IQR: {iqr} semitones)",
                tick=0,
                track="Vocal",
                details={"iqr": iqr},
            )

    def _analyze_vocal_climax(self):
        """Check if melodic peak is in chorus sections."""
        vocal = self.notes_by_channel.get(0, [])
        if len(vocal) < 8:
            return

        peak_note = max(vocal, key=lambda n: n.pitch)
        peak_bar = tick_to_bar(peak_note.start)

        # Check if peak is in a chorus section
        chorus_sections = [s for s in self.sections if s['type'] == 'chorus']
        in_chorus = any(
            s['start_bar'] <= peak_bar <= s['end_bar']
            for s in chorus_sections
        )

        if not in_chorus and chorus_sections:
            self.add_issue(
                severity=Severity.INFO,
                category=Category.MELODIC,
                subcategory="climax_position",
                message=(f"Peak note ({note_name(peak_note.pitch)}) "
                         f"not in chorus (bar {peak_bar})"),
                tick=peak_note.start,
                track="Vocal",
                details={"peak_pitch": peak_note.pitch, "peak_bar": peak_bar},
            )

    def _analyze_vocal_interval_distribution(self):
        """Analyze interval type ratios for singability."""
        vocal = self.notes_by_channel.get(0, [])
        if len(vocal) < 8:
            return

        intervals = {'step': 0, 'third': 0, 'fourth_fifth': 0, 'large': 0}
        total = 0

        for idx in range(1, len(vocal)):
            gap = vocal[idx].start - vocal[idx - 1].end
            if gap > TICKS_PER_BEAT * 2:
                continue
            interval_val = abs(vocal[idx].pitch - vocal[idx - 1].pitch)
            total += 1
            if interval_val <= 2:
                intervals['step'] += 1
            elif interval_val <= 4:
                intervals['third'] += 1
            elif interval_val <= 7:
                intervals['fourth_fifth'] += 1
            else:
                intervals['large'] += 1

        if total < 8:
            return

        large_ratio = intervals['large'] / total
        step_ratio = intervals['step'] / total

        # Flag only if extreme (YOASOBI-aware: large leaps OK if balanced)
        if large_ratio > 0.4:
            self.add_issue(
                severity=Severity.INFO,
                category=Category.MELODIC,
                subcategory="interval_distribution",
                message=f"High large-interval ratio ({large_ratio:.0%})",
                tick=0,
                track="Vocal",
                details={"step": intervals['step'], "third": intervals['third'],
                         "fourth_fifth": intervals['fourth_fifth'],
                         "large": intervals['large']},
            )

        if step_ratio > 0.85:
            self.add_issue(
                severity=Severity.INFO,
                category=Category.MELODIC,
                subcategory="interval_distribution",
                message=f"Very stepwise melody ({step_ratio:.0%} steps)",
                tick=0,
                track="Vocal",
                details=intervals,
            )

    def _analyze_vocal_section_contrast(self):
        """Check verse vs chorus has sufficient contrast."""
        vocal = self.notes_by_channel.get(0, [])
        if len(vocal) < 16:
            return

        verses = [s for s in self.sections if s['type'] == 'verse']
        choruses = [s for s in self.sections if s['type'] == 'chorus']
        if not verses or not choruses:
            return

        avg_verse_density = sum(s['density'] for s in verses) / len(verses)
        avg_chorus_density = sum(s['density'] for s in choruses) / len(choruses)
        avg_verse_pitch = (
            sum(s['avg_pitch'] for s in verses if s['avg_pitch'] > 0) /
            max(1, sum(1 for s in verses if s['avg_pitch'] > 0))
        )
        avg_chorus_pitch = (
            sum(s['avg_pitch'] for s in choruses if s['avg_pitch'] > 0) /
            max(1, sum(1 for s in choruses if s['avg_pitch'] > 0))
        )

        density_ratio = avg_chorus_density / max(0.1, avg_verse_density)
        pitch_diff = avg_chorus_pitch - avg_verse_pitch

        if density_ratio < 1.1 and abs(pitch_diff) < 2:
            self.add_issue(
                severity=Severity.INFO,
                category=Category.STRUCTURE,
                subcategory="section_contrast",
                message=(f"Low verse-chorus contrast "
                         f"(density ratio: {density_ratio:.2f}, "
                         f"pitch diff: {pitch_diff:.1f}st)"),
                tick=0,
                track="Vocal",
                details={"density_ratio": density_ratio,
                         "pitch_diff": pitch_diff},
            )

    def _analyze_climax_placement(self):
        """Verify climax note falls within chorus for MelodyDriven blueprints.

        Only active when the blueprint profile specifies that the climax
        is expected in the chorus section (expected_climax_section == "chorus").
        Skips analysis for profiles with "any" or when no profile is set.
        """
        if not self.profile:
            return
        if self.profile.expected_climax_section != "chorus":
            return

        vocal = self.notes_by_channel.get(0, [])
        if len(vocal) < 4:
            return

        peak_note = max(vocal, key=lambda n: n.pitch)
        peak_bar = tick_to_bar(peak_note.start)

        chorus_sections = [s for s in self.sections if s['type'] == 'chorus']
        if not chorus_sections:
            return

        in_chorus = any(
            s['start_bar'] <= peak_bar <= s['end_bar']
            for s in chorus_sections
        )

        if not in_chorus:
            self.add_issue(
                severity=Severity.WARNING,
                category=Category.MELODIC,
                subcategory="climax_placement",
                message=(f"Climax note ({note_name(peak_note.pitch)}) "
                         f"outside chorus (bar {peak_bar})"),
                tick=peak_note.start,
                track="Vocal",
                details={
                    "peak_pitch": peak_note.pitch,
                    "peak_bar": peak_bar,
                    "blueprint": self.profile.name,
                },
            )

    def _analyze_repetition_balance(self):
        """Check phrase repetition rate for memorability vs monotony balance.

        Splits vocal notes into phrases (separated by >= 1-beat gaps),
        computes a pitch-tuple fingerprint for each phrase, and measures
        how many phrases repeat. Too much repetition feels monotonous;
        too little repetition hurts memorability.
        """
        vocal = self.notes_by_channel.get(0, [])
        if len(vocal) < 4:
            return

        phrase_gap = TICKS_PER_BEAT  # 1 beat gap = new phrase
        phrases = []
        current_phrase = [vocal[0].pitch]

        for idx in range(1, len(vocal)):
            if vocal[idx].start - vocal[idx - 1].end >= phrase_gap:
                if current_phrase:
                    phrases.append(tuple(current_phrase))
                current_phrase = [vocal[idx].pitch]
            else:
                current_phrase.append(vocal[idx].pitch)

        # Flush last phrase
        if current_phrase:
            phrases.append(tuple(current_phrase))

        total_phrases = len(phrases)
        if total_phrases < 2:
            return

        fingerprint_counts = Counter(phrases)
        repeated_phrases = sum(
            count for count in fingerprint_counts.values() if count >= 2
        )
        repetition_rate = repeated_phrases / total_phrases

        if repetition_rate > 0.8:
            self.add_issue(
                severity=Severity.INFO,
                category=Category.MELODIC,
                subcategory="repetition_balance",
                message=(f"High phrase repetition ({repetition_rate:.0%}), "
                         f"may feel monotonous"),
                tick=0,
                track="Vocal",
                details={
                    "repetition_rate": repetition_rate,
                    "total_phrases": total_phrases,
                    "unique_phrases": len(fingerprint_counts),
                },
            )

        if repetition_rate < 0.2 and total_phrases >= 6:
            self.add_issue(
                severity=Severity.INFO,
                category=Category.MELODIC,
                subcategory="repetition_balance",
                message=(f"Low phrase repetition ({repetition_rate:.0%}), "
                         f"may lack memorability"),
                tick=0,
                track="Vocal",
                details={
                    "repetition_rate": repetition_rate,
                    "total_phrases": total_phrases,
                    "unique_phrases": len(fingerprint_counts),
                },
            )
