#!/usr/bin/env python3
"""Compare motif/rhythm profiles between reference MIDI files and generated JSON.

The report focuses on composition-level features that matter for
chord-pulse reference/RhythmSync-style support motifs: onset grid, short pulse ratio,
repeat-cell consistency, chord-tone concentration, and lead interference.
It intentionally ignores velocity and humanized timing as quality levers.
"""

from __future__ import annotations

import argparse
import json
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from statistics import mean
from typing import Iterable

from compare_midi_profile import Note, parse_smf


TICKS_PER_BAR_FALLBACK = 480 * 4


@dataclass
class TrackProfile:
    source: str
    track: str
    notes: int
    notes_per_bar: float
    short_pulse_ratio: float
    eighth_grid_ratio: float
    sixteenth_grid_ratio: float
    repeat_cell_consistency: float
    pitch_class_focus: float
    avg_pitch: float
    lead_overlap_ratio: float | None = None
    lead_overtake_ratio: float | None = None


def load_notes(path: Path) -> tuple[int, list[Note], dict[int, str], list[dict]]:
    if path.suffix.lower() == ".json":
        data = json.loads(path.read_text())
        division = int(data.get("division", 480))
        notes: list[Note] = []
        names: dict[int, str] = {}
        for idx, track in enumerate(data.get("tracks", [])):
            channel = int(track.get("channel", idx))
            names[channel] = str(track.get("name", f"Ch{channel}"))
            for note in track.get("notes", []):
                notes.append(
                    Note(
                        track=channel,
                        channel=channel,
                        pitch=int(note["pitch"]),
                        velocity=int(note.get("velocity", 80)),
                        start=int(note.get("start_ticks", note.get("start", 0))),
                        duration=int(note.get("duration_ticks", note.get("duration", 0))),
                    )
                )
        return division, notes, names, data.get("chords", [])

    midi = parse_smf(path)
    return midi["division"], midi["notes"], midi["track_names"], []


def end_tick(notes: Iterable[Note]) -> int:
    return max((n.start + n.duration for n in notes), default=0)


def ratio_on_grid(notes: list[Note], grid: int, tolerance: int) -> float:
    if not notes or grid <= 0:
        return 0.0
    hits = 0
    for note in notes:
        rem = note.start % grid
        if rem <= tolerance or rem >= grid - tolerance:
            hits += 1
    return hits / len(notes)


def repeat_cell_consistency(notes: list[Note], division: int) -> float:
    if len(notes) < 8:
        return 0.0
    bar_ticks = division * 4
    cells: dict[int, list[int]] = defaultdict(list)
    for note in notes:
        bar = note.start // bar_ticks
        pos = round((note.start % bar_ticks) / (division / 4))
        cells[bar].append(pos)
    patterns = [tuple(sorted(v)) for v in cells.values() if len(v) >= 2]
    if not patterns:
        return 0.0
    counts = Counter(patterns)
    return max(counts.values()) / len(patterns)


def pitch_class_focus(notes: list[Note]) -> float:
    if not notes:
        return 0.0
    pcs = Counter(n.pitch % 12 for n in notes)
    top = sum(count for _, count in pcs.most_common(4))
    return top / len(notes)


def overlap_stats(track: list[Note], lead: list[Note]) -> tuple[float, float]:
    if not track or not lead:
        return 0.0, 0.0
    track_ticks = sum(max(0, n.duration) for n in track)
    if track_ticks <= 0:
        return 0.0, 0.0
    overlap_ticks = 0
    overtake_ticks = 0
    for note in track:
        note_end = note.start + note.duration
        for lead_note in lead:
            lead_end = lead_note.start + lead_note.duration
            overlap = min(note_end, lead_end) - max(note.start, lead_note.start)
            if overlap <= 0:
                continue
            overlap_ticks += overlap
            if note.pitch >= lead_note.pitch - 2:
                overtake_ticks += overlap
    return overlap_ticks / track_ticks, overtake_ticks / max(overlap_ticks, 1)


def profile_track(source: str, label: str, notes: list[Note], division: int,
                  lead: list[Note] | None) -> TrackProfile:
    bars = max(end_tick(notes) / (division * 4), 1.0)
    short_pulse = sum(1 for n in notes if n.duration <= division / 4) / len(notes) if notes else 0.0
    overlap = overtake = None
    if lead is not None:
        overlap, overtake = overlap_stats(notes, lead)
    return TrackProfile(
        source=source,
        track=label,
        notes=len(notes),
        notes_per_bar=round(len(notes) / bars, 2),
        short_pulse_ratio=round(short_pulse, 3),
        eighth_grid_ratio=round(ratio_on_grid(notes, division // 2, max(8, division // 32)), 3),
        sixteenth_grid_ratio=round(ratio_on_grid(notes, division // 4, max(8, division // 32)), 3),
        repeat_cell_consistency=round(repeat_cell_consistency(notes, division), 3),
        pitch_class_focus=round(pitch_class_focus(notes), 3),
        avg_pitch=round(mean([n.pitch for n in notes]), 2) if notes else 0.0,
        lead_overlap_ratio=round(overlap, 3) if overlap is not None else None,
        lead_overtake_ratio=round(overtake, 3) if overtake is not None else None,
    )


def select_candidate_tracks(notes: list[Note], names: dict[int, str], division: int,
                            top: int) -> list[tuple[str, list[Note]]]:
    by_track: dict[int, list[Note]] = defaultdict(list)
    for note in notes:
        if note.channel == 9:
            continue
        by_track[note.track].append(note)
    candidates = []
    for track, track_notes in by_track.items():
        if len(track_notes) < 16:
            continue
        short = sum(1 for n in track_notes if n.duration <= division / 4) / len(track_notes)
        repeat = repeat_cell_consistency(track_notes, division)
        score = short * 0.35 + repeat * 0.35 + min(len(track_notes) / 1000, 1.0) * 0.30
        label = names.get(track, f"track_{track}") or f"track_{track}"
        candidates.append((score, label, track_notes))
    candidates.sort(key=lambda item: item[0], reverse=True)
    return [(label, track_notes) for _, label, track_notes in candidates[:top]]


def generated_tracks(notes: list[Note], names: dict[int, str]) -> list[tuple[str, list[Note], list[Note] | None]]:
    by_channel: dict[int, list[Note]] = defaultdict(list)
    for note in notes:
        by_channel[note.channel].append(note)
    lead = by_channel.get(0, [])
    result = []
    for channel in (3, 5, 6, 4, 1):
        if by_channel.get(channel):
            result.append((names.get(channel, f"Ch{channel}"), by_channel[channel], lead))
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sources", nargs="+", type=Path)
    parser.add_argument("--top", type=int, default=5)
    args = parser.parse_args()

    report = []
    for path in args.sources:
        division, notes, names, _ = load_notes(path)
        if path.suffix.lower() == ".json":
            tracks = generated_tracks(notes, names)
            profiles = [
                profile_track(str(path), label, track_notes, division, lead)
                for label, track_notes, lead in tracks
            ]
        else:
            profiles = [
                profile_track(str(path), label, track_notes, division, None)
                for label, track_notes in select_candidate_tracks(notes, names, division, args.top)
            ]
        report.extend(profile.__dict__ for profile in profiles)

    print(json.dumps(report, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
