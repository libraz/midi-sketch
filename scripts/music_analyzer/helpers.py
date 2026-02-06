"""Helper functions for music analysis.

Utility functions for note naming, tick-to-bar conversion,
edit distance, pattern similarity, and IOI entropy.
"""

import math
from collections import Counter

from .constants import NOTE_NAMES, TICKS_PER_BAR, TICKS_PER_BEAT


def note_name(pitch: int) -> str:
    """Convert MIDI pitch to note name (e.g., 60 -> 'C4')."""
    octave = (pitch // 12) - 1
    return f"{NOTE_NAMES[pitch % 12]}{octave}"


def tick_to_bar(tick: int) -> int:
    """Convert tick to bar number (1-indexed)."""
    return tick // TICKS_PER_BAR + 1


def tick_to_bar_beat(tick: int) -> str:
    """Convert tick to bar:beat format (e.g., 'bar1:1.000')."""
    bar = tick // TICKS_PER_BAR + 1
    beat = (tick % TICKS_PER_BAR) / TICKS_PER_BEAT + 1
    return f"bar{bar}:{beat:.3f}"


def _edit_distance(seq_a: list, seq_b: list) -> int:
    """Levenshtein edit distance between two sequences.

    Standard dynamic programming implementation. Works with any
    comparable elements (ints, strings, etc.).

    Args:
        seq_a: First sequence.
        seq_b: Second sequence.

    Returns:
        Minimum number of insertions, deletions, and substitutions
        to transform seq_a into seq_b.
    """
    len_a = len(seq_a)
    len_b = len(seq_b)

    # Handle empty sequences
    if len_a == 0:
        return len_b
    if len_b == 0:
        return len_a

    # Create DP table (optimize space: only need two rows)
    prev_row = list(range(len_b + 1))
    curr_row = [0] * (len_b + 1)

    for idx_a in range(1, len_a + 1):
        curr_row[0] = idx_a
        for idx_b in range(1, len_b + 1):
            cost = 0 if seq_a[idx_a - 1] == seq_b[idx_b - 1] else 1
            curr_row[idx_b] = min(
                prev_row[idx_b] + 1,       # deletion
                curr_row[idx_b - 1] + 1,    # insertion
                prev_row[idx_b - 1] + cost  # substitution
            )
        prev_row, curr_row = curr_row, prev_row

    return prev_row[len_b]


def _pattern_similarity(pat_a: list, pat_b: list) -> float:
    """Normalized similarity between two interval patterns.

    Uses Levenshtein edit distance normalized by the longer sequence length.
    Returns a value between 0.0 (completely different) and 1.0 (identical).

    Args:
        pat_a: First interval pattern (list of ints).
        pat_b: Second interval pattern (list of ints).

    Returns:
        Similarity score in range [0.0, 1.0].
    """
    if not pat_a or not pat_b:
        return 0.0
    max_len = max(len(pat_a), len(pat_b))
    dist = _edit_distance(pat_a, pat_b)
    return 1.0 - dist / max_len


def _ioi_entropy(ioi_list: list) -> float:
    """Shannon entropy of inter-onset-interval distribution.

    Higher entropy means more rhythmic variety. Zero entropy means
    all intervals are identical (completely monotonous rhythm).

    Args:
        ioi_list: List of inter-onset intervals (in ticks or quantized units).

    Returns:
        Shannon entropy in bits. Zero if empty or all identical.
    """
    if not ioi_list:
        return 0.0
    total = len(ioi_list)
    counts = Counter(ioi_list)
    entropy = 0.0
    for count in counts.values():
        prob = count / total
        if prob > 0:
            entropy -= prob * math.log2(prob)
    return entropy
