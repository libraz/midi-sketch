"""Arrangement analysis: register overlap, track separation, motif quality, blueprint compliance.

Detects arrangement issues including register overlap between tracks,
insufficient pitch separation, motif consistency and contour preservation,
motif-vocal interference, density balance, rhythm sync compliance,
and blueprint paradigm conformance.
"""

from collections import defaultdict
from typing import List

from ..constants import (
    TICKS_PER_BEAT, TICKS_PER_BAR, TRACK_NAMES, GUITAR_CHANNEL,
    Severity, Category,
)
from ..helpers import tick_to_bar, note_name, _pattern_similarity, _ioi_entropy
from ..models import Issue
from .base import BaseAnalyzer


class ArrangementAnalyzer(BaseAnalyzer):
    """Analyzer for arrangement qualities across all tracks.

    Checks register overlap, track separation, motif consistency,
    motif-vocal interference, density balance, rhythm sync, contour
    and rhythm preservation, and blueprint paradigm compliance.
    """

    def analyze(self) -> List[Issue]:
        """Run all arrangement analyses and return collected issues."""
        self._analyze_register_overlap()
        self._analyze_track_separation()
        self._analyze_motif_consistency()
        self._analyze_motif_vocal_interference()
        self._analyze_motif_density_balance()
        self._analyze_blueprint_rhythm_sync()
        self._analyze_motif_contour_preservation()
        self._analyze_motif_rhythm_preservation()
        self._analyze_blueprint_paradigm()
        self._analyze_submelody_vocal_crossing()
        self._analyze_guitar_chord_redundancy()
        self._analyze_guitar_dynamic_variation()
        return self.issues

    # -----------------------------------------------------------------
    # Register and separation
    # -----------------------------------------------------------------

    def _analyze_register_overlap(self):
        """Detect tracks fighting for same register (masking risk)."""
        track_pairs = [(0, 3), (0, 5), (0, 1), (3, 5), (1, 4), (1, 6), (6, 4)]
        for ch_a, ch_b in track_pairs:
            notes_a = self.notes_by_channel.get(ch_a, [])
            notes_b = self.notes_by_channel.get(ch_b, [])
            if not notes_a or not notes_b:
                continue
            close_count = 0
            total_checked = 0
            max_tick = max(n.end for n in self.notes)
            for tick in range(0, max_tick, TICKS_PER_BEAT):
                active_a = [n for n in notes_a if n.start <= tick < n.end]
                active_b = [n for n in notes_b if n.start <= tick < n.end]
                if not active_a or not active_b:
                    continue
                total_checked += 1
                found_close = False
                for note_a in active_a:
                    for note_b in active_b:
                        if (abs(note_a.pitch - note_b.pitch) <= 3
                                and abs(note_a.pitch - note_b.pitch) > 0):
                            found_close = True
                            break
                    if found_close:
                        break
                if found_close:
                    close_count += 1
            if total_checked > 4:
                overlap_ratio = close_count / total_checked
                if overlap_ratio > 0.3:
                    t1 = TRACK_NAMES.get(ch_a, f"Ch{ch_a}")
                    t2 = TRACK_NAMES.get(ch_b, f"Ch{ch_b}")
                    sev = Severity.WARNING if overlap_ratio > 0.5 else Severity.INFO
                    self.add_issue(
                        severity=sev, category=Category.ARRANGEMENT,
                        subcategory="register_overlap",
                        message=f"{t1}/{t2} register overlap ({overlap_ratio:.0%} of time)",
                        tick=0, track=f"{t1}/{t2}",
                        details={"overlap_ratio": overlap_ratio},
                    )

    def _analyze_track_separation(self):
        """Analyze average pitch distance between melodic tracks."""
        melodic_chs = [0, 3, 5]
        active_chs = [ch for ch in melodic_chs if self.notes_by_channel.get(ch)]
        if len(active_chs) < 2:
            return
        avg_pitches = {}
        for ch in active_chs:
            notes = self.notes_by_channel[ch]
            avg_pitches[ch] = sum(n.pitch for n in notes) / len(notes)
        for idx_a in range(len(active_chs)):
            for idx_b in range(idx_a + 1, len(active_chs)):
                ch_a, ch_b = active_chs[idx_a], active_chs[idx_b]
                dist = abs(avg_pitches[ch_a] - avg_pitches[ch_b])
                if dist < 4:
                    t1 = TRACK_NAMES.get(ch_a, f"Ch{ch_a}")
                    t2 = TRACK_NAMES.get(ch_b, f"Ch{ch_b}")
                    self.add_issue(
                        severity=Severity.WARNING, category=Category.ARRANGEMENT,
                        subcategory="track_separation",
                        message=f"{t1}/{t2} too close (avg distance: {dist:.1f} semitones)",
                        tick=0, track=f"{t1}/{t2}",
                        details={"distance": dist},
                    )

    # -----------------------------------------------------------------
    # Motif analyses
    # -----------------------------------------------------------------

    def _analyze_motif_consistency(self):
        """Check motif pattern similarity across sections."""
        motif = self.notes_by_channel.get(3, [])
        if len(motif) < 8 or len(self.sections) < 2:
            return
        min_consistency = (self.profile.motif_consistency_min if self.profile else 0.5)
        section_patterns = []
        for sec in self.sections:
            st = (sec['start_bar'] - 1) * TICKS_PER_BAR
            et = sec['end_bar'] * TICKS_PER_BAR
            sec_notes = [n for n in motif if st <= n.start < et]
            if len(sec_notes) < 3:
                section_patterns.append([])
                continue
            intervals = [
                sec_notes[idx + 1].pitch - sec_notes[idx].pitch
                for idx in range(len(sec_notes) - 1)
            ]
            section_patterns.append(intervals)
        for idx in range(1, len(section_patterns)):
            if not section_patterns[idx] or not section_patterns[idx - 1]:
                continue
            sim = _pattern_similarity(section_patterns[idx - 1], section_patterns[idx])
            if sim < min_consistency:
                sev = (Severity.WARNING if sim < min_consistency * 0.5 else Severity.INFO)
                self.add_issue(
                    severity=sev, category=Category.ARRANGEMENT,
                    subcategory="motif_degradation",
                    message=(f"Motif changed (similarity: {sim:.0%}, "
                             f"expected >= {min_consistency:.0%})"),
                    tick=(self.sections[idx]['start_bar'] - 1) * TICKS_PER_BAR,
                    track="Motif",
                    details={"similarity": sim, "threshold": min_consistency},
                )

    def _analyze_motif_vocal_interference(self):
        """Check if motif creates dissonance when overlapping with vocal."""
        motif = self.notes_by_channel.get(3, [])
        vocal = self.notes_by_channel.get(0, [])
        if not motif or not vocal:
            return
        clash_count = 0
        overlap_count = 0
        for motif_note in motif:
            for vocal_note in vocal:
                if motif_note.start < vocal_note.end and motif_note.end > vocal_note.start:
                    overlap_count += 1
                    interval = abs(motif_note.pitch - vocal_note.pitch) % 12
                    if interval in (1, 2, 6, 11):
                        clash_count += 1
                    break
        if overlap_count > 4:
            clash_ratio = clash_count / overlap_count
            if clash_ratio > 0.3:
                sev = Severity.WARNING if clash_ratio > 0.5 else Severity.INFO
                self.add_issue(
                    severity=sev, category=Category.ARRANGEMENT,
                    subcategory="motif_vocal_clash",
                    message=f"Motif clashes with vocal ({clash_ratio:.0%} of overlaps)",
                    tick=0, track="Motif/Vocal",
                    details={"clash_ratio": clash_ratio, "clash_count": clash_count},
                )

    def _analyze_motif_density_balance(self):
        """Check call-and-response: motif active when vocal rests, quiet when vocal sings."""
        motif = self.notes_by_channel.get(3, [])
        vocal = self.notes_by_channel.get(0, [])
        if len(motif) < 4 or len(vocal) < 4:
            return
        max_bar = max(tick_to_bar(n.start) for n in self.notes)
        both_dense = 0
        total_both = 0
        for bar in range(1, max_bar + 1):
            v_count = sum(1 for n in vocal if tick_to_bar(n.start) == bar)
            m_count = sum(1 for n in motif if tick_to_bar(n.start) == bar)
            if v_count > 0 and m_count > 0:
                total_both += 1
                if v_count > 3 and m_count > 3:
                    both_dense += 1
        if total_both > 4 and both_dense / total_both > 0.6:
            self.add_issue(
                severity=Severity.INFO, category=Category.ARRANGEMENT,
                subcategory="motif_density_balance",
                message=(f"Motif and vocal both dense simultaneously "
                         f"({both_dense}/{total_both} bars)"),
                tick=0, track="Motif/Vocal",
                details={"both_dense_bars": both_dense, "overlap_bars": total_both},
            )

    # -----------------------------------------------------------------
    # Sub-melody vocal crossing
    # -----------------------------------------------------------------

    def _analyze_submelody_vocal_crossing(self):
        """Detect sub-melody tracks (Aux, Motif) sounding above the vocal.

        In pop music, sub-melodies should stay below the vocal to maintain
        clarity. This checks beat-by-beat overlap and overall range
        encroachment.
        """
        vocal = self.notes_by_channel.get(0, [])
        if not vocal:
            return

        max_tick = max(n.end for n in self.notes)

        # Compute vocal median pitch for range encroachment check
        vocal_pitches = [n.pitch for n in vocal]
        sorted_pitches = sorted(vocal_pitches)
        mid_idx = len(sorted_pitches) // 2
        if len(sorted_pitches) % 2 == 0:
            vocal_median = (sorted_pitches[mid_idx - 1] + sorted_pitches[mid_idx]) / 2.0
        else:
            vocal_median = sorted_pitches[mid_idx]

        # Check each sub-melody track (Aux=ch5, Motif=ch3)
        submelody_configs = [
            {
                'channel': 5,
                'name': 'Aux',
                'track_label': 'Aux/Vocal',
                'warn_threshold': 0.10,
                'info_threshold': 0.05,
            },
            {
                'channel': 3,
                'name': 'Motif',
                'track_label': 'Motif/Vocal',
                'warn_threshold': 0.15,
                'info_threshold': 0.08,
            },
        ]

        for config in submelody_configs:
            sub_notes = self.notes_by_channel.get(config['channel'], [])
            if not sub_notes:
                continue

            above_count = 0
            overlap_beats = 0

            for tick in range(0, max_tick, TICKS_PER_BEAT):
                active_vocal = [n for n in vocal if n.start <= tick < n.end]
                active_sub = [n for n in sub_notes if n.start <= tick < n.end]
                if not active_vocal or not active_sub:
                    continue

                overlap_beats += 1
                vocal_max = max(n.pitch for n in active_vocal)
                sub_max = max(n.pitch for n in active_sub)

                if sub_max > vocal_max:
                    above_count += 1

            # Beat-level crossing check
            if overlap_beats > 0:
                crossing_ratio = above_count / overlap_beats
                if crossing_ratio > config['warn_threshold']:
                    self.add_issue(
                        severity=Severity.WARNING,
                        category=Category.ARRANGEMENT,
                        subcategory="submelody_vocal_crossing",
                        message=(
                            f"{config['name']} sounds above vocal "
                            f"({crossing_ratio:.0%} of overlapping beats)"
                        ),
                        tick=0,
                        track=config['track_label'],
                        details={
                            "crossing_ratio": crossing_ratio,
                            "above_beats": above_count,
                            "overlap_beats": overlap_beats,
                        },
                    )
                elif crossing_ratio > config['info_threshold']:
                    self.add_issue(
                        severity=Severity.INFO,
                        category=Category.ARRANGEMENT,
                        subcategory="submelody_vocal_crossing",
                        message=(
                            f"{config['name']} sounds above vocal "
                            f"({crossing_ratio:.0%} of overlapping beats)"
                        ),
                        tick=0,
                        track=config['track_label'],
                        details={
                            "crossing_ratio": crossing_ratio,
                            "above_beats": above_count,
                            "overlap_beats": overlap_beats,
                        },
                    )

            # Overall range encroachment check
            if sub_notes:
                sub_max_pitch = max(n.pitch for n in sub_notes)
                if sub_max_pitch >= vocal_median:
                    self.add_issue(
                        severity=Severity.INFO,
                        category=Category.ARRANGEMENT,
                        subcategory="submelody_vocal_crossing",
                        message=(
                            f"{config['name']} range encroaches vocal "
                            f"tessitura ({config['name']} max: "
                            f"{note_name(sub_max_pitch)}, vocal median: "
                            f"{note_name(int(vocal_median))})"
                        ),
                        tick=0,
                        track=config['track_label'],
                        details={
                            "sub_max_pitch": sub_max_pitch,
                            "vocal_median": vocal_median,
                        },
                    )

    # -----------------------------------------------------------------
    # Blueprint compliance
    # -----------------------------------------------------------------

    def _analyze_blueprint_rhythm_sync(self):
        """Check rhythm sync compliance for RhythmSync blueprints."""
        if not self.profile or not self.profile.rhythm_sync_required:
            return
        motif = self.notes_by_channel.get(3, [])
        vocal = self.notes_by_channel.get(0, [])
        if len(motif) < 4 or len(vocal) < 4:
            return
        max_bar = max(tick_to_bar(n.start) for n in self.notes)
        sync_bars = 0
        total_bars = 0
        for bar in range(1, max_bar + 1):
            m_attacks = set()
            v_attacks = set()
            for note in motif:
                if tick_to_bar(note.start) == bar:
                    m_attacks.add(note.start % TICKS_PER_BAR)
            for note in vocal:
                if tick_to_bar(note.start) == bar:
                    v_attacks.add(note.start % TICKS_PER_BAR)
            if m_attacks and v_attacks:
                total_bars += 1
                overlap = len(m_attacks & v_attacks)
                union = len(m_attacks | v_attacks)
                if union > 0 and overlap / union > 0.4:
                    sync_bars += 1
        if total_bars > 4:
            sync_ratio = sync_bars / total_bars
            if sync_ratio < 0.5:
                self.add_issue(
                    severity=Severity.WARNING, category=Category.ARRANGEMENT,
                    subcategory="rhythm_sync",
                    message=(f"Low rhythm sync ({sync_ratio:.0%}) "
                             f"for RhythmSync blueprint"),
                    tick=0, track="Motif/Vocal",
                    details={"sync_ratio": sync_ratio},
                )

    # -----------------------------------------------------------------
    # Motif contour and rhythm preservation
    # -----------------------------------------------------------------

    def _analyze_motif_contour_preservation(self):
        """Compare motif interval sequences across same-type sections.

        For blueprints with Locked riff policy, motif contour should be
        highly consistent. For Free policy, variation is expected.
        """
        motif = self.notes_by_channel.get(3, [])
        if len(motif) < 8 or len(self.sections) < 2:
            return

        # Determine threshold based on riff_policy
        if self.profile:
            policy = self.profile.riff_policy
            if policy == "Locked":
                min_similarity = 0.9
            elif policy == "Evolving":
                min_similarity = 0.7
            else:
                min_similarity = 0.3
        else:
            min_similarity = 0.3

        # Extract interval sequences per section
        section_intervals = {}
        for sec_idx, sec in enumerate(self.sections):
            st = (sec['start_bar'] - 1) * TICKS_PER_BAR
            et = sec['end_bar'] * TICKS_PER_BAR
            sec_notes = [n for n in motif if st <= n.start < et]
            if len(sec_notes) < 3:
                continue
            intervals = [
                sec_notes[idx + 1].pitch - sec_notes[idx].pitch
                for idx in range(len(sec_notes) - 1)
            ]
            section_intervals[sec_idx] = {
                'type': sec['type'],
                'intervals': intervals,
                'start_bar': sec['start_bar'],
            }

        # Group sections by type
        sections_by_type = defaultdict(list)
        for sec_idx, data in section_intervals.items():
            sections_by_type[data['type']].append(data)

        # Compare same-type sections
        for sec_type, sec_list in sections_by_type.items():
            if len(sec_list) < 2:
                continue
            for idx_a in range(len(sec_list)):
                for idx_b in range(idx_a + 1, len(sec_list)):
                    sim = _pattern_similarity(
                        sec_list[idx_a]['intervals'],
                        sec_list[idx_b]['intervals'],
                    )
                    if sim < min_similarity:
                        self.add_issue(
                            severity=Severity.WARNING,
                            category=Category.ARRANGEMENT,
                            subcategory="motif_contour_preservation",
                            message=(
                                f"Motif contour changed between {sec_type} "
                                f"sections (similarity: {sim:.0%})"
                            ),
                            tick=(sec_list[idx_b]['start_bar'] - 1) * TICKS_PER_BAR,
                            track="Motif",
                            details={
                                "similarity": sim,
                                "threshold": min_similarity,
                                "section_type": sec_type,
                                "bar_a": sec_list[idx_a]['start_bar'],
                                "bar_b": sec_list[idx_b]['start_bar'],
                            },
                        )

    def _analyze_motif_rhythm_preservation(self):
        """Compare motif IOI patterns across same-type sections.

        IOI (inter-onset interval) patterns are quantized to 16th notes
        for comparison. Thresholds depend on the riff_policy.
        """
        motif = self.notes_by_channel.get(3, [])
        if len(motif) < 8 or len(self.sections) < 2:
            return

        # Determine threshold based on riff_policy
        if self.profile:
            policy = self.profile.riff_policy
            if policy == "Locked":
                min_similarity = 0.9
            elif policy == "Evolving":
                min_similarity = 0.7
            else:
                min_similarity = 0.3
        else:
            min_similarity = 0.3

        sixteenth = TICKS_PER_BEAT // 4  # 120 ticks

        # Extract quantized IOI sequences per section
        section_ioi = {}
        for sec_idx, sec in enumerate(self.sections):
            st = (sec['start_bar'] - 1) * TICKS_PER_BAR
            et = sec['end_bar'] * TICKS_PER_BAR
            sec_notes = [n for n in motif if st <= n.start < et]
            if len(sec_notes) < 3:
                continue
            ioi_list = [
                round((sec_notes[idx + 1].start - sec_notes[idx].start) / sixteenth)
                for idx in range(len(sec_notes) - 1)
            ]
            section_ioi[sec_idx] = {
                'type': sec['type'],
                'ioi': ioi_list,
                'start_bar': sec['start_bar'],
            }

        # Group sections by type
        sections_by_type = defaultdict(list)
        for sec_idx, data in section_ioi.items():
            sections_by_type[data['type']].append(data)

        # Compare same-type sections
        for sec_type, sec_list in sections_by_type.items():
            if len(sec_list) < 2:
                continue
            for idx_a in range(len(sec_list)):
                for idx_b in range(idx_a + 1, len(sec_list)):
                    sim = _pattern_similarity(
                        sec_list[idx_a]['ioi'],
                        sec_list[idx_b]['ioi'],
                    )
                    if sim < min_similarity:
                        self.add_issue(
                            severity=Severity.WARNING,
                            category=Category.ARRANGEMENT,
                            subcategory="motif_rhythm_preservation",
                            message=(
                                f"Motif rhythm changed between {sec_type} "
                                f"sections (similarity: {sim:.0%})"
                            ),
                            tick=(sec_list[idx_b]['start_bar'] - 1) * TICKS_PER_BAR,
                            track="Motif",
                            details={
                                "similarity": sim,
                                "threshold": min_similarity,
                                "section_type": sec_type,
                                "bar_a": sec_list[idx_a]['start_bar'],
                                "bar_b": sec_list[idx_b]['start_bar'],
                            },
                        )

    # -----------------------------------------------------------------
    # Blueprint paradigm
    # -----------------------------------------------------------------

    def _analyze_blueprint_paradigm(self):
        """Check blueprint paradigm-specific expectations.

        RhythmSync: attack positions should correlate across melodic tracks.
        MelodyDriven: vocal should dominate in chorus sections.
        Ballad: density should remain low.
        """
        if not self.profile:
            return

        paradigm = self.profile.paradigm

        if paradigm == "RhythmSync":
            self._check_rhythm_sync_correlation()
        elif paradigm == "MelodyDriven":
            self._check_melody_driven_dominance()

        # Ballad density check (density_tolerance < 1.0)
        if self.profile.density_tolerance < 1.0:
            self._check_ballad_density()

    def _check_rhythm_sync_correlation(self):
        """Check attack correlation between melodic tracks for RhythmSync."""
        melodic_chs = [0, 3, 5]
        active_chs = [
            ch for ch in melodic_chs if self.notes_by_channel.get(ch)
        ]
        if len(active_chs) < 2:
            return

        max_bar = max(tick_to_bar(n.start) for n in self.notes)
        total_jaccard = 0.0
        bar_count = 0

        for bar in range(1, max_bar + 1):
            # Collect attack positions for each active track in this bar
            attacks_per_track = []
            for ch in active_chs:
                attacks = set()
                for note in self.notes_by_channel[ch]:
                    if tick_to_bar(note.start) == bar:
                        attacks.add(note.start % TICKS_PER_BAR)
                attacks_per_track.append(attacks)

            # Only count bars where at least 2 tracks have attacks
            non_empty = [att for att in attacks_per_track if att]
            if len(non_empty) < 2:
                continue

            # Compute pairwise Jaccard and average
            pair_count = 0
            pair_jaccard_sum = 0.0
            for idx_a in range(len(non_empty)):
                for idx_b in range(idx_a + 1, len(non_empty)):
                    union = len(non_empty[idx_a] | non_empty[idx_b])
                    if union == 0:
                        continue
                    intersection = len(non_empty[idx_a] & non_empty[idx_b])
                    pair_jaccard_sum += intersection / union
                    pair_count += 1

            if pair_count > 0:
                total_jaccard += pair_jaccard_sum / pair_count
                bar_count += 1

        if bar_count > 0:
            avg_correlation = total_jaccard / bar_count
            if avg_correlation < 0.3:
                self.add_issue(
                    severity=Severity.WARNING,
                    category=Category.ARRANGEMENT,
                    subcategory="blueprint_paradigm",
                    message=(
                        f"Low RhythmSync attack correlation "
                        f"({avg_correlation:.0%})"
                    ),
                    tick=0, track="All melodic",
                    details={
                        "correlation": avg_correlation,
                        "paradigm": "RhythmSync",
                        "bars_checked": bar_count,
                    },
                )

    def _check_melody_driven_dominance(self):
        """Check that vocal dominates in chorus sections for MelodyDriven."""
        vocal = self.notes_by_channel.get(0, [])
        motif = self.notes_by_channel.get(3, [])
        if not vocal or not motif:
            return

        chorus_sections = [sec for sec in self.sections if sec['type'] == 'chorus']
        if not chorus_sections:
            return

        for sec in chorus_sections:
            st = (sec['start_bar'] - 1) * TICKS_PER_BAR
            et = sec['end_bar'] * TICKS_PER_BAR
            vocal_count = sum(1 for n in vocal if st <= n.start < et)
            motif_count = sum(1 for n in motif if st <= n.start < et)

            if motif_count > vocal_count and vocal_count > 0:
                self.add_issue(
                    severity=Severity.INFO,
                    category=Category.ARRANGEMENT,
                    subcategory="blueprint_paradigm",
                    message="Motif dominates vocal in chorus",
                    tick=st, track="Motif/Vocal",
                    details={
                        "vocal_count": vocal_count,
                        "motif_count": motif_count,
                        "paradigm": "MelodyDriven",
                        "section_start_bar": sec['start_bar'],
                    },
                )

    def _check_ballad_density(self):
        """Check that density stays low for Ballad-style blueprints."""
        melodic_chs = [0, 3, 5]
        active_chs = [
            ch for ch in melodic_chs if self.notes_by_channel.get(ch)
        ]
        if not active_chs:
            return

        max_bar = max(tick_to_bar(n.start) for n in self.notes)
        if max_bar == 0:
            return

        total_notes = 0
        for ch in active_chs:
            total_notes += len(self.notes_by_channel[ch])

        avg_notes_per_bar = total_notes / max_bar
        density_limit = 8 * self.profile.density_tolerance

        if avg_notes_per_bar > density_limit:
            self.add_issue(
                severity=Severity.INFO,
                category=Category.ARRANGEMENT,
                subcategory="blueprint_paradigm",
                message=(
                    f"High density for Ballad blueprint "
                    f"({avg_notes_per_bar:.1f} notes/bar, "
                    f"limit: {density_limit:.1f})"
                ),
                tick=0, track="All melodic",
                details={
                    "avg_notes_per_bar": avg_notes_per_bar,
                    "density_limit": density_limit,
                    "density_tolerance": self.profile.density_tolerance,
                    "paradigm": self.profile.paradigm,
                },
            )

    # -----------------------------------------------------------------
    # Guitar analyses
    # -----------------------------------------------------------------

    def _analyze_guitar_chord_redundancy(self):
        """Detect guitar duplicating chord track pitch classes.

        Compares guitar and chord pitch-class sets (mod 12) at each beat.
        Flags when guitar wastes sonic space by duplicating chord voicing.
        """
        guitar_notes = self.notes_by_channel.get(GUITAR_CHANNEL, [])
        chord_notes = self.notes_by_channel.get(1, [])
        if not guitar_notes or not chord_notes:
            return

        max_tick = max(n.end for n in self.notes)
        identical_count = 0
        total_checked = 0

        for tick in range(0, max_tick, TICKS_PER_BEAT):
            guitar_active = [
                n for n in guitar_notes if n.start <= tick < n.end
            ]
            chord_active = [
                n for n in chord_notes if n.start <= tick < n.end
            ]
            if not guitar_active or not chord_active:
                continue

            total_checked += 1
            guitar_pcs = {n.pitch % 12 for n in guitar_active}
            chord_pcs = {n.pitch % 12 for n in chord_active}

            if guitar_pcs == chord_pcs:
                identical_count += 1

        if total_checked == 0:
            return

        identical_ratio = identical_count / total_checked
        if identical_ratio > 0.7:
            self.add_issue(
                severity=Severity.WARNING,
                category=Category.ARRANGEMENT,
                subcategory="guitar_chord_redundancy",
                message=(f"Guitar duplicates chord pitch classes "
                         f"({identical_ratio:.0%} of beats)"),
                tick=0,
                track="Guitar/Chord",
                details={"identical_ratio": identical_ratio,
                         "identical_count": identical_count,
                         "total_checked": total_checked},
            )
        elif identical_ratio > 0.5:
            self.add_issue(
                severity=Severity.INFO,
                category=Category.ARRANGEMENT,
                subcategory="guitar_chord_redundancy",
                message=(f"Guitar partially duplicates chord pitch classes "
                         f"({identical_ratio:.0%} of beats)"),
                tick=0,
                track="Guitar/Chord",
                details={"identical_ratio": identical_ratio,
                         "identical_count": identical_count,
                         "total_checked": total_checked},
            )

    def _analyze_guitar_dynamic_variation(self):
        """Check guitar velocity contrast between verse and chorus.

        Compares average guitar velocity in verse vs chorus sections.
        In pop music, chorus should typically be louder than verse.
        """
        guitar_notes = self.notes_by_channel.get(GUITAR_CHANNEL, [])
        if not guitar_notes:
            return

        verse_vels = []
        chorus_vels = []

        for sec in self.sections:
            st = (sec['start_bar'] - 1) * TICKS_PER_BAR
            et = sec['end_bar'] * TICKS_PER_BAR
            sec_guitar = [
                n for n in guitar_notes if st <= n.start < et
            ]
            if not sec_guitar:
                continue

            avg_vel = sum(n.velocity for n in sec_guitar) / len(sec_guitar)
            if sec['type'] == 'verse':
                verse_vels.append(avg_vel)
            elif sec['type'] == 'chorus':
                chorus_vels.append(avg_vel)

        if not verse_vels or not chorus_vels:
            return

        avg_verse = sum(verse_vels) / len(verse_vels)
        avg_chorus = sum(chorus_vels) / len(chorus_vels)

        if avg_verse >= avg_chorus:
            self.add_issue(
                severity=Severity.INFO,
                category=Category.ARRANGEMENT,
                subcategory="guitar_dynamic_variation",
                message=(f"Guitar has no dynamic contrast "
                         f"(verse: {avg_verse:.0f}, chorus: {avg_chorus:.0f})"),
                tick=0,
                track="Guitar",
                details={"avg_verse_velocity": avg_verse,
                         "avg_chorus_velocity": avg_chorus},
            )
