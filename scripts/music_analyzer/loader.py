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

    notes = []
    for track in data.get('tracks', []):
        channel = track.get('channel', 0)
        for note_data in track.get('notes', []):
            notes.append(Note(
                start=note_data.get('start_ticks', note_data.get('start', 0)),
                duration=note_data.get('duration_ticks', note_data.get('duration', 0)),
                pitch=note_data['pitch'],
                velocity=note_data.get('velocity', 100),
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
        return {
            'blueprint': data.get('metadata', {}).get('blueprint'),
            'style': data.get('metadata', {}).get('style'),
            'bpm': data.get('metadata', {}).get('bpm'),
            'sections': data.get('sections', []),
        }
    except Exception:
        return {}
