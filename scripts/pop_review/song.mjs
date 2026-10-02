/**
 * Loads a generated song (`output.json`) or a reference MIDI file into one
 * beat-domain shape, so every measurement downstream treats both sides alike.
 *
 * Parts are reduced to four roles: `melody` (the sung line), `bass`,
 * `harmony` (what carries the chords) and `drums`; anything else is kept only
 * for density. A reference file is read through its `track_roles.json` labels
 * rather than guessed at, and its vocal is skyline-filtered because several
 * references carry harmony dyads on the vocal track.
 */

import { existsSync, readFileSync } from 'node:fs';
import { basename, dirname, join } from 'node:path';
import { readSmf } from './smf.mjs';

/**
 * @typedef {{ pitch: number, startBeat: number, durationBeat: number, velocity: number }} Note
 * @typedef {'melody' | 'bass' | 'harmony' | 'drums' | 'other'} Role
 * @typedef {{ name: string, role: Role, notes: Note[] }} Part
 * @typedef {{ type: string, startBeat: number, endBeat: number }} DeclaredSection
 * @typedef {{
 *   source: 'generated' | 'reference',
 *   label: string,
 *   bpm: number,
 *   tempos: { beat: number, bpm: number }[],
 *   totalBeats: number,
 *   parts: Part[],
 *   melody: Note[],
 *   bass: Note[],
 *   harmony: Note[],
 *   drums: Note[],
 *   sections: DeclaredSection[] | null,
 *   blueprint: number | null,
 *   declared: Declared | null,
 * }} Song
 * @typedef {{ startBeat: number, endBeat: number, rootPc: number }} DeclaredChord
 * @typedef {{
 *   keyPc: number,
 *   modulationBeat: number | null,
 *   modulationSemitones: number,
 *   chords: DeclaredChord[],
 *   vocalLow: number,
 *   vocalHigh: number,
 * }} Declared What the generator says it wrote; only a generated song has one.
 */

/**
 * Semitones above the tonic for a generator chord degree: 0-6 diatonic, 8 and
 * 10-14 borrowed. Mirrors `degreeToSemitone` in `src/core/chord.cpp`.
 */
const DEGREE_SEMITONES = {
  0: 0,
  1: 2,
  2: 4,
  3: 5,
  4: 7,
  5: 9,
  6: 11,
  8: 8,
  10: 10,
  11: 3,
  12: 5,
  13: 1,
  14: 6,
};

const BEATS_PER_BAR = 4;

/** midi-sketch track names to review roles. */
const GENERATED_ROLE = {
  Vocal: 'melody',
  Bass: 'bass',
  Chord: 'harmony',
  Guitar: 'harmony',
  Arpeggio: 'harmony',
  Drums: 'drums',
};

/** `track_roles.json` role vocabulary to review roles. */
const REFERENCE_ROLE = {
  vocal: 'melody',
  bass: 'bass',
  chord_comping: 'harmony',
  keys: 'harmony',
  pad: 'harmony',
  strings: 'harmony',
  riff: 'harmony',
  arpeggio: 'harmony',
  drums: 'drums',
};

const byTime = (a, b) => a.startBeat - b.startBeat || a.pitch - b.pitch;

/**
 * Seconds from the start of the song to a beat, through the tempo map.
 *
 * @param {Song} song
 * @param {number} beat
 */
export function secondsAt(song, beat) {
  let seconds = 0;
  let from = 0;
  let bpm = song.tempos[0]?.bpm ?? song.bpm;
  for (const change of song.tempos) {
    if (change.beat >= beat) {
      break;
    }
    seconds += ((change.beat - from) * 60) / bpm;
    from = change.beat;
    bpm = change.bpm;
  }
  return seconds + ((beat - from) * 60) / bpm;
}

/** Keep the highest note at each onset. */
export function skyline(notes) {
  const best = new Map();
  for (const note of notes) {
    const current = best.get(note.startBeat);
    if (!current || note.pitch > current.pitch) {
      best.set(note.startBeat, note);
    }
  }
  return [...best.values()].sort(byTime);
}

function assemble(base, parts) {
  const of = (role) =>
    parts
      .filter((p) => p.role === role)
      .flatMap((p) => p.notes)
      .sort(byTime);
  const totalBeats = Math.max(
    0,
    ...parts.flatMap((p) => p.notes.map((n) => n.startBeat + n.durationBeat)),
  );
  const span = base.totalBeats ?? totalBeats;
  return {
    ...base,
    totalBeats: span,
    sections: base.sections?.map((s) => ({ ...s, endBeat: Math.min(s.endBeat, span) })) ?? null,
    parts,
    melody: of('melody'),
    bass: of('bass'),
    harmony: of('harmony'),
    drums: of('drums'),
  };
}

/**
 * Load a generator `output.json`.
 *
 * @param {string} path
 * @returns {Song}
 */
