"""Vertical dissonance classification, mirroring the shipped analyzer.

The generator refuses to place a note that clashes with the notes already
registered around it, so re-deriving that decision here can only ever confirm
it. What this module is for is the other half: a single place that classifies a
sounding interval the same way ``src/analysis/dissonance.cpp`` does, so the
Python report and the C++ ``--analyze`` report cannot drift, and so the checks
that ask questions the generator never asks (see ``harmonic.py``) have one
definition of "dissonant" to build on.

The classification is a property of the sounding interval and the chord under
it, not of how the note came to be there.
"""

from .constants import Severity

# Pitch-class intervals that can be dissonant at all.
_MINOR_SECOND = 1
_MAJOR_SECOND = 2
_TRITONE = 6
_MAJOR_SEVENTH = 11

# Two octaves apart the two notes occupy different registral space and the
# beating is no longer part of the same sonority.
WIDE_SEPARATION_SEMITONES = 24

# Chord degrees the tritone belongs to: it is what defines V7 and vii.
TRITONE_HOME_DEGREES = (4, 6)

# Chord degrees on which a major 7th is a conventional colour rather than a
# clash (I and IV take a major 7th in pop harmony).
MAJOR_SEVENTH_COLOUR_DEGREES = (0, 3)

INTERVAL_NAMES = {
    _MINOR_SECOND: "minor 2nd",
    _MAJOR_SECOND: "major 2nd",
    _TRITONE: "tritone",
    _MAJOR_SEVENTH: "major 7th",
    13: "minor 9th",
}


def interval_name(semitones: int) -> str:
    """Human-readable name for a sounding interval."""
    if semitones == 13:
        return INTERVAL_NAMES[13]
    return INTERVAL_NAMES.get(semitones % 12, "dissonant interval")


def classify_interval_dissonance(semitones: int, chord_degree: int):
    """Classify a sounding interval over a chord degree.

    Args:
        semitones: Absolute distance between the two sounding pitches.
        chord_degree: Scale degree of the chord under them, or a negative
            value when the harmony at that point is unknown.

    Returns:
        The Severity of the clash, or None when the interval is not dissonant.
    """
    if semitones > WIDE_SEPARATION_SEMITONES:
        return None

    pitch_class = semitones % 12
    compound = semitones > 12
    normalized_degree = chord_degree % 7 if chord_degree >= 0 else -1

    if pitch_class == _MINOR_SECOND:
        # Within the separation limit only the minor 2nd and the minor 9th
        # occur, and both beat harshly.
        return Severity.ERROR

    if pitch_class == _MAJOR_SECOND:
        # Only the close major 2nd beats; the major 9th is a pop extension.
        return Severity.ERROR if semitones == _MAJOR_SECOND else None

    if pitch_class == _MAJOR_SEVENTH:
        if compound:
            return Severity.INFO
        if normalized_degree in MAJOR_SEVENTH_COLOUR_DEGREES:
            return Severity.WARNING
        return Severity.ERROR

    if pitch_class == _TRITONE:
        if normalized_degree in TRITONE_HOME_DEGREES:
            return None
        return Severity.INFO if compound else Severity.WARNING

    return None
