/**
 * Is the song musically sound? Measured by libcantus, which never sees the
 * generator's intent, so a reading here is an outside opinion of the notes.
 *
 * Every metric is defined so it can be taken on a reference song too: a rate
 * means nothing until the same rate is known for real pop, which is what
 * `baseline.mjs` provides. The checks that need the generator's own
 * declarations (chords, key, vocal range) are taken only for a generated song.
 *
 * libcantus's dissonance model is not the generator's (the generator allows a
 * tritone on V and vii°, and treats a major 7th by context), so only the
 * vertical flag — a note clashing with another sounding note on a strong beat —
 * is counted as a clash. Parallel-motion flags are not read at all: libcantus
 * reports oblique motion under a held note as a parallel octave.
 */

import { chordTimelineFromNotes, NoteSafety } from '@libraz/libcantus';
import { BEATS_PER_BAR, chordAt } from './analysis.mjs';

/** libcantus `SafetyReason.VerticalDissonance`. */
const VERTICAL_DISSONANCE = 1 << 10;

const ORNAMENTAL = new Set([
  'passing',
  'neighbor',
  'suspension',
  'appoggiatura',
  'anticipation',
  'escape',
]);
const UNRESOLVED = new Set(['avoid', 'needsResolution']);

const ratio = (part, whole) => (whole ? part / whole : null);
const isStrongBeat = (beat) => Math.abs(beat % 2) < 1e-6;

/** Vertical clashes per 100 bars, by part. */
function clashes(reading, bars) {
  const { analysis, tracks } = reading.harmony;
  const byPart = {};
  let total = 0;
  for (const c of analysis.conflicts) {
    if (c.safety !== NoteSafety.Dissonant || !(c.reasons & VERTICAL_DISSONANCE)) {
      continue;
    }
    const part = tracks[c.trackIndex]?.name ?? '?';
    byPart[part] = (byPart[part] ?? 0) + 1;
    total += 1;
  }
  const per100 = (n) => (bars ? (100 * n) / bars : null);
  return {
    verticalClashesPer100Bars: per100(total),
    melodyClashesPer100Bars: per100(byPart.melody ?? 0),
    bassClashesPer100Bars: per100(byPart.bass ?? 0),
    harmonyClashesPer100Bars: per100(byPart.harmony ?? 0),
  };
}

/** How the sung line sits on the harmony, from libcantus's per-note labels. */
function vocalTones(reading) {
  const index = reading.harmony.trackIndex('melody');
  const notes = index >= 0 ? reading.harmony.analysis.tracks[index].notes : [];
  let chordTone = 0;
  let tension = 0;
  let ornamental = 0;
  let unresolved = 0;
  let strongUnresolved = 0;
  for (const note of notes) {
    const kinds = note.labels.map((l) => l.kind);
    if (kinds.includes('chordTone')) {
      chordTone += 1;
    } else if (kinds.includes('tension')) {
      tension += 1;
    } else if (kinds.some((k) => ORNAMENTAL.has(k))) {
      ornamental += 1;
    } else if (kinds.some((k) => UNRESOLVED.has(k))) {
      unresolved += 1;
      if (isStrongBeat(note.startBeat)) {
        strongUnresolved += 1;
      }
    }
  }
  return {
    vocalChordToneRatio: ratio(chordTone, notes.length),
    vocalTensionRatio: ratio(tension, notes.length),
    vocalOrnamentalRatio: ratio(ornamental, notes.length),
    vocalUnresolvedRatio: ratio(unresolved, notes.length),
    vocalStrongBeatUnresolvedRatio: ratio(strongUnresolved, notes.length),
  };
}

/** How clearly one key governs the song. */
function keyClarity(reading) {
  const { keys } = reading.harmony.analysis;
  const span = reading.song.totalBeats || 1;
  const coverage = new Map();
  for (const k of keys) {
    const id = `${k.key.scale.rootPc}:${k.key.variant}`;
    coverage.set(id, (coverage.get(id) ?? 0) + (k.endBeat - k.startBeat));
  }
  return {
    prevailingKeyShare: Math.max(0, ...coverage.values()) / span,
    keyRegionsPer32Bars: (32 * keys.length) / (span / BEATS_PER_BAR),
  };
}

