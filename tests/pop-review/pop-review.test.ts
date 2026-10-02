/**
 * The pop review's readings on hand-written songs whose answers are known.
 *
 * Each case is a control: a song built so that the property under test is
 * unambiguous, which is what shows a reading on generated output is about the
 * music and not about the detector.
 */

import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { describe, expect, it } from 'vitest';
// @ts-expect-error -- plain ESM tooling module without type declarations
import { readSong } from '../../scripts/pop_review/analysis.mjs';
// @ts-expect-error -- plain ESM tooling module without type declarations
import { correctnessOf, vocalFollowsAccompaniment } from '../../scripts/pop_review/correctness.mjs';
// @ts-expect-error -- plain ESM tooling module without type declarations
import { popOf } from '../../scripts/pop_review/pop.mjs';
// @ts-expect-error -- plain ESM tooling module without type declarations
import { bandsOf } from '../../scripts/pop_review/report.mjs';
// @ts-expect-error -- plain ESM tooling module without type declarations
import { readSmf } from '../../scripts/pop_review/smf.mjs';
// @ts-expect-error -- plain ESM tooling module without type declarations
import { loadGenerated, loadReference } from '../../scripts/pop_review/song.mjs';

interface Note {
  pitch: number;
  startBeat: number;
  durationBeat: number;
  velocity: number;
}

const note = (pitch: number, startBeat: number, durationBeat = 1, velocity = 80): Note => ({
  pitch,
  startBeat,
  durationBeat,
  velocity,
});

/** C major roots of I, V, vi, IV — one chord per bar. */
const AXIS = [0, 7, 9, 5];
const CHORD = [
  [60, 64, 67],
  [59, 62, 67],
  [57, 60, 64],
  [57, 60, 65],
];
/** Order the chord's tones are sung in, per bar: the verse arpeggiates, the chorus climbs. */
const VERSE_TUNE = [0, 1, 2, 1];
const CHORUS_TUNE = [2, 1, 2, 0];

/**
 * Verse (8 bars) then chorus (8 bars), twice, over the I-V-vi-IV loop, with a
 * root-position bass and the chorus sung an octave up, denser and louder.
 */
function buildSong() {
  const melody: Note[] = [];
  const bass: Note[] = [];
  const harmony: Note[] = [];
  const sections: { type: string; startBeat: number; endBeat: number }[] = [];
  const plan = [
    ['A', VERSE_TUNE, 70],
    ['Chorus', CHORUS_TUNE, 100],
    ['A', VERSE_TUNE, 70],
    ['Chorus', CHORUS_TUNE, 100],
  ] as const;
  plan.forEach(([type, tune, velocity], index) => {
    {
      const start = index * 32;
      sections.push({ type, startBeat: start, endBeat: start + 32 });
      for (let bar = 0; bar < 8; bar += 1) {
        const b = start + bar * 4;
        const chord = bar % 4;
        bass.push(note(36 + AXIS[chord], b, 4, velocity));
        for (const p of CHORD[chord]) {
          harmony.push(note(p - 12, b, 4, velocity));
        }
        const step = type === 'Chorus' ? 0.5 : 1;
        for (let i = 0; i < 4 / step; i += 1) {
          const octave = type === 'Chorus' ? 24 : 12;
          const pitch = CHORD[chord][tune[i % 4]] + octave;
          melody.push(note(pitch, b + i * step, step, velocity));
        }
      }
    }
  });
  const parts = [
    { name: 'Vocal', role: 'melody', notes: melody },
    { name: 'Bass', role: 'bass', notes: bass },
    { name: 'Chord', role: 'harmony', notes: harmony },
  ];
  return {
    source: 'generated',
    label: 'control',
    bpm: 120,
    tempos: [{ beat: 0, bpm: 120 }],
    totalBeats: 128,
    parts,
    melody,
    bass,
    harmony,
    drums: [],
    sections,
    blueprint: null,
    declared: {
      keyPc: 0,
      modulationBeat: null,
      modulationSemitones: 0,
      chords: Array.from({ length: 32 }, (_, bar) => ({
        startBeat: bar * 4,
        endBeat: bar * 4 + 4,
        rootPc: AXIS[bar % 4],
      })),
      vocalLow: 60,
      vocalHigh: 91,
    },
  };
}

