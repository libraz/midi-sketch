/**
 * A candidate against the song it is aiming at: how close it is built, and
 * whether it is close in the wrong way.
 *
 * Closeness is read on structure — form, harmonic function and rhythm, motif
 * derivation, and the pop layout — never on the notes themselves. The one
 * surface reading, how much the candidate's hook resembles any passage of the
 * target, is the copy check: a new song may share the target's architecture
 * but not its tune.
 */

import { compareReferences, melodicSimilarity } from '@libraz/libcantus';
import { BEATS_PER_BAR, BUDGET } from './analysis.mjs';

/**
 * A hook this alike to some passage of the target is reported as a likely copy.
 * Between unrelated reference songs the same reading has median 0.65 and never exceeds 0.84.
 */
export const COPY_LIKENESS = 0.9;

/**
 * Pop-layout metrics compared for fit, each with the difference at which two
 * songs count as entirely unlike on it.
 */
const FIT_SCALES = {
  bpm: 30,
  durationSec: 90,
  introSec: 20,
  firstChorusSec: 40,
  chorusCount: 3,
  chorusShare: 0.3,
  chorusMeanPitchLift: 6,
  chorusArrangementDensityRatio: 1,
  hookStatementsPerChorus: 3,
  vocalOnsetsPerBar: 4,
  vocalRange: 10,
  chordChangesPerBar: 1.5,
};

/** Closeness of two pop metric sets, one value in [0, 1] per comparable metric. */
function layoutFit(candidate, target) {
  const fit = {};
  for (const [name, scale] of Object.entries(FIT_SCALES)) {
    const c = candidate[name];
    const t = target[name];
    if (typeof c === 'number' && typeof t === 'number') {
      fit[name] = Math.max(0, 1 - Math.abs(c - t) / scale);
    }
  }
  return fit;
}

/** The likeness of the candidate's hook to the closest passage of the target melody. */
function hookCopyLikeness(candidateReading, targetReading) {
  const hook = candidateReading.hook;
  const melody = targetReading.song.melody;
  if (!hook || melody.length < 3) {
    return null;
  }
  const span =
    hook.notes[hook.notes.length - 1].startBeat - hook.notes[0].startBeat + BEATS_PER_BAR;
  let best = 0;
  for (let s = 0; s + span <= targetReading.song.totalBeats; s += 1) {
    const window = melody.filter((n) => n.startBeat >= s && n.startBeat < s + span);
    if (window.length >= 3) {
      best = Math.max(best, melodicSimilarity(hook.notes, window));
    }
  }
  return best;
}

const meanOf = (values) => {
  const xs = values.filter((v) => typeof v === 'number');
  return xs.length ? xs.reduce((a, b) => a + b, 0) / xs.length : null;
};

/**
 * Compare a candidate with its target.
 *
 * @returns {{ structure: object, layout: Record<string, number>, structureFit: number | null,
 *   layoutFit: number | null, hookCopyLikeness: number | null, surfaceSimilarity: number | null,
 *   copyRisk: boolean }}
 */
export function targetFit(candidateReading, candidatePop, targetReading, targetPop) {
  const structure = compareReferences(candidateReading.profile, targetReading.profile, {
    budget: BUDGET,
  });
  const layout = layoutFit(candidatePop, targetPop);
  const structural = [
    ...Object.values(structure.form),
    ...Object.values(structure.harmony),
    structure.melody.motifStructureSimilarity,
    structure.melody.contourSimilarity,
    structure.melody.registerSimilarity,
    ...Object.values(structure.rhythm),
  ];
  const copy = hookCopyLikeness(candidateReading, targetReading);
  return {
    structure,
    layout,
    structureFit: meanOf(structural),
    layoutFit: meanOf(Object.values(layout)),
    hookCopyLikeness: copy,
    surfaceSimilarity: structure.melody.surfaceSimilarity,
    copyRisk: copy !== null && copy >= COPY_LIKENESS,
  };
}