/** Share of bars whose first bass note is the root of the chord read there. */
function bassOnRoot(reading) {
  const { song, harmony } = reading;
  let bars = 0;
  let onRoot = 0;
  for (let b = 0; b < song.totalBeats; b += BEATS_PER_BAR) {
    const note = song.bass.find((n) => n.startBeat >= b && n.startBeat < b + 1);
    const chord = chordAt(harmony, b);
    if (!note || !chord) {
      continue;
    }
    bars += 1;
    if (note.pitch % 12 === chord.rootPc) {
      onRoot += 1;
    }
  }
  return { bassRootOnDownbeatRatio: ratio(onRoot, bars) };
}

/** Share of choruses that close on the tonic or on a cadence into it. */
function chorusClosure(reading) {
  const { runs, harmony } = reading;
  if (!runs.length) {
    return { chorusTonicClosureRatio: null };
  }
  let closed = 0;
  for (const run of runs) {
    const last = chordAt(harmony, run.endBeat - 1);
    const cadence = harmony.analysis.cadences.some(
      (c) =>
        c.atBeat >= run.endBeat - 2 * BEATS_PER_BAR &&
        c.atBeat < run.endBeat &&
        (c.cadence.type === 'authentic' || c.cadence.type === 'plagal'),
    );
    if (last?.degree === 0 || cadence) {
      closed += 1;
    }
  }
  return { chorusTonicClosureRatio: closed / runs.length };
}

/** Share of libcantus phrases that land on the 2/4/8-bar grid pop phrasing rests on. */
function phraseGrid(reading) {
  const phrases = reading.profile.form.phrases;
  const onGrid = phrases.filter((p) => [2, 4, 8].some((n) => Math.abs(p.bars - n) <= 0.25)).length;
  return { phraseGridRatio: ratio(onGrid, phrases.length) };
}

/**
 * The root libcantus reads from the bass and harmony notes sounding inside one
 * declared chord span, or null when nothing sounds there.
 *
 * The span is read on its own rather than through the song's segmentation:
 * that segmentation merges neighbours sharing two tones (vi then IV read as one
 * IVmaj7) and cannot place a change on a half beat, so across it the check
 * would score the reader's boundaries instead of the notes.
 */
function rootSoundingIn(song, harmony, span) {
  const length = span.endBeat - span.startBeat;
  const notes = [...song.bass, ...song.harmony].flatMap((n) => {
    const start = Math.max(n.startBeat, span.startBeat);
    const end = Math.min(n.startBeat + n.durationBeat, span.endBeat);
    return end > start
      ? [{ ...n, startBeat: start - span.startBeat, durationBeat: end - start }]
      : [];
  });
  if (!notes.length) {
    return null;
  }
  const { timeline } = chordTimelineFromNotes(notes, {
    segmentation: 'grid',
    harmonicRhythm: length,
    key: harmony.keyAt(span.startBeat),
    totalBeats: length,
  });
  return timeline.at(0)?.rootPc ?? null;
}

/** Checks against what the generator declares it wrote. */
function declarations(reading) {
  const { song, harmony } = reading;
  const declared = song.declared;
  if (!declared) {
    return {};
  }
  // Beats of declared harmony something voices; a silent span cannot disagree.
  let voiced = 0;
  let agree = 0;
  for (const c of declared.chords) {
    const rootPc = rootSoundingIn(song, harmony, c);
    if (rootPc === null) {
      continue;
    }
    voiced += c.endBeat - c.startBeat;
    if (rootPc === c.rootPc) {
      agree += c.endBeat - c.startBeat;
    }
  }
  const key = harmony.analysis.prevailingKey;
  const tonic = key.variant === 'major' ? key.scale.rootPc : (key.scale.rootPc + 3) % 12;
  const shiftAt = (beat) =>
    declared.modulationBeat !== null && beat >= declared.modulationBeat
      ? declared.modulationSemitones
      : 0;
  const outOfRange = song.melody.filter(
    (n) =>
      n.pitch < declared.vocalLow + shiftAt(n.startBeat) ||
      n.pitch > declared.vocalHigh + shiftAt(n.startBeat),
  ).length;
  return {
    declaredRootAgreement: ratio(agree, voiced),
    declaredKeyMatch: tonic === declared.keyPc ? 1 : 0,
    vocalOutOfDeclaredRangeRatio: ratio(outOfRange, song.melody.length),
  };
}

/** Metrics that read the sung line against the accompaniment sounding with it. */
export const VOCAL_HARMONY_METRICS = new Set([
  'verticalClashesPer100Bars',
  'melodyClashesPer100Bars',
  'bassClashesPer100Bars',
  'harmonyClashesPer100Bars',
  'vocalChordToneRatio',
  'vocalTensionRatio',
  'vocalOrnamentalRatio',
  'vocalUnresolvedRatio',
  'vocalStrongBeatUnresolvedRatio',
]);