export function loadGenerated(path) {
  const raw = JSON.parse(readFileSync(path, 'utf8'));
  const ppq = raw.division || 480;
  const parts = (raw.tracks ?? []).map((track) => ({
    name: track.name,
    role: GENERATED_ROLE[track.name] ?? 'other',
    notes: (track.notes ?? [])
      .map((n) => ({
        pitch: n.pitch,
        startBeat: n.start_ticks / ppq,
        durationBeat: n.duration_ticks / ppq,
        velocity: n.velocity,
      }))
      .sort(byTime),
  }));
  const meta = raw.metadata ?? {};
  const keyPc = meta.key ?? 0;
  const modulationBeat = meta.modulation_semitones ? meta.modulation_tick / ppq : null;
  const modulationSemitones = meta.modulation_semitones ?? 0;
  const shiftAt = (beat) =>
    modulationBeat !== null && beat >= modulationBeat ? modulationSemitones : 0;
  const declared = {
    keyPc,
    modulationBeat,
    modulationSemitones,
    chords: (raw.chords ?? []).map((c) => {
      const startBeat = c.tick / ppq;
      return {
        startBeat,
        endBeat: c.endTick / ppq,
        rootPc: ((((DEGREE_SEMITONES[c.degree] ?? 0) + keyPc + shiftAt(startBeat)) % 12) + 12) % 12,
      };
    }),
    vocalLow: meta.vocal_low ?? 60,
    vocalHigh: meta.vocal_high ?? 79,
  };
  return assemble(
    {
      source: 'generated',
      label: `bp${meta.blueprint ?? '?'} seed${meta.seed ?? '?'}`,
      bpm: raw.bpm,
      tempos: [
        { beat: 0, bpm: raw.bpm },
        ...(raw.tempo_map ?? []).map((t) => ({ beat: t.tick / ppq, bpm: t.bpm })),
      ],
      totalBeats: raw.duration_ticks ? raw.duration_ticks / ppq : undefined,
      sections: (raw.sections ?? []).map((s) => ({
        type: s.type,
        startBeat: s.start_ticks / ppq,
        endBeat: s.end_ticks / ppq,
      })),
      blueprint: meta.blueprint ?? null,
      declared,
    },
    parts,
  );
}

/**
 * Load a reference MIDI file through its role labels.
 *
 * An optional `_sections` entry in the file's labels states its form by hand,
 * as `[{ "type": "Chorus", "bar": 24 }, ...]` in time order (bars from 0, the
 * generator's section type names); each section runs to the next one's bar.
 *
 * @param {string} path
 * @param {string} [rolesPath] defaults to `track_roles.json` beside the file
 * @returns {Song}
 */
export function loadReference(path, rolesPath = join(dirname(path), 'track_roles.json')) {
  if (!existsSync(rolesPath)) {
    throw new Error(`no role labels for ${path}: expected ${rolesPath}`);
  }
  const file = basename(path);
  const labels = JSON.parse(readFileSync(rolesPath, 'utf8'))[file];
  if (!labels) {
    throw new Error(`${rolesPath} has no entry for ${file}`);
  }
  const smf = readSmf(path);
  const parts = smf.parts.map((part) => {
    const label = labels[`trk${part.track}ch${part.channel}`];
    let role = REFERENCE_ROLE[label?.role] ?? 'other';
    // A vocal label the labeller was unsure of is not trusted as the sung line,
    // the same rule build_reference_targets.py applies.
    if (role === 'melody' && (label.melody_exclude || label.confidence === 'low')) {
      role = 'other';
    }
    if (part.channel === 9) {
      role = 'drums';
    }
    let notes = part.notes
      .map((n) => ({
        pitch: n.pitch,
        startBeat: n.start / smf.ppq,
        durationBeat: (n.end - n.start) / smf.ppq,
        velocity: n.velocity,
      }))
      .sort(byTime);
    if (role === 'melody') {
      notes = skyline(notes);
    }
    return { name: `trk${part.track}ch${part.channel}:${label?.role ?? '?'}`, role, notes };
  });
  const firstTempo = smf.tempos[0]?.usPerBeat ?? 500000;
  return assemble(
    {
      source: 'reference',
      label: file.replace(/\.mid$/i, ''),
      bpm: Math.round((60_000_000 / firstTempo) * 10) / 10,
      tempos: smf.tempos.map((t) => ({ beat: t.tick / smf.ppq, bpm: 60_000_000 / t.usPerBeat })),
      sections: labelledSections(labels._sections),
      blueprint: null,
      declared: null,
    },
    parts,
  );
}

/** Hand-labelled sections in beats, or null when the file carries none. */
function labelledSections(entries) {
  if (!Array.isArray(entries) || !entries.length) {
    return null;
  }
  return entries.map((entry, i) => ({
    type: entry.type,
    startBeat: entry.bar * BEATS_PER_BAR,
    endBeat: i + 1 < entries.length ? entries[i + 1].bar * BEATS_PER_BAR : Number.POSITIVE_INFINITY,
  }));
}

/**
 * Load either kind of input by extension.
 *
 * @param {string} path
 * @param {{ roles?: string }} [opts]
 * @returns {Song}
 */
export function loadSong(path, opts = {}) {
  return /\.midi?$/i.test(path) ? loadReference(path, opts.roles) : loadGenerated(path);
}
