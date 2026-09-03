"""File loading functions for music analysis.

Load MIDI note data and metadata from JSON output files.
"""

import json
from typing import List

from .models import Note


def load_json_output(filepath: str) -> List[Note]:
    """Load notes from JSON output.

    Supports both 'start_ticks'/'duration_ticks' and 'start'/'duration'
    field naming conventions.

    Args:
        filepath: Path to the JSON output file.

    Returns:
        List of Note objects parsed from all tracks.
    """
    with open(filepath, 'r') as file_handle:
        data = json.load(file_handle)
    if not isinstance(data, dict):
        raise ValueError("top-level JSON value must be an object")
    tracks = data.get('tracks')
    if not isinstance(tracks, list):
        raise ValueError("missing or invalid 'tracks' array")

    notes = []
    for track_index, track in enumerate(tracks):
        if not isinstance(track, dict):
            raise ValueError(f"track {track_index} must be an object")
        if 'channel' not in track:
            raise ValueError(f"track {track_index} is missing required 'channel'")
        try:
            channel = int(track['channel'])
        except (TypeError, ValueError) as exc:
            raise ValueError(f"track {track_index} has invalid channel") from exc
        if not 0 <= channel <= 15:
            raise ValueError(f"track {track_index} channel must be in 0..15")
        track_notes = track.get('notes')
        if not isinstance(track_notes, list):
            raise ValueError(f"track {track_index} has invalid 'notes' array")
        for note_index, note_data in enumerate(track_notes):
            if not isinstance(note_data, dict):
                raise ValueError(f"track {track_index} note {note_index} must be an object")
            try:
                start = int(note_data.get('start_ticks', note_data.get('start', 0)))
                duration = int(note_data.get('duration_ticks', note_data.get('duration', 0)))
                pitch = int(note_data['pitch'])
                velocity = int(note_data.get('velocity', 100))
            except (KeyError, TypeError, ValueError) as exc:
                raise ValueError(
                    f"track {track_index} note {note_index} has invalid timing, pitch, or velocity"
                ) from exc
            if start < 0 or duration <= 0 or not 0 <= pitch <= 127 or not 0 <= velocity <= 127:
                raise ValueError(
                    f"track {track_index} note {note_index} has values outside MIDI bounds"
                )
            notes.append(Note(
                start=start,
                duration=duration,
                pitch=pitch,
                velocity=velocity,
                channel=channel,
                provenance=note_data.get('provenance'),
            ))

    return notes


def load_json_metadata(filepath: str) -> dict:
    """Load metadata from JSON output.

    Extracts blueprint, style, bpm, and section information from the
    metadata block of the JSON output file.

    Args:
        filepath: Path to the JSON output file.

    Returns:
        Dictionary with metadata fields, or empty dict on failure.
    """
    try:
        with open(filepath, 'r') as file_handle:
            data = json.load(file_handle)
        meta = data.get('metadata', {})
        tracks = data.get('tracks', [])
        # Channels the generator says it transposed. Absent from output written
        # before the flag existed, in which case the reader falls back to the
        # channels that were untransposed then.
        transposed_channels = None
        if any(isinstance(track, dict) and 'transposed' in track for track in tracks):
            transposed_channels = {
                int(track['channel']) for track in tracks
                if isinstance(track, dict) and track.get('transposed')
                and track.get('channel') is not None
            }
        return {
            'blueprint': meta.get('blueprint'),
            'style': meta.get('style'),
            'bpm': data.get('bpm') if data.get('bpm') is not None else meta.get('bpm'),
            'sections': data.get('sections', []),
            'chords': data.get('chords', []),
            'vocal_style': data.get('vocal_style') if data.get('vocal_style') is not None else meta.get('vocal_style'),
            # Key and modulation as the generator declared them: the shift from
            # the internal C major space, with no reconstruction needed.
            'key': meta.get('key'),
            'modulation_tick': meta.get('modulation_tick'),
            'modulation_semitones': meta.get('modulation_semitones'),
            # The range the vocal was written against. Absent from output that
            # does not state it, in which case the documented default applies.
            'vocal_low': meta.get('vocal_low'),
            'vocal_high': meta.get('vocal_high'),
            'transposed_channels': transposed_channels,
        }
    except Exception:
        return {}