/**
 * Metrics read through the song's form or phrases, which a reference has only
 * as inferred from its vocal and its harmony together: the chorus runs, the
 * hook found in them, and the cadence-weighed phrases.
 */
export const VOCAL_FORM_METRICS = new Set([
  'firstChorusSec',
  'chorusCount',
  'chorusShare',
  'meanChorusBars',
  'chorusMeanPitchLift',
  'chorusPeakLift',
  'songPeakInChorus',
  'chorusVocalDensityRatio',
  'chorusArrangementDensityRatio',
  'chorusVelocityLift',
  'hookStatements',
  'hookStatementsPerChorus',
  'hookChorusExclusivity',
  'hookOpensChorusRatio',
  'hookNoteCount',
  'hookRange',
  'hookMaxLeap',
  'hookRepeatedNoteRatio',
  'hookOffBeatRatio',
  'hookReachesChorusPeak',
  'hookVerseLikeness',
  'chorusRestatementLikeness',
  'verseChorusHarmonyDiffers',
  'chorusTonicClosureRatio',
  'chorusRecognizedRecall',
  'phraseGridRatio',
]);

/** What a song whose vocal does not follow its accompaniment cannot be read for. */
export const UNALIGNED_VOCAL_METRICS = new Set([...VOCAL_HARMONY_METRICS, ...VOCAL_FORM_METRICS]);

/** Vocal offsets, in beats, the written placement is ranked against: every quarter within 12 bars. */
const ALIGNMENT_SHIFTS = Array.from({ length: 384 }, (_, i) => (i < 192 ? i - 192 : i - 191) / 4);
/**
 * Largest share of offsets that may agree better than the written placement.
 * On the reference corpus a vocal in place is beaten by at most 3% of them and
 * one taken from another edit by 25% or more.
 */
const ALIGNMENT_MAX_BEATEN = 0.1;
/** Grid the accompaniment's sounding pitch classes are sampled on, per beat. */
const ALIGNMENT_GRID = 8;

/**
 * Whether the sung line sits where the accompaniment expects it: the share of
 * vocal onsets whose pitch class the accompaniment is sounding, at the written
 * placement, must rank near the top of the same share at shifted placements.
 * A vocal track taken from another edit of the song agrees with its
 * accompaniment no better than chance, so its readings against the harmony
 * mean nothing.
 *
 * @returns {boolean | null} null when there is no vocal or no accompaniment.
 */
export function vocalFollowsAccompaniment(song) {
  const accompaniment = [...song.bass, ...song.harmony];
  if (!song.melody.length || !accompaniment.length) {
    return null;
  }
  const cells = Math.ceil(song.totalBeats * ALIGNMENT_GRID);
  const sounding = new Uint16Array(cells);
  for (const n of accompaniment) {
    const from = Math.max(0, Math.floor(n.startBeat * ALIGNMENT_GRID));
    const to = Math.min(cells, Math.ceil((n.startBeat + n.durationBeat) * ALIGNMENT_GRID));
    for (let i = from; i < to; i += 1) {
      sounding[i] |= 1 << (n.pitch % 12);
    }
  }
  const agreementAt = (shift) => {
    let heard = 0;
    let agree = 0;
    for (const v of song.melody) {
      const cell = Math.floor((v.startBeat + shift) * ALIGNMENT_GRID);
      if (cell < 0 || cell >= cells || !sounding[cell]) {
        continue;
      }
      heard += 1;
      agree += (sounding[cell] >> (v.pitch % 12)) & 1;
    }
    return heard ? agree / heard : 0;
  };
  const written = agreementAt(0);
  const beaten = ALIGNMENT_SHIFTS.filter((shift) => agreementAt(shift) > written).length;
  return beaten <= ALIGNMENT_MAX_BEATEN * ALIGNMENT_SHIFTS.length;
}

/**
 * All soundness metrics for one read song.
 *
 * @returns {Record<string, number | null>}
 */
export function correctnessOf(reading) {
  const bars = reading.song.totalBeats / BEATS_PER_BAR;
  return {
    ...clashes(reading, bars),
    ...vocalTones(reading),
    ...keyClarity(reading),
    ...bassOnRoot(reading),
    ...chorusClosure(reading),
    ...phraseGrid(reading),
    ...declarations(reading),
  };
}
