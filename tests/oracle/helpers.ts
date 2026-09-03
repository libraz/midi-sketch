/**
 * Shared plumbing for the oracle tier.
 *
 * These tests read the generator's own output back with an independent
 * music-theory engine (`@libraz/libcantus`), which never sees midi-sketch's
 * intent and only ever looks at the emitted notes. That is what makes them an
 * oracle rather than a restatement: an assertion here can only be satisfied by
 * the music actually having the property, not by the generator agreeing with
 * itself.
 */

import { execFileSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = fileURLToPath(new URL('../..', import.meta.url));

/**
 * The native CLI. The oracle tier drives the shipped binary, not the WASM build.
 *
 * `MIDISKETCH_CLI` overrides it so the same assertions can be pointed at another
 * build — which is how you check that a fence here would actually have caught
 * the defect it was written for, rather than passing because it is loose.
 */
export const CLI = process.env.MIDISKETCH_CLI ?? join(repoRoot, 'build', 'bin', 'midisketch_cli');

/** One row of the pairwise generation matrix. */
export interface GenerationCase {
  blueprint: number;
  seed: number;
  mood: string;
  chord: string;
  extensions: 'none' | 'ninth' | 'sus' | 'both';
}

/** A note as libcantus wants it: pitch plus beat-domain timing. */
export interface OracleNote {
  pitch: number;
  startBeat: number;
  durationBeat: number;
  velocity: number;
}

export interface OracleTrack {
  name: string;
  role: 'melody' | 'harmony' | 'bass' | 'drums' | 'other';
  notes: OracleNote[];
  /** Onsets that carry more than one note, i.e. chord stacks. */
  stacks: OracleNote[][];
}

export interface OracleSection {
  type: string;
  startBeat: number;
  endBeat: number;
}

export interface OracleSong {
  bpm: number;
  tracks: OracleTrack[];
  sections: OracleSection[];
  /** The progression the generator says it wrote, as scale degrees. */
  chords: { startBeat: number; endBeat: number; degree: number }[];
  track(name: string): OracleTrack | undefined;
}

/** midi-sketch track names to the roles libcantus reasons in. */
const ROLE: Record<string, OracleTrack['role']> = {
  Vocal: 'melody',
  Chord: 'harmony',
  Bass: 'bass',
  Motif: 'other',
  Aux: 'harmony',
  Guitar: 'harmony',
  Drums: 'drums',
  SE: 'other',
};

function extensionFlags(extensions: GenerationCase['extensions']): string[] {
  switch (extensions) {
    case 'ninth':
      return ['--enable-9th'];
    case 'sus':
      return ['--enable-sus'];
    case 'both':
      return ['--enable-9th', '--enable-sus'];
    default:
      return [];
  }
}

/**
 * Generate one song and read it back.
 *
 * The CLI writes `output.json` into its working directory and takes no flag for
 * it, so each case gets its own temporary directory.
 */
export function generate(testCase: GenerationCase): OracleSong {
  const dir = mkdtempSync(join(tmpdir(), 'midisketch-oracle-'));
  try {
    const args = [
      '--seed',
      String(testCase.seed),
      '--blueprint',
      String(testCase.blueprint),
      '--mood',
      testCase.mood,
      ...(testCase.chord === 'auto' ? [] : ['--chord', testCase.chord]),
      ...extensionFlags(testCase.extensions),
    ];
    execFileSync(CLI, args, { cwd: dir, stdio: 'pipe' });
    return loadSong(join(dir, 'output.json'));
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
}

/** Parse a generated `output.json` into the beat domain libcantus works in. */
export function loadSong(path: string): OracleSong {
  const raw = JSON.parse(readFileSync(path, 'utf8'));
  const ppq: number = raw.division || 480;

  const tracks: OracleTrack[] = (raw.tracks ?? []).map((track: Record<string, unknown>) => {
    const notes: OracleNote[] = ((track.notes as Record<string, number>[]) ?? [])
      .map((note) => ({
        pitch: note.pitch,
        startBeat: note.start_ticks / ppq,
        durationBeat: note.duration_ticks / ppq,
        velocity: note.velocity,
      }))
      .sort((a, b) => a.startBeat - b.startBeat || a.pitch - b.pitch);

    const byOnset = new Map<number, OracleNote[]>();
    for (const note of notes) {
      const bucket = byOnset.get(note.startBeat);
      if (bucket) {
        bucket.push(note);
      } else {
        byOnset.set(note.startBeat, [note]);
      }
    }

    return {
      name: track.name as string,
      role: ROLE[track.name as string] ?? 'other',
      notes,
      stacks: [...byOnset.values()].filter((group) => group.length > 1),
    };
  });

  const song: OracleSong = {
    bpm: raw.bpm,
    tracks,
    sections: (raw.sections ?? []).map((section: Record<string, number | string>) => ({
      type: section.type as string,
      startBeat: (section.start_ticks as number) / ppq,
      endBeat: (section.end_ticks as number) / ppq,
    })),
    chords: (raw.chords ?? []).map((chord: Record<string, number>) => ({
      startBeat: chord.tick / ppq,
      endBeat: chord.endTick / ppq,
      degree: chord.degree,
    })),
    track: (name: string) => tracks.find((t) => t.name === name),
  };
  return song;
}

/** Pitch class of a MIDI pitch. */
export const pitchClass = (pitch: number): number => ((pitch % 12) + 12) % 12;

/** The distinct pitch classes a group of simultaneous notes sounds. */
export const distinctPitchClasses = (notes: OracleNote[]): number[] => [
  ...new Set(notes.map((n) => pitchClass(n.pitch))),
];

/**
 * Whether a set of simultaneous pitch classes sounds any third.
 *
 * A third between some pair is what gives a chord its major or minor identity;
 * a stack of roots and fifths has none, however many notes it contains.
 */
export function soundsAThird(pcs: number[]): boolean {
  for (let i = 0; i < pcs.length; i += 1) {
    for (let j = i + 1; j < pcs.length; j += 1) {
      const interval = (pcs[j] - pcs[i] + 12) % 12;
      if (interval === 3 || interval === 4 || interval === 8 || interval === 9) {
        return true;
      }
    }
  }
  return false;
}

/** The section covering a beat, or undefined outside every section. */
export function sectionAt(song: OracleSong, beat: number): OracleSection | undefined {
  return song.sections.find((s) => beat >= s.startBeat && beat < s.endBeat);
}

/**
 * The melodic-interval ceiling the C++ core declares for a section type.
 *
 * Mirrors `getMaxMelodicIntervalForSection` in `src/core/pitch_utils.h`. It is
 * duplicated here on purpose: the point of an oracle is to state the expected
 * property independently, so a change to the C++ table has to be a deliberate
 * change here too rather than silently redefining what the test checks.
 */
export function declaredLeapCeiling(sectionType: string): number {
  switch (sectionType) {
    case 'Chorus':
    case 'MixBreak':
    case 'Drop':
      return 12;
    case 'Bridge':
      return 14;
    case 'B':
      return 10;
    default:
      return 9;
  }
}

/** Load the pairwise generation matrix. */
export function loadMatrix(): GenerationCase[] {
  const path = fileURLToPath(new URL('./fixtures/matrix.json', import.meta.url));
  return JSON.parse(readFileSync(path, 'utf8')).cases as GenerationCase[];
}
