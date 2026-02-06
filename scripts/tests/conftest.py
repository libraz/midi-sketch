"""Shared test fixtures and helpers for music_analyzer tests."""

import sys
from pathlib import Path
from typing import List

# Ensure the scripts/ directory is on the path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from music_analyzer import (
    Note, Issue, MusicAnalyzer, Severity, Category,
    note_name, tick_to_bar, TICKS_PER_BAR, TICKS_PER_BEAT,
)


def make_chord_notes(tick: int, pitches: List[int], duration: int = 480) -> List[Note]:
    """Create chord notes at given tick."""
    return [
        Note(start=tick, duration=duration, pitch=p, velocity=80, channel=1)
        for p in pitches
    ]


def make_bass_note(tick: int, pitch: int, duration: int = 480) -> Note:
    """Create bass note."""
    return Note(start=tick, duration=duration, pitch=pitch, velocity=80, channel=2)


def make_vocal_note(tick: int, pitch: int, duration: int = 480) -> Note:
    """Create vocal note."""
    return Note(start=tick, duration=duration, pitch=pitch, velocity=100, channel=0)
