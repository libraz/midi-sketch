"""Vertical dissonance classification, mirroring the shipped analyzer's interval step.

One place that classifies a sounding interval over a chord degree the way
``checkIntervalDissonance`` in ``src/analysis/dissonance.cpp`` does, so the
checks in ``harmonic.py`` have one definition to build on.

The reach is the analyzer's, judged on the actual interval: the minor 2nd at 1
and 13, the major 2nd at 2 only, the major 7th and the tritone on every octave
up to 24 semitones. Generation (``isDissonantActualInterval``) still refuses a
tritone at 30 and a major 7th at 35; neither report sees those.

``output.json`` does not carry what the C++ report excuses after this step:
the realized chord tones (``chordExcusesFlaggedPair``), the registered
root-maj7, the prepared-suspension tolerance, and the track roles the low-bass
major 7th rule needs. A count from this module is the raw interval rule, an
upper bound on what ``--analyze`` reports, not a copy of it.
"""

from .constants import Severity

# Pitch-class intervals that can be dissonant at all.
_MINOR_SECOND = 1
_MAJOR_SECOND = 2
_TRITONE = 6
_MAJOR_SEVENTH = 11

# Intervals whose verdict depends on the chord; with no harmony known they are
# left unreported rather than judged against an assumed chord.
_CONTEXT_DEPENDENT = (_MAJOR_SECOND, _TRITONE, _MAJOR_SEVENTH)

# Two octaves apart the two notes occupy different registral space and the
# beating is no longer part of the same sonority.
WIDE_SEPARATION_SEMITONES = 24

# Chord degrees whose chord owns a tritone: V7 and vii, and the borrowed
# #IVdim (14). Borrowed degrees are identifiers, not scale steps, so they are
# never reduced modulo 7.
TRITONE_HOME_DEGREES = (4, 6, 14)

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
        chord_degree: Chord degree under them as ``output.json`` names it
            (0-6 diatonic, 8 and above borrowed), or a negative value when the
            harmony at that point is unknown.

    Returns:
        The Severity of the clash, or None when the interval is not dissonant.
    """
    if semitones > WIDE_SEPARATION_SEMITONES:
        return None

    pitch_class = semitones % 12
    compound = semitones > 12

    if chord_degree < 0 and pitch_class in _CONTEXT_DEPENDENT:
        return None

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
        if chord_degree in MAJOR_SEVENTH_COLOUR_DEGREES:
            return Severity.WARNING
        return Severity.ERROR

    if pitch_class == _TRITONE:
        if chord_degree in TRITONE_HOME_DEGREES:
            return None
        return Severity.INFO if compound else Severity.WARNING

    return None
