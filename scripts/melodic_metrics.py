"""Melodic discipline metrics — measurement only.

Measures melodic properties of a monophonic note sequence. This module
deliberately contains NO thresholds, judgments, severities, or genre
defaults: judgment values live exclusively in the layered rule tables
(target_profiles.json `_common` / per-category sections). Any constant
here is a *definition* of what is being measured (e.g. "step" means
<= 2 semitones), never a pass/fail boundary.

Note sequences are lists of (start_tick, duration_tick, pitch) tuples
sorted by start tick. Use `from_objects()` to adapt Note-like objects
(compare_midi_profile.Note, music_analyzer models) and `skyline()` to
extract a monophonic melody from tracks containing harmony dyads.

Shared by:
- scripts/validate_melodic_rules.py (Layer 1 corpus validation)
- scripts/build_reference_targets.py (Layer 2 category range derivation)
- scripts/music_analyzer/ (generation-side evaluation)
Keeping a single implementation prevents the evaluator and the reference
profiler from drifting apart.
"""

from __future__ import annotations

from collections import Counter

# Interval class boundaries (definitions, not judgments).
STEP_MAX = 2  # |interval| <= 2 semitones = stepwise (incl. minor/major 2nd)
LEAP_SMALL_MAX = 4  # 3-4 semitones = small leap (3rds)
LEAP_LARGE_MIN = 5  # >= 5 semitones = large leap


MelNote = tuple[int, int, int]  # (start_tick, duration_tick, pitch)


def from_objects(notes) -> list[MelNote]:
    """Adapt Note-like objects (.start/.duration/.pitch) to MelNote tuples."""
    seq = []
    for n in notes:
        start = getattr(n, "start", None)
        if start is None:
            start = n.start_tick
        seq.append((int(start), int(n.duration), int(n.pitch)))
    seq.sort()
    return seq


def skyline(seq: list[MelNote]) -> list[MelNote]:
    """Keep only the highest pitch at each onset (melody from harmony dyads)."""
    best: dict[int, MelNote] = {}
    for note in seq:
        cur = best.get(note[0])
        if cur is None or note[2] > cur[2]:
            best[note[0]] = note
    return sorted(best.values())


def intervals(seq: list[MelNote]) -> list[int]:
    """Signed pitch deltas between consecutive notes."""
    return [seq[i + 1][2] - seq[i][2] for i in range(len(seq) - 1)]


# ---------------------------------------------------------------------------
# S1: interval distribution
# ---------------------------------------------------------------------------


def interval_distribution(seq: list[MelNote]) -> dict:
    """Ratios of same / step / small-leap / large-leap moves."""
    ivs = intervals(seq)
    c = Counter()
    for iv in ivs:
        a = abs(iv)
        if a == 0:
            c["same"] += 1
        elif a <= STEP_MAX:
            c["step"] += 1
        elif a <= LEAP_SMALL_MAX:
            c["leap_small"] += 1
        else:
            c["leap_large"] += 1
    total = max(1, len(ivs))
    return {
        "moves": len(ivs),
        "same_ratio": c["same"] / total,
        "step_ratio": c["step"] / total,
        "leap_small_ratio": c["leap_small"] / total,
        "leap_large_ratio": c["leap_large"] / total,
    }


# ---------------------------------------------------------------------------
# S2 / C2: leap recovery
# ---------------------------------------------------------------------------


def leap_recovery(seq: list[MelNote], leap_min: int = LEAP_LARGE_MIN) -> dict:
    """How leaps >= leap_min semitones are followed.

    step_recovery: next move is contrary direction AND <= STEP_MAX semitones.
    contrary: next move is contrary direction (any size) or repeats the pitch.
    Leaps at the very end of the sequence have no follow-up and are skipped.
    """
    ivs = intervals(seq)
    leaps = 0
    step_recovered = 0
    contrary = 0
    for i in range(len(ivs) - 1):
        if abs(ivs[i]) < leap_min:
            continue
        leaps += 1
        nxt = ivs[i + 1]
        opposite = (ivs[i] > 0) != (nxt > 0)
        if nxt == 0 or opposite:
            contrary += 1
            if abs(nxt) <= STEP_MAX and nxt != 0:
                step_recovered += 1
    return {
        "leaps": leaps,
        "step_recovery_rate": step_recovered / leaps if leaps else None,
        "contrary_rate": contrary / leaps if leaps else None,
    }


# ---------------------------------------------------------------------------
# S3 / C1: same-direction leap chains
# ---------------------------------------------------------------------------


