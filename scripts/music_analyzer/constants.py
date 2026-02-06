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
    6: "Guitar",
    9: "Drums",
    15: "SE"
}

TRACK_CHANNELS = {v: k for k, v in TRACK_NAMES.items()}

# Guitar analysis constants
GUITAR_CHANNEL = 6
GUITAR_BASS_MUD_THRESHOLD = 52   # E3: below this overlaps bass register
GUITAR_STRUM_MIN_VOICES = 3      # Minimum voices for proper strum chord

NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']

# VocalStylePreset IDs (from melody_types.h)
VOCAL_STYLE_ULTRA_VOCALOID = 3

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
    6: (40, 88),   # Guitar: E2-E6
    2: (28, 60),   # Bass: E1-C4
}

# Estimated section length for structural analysis
SECTION_LENGTH_BARS = 8

# Bass preferred chord degrees (legacy, kept for compatibility)
BASS_PREFERRED_DEGREES = {0, 4}  # Root, 5th
BASS_ACCEPTABLE_DEGREES = {0, 2, 4}  # Root, 3rd, 5th

# Chord degree to root pitch class mapping (C major, 0-indexed degrees)
# degree 0=I(C), 1=ii(D), 2=iii(E), 3=IV(F), 4=V(G), 5=vi(A), 6=vii(B)
DEGREE_TO_ROOT_PC = {0: 0, 1: 2, 2: 4, 3: 5, 4: 7, 5: 9, 6: 11}

# Chord degree to chord tones (pitch classes in C major)
# Each chord has root, 3rd, 5th as diatonic chord tones
DEGREE_TO_CHORD_TONES = {
    0: {0, 4, 7},    # I:   C, E, G
    1: {2, 5, 9},    # ii:  D, F, A
    2: {4, 7, 11},   # iii: E, G, B
    3: {5, 9, 0},    # IV:  F, A, C
    4: {7, 11, 2},   # V:   G, B, D
    5: {9, 0, 4},    # vi:  A, C, E
    6: {11, 2, 5},   # vii: B, D, F
}

# Chord function mapping: root pitch class -> harmonic function
# T=Tonic, S=Subdominant, D=Dominant
CHORD_FUNCTION_MAP = {0: 'T', 2: 'S', 4: 'S', 5: 'T', 7: 'D', 9: 'T', 11: 'D'}

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
