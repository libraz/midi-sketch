/**
 * Pop-craft measurements: how the song is laid out in time, whether its chorus
 * lifts, whether it has a hook and how that hook is built, and which of the
 * well-worn pop progressions it leans on.
 *
 * Each metric is a plain number with no verdict attached; whether a value is
 * good is read against the reference baseline, not decided here.
 */

import { analyzeRhythm, melodicSimilarity } from '@libraz/libcantus';
import { BEATS_PER_BAR } from './analysis.mjs';
import { secondsAt } from './song.mjs';

/** Named four-chord pop progressions, as major-key degrees in semitones. */
export const POP_PROGRESSIONS = {
  royalRoad: [5, 7, 4, 9], // IV V iii vi
  komuro: [9, 5, 7, 0], // vi IV V I
  axis: [0, 7, 9, 5], // I V vi IV
  axisMinor: [9, 5, 0, 7], // vi IV I V
  canonHead: [0, 7, 9, 4], // I V vi iii
  justTheTwo: [5, 4, 9, 0], // IV III vi I (the 丸サ shape)
  fifties: [0, 9, 5, 7], // I vi IV V
};

const mean = (xs) => (xs.length ? xs.reduce((a, b) => a + b, 0) / xs.length : null);
const within = (notes, start, end) =>
  notes.filter((n) => n.startBeat >= start && n.startBeat < end);
const inRuns = (beat, runs) => runs.some((r) => beat >= r.startBeat && beat < r.endBeat);

/** Where things happen, in seconds and bars. */
function layout(reading) {
  const { song, runs } = reading;
  const firstVocal = song.melody[0]?.startBeat ?? null;
  const chorusBeats = runs.reduce((s, r) => s + r.endBeat - r.startBeat, 0);
  return {
    bpm: song.bpm,
    durationSec: secondsAt(song, song.totalBeats),
    bars: song.totalBeats / BEATS_PER_BAR,
    introSec: firstVocal === null ? null : secondsAt(song, firstVocal),
    firstChorusSec: runs.length ? secondsAt(song, runs[0].startBeat) : null,
    chorusCount: runs.length,
    chorusShare: song.totalBeats ? chorusBeats / song.totalBeats : null,
    meanChorusBars: runs.length ? chorusBeats / runs.length / BEATS_PER_BAR : null,
  };
}

/** The chorus against the verse: register, density, loudness. */
function lift(reading) {
  const { song, runs } = reading;
  if (!runs.length || !song.melody.length) {
    return {};
  }
  const firstChorus = runs[0].startBeat;
  const sungChorus = song.melody.filter((n) => inRuns(n.startBeat, runs));
  let verse = song.melody.filter((n) => n.startBeat < firstChorus);
  if (!verse.length) {
    verse = song.melody.filter((n) => !inRuns(n.startBeat, runs));
  }
  const all = song.parts.flatMap((p) => p.notes);
  const beatsIn = (pred) => {
    let beats = 0;
    for (let b = 0; b < song.totalBeats; b += 1) {
      if (pred(b)) {
        beats += 1;
      }
    }
    return beats || 1;
  };
  const chorusBeats = beatsIn((b) => inRuns(b, runs));
  const verseBeats = beatsIn((b) => b < firstChorus || (!inRuns(b, runs) && firstChorus === 0));
  const verseNotes = all.filter((n) => n.startBeat < firstChorus);
  const chorusNotes = all.filter((n) => inRuns(n.startBeat, runs));
  const peak = Math.max(...song.melody.map((n) => n.pitch));
  const pitches = (ns) => ns.map((n) => n.pitch);
  return {
    chorusMeanPitchLift: verse.length ? mean(pitches(sungChorus)) - mean(pitches(verse)) : null,
    chorusPeakLift: verse.length
      ? Math.max(...pitches(sungChorus)) - Math.max(...pitches(verse))
      : null,
    songPeakInChorus: sungChorus.some((n) => n.pitch === peak) ? 1 : 0,
    chorusVocalDensityRatio:
      verse.length && sungChorus.length
        ? sungChorus.length / chorusBeats / (verse.length / verseBeats)
        : null,
    chorusArrangementDensityRatio: verseNotes.length
      ? chorusNotes.length / chorusBeats / (verseNotes.length / verseBeats)
      : null,
    chorusVelocityLift: verseNotes.length
      ? mean(chorusNotes.map((n) => n.velocity)) - mean(verseNotes.map((n) => n.velocity))
      : null,
  };
}