describe('pop review on a hand-written control song', () => {
  const reading = readSong(buildSong());
  const correctness = correctnessOf(reading);
  const pop = popOf(reading);

  it('reads the declared choruses and finds the repeated hook in each', () => {
    expect(reading.runs).toEqual([
      { startBeat: 32, endBeat: 64 },
      { startBeat: 96, endBeat: 128 },
    ]);
    expect(pop.chorusCount).toBe(2);
    expect(pop.hookChorusExclusivity).toBe(1);
    expect(pop.hookOpensChorusRatio).toBe(1);
    expect(pop.chorusRestatementLikeness).toBeCloseTo(1, 5);
  });

  it('recovers the same choruses from self-similarity alone', () => {
    expect(reading.agreement.recall).toBe(1);
    expect(reading.agreement.precision).toBe(1);
  });

  it('measures the lift the chorus was written with', () => {
    expect(pop.chorusMeanPitchLift).toBeGreaterThan(10);
    expect(pop.chorusVocalDensityRatio).toBeCloseTo(2, 5);
    expect(pop.songPeakInChorus).toBe(1);
  });

  it('names the I-V-vi-IV loop', () => {
    expect(pop.progressions.axis).toBeGreaterThan(0);
  });

  it('agrees with every declaration of a song written exactly as declared', () => {
    expect(correctness.declaredRootAgreement).toBe(1);
    expect(correctness.declaredKeyMatch).toBe(1);
    expect(correctness.bassRootOnDownbeatRatio).toBe(1);
    expect(correctness.vocalOutOfDeclaredRangeRatio).toBe(0);
    expect(correctness.chorusTonicClosureRatio).toBe(0);
  });

  it('reports no vertical clash between consonant parts', () => {
    expect(correctness.melodyClashesPer100Bars).toBe(0);
    expect(correctness.bassClashesPer100Bars).toBe(0);
  });
});

describe('a bass moved off the root', () => {
  it('is read as such by the downbeat check', () => {
    const song = buildSong();
    for (const n of song.bass) {
      n.pitch += 7;
    }
    const reading = readSong(song);
    expect(correctnessOf(reading).bassRootOnDownbeatRatio).toBeLessThan(0.5);
  });
});

describe('declared root agreement', () => {
  /** I-vi-IV-V at two beats a chord: each change shares two tones with the chord before it. */
  const HALF_BAR_ROOTS = [0, 9, 5, 7];
  const HALF_BAR_CHORDS = [
    [48, 52, 55],
    [45, 48, 52],
    [45, 48, 53],
    [47, 50, 55],
  ];

  /** The loop with every change after the first pushed `push` beats early. */
  function halfBarSong(push = 0) {
    const song = buildSong();
    song.bass = [];
    song.harmony = [];
    song.declared.chords = [];
    for (let i = 0; i < 64; i += 1) {
      const start = i === 0 ? 0 : i * 2 - push;
      const end = Math.min(i * 2 + 2 - push, 128);
      song.bass.push(note(36 + HALF_BAR_ROOTS[i % 4], start, end - start));
      for (const p of HALF_BAR_CHORDS[i % 4]) {
        song.harmony.push(note(p, start, end - start));
      }
      song.declared.chords.push({ startBeat: start, endBeat: end, rootPc: HALF_BAR_ROOTS[i % 4] });
    }
    song.parts = [
      { name: 'Vocal', role: 'melody', notes: song.melody },
      { name: 'Bass', role: 'bass', notes: song.bass },
      { name: 'Chord', role: 'harmony', notes: song.harmony },
    ];
    return song;
  }

  it('credits a change the bass and chord both make half a beat early', () => {
    // The song's own segmentation cannot cut on a half beat and reads vi-IV as one IVmaj7 here.
    expect(correctnessOf(readSong(halfBarSong(0.5))).declaredRootAgreement).toBe(1);
  });

  it('ignores declared chords that nothing voices', () => {
    const song = halfBarSong();
    const cut = 96;
    song.bass = song.bass.filter((n: Note) => n.startBeat < cut);
    song.harmony = song.harmony.filter((n: Note) => n.startBeat < cut);
    song.parts[1].notes = song.bass;
    song.parts[2].notes = song.harmony;
    expect(correctnessOf(readSong(song)).declaredRootAgreement).toBe(1);
  });

  it('drops when the sounding chords are not the declared ones', () => {
    const song = halfBarSong();
    for (const c of song.declared.chords) {
      c.rootPc = 0;
    }
    expect(correctnessOf(readSong(song)).declaredRootAgreement).toBeCloseTo(0.25, 5);
  });
});

describe('vocal alignment with the accompaniment', () => {
  it('holds for a vocal sung on the chords under it', () => {
    expect(vocalFollowsAccompaniment(buildSong())).toBe(true);
  });

  it('fails for the same vocal moved off its accompaniment', () => {
    const song = buildSong();
    song.melody = song.melody.map((n: Note) => ({ ...n, startBeat: n.startBeat + 9 }));
    expect(vocalFollowsAccompaniment(song)).toBe(false);
  });

  it('has nothing to judge without a vocal', () => {
    const song = buildSong();
    song.melody = [];
    expect(vocalFollowsAccompaniment(song)).toBeNull();
  });
});

describe('a chorus that is not restated', () => {
  it('lowers the restatement likeness', () => {
    const song = buildSong();
    let i = 0;
    song.melody = song.melody.map((n) =>
      n.startBeat >= 96 ? { ...n, pitch: 72 + ((i++ * 5) % 12) } : n,
    );
    expect(popOf(readSong(song)).chorusRestatementLikeness).toBeLessThan(0.8);
  });
});