def same_direction_leap_chains(seq: list[MelNote], leap_min: int = 3) -> dict:
    """Chains of consecutive same-direction leaps (each |iv| >= leap_min).

    chain length = number of consecutive leap intervals in one direction.
    A chain of 3+ spans 4+ notes and outlines an arpeggio, not a line.
    """
    ivs = intervals(seq)
    max_chain = 0
    chains3 = 0
    cur = 0
    cur_sign = 0
    for iv in ivs:
        sign = 1 if iv > 0 else -1 if iv < 0 else 0
        if abs(iv) >= leap_min and sign != 0 and (cur == 0 or sign == cur_sign):
            cur += 1
            cur_sign = sign
        else:
            if cur >= 3:
                chains3 += 1
            cur = 1 if abs(iv) >= leap_min and sign != 0 else 0
            cur_sign = sign
        max_chain = max(max_chain, cur)
    if cur >= 3:
        chains3 += 1
    return {
        "max_chain": max_chain,
        "chains_3plus": chains3,
        "chains_3plus_per_100_moves": 100.0 * chains3 / max(1, len(ivs)),
    }


# ---------------------------------------------------------------------------
# S4: contour turns
# ---------------------------------------------------------------------------


def contour_turn_rate(seq: list[MelNote]) -> dict:
    """Direction changes per 100 non-zero moves."""
    dirs = [1 if iv > 0 else -1 for iv in intervals(seq) if iv != 0]
    turns = sum(1 for i in range(len(dirs) - 1) if dirs[i] != dirs[i + 1])
    return {
        "directed_moves": len(dirs),
        "turns_per_100": 100.0 * turns / max(1, len(dirs) - 1) if len(dirs) > 1 else None,
    }


# ---------------------------------------------------------------------------
# S5: fast-run conduct
# ---------------------------------------------------------------------------


def fast_run_step_ratio(seq: list[MelNote], division: int) -> dict:
    """Within fast runs, how much motion is same/stepwise.

    A fast run = 3+ consecutive notes whose inter-onset interval is at or
    below an eighth note (division // 2). Fast passages in singable melody
    move by step or repeat pitch (scale runs / chant); leaping at speed is
    what makes generated melody sound instrumental.
    """
    ioi_max = division // 2
    runs: list[list[MelNote]] = []
    cur = [seq[0]] if seq else []
    for i in range(1, len(seq)):
        if seq[i][0] - seq[i - 1][0] <= ioi_max:
            cur.append(seq[i])
        else:
            if len(cur) >= 3:
                runs.append(cur)
            cur = [seq[i]]
    if len(cur) >= 3:
        runs.append(cur)

    moves = 0
    conjunct = 0
    run_notes = 0
    for run in runs:
        run_notes += len(run)
        for iv in intervals(run):
            moves += 1
            if abs(iv) <= STEP_MAX:
                conjunct += 1
    return {
        "runs": len(runs),
        "run_note_ratio": run_notes / max(1, len(seq)),
        "run_conjunct_ratio": conjunct / moves if moves else None,
    }


# ---------------------------------------------------------------------------
# S6: range and climax
# ---------------------------------------------------------------------------


def range_climax(seq: list[MelNote]) -> dict:
    """Total range and where/how often the highest pitch is spent."""
    if not seq:
        return {"range": 0, "climax_position": None, "climax_count": 0}
    pitches = [p for _, _, p in seq]
    hi = max(pitches)
    first_tick = seq[0][0]
    last_tick = seq[-1][0]
    span = max(1, last_tick - first_tick)
    first_hi = next(s for s, _, p in seq if p == hi)
    return {
        "range": hi - min(pitches),
        "climax_position": (first_hi - first_tick) / span,
        "climax_count": pitches.count(hi),
    }


# ---------------------------------------------------------------------------
# S7: phrase arcs
# ---------------------------------------------------------------------------


def split_phrases(seq: list[MelNote], division: int, gap_ticks: int | None = None
                  ) -> list[list[MelNote]]:
    """Split on silences >= gap_ticks (default: quarter note) between notes."""
    if gap_ticks is None:
        gap_ticks = division
    phrases: list[list[MelNote]] = []
    cur: list[MelNote] = []
    for note in seq:
        if cur and note[0] - (cur[-1][0] + cur[-1][1]) >= gap_ticks:
            phrases.append(cur)
            cur = []
        cur.append(note)
    if cur:
        phrases.append(cur)
    return phrases


