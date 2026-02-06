"""Constants and enums for music analysis.

All MIDI constants, track definitions, interval classifications,
and severity/category enums used across the music analyzer.
"""

from enum import Enum


# =============================================================================
# MIDI CONSTANTS
# =============================================================================

TICKS_PER_BEAT = 480
TICKS_PER_BAR = 1920

TRACK_NAMES = {
    0: "Vocal",
    1: "Chord",
    2: "Bass",
    3: "Motif",
    4: "Arpeggio",
    5: "Aux",
    9: "Drums",
    15: "SE"
}

TRACK_CHANNELS = {v: k for k, v in TRACK_NAMES.items()}

NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']

BLUEPRINT_NAMES = {
    0: "Traditional",
    1: "RhythmLock",
    2: "StoryPop",
    3: "Ballad",
    4: "IdolStandard",
    5: "IdolHyper",
    6: "IdolKawaii",
    7: "IdolCoolPop",
    8: "IdolEmo",
}

# C major scale pitch classes
C_MAJOR_SCALE = {0, 2, 4, 5, 7, 9, 11}  # C, D, E, F, G, A, B

# Dissonant intervals
DISSONANT_INTERVALS = {
    1: "minor 2nd",
    2: "major 2nd",
    11: "major 7th",
    13: "minor 9th",  # compound
}

# Consonant and semi-consonant leap intervals (in semitones)
CONSONANT_LEAPS = {5, 7, 12}  # P4, P5, P8
SEMI_CONSONANT_LEAPS = {3, 4, 8, 9}  # m3, M3, m6, M6

# Track ranges (approximate expected pitch ranges)
TRACK_RANGES = {
    0: (55, 84),   # Vocal: G3-C6
    3: (48, 84),   # Motif: C3-C6
    4: (48, 84),   # Arpeggio: C3-C6
    5: (48, 84),   # Aux: C3-C6
    2: (28, 60),   # Bass: E1-C4
}

# Estimated section length for structural analysis
SECTION_LENGTH_BARS = 8

# Bass preferred chord degrees
BASS_PREFERRED_DEGREES = {0, 4}  # Root, 5th
BASS_ACCEPTABLE_DEGREES = {0, 2, 4}  # Root, 3rd, 5th

# Beat strength weights (1-indexed beat positions in 4/4)
BEAT_STRENGTH = {1: 1.0, 2: 0.4, 3: 0.7, 4: 0.4}

# Singability interval thresholds (in semitones)
SINGABILITY_STEP_MAX = 5   # <=5 semitones = step
SINGABILITY_SKIP_MAX = 9   # <=9 semitones = skip


# =============================================================================
# ENUMS
# =============================================================================

class Severity(Enum):
    ERROR = "error"
    WARNING = "warning"
    INFO = "info"


class Category(Enum):
    MELODIC = "melodic"
    HARMONIC = "harmonic"
    RHYTHM = "rhythm"
    ARRANGEMENT = "arrangement"
    STRUCTURE = "structure"