describe('loaders', () => {
  it('reads an SMF file into note spans by track and channel', () => {
    const dir = mkdtempSync(join(tmpdir(), 'pop-review-smf-'));
    try {
      const track = [
        0x00,
        0xff,
        0x51,
        0x03,
        0x07,
        0xa1,
        0x20, // 120 BPM
        0x00,
        0x90,
        60,
        100, // C4 on, channel 0
        0xbc,
        0x00,
        0x80,
        60,
        0, // off after 7680 ticks (4 bars)
        0x00,
        0x99,
        36,
        90, // kick on, channel 9
        0x60,
        36,
        0, // running status note-on velocity 0 = off after 96
        0x00,
        0xff,
        0x2f,
        0x00,
      ];
      const header = [0x4d, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xe0];
      const chunk = [0x4d, 0x54, 0x72, 0x6b, 0, 0, 0, track.length, ...track];
      const path = join(dir, 'x.mid');
      writeFileSync(path, Buffer.from([...header, ...chunk]));
      writeFileSync(
        join(dir, 'track_roles.json'),
        JSON.stringify({
          'x.mid': {
            trk0ch0: { role: 'vocal' },
            _sections: [
              { type: 'A', bar: 0 },
              { type: 'Chorus', bar: 1 },
            ],
          },
        }),
      );
      const song = loadReference(path);
      expect(song.melody.map((n: Note) => n.pitch)).toEqual([60]);
      expect(song.drums).toHaveLength(1);
      expect(song.sections).toEqual([
        { type: 'A', startBeat: 0, endBeat: 4 },
        { type: 'Chorus', startBeat: 4, endBeat: song.totalBeats },
      ]);
      const smf = readSmf(path);
      expect(smf.ppq).toBe(480);
      expect(smf.tempos).toEqual([{ tick: 0, usPerBeat: 500000 }]);
      const byChannel = Object.fromEntries(
        smf.parts.map((p: { channel: number; notes: unknown[] }) => [p.channel, p.notes]),
      );
      expect(byChannel[0]).toEqual([{ pitch: 60, start: 0, end: 7680, velocity: 100 }]);
      expect(byChannel[9]).toEqual([{ pitch: 36, start: 7680, end: 7776, velocity: 90 }]);
    } finally {
      rmSync(dir, { recursive: true, force: true });
    }
  });

  it('maps borrowed chord degrees and a modulation onto sounding roots', () => {
    const dir = mkdtempSync(join(tmpdir(), 'pop-review-json-'));
    try {
      const path = join(dir, 'output.json');
      writeFileSync(
        path,
        JSON.stringify({
          bpm: 120,
          division: 480,
          duration_ticks: 3 * 1920,
          metadata: {
            blueprint: 0,
            seed: 1,
            key: 2,
            modulation_tick: 2 * 1920,
            modulation_semitones: 1,
          },
          tracks: [
            {
              name: 'Vocal',
              notes: [{ pitch: 62, start_ticks: 0, duration_ticks: 480, velocity: 90 }],
            },
          ],
          sections: [{ type: 'A', start_ticks: 0, end_ticks: 3 * 1920 }],
          chords: [
            { tick: 0, endTick: 1920, degree: 10 },
            { tick: 1920, endTick: 3840, degree: 4 },
            { tick: 3840, endTick: 5760, degree: 0 },
          ],
        }),
      );
      const song = loadGenerated(path);
      // bVII of D is C; V of D is A; I after the +1 modulation is Eb.
      expect(song.declared.chords.map((c: { rootPc: number }) => c.rootPc)).toEqual([0, 9, 3]);
    } finally {
      rmSync(dir, { recursive: true, force: true });
    }
  });
});

describe('vocal drive', () => {
  it('counts a same-pitch rapid run and nothing in a plain line', () => {
    const song = buildSong();
    expect(popOf(readSong(song)).vocalRapidRepeatRunsPerMin).toBe(0);
    // Six sixteenths on one pitch at 120 BPM: 125 ms apart.
    const run = Array.from({ length: 6 }, (_, i) => note(76, 4 + i * 0.25, 0.25));
    song.melody = [...song.melody.filter((n) => n.startBeat < 4 || n.startBeat >= 6), ...run].sort(
      (a, b) => a.startBeat - b.startBeat,
    );
    const pop = popOf(readSong(song));
    expect(pop.vocalRapidRepeatRunsPerMin).toBeGreaterThan(0);
    expect(pop.vocalLongestRapidRepeat).toBe(6);
  });

  it('reads seconds through a tempo change', () => {
    const song = buildSong();
    song.tempos = [
      { beat: 0, bpm: 120 },
      { beat: 64, bpm: 60 },
    ];
    expect(popOf(readSong(song)).durationSec).toBeCloseTo(32 + 64, 6);
  });
});

describe('reference bands', () => {
  it('takes n, min, median and max over finite values only', () => {
    const bands = bandsOf([{ x: 1 }, { x: 3 }, { x: null }, { x: 2, y: 5 }]);
    expect(bands.x).toEqual({ n: 3, min: 1, median: 2, max: 3 });
    expect(bands.y).toEqual({ n: 1, min: 5, median: 5, max: 5 });
  });
});