def _classify_arc(phrase: list[MelNote]) -> str:
    pitches = [p for _, _, p in phrase]
    lo, hi = min(pitches), max(pitches)
    if hi - lo <= STEP_MAX:
        return "flat"
    peak_pos = pitches.index(hi) / max(1, len(pitches) - 1)
    rises = pitches[-1] - pitches[0]
    if peak_pos >= 0.7 and rises >= 3:
        return "ascending"
    if peak_pos <= 0.3 and rises <= -3:
        return "descending"
    if 0.2 <= peak_pos <= 0.8 and pitches[0] < hi > pitches[-1]:
        return "arch"
    return "other"


def phrase_arc_distribution(seq: list[MelNote], division: int,
                            min_notes: int = 4) -> dict:
    """Distribution of phrase contour classes (phrases with >= min_notes)."""
    phrases = [p for p in split_phrases(seq, division) if len(p) >= min_notes]
    c = Counter(_classify_arc(p) for p in phrases)
    total = max(1, len(phrases))
    return {
        "phrases": len(phrases),
        "flat_ratio": c["flat"] / total,
        "arch_ratio": c["arch"] / total,
        "ascending_ratio": c["ascending"] / total,
        "descending_ratio": c["descending"] / total,
        "other_ratio": c["other"] / total,
    }


# ---------------------------------------------------------------------------
# S8: pitch-aware repetition
# ---------------------------------------------------------------------------


def pitch_cell_consistency(seq: list[MelNote], division: int) -> dict:
    """Bar-level melodic repetition with pitch included.

    Cell = ordered (16th-grid position, pitch) pairs per bar. Unlike the
    rhythm-only repeat_cell_consistency, two bars only match when both
    rhythm AND pitches repeat — this measures hooks/riffs, not pulse.
    Bars with >= 2 notes are eligible; consistency = most common cell count
    / eligible bars.
    """
    bar_ticks = division * 4
    sixteenth = max(1, division // 4)
    cells: dict[int, list[tuple[int, int]]] = {}
    for start, _, pitch in seq:
        cells.setdefault(start // bar_ticks, []).append(
            (round((start % bar_ticks) / sixteenth), pitch))
    eligible = {bar: tuple(v) for bar, v in cells.items() if len(v) >= 2}
    if not eligible:
        return {"eligible_bars": 0, "pitch_cell_consistency": None}
    counts = Counter(eligible.values())
    return {
        "eligible_bars": len(eligible),
        "pitch_cell_consistency": counts.most_common(1)[0][1] / len(eligible),
    }


# ---------------------------------------------------------------------------
# Same-pitch streaks
# ---------------------------------------------------------------------------


def same_pitch_streaks(seq: list[MelNote]) -> dict:
    """Max and mean run length of consecutive identical pitches."""
    if not seq:
        return {"max_streak": 0, "mean_streak": None}
    streaks = []
    cur = 1
    for iv in intervals(seq):
        if iv == 0:
            cur += 1
        else:
            streaks.append(cur)
            cur = 1
    streaks.append(cur)
    return {
        "max_streak": max(streaks),
        "mean_streak": sum(streaks) / len(streaks),
    }


# ---------------------------------------------------------------------------
# H2 / C4: physical singability
# ---------------------------------------------------------------------------


def unsingable_moves(seq: list[MelNote], division: int, bpm: float,
                     leap_min: int = 13, max_seconds: float = 0.15) -> dict:
    """Moves wider than an octave executed faster than max_seconds."""
    sec_per_tick = 60.0 / (bpm * division)
    ivs = intervals(seq)
    count = 0
    for i, iv in enumerate(ivs):
        ioi = (seq[i + 1][0] - seq[i][0]) * sec_per_tick
        if abs(iv) >= leap_min and ioi < max_seconds:
            count += 1
    return {
        "moves": len(ivs),
        "unsingable": count,
        "unsingable_rate": count / max(1, len(ivs)),
    }


# ---------------------------------------------------------------------------
# Aggregate
# ---------------------------------------------------------------------------


def melody_profile(seq: list[MelNote], division: int, bpm: float | None = None) -> dict:
    """All melodic measurements for one monophonic sequence."""
    profile: dict = {"notes": len(seq)}
    profile.update(interval_distribution(seq))
    profile.update({f"leap5_{k}": v for k, v in leap_recovery(seq, 5).items()})
    profile.update({f"leap12_{k}": v for k, v in leap_recovery(seq, 12).items()})
    profile.update(same_direction_leap_chains(seq))
    profile.update(contour_turn_rate(seq))
    profile.update(fast_run_step_ratio(seq, division))
    profile.update(range_climax(seq))
    profile.update(phrase_arc_distribution(seq, division))
    profile.update(pitch_cell_consistency(seq, division))
    profile.update(same_pitch_streaks(seq))
    if bpm:
        profile.update(unsingable_moves(seq, division, bpm))
    return profile