/** The hook: how often it returns, where, and what it is made of. */
function hook(reading) {
  const { song, runs } = reading;
  const h = reading.hook;
  if (!h) {
    return { hookStatements: 0 };
  }
  const inChorus = h.statements.filter((s) => inRuns(s, runs)).length;
  const pitches = h.notes.map((n) => n.pitch);
  const intervals = pitches.slice(1).map((p, i) => Math.abs(p - pitches[i]));
  const offBeat = h.notes.filter((n) => Math.abs(n.startBeat % 1) > 1e-6).length;
  const chorusNotes = song.melody.filter((n) => inRuns(n.startBeat, runs));
  const chorusPeak = chorusNotes.length ? Math.max(...chorusNotes.map((n) => n.pitch)) : null;
  const opensChorus = runs.filter((r) =>
    h.statements.some((s) => s >= r.startBeat && s < r.startBeat + 2 * BEATS_PER_BAR),
  ).length;
  const verseNotes = song.melody.filter((n) => n.startBeat < (runs[0]?.startBeat ?? 0));
  return {
    hookStatements: h.statements.length,
    hookStatementsPerChorus: runs.length ? inChorus / runs.length : null,
    hookChorusExclusivity: h.statements.length ? inChorus / h.statements.length : null,
    hookOpensChorusRatio: runs.length ? opensChorus / runs.length : null,
    hookNoteCount: h.notes.length,
    hookRange: Math.max(...pitches) - Math.min(...pitches),
    hookMaxLeap: intervals.length ? Math.max(...intervals) : 0,
    hookRepeatedNoteRatio: intervals.length
      ? intervals.filter((i) => i === 0).length / intervals.length
      : null,
    hookOffBeatRatio: offBeat / h.notes.length,
    hookReachesChorusPeak: chorusPeak !== null && Math.max(...pitches) >= chorusPeak - 1 ? 1 : 0,
    hookVerseLikeness:
      verseNotes.length >= 3
        ? melodicSimilarity(h.notes, verseNotes.slice(0, h.notes.length))
        : null,
  };
}

/** Repetition across choruses: the same chorus, or a new tune each time? */
function chorusConsistency(reading) {
  const { song, runs } = reading;
  if (runs.length < 2) {
    return { chorusRestatementLikeness: null };
  }
  const span = Math.min(...runs.map((r) => r.endBeat - r.startBeat));
  const first = within(song.melody, runs[0].startBeat, runs[0].startBeat + span);
  const likeness = runs
    .slice(1)
    .map((r) => within(song.melody, r.startBeat, r.startBeat + span))
    .filter((notes) => notes.length >= 3 && first.length >= 3)
    .map((notes) => melodicSimilarity(first, notes));
  return { chorusRestatementLikeness: mean(likeness) };
}

/** Which named progressions occur, and how varied the harmony is. */
function harmonyIdioms(reading) {
  const { harmony, runs, song } = reading;
  const sequence = [];
  for (const c of harmony.chords) {
    if (sequence[sequence.length - 1]?.degree !== c.degree) {
      sequence.push(c);
    }
  }
  const found = {};
  for (const [name, pattern] of Object.entries(POP_PROGRESSIONS)) {
    let count = 0;
    for (let i = 0; i + 4 <= sequence.length; i += 1) {
      if (pattern.every((d, j) => sequence[i + j].degree === d)) {
        count += 1;
      }
    }
    if (count) {
      found[name] = count;
    }
  }
  const chorusChords = harmony.chords.filter((c) => inRuns(c.startBeat, runs));
  const verseChords = harmony.chords.filter((c) => c.startBeat < (runs[0]?.startBeat ?? 0));
  const loop = (chords) => chords.map((c) => c.degree).join(',');
  const distinctRomans = new Set(harmony.chords.map((c) => c.roman)).size;
  const nonDiatonic = harmony.chords.filter((c) => ![0, 2, 4, 5, 7, 9, 11].includes(c.degree));
  const bars = song.totalBeats / BEATS_PER_BAR;
  return {
    progressions: found,
    chordChangesPerBar: bars ? sequence.length / bars : null,
    distinctChordCount: distinctRomans,
    nonDiatonicChordRatio: harmony.chords.length
      ? nonDiatonic.length / harmony.chords.length
      : null,
    verseChorusHarmonyDiffers:
      verseChords.length && chorusChords.length
        ? Number(loop(verseChords) !== loop(chorusChords))
        : null,
  };
}

