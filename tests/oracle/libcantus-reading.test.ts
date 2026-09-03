/**
 * Oracle tier: the generated music read back by an independent theory engine.
 *
 * `@libraz/libcantus` never sees midi-sketch's intent — no config, no
 * blueprint, no chord table — only the notes that were written. What it reports
 * is therefore a fact about the output rather than the generator agreeing with
 * itself, which is the whole reason these assertions are worth more than the
 * ones the generator could make about its own state.
 */

import { compareMelodies, detectChordBest } from '@libraz/libcantus';
import { beforeAll, describe, expect, it } from 'vitest';
import {
  type GenerationCase,
  generate,
  loadMatrix,
  type OracleNote,
  type OracleSong,
  pitchClass,
} from './helpers.js';

interface Generated {
  testCase: GenerationCase;
  song: OracleSong;
}

/** Scale degrees, as pitch classes of the internal C major space. */
const DEGREE_PITCH_CLASS = [0, 2, 4, 5, 7, 9, 11];

/** Blueprints whose riff policy is Locked: the riff must be the same riff. */
const LOCKED_RIFF_BLUEPRINTS = new Set([1, 5, 6, 7, 8, 9]);

const HARMONY_TRACKS = ['Chord', 'Bass', 'Guitar', 'Aux'];
const EPS = 1e-6;

const matrix = loadMatrix();
let corpus: Generated[] = [];

beforeAll(() => {
  corpus = matrix.map((testCase) => ({ testCase, song: generate(testCase) }));
}, 600_000);

const describeCase = (testCase: GenerationCase): string =>
  `bp${testCase.blueprint}/seed${testCase.seed}/${testCase.mood}/${testCase.chord}/${testCase.extensions}`;

const overlaps = (note: OracleNote, from: number, to: number): boolean =>
  note.startBeat < to - EPS && note.startBeat + note.durationBeat > from + EPS;

describe('the harmony that sounds', () => {
  it('sounds the root of the chord the generator says it wrote', () => {
    let spans = 0;
    let rootless = 0;
    const examples: string[] = [];
    for (const { testCase, song } of corpus) {
      const pool = HARMONY_TRACKS.flatMap((name) => song.track(name)?.notes ?? []);
      for (const chord of song.chords) {
        const root = DEGREE_PITCH_CLASS[chord.degree];
        if (root === undefined) continue;
        const sounding = pool.filter((n) => overlaps(n, chord.startBeat, chord.endBeat));
        if (sounding.length === 0) continue;
        spans += 1;
        if (!sounding.some((n) => pitchClass(n.pitch) === root)) {
          rootless += 1;
          if (examples.length < 5) {
            examples.push(`${describeCase(testCase)} bar ${(chord.startBeat / 4 + 1).toFixed(0)}`);
          }
        }
      }
    }
    expect(spans).toBeGreaterThan(1000);
    expect(
      rootless / spans,
      `${rootless}/${spans} chord spans never sound their own root. Examples: ${examples.join(', ')}`,
    ).toBeLessThan(0.05);
  });

  it('is read as a chord at all: an independent detector names one for nearly every span', () => {
    let spans = 0;
    let unnameable = 0;
    for (const { song } of corpus) {
      const pool = HARMONY_TRACKS.flatMap((name) => song.track(name)?.notes ?? []);
      const bass = song.track('Bass')?.notes ?? [];
      for (const chord of song.chords) {
        const sounding = pool.filter((n) => overlaps(n, chord.startBeat, chord.endBeat));
        if (sounding.length < 2) continue;
        spans += 1;
        const bassHere = bass.filter((n) => overlaps(n, chord.startBeat, chord.endBeat));
        const detected = detectChordBest(
          sounding.map((n) => n.pitch),
          bassHere.length > 0 ? { bassPc: pitchClass(bassHere[0].pitch) } : {},
        );
        if (detected === null) unnameable += 1;
      }
    }
    expect(spans).toBeGreaterThan(1000);
    expect(unnameable / spans, `${unnameable}/${spans} spans name no chord`).toBeLessThan(0.02);
  });
});

describe('locked riffs', () => {
  /**
   * Rhythm similarity between the first chorus's riff and every later one,
   * grouped by blueprint.
   */
  function riffRhythmSimilarityByBlueprint(): Map<number, number[]> {
    const byBlueprint = new Map<number, number[]>();
    for (const { testCase, song } of corpus) {
      const motif = song.track('Motif')?.notes ?? [];
      const choruses = song.sections.filter((s) => s.type === 'Chorus');
      if (choruses.length < 2) continue;

      const heads = choruses.map((section) =>
        motif
          .filter(
            (n) => n.startBeat >= section.startBeat - EPS && n.startBeat < section.startBeat + 16,
          )
          .map((n) => ({ ...n, startBeat: n.startBeat - section.startBeat })),
      );
      if (heads[0].length < 3) continue;

      for (let i = 1; i < heads.length; i += 1) {
        if (heads[i].length < 3) continue;
        const { rhythmSimilarity } = compareMelodies(heads[0], heads[i]);
        const bucket = byBlueprint.get(testCase.blueprint);
        if (bucket) {
          bucket.push(rhythmSimilarity);
        } else {
          byBlueprint.set(testCase.blueprint, [rhythmSimilarity]);
        }
      }
    }
    return byBlueprint;
  }

  const mean = (xs: number[]): number => xs.reduce((a, b) => a + b, 0) / xs.length;

  it('replays the riff rhythm in every chorus of a Locked-policy blueprint', () => {
    // What "Locked" promises is the figure, not the pitches: RiffPolicy::Locked
    // is LockedContour, which permits the contour to be re-voiced against the
    // harmony, while LockedPitch (BehavioralLoop) replays verbatim. The rhythm
    // is what both keep, and it is what a listener hears as "the same riff".
    const byBlueprint = riffRhythmSimilarityByBlueprint();
    const failures: string[] = [];
    for (const blueprint of LOCKED_RIFF_BLUEPRINTS) {
      const samples = byBlueprint.get(blueprint);
      if (!samples || samples.length === 0) continue;
      const average = mean(samples);
      if (average < 0.85) {
        failures.push(
          `blueprint ${blueprint}: mean riff-rhythm similarity ${average.toFixed(3)} over ${samples.length} chorus pairs`,
        );
      }
    }
    expect(byBlueprint.size).toBeGreaterThan(0);
    expect(failures, `a Locked riff is not replayed:\n${failures.join('\n')}`).toEqual([]);
  });

  it('lets an Evolving-policy riff actually evolve', () => {
    // The counterpart assertion, and what makes the one above mean something:
    // if every blueprint scored alike, "locked" would not be a property the
    // measurement can see. StoryPop and IdolStandard carry RiffPolicy::Evolving.
    const byBlueprint = riffRhythmSimilarityByBlueprint();
    for (const blueprint of [2, 4]) {
      const samples = byBlueprint.get(blueprint);
      if (!samples || samples.length < 4) continue;
      expect(
        mean(samples),
        `blueprint ${blueprint} declares an Evolving riff but replays it as rigidly as a Locked one`,
      ).toBeLessThan(0.85);
    }
  });
});
