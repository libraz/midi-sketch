"""Structure analysis: phrase structure, track balance, energy contrast, section density.

Detects structural issues including short/long phrases, track dominance,
empty tracks, low verse-chorus energy contrast, and thin chorus arrangements.
"""

from collections import defaultdict
from typing import List

from ..constants import TICKS_PER_BEAT, TICKS_PER_BAR, TRACK_NAMES, Severity, Category
from ..helpers import tick_to_bar
from ..models import Issue
from .base import BaseAnalyzer


class StructureAnalyzer(BaseAnalyzer):
    """Analyzer for song structure and arrangement balance.

    Checks phrase lengths, track balance, verse-chorus energy contrast,
    and chorus section density across non-drum tracks.
    """

    def analyze(self) -> List[Issue]:
        """Run all structure analyses and return collected issues."""
        self._analyze_phrase_structure()
        self._analyze_track_balance()
        self._analyze_energy_contrast()
        self._analyze_section_density()
        return self.issues

    # -----------------------------------------------------------------
    # Phrase and balance analyses
    # -----------------------------------------------------------------

    def _analyze_phrase_structure(self):
        """Analyze phrase lengths (short/long phrases)."""
        vocal_notes = self.notes_by_channel.get(0, [])
        if len(vocal_notes) < 4:
            return

        phrase_gap = TICKS_PER_BEAT
        phrases = []
        current_phrase_start = vocal_notes[0].start
        current_phrase_notes = 1

        for idx in range(1, len(vocal_notes)):
            gap = vocal_notes[idx].start - vocal_notes[idx - 1].end
            if gap >= phrase_gap:
                phrase_duration = vocal_notes[idx - 1].end - current_phrase_start
                phrases.append({
                    'start': current_phrase_start,
                    'duration': phrase_duration,
                    'note_count': current_phrase_notes,
                })
                current_phrase_start = vocal_notes[idx].start
                current_phrase_notes = 1
            else:
                current_phrase_notes += 1

        for phrase in phrases:
            if phrase['note_count'] <= 2 and phrase['duration'] < TICKS_PER_BEAT * 2:
                self.add_issue(
                    severity=Severity.INFO, category=Category.STRUCTURE,
                    subcategory="short_phrase",
                    message=f"Very short phrase ({phrase['note_count']} notes)",
                    tick=phrase['start'], track="Vocal",
                    details={"note_count": phrase['note_count']},
                )
            elif phrase['note_count'] > 20:
                self.add_issue(
                    severity=Severity.INFO, category=Category.STRUCTURE,
                    subcategory="long_phrase",
                    message=f"Long phrase ({phrase['note_count']} notes)",
                    tick=phrase['start'], track="Vocal",
                    details={"note_count": phrase['note_count']},
                )

    def _analyze_track_balance(self):
        """Analyze balance between tracks (dominance/empty tracks)."""
        track_note_counts = {}
        for channel, notes in self.notes_by_channel.items():
            if channel != 9:
                track_note_counts[TRACK_NAMES.get(channel, f"Ch{channel}")] = len(notes)

        if not track_note_counts:
            return

        total_notes = sum(track_note_counts.values())
        if total_notes == 0:
            return

        for track, count in track_note_counts.items():
            ratio = count / total_notes
            if ratio > 0.5 and count > 100:
                self.add_issue(
                    severity=Severity.INFO, category=Category.STRUCTURE,
                    subcategory="track_dominance",
                    message=f"Dominates with {ratio * 100:.1f}% of notes ({count})",
                    tick=0, track=track,
                    details={"count": count, "ratio": ratio},
                )

        for channel in [0, 3, 5]:
            track_name = TRACK_NAMES.get(channel, f"Ch{channel}")
            if (track_name not in track_note_counts
                    or track_note_counts[track_name] == 0):
                self.add_issue(
                    severity=Severity.WARNING, category=Category.STRUCTURE,
                    subcategory="empty_track",
                    message="Track is empty",
                    tick=0, track=track_name,
                    details={},
                )

    # -----------------------------------------------------------------
    # Energy and density analyses
    # -----------------------------------------------------------------

    def _analyze_energy_contrast(self):
        """Analyze energy contrast between verse and chorus sections.

        Computes an energy metric for each section based on average velocity
        and note density, then compares average verse energy against average
        chorus energy. Flags when choruses do not have meaningfully higher
        energy than verses (less than 10% increase).
        """
        sections = self.sections
        if not sections:
            return

        verse_energies = []
        chorus_energies = []

        for section in sections:
            energy = ((section['avg_velocity'] / 127) * 0.6
                      + min(section['density'] / 8, 1.0) * 0.4)

            if section['type'] == 'verse':
                verse_energies.append(energy)
            elif section['type'] == 'chorus':
                chorus_energies.append(energy)

        if not verse_energies or not chorus_energies:
            return

        avg_verse_energy = sum(verse_energies) / len(verse_energies)
        avg_chorus_energy = sum(chorus_energies) / len(chorus_energies)

        if avg_chorus_energy <= avg_verse_energy * 1.1:
            self.add_issue(
                severity=Severity.INFO, category=Category.STRUCTURE,
                subcategory="energy_contrast",
                message=(f"Low verse-chorus energy contrast "
                         f"(verse: {avg_verse_energy:.2f}, "
                         f"chorus: {avg_chorus_energy:.2f})"),
                tick=0, track="",
                details={
                    "verse_energy": round(avg_verse_energy, 4),
                    "chorus_energy": round(avg_chorus_energy, 4),
                    "verse_count": len(verse_energies),
                    "chorus_count": len(chorus_energies),
                },
            )

    def _analyze_section_density(self):
        """Analyze track density in chorus sections.

        For each chorus section, counts how many non-drum tracks have
        meaningful activity (at least 2 notes). Flags choruses with
        fewer than 3 active tracks as potentially thin arrangements.
        """
        sections = self.sections
        if not sections:
            return

        # Non-drum track channels to check
        melody_channels = [0, 1, 2, 3, 4, 5]

        for section in sections:
            if section['type'] != 'chorus':
                continue

            section_start = (section['start_bar'] - 1) * TICKS_PER_BAR
            section_end = section['end_bar'] * TICKS_PER_BAR
            active_count = 0

            for channel in melody_channels:
                notes = self.notes_by_channel.get(channel, [])
                notes_in_section = [
                    note for note in notes
                    if section_start <= note.start < section_end
                ]
                if len(notes_in_section) >= 2:
                    active_count += 1

            if active_count < 3:
                self.add_issue(
                    severity=Severity.INFO, category=Category.STRUCTURE,
                    subcategory="section_density",
                    message=(f"Thin chorus arrangement "
                             f"({active_count} active tracks, "
                             f"bars {section['start_bar']}-{section['end_bar']})"),
                    tick=section_start, track="",
                    details={
                        "active_tracks": active_count,
                        "start_bar": section['start_bar'],
                        "end_bar": section['end_bar'],
                    },
                )