/** The value below which a share `q` of the values fall. */
function percentile(values, q) {
  const sorted = [...values].sort((a, b) => a - b);
  return sorted[Math.min(sorted.length - 1, Math.floor(q * sorted.length))];
}

/**
 * The melody's rhythm and line, through libcantus's rhythm reading. The range
 * leaves out the outer 1% of notes so a stray marker or one outlying note does
 * not stand for the singer's compass.
 */
function melodyShape(reading) {
  const { song } = reading;
  if (song.melody.length < 2) {
    return {};
  }
  const rhythm = analyzeRhythm(song.melody, { totalBeats: song.totalBeats });
  const pitches = song.melody.map((n) => n.pitch);
  const steps = pitches.slice(1).map((p, i) => Math.abs(p - pitches[i]));
  return {
    vocalRange: percentile(pitches, 0.99) - percentile(pitches, 0.01),
    vocalOnsetsPerBar: rhythm.onsetDensity,
    vocalSyncopation: rhythm.syncopation,
    vocalStepRatio: steps.filter((s) => s > 0 && s <= 2).length / steps.length,
    vocalLeapRatio: steps.filter((s) => s >= 5).length / steps.length,
    vocalRepeatRatio: steps.filter((s) => s === 0).length / steps.length,
  };
}

/** Metrics the sung line decides on its own, with no accompaniment needed. */
export const MELODY_ONLY_METRICS = new Set([
  'vocalRange',
  'vocalOnsetsPerBar',
  'vocalSyncopation',
  'vocalStepRatio',
  'vocalLeapRatio',
  'vocalRepeatRatio',
  'vocalRapidShare',
  'vocalRapidRepeatRunsPerMin',
  'vocalLongestRapidRepeat',
]);

/** Onsets this close or closer are a rapid-fire syllable. */
const RAPID_SECONDS = 0.19;
/** A same-pitch rapid-fire run needs this many notes to count as the recitation figure. */
const RAPID_RUN_NOTES = 4;

/**
 * Vocal drive: how much of the line is rapid-fire, and how often it recites
 * one pitch in a rapid run — the figure fast vocaloid and idol lines open
 * phrases with. Read in seconds through the tempo map, since what makes a line
 * fast is the singer's syllable rate, not the notated value.
 */
function vocalDrive(reading) {
  const { song } = reading;
  const m = song.melody;
  if (m.length < 2) {
    return {};
  }
  const t = m.map((n) => secondsAt(song, n.startBeat));
  const gaps = t.slice(1).map((x, i) => x - t[i]);
  let runs = 0;
  let longest = 1;
  let length = 1;
  for (let i = 1; i <= m.length; i += 1) {
    const continues =
      i < m.length &&
      m[i].pitch === m[i - 1].pitch &&
      gaps[i - 1] > 0 &&
      gaps[i - 1] <= RAPID_SECONDS;
    if (continues) {
      length += 1;
      continue;
    }
    if (length >= RAPID_RUN_NOTES) {
      runs += 1;
    }
    longest = Math.max(longest, length);
    length = 1;
  }
  const minutes = (t[t.length - 1] - t[0]) / 60 || 1;
  return {
    vocalRapidShare: gaps.filter((g) => g > 0 && g <= RAPID_SECONDS).length / gaps.length,
    vocalRapidRepeatRunsPerMin: runs / minutes,
    vocalLongestRapidRepeat: longest,
  };
}

/** All pop-craft metrics for one read song. */
export function popOf(reading) {
  const agreement = reading.agreement;
  return {
    ...layout(reading),
    ...lift(reading),
    ...hook(reading),
    ...chorusConsistency(reading),
    ...harmonyIdioms(reading),
    ...melodyShape(reading),
    ...vocalDrive(reading),
    chorusRecognizedRecall: agreement?.recall ?? null,
  };
}
