#!/usr/bin/env python3
"""Compare lightweight MIDI profiles.

Extracts tempo, duration, note density, pitch range, rhythmic grid usage, and
rough vertical sonority changes from SMF files. This intentionally avoids third
party dependencies so it can run in this repository's default environment.
"""

from __future__ import annotations

import argparse
import json
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from statistics import mean


@dataclass
class Note:
    track: int
    channel: int
    pitch: int
    velocity: int
    start: int
    duration: int


def read_varlen(data: bytes, pos: int) -> tuple[int, int]:
    value = 0
    while True:
        byte = data[pos]
        pos += 1
        value = (value << 7) | (byte & 0x7F)
        if not (byte & 0x80):
            return value, pos


def parse_smf(path: Path) -> dict:
    data = path.read_bytes()
    pos = 0
    if data[pos:pos + 4] != b"MThd":
        raise ValueError(f"{path} is not an SMF file")
    pos += 4
    header_len = int.from_bytes(data[pos:pos + 4], "big")
    pos += 4
    fmt = int.from_bytes(data[pos:pos + 2], "big")
    ntrks = int.from_bytes(data[pos + 2:pos + 4], "big")
    division = int.from_bytes(data[pos + 4:pos + 6], "big")
    pos += header_len

    notes: list[Note] = []
    tempos: list[tuple[int, int]] = []
    track_names: dict[int, str] = {}

    for track_index in range(ntrks):
        if data[pos:pos + 4] != b"MTrk":
            raise ValueError(f"{path} has invalid track header at track {track_index}")
        pos += 4
        track_len = int.from_bytes(data[pos:pos + 4], "big")
        pos += 4
        end = pos + track_len
        tick = 0
        running_status = None
        active: dict[tuple[int, int], list[tuple[int, int]]] = defaultdict(list)

        while pos < end:
            delta, pos = read_varlen(data, pos)
            tick += delta
            status = data[pos]
            if status < 0x80:
                if running_status is None:
                    raise ValueError(f"{path} has MIDI running status without status byte")
                status = running_status
            else:
                pos += 1
                if status < 0xF0:
                    running_status = status

            if status == 0xFF:
                meta_type = data[pos]
                pos += 1
                length, pos = read_varlen(data, pos)
                payload = data[pos:pos + length]
                pos += length
                if meta_type == 0x03:
                    try:
                        track_names[track_index] = payload.decode("utf-8", "replace")
                    except UnicodeDecodeError:
                        pass
                elif meta_type == 0x51 and length == 3:
                    tempos.append((tick, int.from_bytes(payload, "big")))
                continue

            if status in (0xF0, 0xF7):
                length, pos = read_varlen(data, pos)
                pos += length
                continue

            event_type = status & 0xF0
            channel = status & 0x0F
            data_len = 1 if event_type in (0xC0, 0xD0) else 2
            event_data = data[pos:pos + data_len]
            pos += data_len

            if event_type == 0x90:
                pitch, velocity = event_data
                key = (channel, pitch)
                if velocity > 0:
                    active[key].append((tick, velocity))
                elif active[key]:
                    start, start_velocity = active[key].pop(0)
                    notes.append(Note(track_index, channel, pitch, start_velocity, start, tick - start))
            elif event_type == 0x80:
                pitch = event_data[0]
                key = (channel, pitch)
                if active[key]:
                    start, start_velocity = active[key].pop(0)
                    notes.append(Note(track_index, channel, pitch, start_velocity, start, tick - start))

        pos = end

    return {
        "format": fmt,
        "tracks": ntrks,
        "division": division,
        "notes": notes,
        "tempos": tempos,
        "track_names": track_names,
    }


def ticks_to_seconds(tick: int, division: int, tempos: list[tuple[int, int]]) -> float:
    if not tempos:
        tempos = [(0, 500000)]
    tempos = sorted(tempos)
    if tempos[0][0] != 0:
        tempos.insert(0, (0, 500000))
    seconds = 0.0
    last_tick = 0
    last_mpqn = tempos[0][1]
    for tempo_tick, mpqn in tempos[1:]:
        if tick <= tempo_tick:
            break
        seconds += (tempo_tick - last_tick) * last_mpqn / 1_000_000 / division
        last_tick = tempo_tick
        last_mpqn = mpqn
    seconds += (tick - last_tick) * last_mpqn / 1_000_000 / division
    return seconds


def profile(path: Path) -> dict:
    midi = parse_smf(path)
    notes: list[Note] = midi["notes"]
    division = midi["division"]
    end_tick = max((n.start + n.duration for n in notes), default=0)
    duration_seconds = ticks_to_seconds(end_tick, division, midi["tempos"])
    bpm_values = [round(60_000_000 / mpqn, 2) for _, mpqn in midi["tempos"] if mpqn > 0]
    first_bpm = bpm_values[0] if bpm_values else 120.0
    beats = end_tick / division if division else 0
    bars = beats / 4

    by_track = defaultdict(list)
    by_channel = defaultdict(list)
    duration_counter = Counter()
    onset_grid = Counter()
    pitch_classes = Counter()
    for note in notes:
        by_track[note.track].append(note)
        by_channel[note.channel].append(note)
        q_duration = round(note.duration / division, 3)
        duration_counter[q_duration] += 1
        grid = round((note.start % division) / division, 3)
        onset_grid[grid] += 1
        pitch_classes[note.pitch % 12] += 1

    non_drum = [n for n in notes if n.channel != 9]
    melodic = sorted(non_drum, key=lambda n: (n.start, -n.pitch))
    intervals = [abs(b.pitch - a.pitch) for a, b in zip(melodic, melodic[1:]) if a.channel == b.channel]

    track_profiles = []
    for track, track_notes in sorted(by_track.items()):
        track_end = max((n.start + n.duration for n in track_notes), default=0)
        track_bars = (track_end / division / 4) if division else 0
        pitches = [n.pitch for n in track_notes]
        track_profiles.append({
            "track": track,
            "name": midi["track_names"].get(track, ""),
            "channel": min((n.channel for n in track_notes), default=0),
            "notes": len(track_notes),
            "notes_per_bar": round(len(track_notes) / track_bars, 2) if track_bars else 0,
            "pitch_min": min(pitches) if pitches else None,
            "pitch_max": max(pitches) if pitches else None,
            "avg_pitch": round(mean(pitches), 2) if pitches else None,
        })

    return {
        "file": str(path),
        "format": midi["format"],
        "tracks": midi["tracks"],
        "division": division,
        "bpm_first": first_bpm,
        "bpm_values": bpm_values[:8],
        "duration_seconds": round(duration_seconds, 2),
        "bars": round(bars, 2),
        "total_notes": len(notes),
        "non_drum_notes": len(non_drum),
        "drum_notes": len(notes) - len(non_drum),
        "notes_per_bar": round(len(notes) / bars, 2) if bars else 0,
        "non_drum_notes_per_bar": round(len(non_drum) / bars, 2) if bars else 0,
        "sixteenth_onset_ratio": round(
            sum(c for g, c in onset_grid.items() if abs((g * 4) - round(g * 4)) < 0.02) / len(notes),
            3,
        ) if notes else 0,
        "top_durations_quarters": duration_counter.most_common(8),
        "top_onset_positions_quarter": onset_grid.most_common(8),
        "pitch_class_histogram": dict(sorted(pitch_classes.items())),
        "avg_same_channel_interval": round(mean(intervals), 2) if intervals else 0,
        "track_profiles": track_profiles,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("midi", nargs="+", type=Path)
    args = parser.parse_args()
    print(json.dumps([profile(path) for path in args.midi], indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
