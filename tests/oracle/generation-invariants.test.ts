/**
 * Oracle tier: properties of the music the generator actually emits.
 *
 * Every assertion here is stated as a property of the output and checked by
 * reading the notes back, so it can only pass by the music having the property.
 * The generation matrix is pairwise-complete over blueprint / seed / mood /
 * chord preset / extension setting (see `fixtures/matrix.json`); combinations
 * are not chosen by eye.
 *
 * The rate bounds sit between what the code produced before the voicing and
 * interval work and what it produces now, with headroom on both sides: they are
 * regression fences, not targets. Tightening one is a deliberate decision that
 * belongs in the same change as whatever improved the number.
 */

import { beforeAll, describe, expect, it } from 'vitest';
import {
  declaredLeapCeiling,
  distinctPitchClasses,
  type GenerationCase,
  generate,
  loadMatrix,
  type OracleSong,
  sectionAt,
  soundsAThird,
} from './helpers.js';

interface Generated {
  testCase: GenerationCase;
  song: OracleSong;
}

const matrix = loadMatrix();
let corpus: Generated[] = [];

beforeAll(() => {
  corpus = matrix.map((testCase) => ({ testCase, song: generate(testCase) }));
}, 600_000);

const describeCase = (testCase: GenerationCase): string =>
  `bp${testCase.blueprint}/seed${testCase.seed}/${testCase.mood}/${testCase.chord}/${testCase.extensions}`;

describe('chord voicing', () => {
  it('keeps a chord a chord: a stack never collapses onto one pitch class more than rarely', () => {
    let stacks = 0;
    let collapsed = 0;
    const worst: string[] = [];
    for (const { testCase, song } of corpus) {
      for (const stack of song.track('Chord')?.stacks ?? []) {
        stacks += 1;
        if (distinctPitchClasses(stack).length === 1) {
          collapsed += 1;
          if (worst.length < 5) worst.push(`${describeCase(testCase)} @beat ${stack[0].startBeat}`);
        }
      }
    }
    expect(stacks).toBeGreaterThan(1000);
    const rate = collapsed / stacks;
    expect(
      rate,
      `${collapsed}/${stacks} chord stacks sound a single pitch class. Examples: ${worst.join(', ')}`,
    ).toBeLessThan(0.025);
  });

  it('sounds a third in most voicings, so chords keep a major/minor identity', () => {
    let stacks = 0;
    let thirdless = 0;
    for (const { song } of corpus) {
      for (const stack of song.track('Chord')?.stacks ?? []) {
        stacks += 1;
        if (!soundsAThird(distinctPitchClasses(stack))) thirdless += 1;
      }
    }
    expect(
      thirdless / stacks,
      `${thirdless}/${stacks} chord voicings sound no third at all (root-and-fifth stacks have no major or minor identity)`,
    ).toBeLessThan(0.15);
  });

  it('spends its voices on distinct tones rather than octave doublings', () => {
    let stacks = 0;
    let complete = 0;
    let doubled = 0;
    for (const { song } of corpus) {
      for (const stack of song.track('Chord')?.stacks ?? []) {
        stacks += 1;
        const pcs = distinctPitchClasses(stack);
        if (pcs.length >= 3) complete += 1;
        if (stack.length > pcs.length) doubled += 1;
      }
    }
    expect(complete / stacks, `only ${complete}/${stacks} voicings sound three or more distinct tones`).toBeGreaterThan(0.75);
    expect(doubled / stacks, `${doubled}/${stacks} voicings spend a voice doubling a tone already sounding`).toBeLessThan(0.25);
  });
});

describe('vocal line', () => {
  it('never leaps further than the section it is in allows', () => {
    const violations: string[] = [];
    let checked = 0;
    for (const { testCase, song } of corpus) {
      const notes = song.track('Vocal')?.notes ?? [];
      for (let i = 1; i < notes.length; i += 1) {
        const section = sectionAt(song, notes[i].startBeat);
        if (!section) continue;
        checked += 1;
        const ceiling = declaredLeapCeiling(section.type);
        const interval = Math.abs(notes[i].pitch - notes[i - 1].pitch);
        if (interval > ceiling) {
          violations.push(
            `${describeCase(testCase)} bar ${(notes[i].startBeat / 4 + 1).toFixed(2)}: ` +
              `${interval} semitones in ${section.type} (ceiling ${ceiling})`,
          );
        }
      }
    }
    expect(checked).toBeGreaterThan(1000);
    expect(violations, `interval ceiling exceeded:\n${violations.slice(0, 10).join('\n')}`).toEqual([]);
  });

  it('stays inside the pitch range the generator was given', () => {
    // The default range is 60..79; a documented climax lift may reach a little
    // above it, so the assertion is that nothing escapes the range wholesale.
    for (const { testCase, song } of corpus) {
      const notes = song.track('Vocal')?.notes ?? [];
      if (notes.length === 0) continue;
      const low = Math.min(...notes.map((n) => n.pitch));
      const high = Math.max(...notes.map((n) => n.pitch));
      expect(low, `${describeCase(testCase)} sings below the range floor`).toBeGreaterThanOrEqual(55);
      expect(high, `${describeCase(testCase)} sings above the range ceiling plus its climax lift`).toBeLessThanOrEqual(83);
    }
  });
});

describe('blueprint identity', () => {
  it('gives every blueprint whose identity is a riff an actual riff', () => {
    // BehavioralLoop is documented in the CLI help as "fixed riff, maximum
    // hook"; RhythmLock, IdolHyper and IdolCoolPop are RhythmSync blueprints
    // whose coordinate axis IS the motif. A blueprint that names a riff and
    // emits no motif track has no identity left to hear.
    const riffBlueprints = new Set([1, 5, 7, 9]);
    for (const { testCase, song } of corpus) {
      if (!riffBlueprints.has(testCase.blueprint)) continue;
      const motif = song.track('Motif');
      expect(
        motif?.notes.length ?? 0,
        `${describeCase(testCase)} declares a riff but emitted no motif notes`,
      ).toBeGreaterThan(0);
    }
  });

  it('emits every song with a vocal, a bass and a chord bed', () => {
    for (const { testCase, song } of corpus) {
      for (const name of ['Vocal', 'Bass', 'Chord']) {
        expect(
          song.track(name)?.notes.length ?? 0,
          `${describeCase(testCase)} emitted no ${name} notes`,
        ).toBeGreaterThan(0);
      }
    }
  });
});
