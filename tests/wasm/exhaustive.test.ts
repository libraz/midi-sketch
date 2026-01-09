import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { type SongConfigOptions, WasmTestContext } from './test-helpers';

// Parameter ranges based on midisketch_c.h and validation rules
const PARAM_RANGES = {
  stylePresetId: [0, 1, 2],
  key: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11],
  bpm: [0, 40, 60, 120, 180, 240], // 0 = use default, valid: 40-240
  chordProgressionId: Array.from({ length: 20 }, (_, i) => i),
  formId: Array.from({ length: 10 }, (_, i) => i),
  vocalAttitude: [0, 1], // 2 (Raw) requires specific style support
  compositionStyle: [0, 1, 2],
  arpeggioPattern: [0, 1, 2, 3],
  arpeggioSpeed: [0, 1, 2],
  arpeggioOctaveRange: [1, 2, 3],
  vocalStyle: [0, 1, 2, 3, 4, 5, 6, 7, 8],
  melodyTemplate: [0, 1, 2, 3, 4, 5, 6, 7],
  melodicComplexity: [0, 1, 2],
  hookIntensity: [0, 1, 2, 3],
  vocalGroove: [0, 1, 2, 3, 4, 5],
  modulationTiming: [0, 1, 2, 3, 4],
  modulationSemitones: [1, 2, 3, 4],
  arrangementGrowth: [0, 1],
  motifRepeatScope: [0, 1],
  callDensity: [0, 1, 2, 3],
  introChant: [0, 1, 2],
  mixPattern: [0, 1, 2],
} as const;

// Vocal range limits (MIDI note)
const VOCAL_RANGE = { min: 36, max: 96 } as const;

// Style-specific allowed attitudes
const STYLE_ALLOWED_ATTITUDES: Record<number, number[]> = {
  0: [0, 1], // Style 0: Clean, Expressive
  1: [0, 1], // Style 1: Clean, Expressive
  2: [0], // Style 2: Clean only
};

// Generate random combinations for testing
function* generateCombinations(count: number, baseSeed: number): Generator<SongConfigOptions> {
  const rng = createRng(baseSeed);

  for (let i = 0; i < count; i++) {
    const callEnabled = rng() > 0.5;
    const introChant = pick(PARAM_RANGES.introChant, rng);
    const mixPattern = pick(PARAM_RANGES.mixPattern, rng);

    // When callEnabled, need very long duration (at 40 BPM, minimum is ~144 seconds)
    // Safest to use 0 (auto) which lets the system determine appropriate duration
    let targetDurationSeconds: number;
    if (callEnabled) {
      // Always use 0 (auto) when call is enabled to avoid validation errors
      targetDurationSeconds = 0;
    } else {
      targetDurationSeconds = pick([0, 30, 60, 120, 180, 240], rng);
    }

    // Pick style first, then constrain vocalAttitude based on style
    const stylePresetId = pick(PARAM_RANGES.stylePresetId, rng);
    const allowedAttitudes = STYLE_ALLOWED_ATTITUDES[stylePresetId] ?? [0];
    const vocalAttitude = pick(allowedAttitudes, rng);

    yield {
      seed: Math.floor(rng() * 1000000),
      stylePresetId,
      key: pick(PARAM_RANGES.key, rng),
      bpm: pick(PARAM_RANGES.bpm, rng),
      chordProgressionId: pick(PARAM_RANGES.chordProgressionId, rng),
      formId: pick(PARAM_RANGES.formId, rng),
      vocalAttitude,
      drumsEnabled: rng() > 0.2,
      arpeggioEnabled: rng() > 0.5,
      arpeggioPattern: pick(PARAM_RANGES.arpeggioPattern, rng),
      arpeggioSpeed: pick(PARAM_RANGES.arpeggioSpeed, rng),
      arpeggioOctaveRange: pick(PARAM_RANGES.arpeggioOctaveRange, rng),
      arpeggioGate: Math.floor(rng() * 100),
      vocalLow: VOCAL_RANGE.min + Math.floor(rng() * 24), // 36-59
      vocalHigh: Math.min(VOCAL_RANGE.max, VOCAL_RANGE.min + 24 + Math.floor(rng() * 36)), // 60-95 capped at 96
      skipVocal: rng() > 0.8,
      humanize: rng() > 0.5,
      humanizeTiming: Math.floor(rng() * 100),
      humanizeVelocity: Math.floor(rng() * 100),
      chordExtSus: rng() > 0.5,
      chordExt7th: rng() > 0.5,
      chordExt9th: rng() > 0.5,
      chordExtSusProb: Math.floor(rng() * 100),
      chordExt7thProb: Math.floor(rng() * 100),
      chordExt9thProb: Math.floor(rng() * 100),
      compositionStyle: pick(PARAM_RANGES.compositionStyle, rng),
      targetDurationSeconds,
      modulationTiming: pick(PARAM_RANGES.modulationTiming, rng),
      // modulationSemitones: 1-4 always (required when modulationTiming != 0)
      modulationSemitones: pick(PARAM_RANGES.modulationSemitones, rng),
      seEnabled: rng() > 0.3,
      callEnabled,
      callNotesEnabled: rng() > 0.5,
      introChant,
      mixPattern,
      callDensity: pick(PARAM_RANGES.callDensity, rng),
      vocalStyle: pick(PARAM_RANGES.vocalStyle, rng),
      melodyTemplate: pick(PARAM_RANGES.melodyTemplate, rng),
      arrangementGrowth: pick(PARAM_RANGES.arrangementGrowth, rng),
      arpeggioSyncChord: rng() > 0.5,
      motifRepeatScope: pick(PARAM_RANGES.motifRepeatScope, rng),
      motifFixedProgression: rng() > 0.5,
      motifMaxChordCount: pick([0, 2, 3, 4, 5, 6, 7, 8], rng),
      melodicComplexity: pick(PARAM_RANGES.melodicComplexity, rng),
      hookIntensity: pick(PARAM_RANGES.hookIntensity, rng),
      vocalGroove: pick(PARAM_RANGES.vocalGroove, rng),
    };
  }
}

// Simple seeded RNG (mulberry32)
function createRng(seed: number): () => number {
  return () => {
    seed |= 0;
    seed = (seed + 0x6d2b79f5) | 0;
    let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function pick<T>(arr: readonly T[], rng: () => number): T {
  return arr[Math.floor(rng() * arr.length)];
}

describe('MidiSketch WASM - Exhaustive Parameter Tests', () => {
  const ctx = new WasmTestContext();

  beforeAll(async () => {
    await ctx.init();
  });

  afterAll(() => {
    ctx.destroy();
  });

  describe('Single parameter sweep', () => {
    // Test each parameter independently at all values
    it.each(PARAM_RANGES.stylePresetId)('stylePresetId=%i', (value) => {
      const result = ctx.generateFromConfig({ seed: 1000 + value, stylePresetId: value });
      expect(result).toBe(0);
    });

    it.each(PARAM_RANGES.key)('key=%i', (value) => {
      const result = ctx.generateFromConfig({ seed: 2000 + value, key: value });
      expect(result).toBe(0);
    });

    it.each(PARAM_RANGES.chordProgressionId)('chordProgressionId=%i', (value) => {
      const result = ctx.generateFromConfig({ seed: 3000 + value, chordProgressionId: value });
      expect(result).toBe(0);
    });

    it.each(PARAM_RANGES.formId)('formId=%i', (value) => {
      const result = ctx.generateFromConfig({ seed: 4000 + value, formId: value });
      expect(result).toBe(0);
    });

    it.each(PARAM_RANGES.vocalAttitude)('vocalAttitude=%i (Clean/Expressive)', (value) => {
      const result = ctx.generateFromConfig({ seed: 5000 + value, vocalAttitude: value });
      expect(result).toBe(0);
    });

    // vocalAttitude=2 (Raw) is only valid for certain styles
    it('vocalAttitude=2 (Raw) should fail on default style', () => {
      const result = ctx.generateFromConfig({ seed: 5002, vocalAttitude: 2 });
      expect(result).not.toBe(0); // Expected to fail validation
    });

    it.each(PARAM_RANGES.compositionStyle)('compositionStyle=%i', (value) => {
      const result = ctx.generateFromConfig({ seed: 6000 + value, compositionStyle: value });
      expect(result).toBe(0);
    });

    it.each(PARAM_RANGES.vocalStyle)('vocalStyle=%i', (value) => {
      const result = ctx.generateFromConfig({ seed: 7000 + value, vocalStyle: value });
      expect(result).toBe(0);
    });

    it.each(PARAM_RANGES.melodyTemplate)('melodyTemplate=%i', (value) => {
      const result = ctx.generateFromConfig({ seed: 8000 + value, melodyTemplate: value });
      expect(result).toBe(0);
    });

    it.each(PARAM_RANGES.melodicComplexity)('melodicComplexity=%i', (value) => {
      const result = ctx.generateFromConfig({ seed: 9000 + value, melodicComplexity: value });
      expect(result).toBe(0);
    });

    it.each(PARAM_RANGES.hookIntensity)('hookIntensity=%i', (value) => {
      const result = ctx.generateFromConfig({ seed: 10000 + value, hookIntensity: value });
      expect(result).toBe(0);
    });

    it.each(PARAM_RANGES.vocalGroove)('vocalGroove=%i', (value) => {
      const result = ctx.generateFromConfig({ seed: 11000 + value, vocalGroove: value });
      expect(result).toBe(0);
    });

    it.each(PARAM_RANGES.arpeggioPattern)('arpeggioPattern=%i', (value) => {
      const result = ctx.generateFromConfig({
        seed: 12000 + value,
        arpeggioEnabled: true,
        arpeggioPattern: value,
      });
      expect(result).toBe(0);
    });

    it.each(PARAM_RANGES.arpeggioSpeed)('arpeggioSpeed=%i', (value) => {
      const result = ctx.generateFromConfig({
        seed: 13000 + value,
        arpeggioEnabled: true,
        arpeggioSpeed: value,
      });
      expect(result).toBe(0);
    });

    it.each(PARAM_RANGES.modulationTiming)('modulationTiming=%i', (value) => {
      const result = ctx.generateFromConfig({ seed: 14000 + value, modulationTiming: value });
      expect(result).toBe(0);
    });

    it.each(PARAM_RANGES.callDensity)('callDensity=%i', (value) => {
      const result = ctx.generateFromConfig({
        seed: 15000 + value,
        callEnabled: true,
        callDensity: value,
      });
      expect(result).toBe(0);
    });

    it.each(PARAM_RANGES.introChant)('introChant=%i', (value) => {
      const result = ctx.generateFromConfig({
        seed: 16000 + value,
        callEnabled: true,
        introChant: value,
      });
      expect(result).toBe(0);
    });

    it.each(PARAM_RANGES.mixPattern)('mixPattern=%i', (value) => {
      const result = ctx.generateFromConfig({
        seed: 17000 + value,
        callEnabled: true,
        mixPattern: value,
      });
      expect(result).toBe(0);
    });
  });

  describe('Random combinations - generateFromConfig', () => {
    const COMBINATION_COUNT = 100;
    const combinations = [...generateCombinations(COMBINATION_COUNT, 42)];

    it.each(
      combinations.map((c, i) => [i, c] as const),
    )('combination #%i should generate without crash', (_index, config) => {
      const result = ctx.generateFromConfig(config);

      // If failed, skip detailed error logging for now (known validation issues)
      // The test will fail and show which combination index failed

      expect(result).toBe(0);

      // Also verify we can get events JSON without crash
      const { cleanup } = ctx.getEventsJson();
      cleanup();
    });
  });

  // regenerate_vocal tests removed - API deprecated

  // Regression tests for regenerateVocal removed - API deprecated

  describe('Edge cases - vocal range (valid)', () => {
    const validEdgeCases = [
      { vocalLow: 36, vocalHigh: 96 }, // Max valid range
      { vocalLow: 60, vocalHigh: 60 }, // Same note (minimum range)
      { vocalLow: 60, vocalHigh: 61 }, // Minimal range
      { vocalLow: 36, vocalHigh: 36 }, // Lowest valid
      { vocalLow: 96, vocalHigh: 96 }, // Highest valid
      { vocalLow: 48, vocalHigh: 72 }, // Standard
      { vocalLow: 55, vocalHigh: 75 }, // Common vocal range
    ];

    it.each(validEdgeCases)('vocal range low=%i high=%i', ({ vocalLow, vocalHigh }) => {
      const result = ctx.generateFromConfig({
        seed: 20000 + vocalLow * 128 + vocalHigh,
        vocalLow,
        vocalHigh,
      });
      expect(result).toBe(0);
    });
  });

  describe('Edge cases - vocal range (invalid should fail)', () => {
    const invalidEdgeCases = [
      { vocalLow: 0, vocalHigh: 127, desc: 'out of range' },
      { vocalLow: 97, vocalHigh: 100, desc: 'above max' },
      { vocalLow: 30, vocalHigh: 50, desc: 'below min' },
      { vocalLow: 80, vocalHigh: 60, desc: 'inverted' },
    ];

    it.each(invalidEdgeCases)('invalid: $desc', ({ vocalLow, vocalHigh }) => {
      const result = ctx.generateFromConfig({
        seed: 21000,
        vocalLow,
        vocalHigh,
      });
      expect(result).not.toBe(0); // Should return error
    });
  });

  describe('Edge cases - BPM (valid)', () => {
    const validBpmCases = [0, 40, 60, 90, 120, 150, 180, 200, 240];

    it.each(validBpmCases)('bpm=%i', (bpm) => {
      const result = ctx.generateFromConfig({ seed: 30000 + bpm, bpm });
      expect(result).toBe(0);
    });
  });

  describe('Edge cases - BPM (invalid should fail)', () => {
    const invalidBpmCases = [39, 241, 300, 500];

    it.each(invalidBpmCases)('invalid bpm=%i', (bpm) => {
      const result = ctx.generateFromConfig({ seed: 31000 + bpm, bpm });
      expect(result).not.toBe(0); // Should return error
    });
  });

  describe('Edge cases - duration', () => {
    const durationCases = [0, 15, 30, 60, 90, 120, 180, 240, 300, 600];

    it.each(durationCases)('targetDurationSeconds=%i', (duration) => {
      const result = ctx.generateFromConfig({
        seed: 40000 + duration,
        targetDurationSeconds: duration,
      });
      expect(result).toBe(0);
    });
  });

  describe('Composition style specific combinations', () => {
    // MelodyLead (0)
    it('MelodyLead with all vocal styles', () => {
      for (const vocalStyle of PARAM_RANGES.vocalStyle) {
        const result = ctx.generateFromConfig({
          seed: 50000 + vocalStyle,
          compositionStyle: 0,
          vocalStyle,
        });
        expect(result).toBe(0);
      }
    });

    // BackgroundMotif (1)
    it('BackgroundMotif with all motif settings', () => {
      for (const motifRepeatScope of PARAM_RANGES.motifRepeatScope) {
        for (const motifMaxChordCount of [0, 2, 4, 8]) {
          const result = ctx.generateFromConfig({
            seed: 60000 + motifRepeatScope * 10 + motifMaxChordCount,
            compositionStyle: 1,
            motifRepeatScope,
            motifMaxChordCount,
            motifFixedProgression: true,
          });
          expect(result).toBe(0);
        }
      }
    });

    // SynthDriven (2)
    it('SynthDriven with arpeggio combinations', () => {
      for (const arpeggioPattern of PARAM_RANGES.arpeggioPattern) {
        for (const arpeggioSpeed of PARAM_RANGES.arpeggioSpeed) {
          const result = ctx.generateFromConfig({
            seed: 70000 + arpeggioPattern * 10 + arpeggioSpeed,
            compositionStyle: 2,
            arpeggioEnabled: true,
            arpeggioPattern,
            arpeggioSpeed,
          });
          expect(result).toBe(0);
        }
      }
    });
  });

  describe('Stress test - rapid sequential generation', () => {
    it('should handle 50 rapid sequential generations', () => {
      for (let i = 0; i < 50; i++) {
        const result = ctx.generateFromConfig({ seed: 80000 + i });
        expect(result).toBe(0);
      }
    });

    // regenerate_vocal cycle test removed - API deprecated
  });

  // ============================================================================
  // Regression test for duration_ticks underflow bug (fixed 2026-01-07)
  // Bug: uint32_t underflow caused duration_ticks to become -1 (0xFFFFFFFF)
  // ============================================================================
  describe('Regression - duration_ticks underflow bug', () => {
    interface NoteData {
      pitch: number;
      velocity: number;
      start_ticks: number;
      duration_ticks: number;
      start_seconds: number;
      duration_seconds: number;
    }

    interface TrackData {
      name: string;
      channel: number;
      program: number;
      notes: NoteData[];
    }

    interface EventData {
      bpm: number;
      division: number;
      duration_ticks: number;
      duration_seconds: number;
      tracks: TrackData[];
    }

    function validateNoteData(note: NoteData, _trackName: string, _noteIndex: number): void {
      // duration_ticks must be positive and reasonable (not underflowed)
      expect(note.duration_ticks).toBeGreaterThan(0);
      expect(note.duration_ticks).toBeLessThan(100000); // Reasonable upper bound

      // Check for underflow signature (-1 as uint32 = 4294967295)
      expect(note.duration_ticks).not.toBe(4294967295);
      expect(note.duration_ticks).not.toBe(-1);

      // start_ticks must be non-negative
      expect(note.start_ticks).toBeGreaterThanOrEqual(0);

      // Validate other fields
      expect(note.pitch).toBeGreaterThanOrEqual(0);
      expect(note.pitch).toBeLessThanOrEqual(127);
      expect(note.velocity).toBeGreaterThan(0);
      expect(note.velocity).toBeLessThanOrEqual(127);
      expect(note.duration_seconds).toBeGreaterThan(0);
    }

    // regenerateVocal duration_ticks test removed - API deprecated

    it('should not produce negative duration_ticks with humanization enabled', () => {
      // Humanization can cause timing shifts that lead to overlaps
      const result = ctx.generateFromConfig({
        seed: 12345,
        humanize: true,
        humanizeTiming: 100, // Maximum timing variation
        humanizeVelocity: 100,
      });
      expect(result).toBe(0);

      const { data, cleanup } = ctx.getEventsJson();
      const events = data as EventData;

      for (const track of events.tracks) {
        for (let i = 0; i < track.notes.length; i++) {
          validateNoteData(track.notes[i], track.name, i);
        }
      }

      cleanup();
    });

    // multiple regenerateVocal calls test removed - API deprecated
  });

  // ============================================================================
  // Data integrity validation - ensure no anomalous data is generated
  // ============================================================================
  describe('Data integrity validation', () => {
    interface NoteData {
      pitch: number;
      velocity: number;
      start_ticks: number;
      duration_ticks: number;
    }

    interface TrackData {
      name: string;
      notes: NoteData[];
    }

    interface SectionData {
      name: string;
      startTick: number;
      endTick: number;
      bars: number;
    }

    interface EventData {
      bpm: number;
      division: number;
      duration_ticks: number;
      tracks: TrackData[];
      sections: SectionData[];
    }

    function validateEventData(events: EventData, description: string): void {
      // Validate global properties
      expect(events.bpm).toBeGreaterThanOrEqual(40);
      expect(events.bpm).toBeLessThanOrEqual(240);
      expect(events.division).toBe(480); // TICKS_PER_BEAT
      expect(events.duration_ticks).toBeGreaterThan(0);

      // Validate tracks
      expect(events.tracks.length).toBeGreaterThan(0);

      for (const track of events.tracks) {
        // Skip SE track which may have no notes
        if (track.name === 'SE') {
          continue;
        }

        for (let i = 0; i < track.notes.length; i++) {
          const note = track.notes[i];
          const _noteDesc = `${description} - ${track.name}[${i}]`;

          // Pitch validation
          expect(note.pitch).toBeGreaterThanOrEqual(0);
          expect(note.pitch).toBeLessThanOrEqual(127);

          // Velocity validation (must be positive)
          expect(note.velocity).toBeGreaterThan(0);
          expect(note.velocity).toBeLessThanOrEqual(127);

          // Timing validation
          expect(note.start_ticks).toBeGreaterThanOrEqual(0);
          expect(note.duration_ticks).toBeGreaterThan(0);
          expect(note.duration_ticks).toBeLessThan(50000); // ~26 bars max

          // No underflow check
          expect(note.duration_ticks).not.toBe(4294967295);
        }

        // For vocal track, verify no overlaps
        if (track.name === 'Vocal' && track.notes.length > 1) {
          for (let i = 0; i < track.notes.length - 1; i++) {
            const current = track.notes[i];
            const next = track.notes[i + 1];
            const endTick = current.start_ticks + current.duration_ticks;

            expect(endTick).toBeLessThanOrEqual(next.start_ticks);
          }
        }
      }

      // Validate sections
      expect(events.sections.length).toBeGreaterThan(0);
      for (const section of events.sections) {
        expect(section.startTick).toBeGreaterThanOrEqual(0);
        expect(section.endTick).toBeGreaterThan(section.startTick);
        expect(section.bars).toBeGreaterThan(0);
      }
    }

    it('should produce valid data for all style presets', () => {
      for (let styleId = 0; styleId < 3; styleId++) {
        const result = ctx.generateFromConfig({ seed: 100000 + styleId, stylePresetId: styleId });
        expect(result).toBe(0);

        const { data, cleanup } = ctx.getEventsJson();
        validateEventData(data as EventData, `stylePresetId=${styleId}`);
        cleanup();
      }
    });

    it('should produce valid data for all composition styles', () => {
      for (let compStyle = 0; compStyle < 3; compStyle++) {
        const result = ctx.generateFromConfig({
          seed: 110000 + compStyle,
          compositionStyle: compStyle,
        });
        expect(result).toBe(0);

        const { data, cleanup } = ctx.getEventsJson();
        validateEventData(data as EventData, `compositionStyle=${compStyle}`);
        cleanup();
      }
    });

    it('should produce valid data with extreme vocal ranges', () => {
      const extremeRanges = [
        { vocalLow: 36, vocalHigh: 96 }, // Maximum range
        { vocalLow: 60, vocalHigh: 65 }, // Very narrow range
        { vocalLow: 36, vocalHigh: 48 }, // Low register
        { vocalLow: 84, vocalHigh: 96 }, // High register
      ];

      for (const range of extremeRanges) {
        const result = ctx.generateFromConfig({
          seed: 120000 + range.vocalLow,
          ...range,
        });
        expect(result).toBe(0);

        const { data, cleanup } = ctx.getEventsJson();
        validateEventData(data as EventData, `range=${range.vocalLow}-${range.vocalHigh}`);
        cleanup();
      }
    });

    it('should produce valid data with various groove feels', () => {
      for (let groove = 0; groove <= 5; groove++) {
        const result = ctx.generateFromConfig({
          seed: 130000 + groove,
          vocalGroove: groove,
        });
        expect(result).toBe(0);

        const { data, cleanup } = ctx.getEventsJson();
        validateEventData(data as EventData, `vocalGroove=${groove}`);
        cleanup();
      }
    });

    // BGM + vocal regeneration workflow test removed - API deprecated

    it('should produce valid data across 20 random configurations', () => {
      const rng = createTestRng(42);

      for (let i = 0; i < 20; i++) {
        const config: SongConfigOptions = {
          seed: Math.floor(rng() * 1000000),
          stylePresetId: Math.floor(rng() * 3),
          compositionStyle: Math.floor(rng() * 3),
          vocalStyle: Math.floor(rng() * 9),
          vocalGroove: Math.floor(rng() * 6),
          melodicComplexity: Math.floor(rng() * 3),
          hookIntensity: Math.floor(rng() * 4),
          humanize: rng() > 0.5,
          humanizeTiming: Math.floor(rng() * 100),
        };

        const result = ctx.generateFromConfig(config);
        expect(result).toBe(0);

        const { data, cleanup } = ctx.getEventsJson();
        validateEventData(data as EventData, `random config #${i}`);
        cleanup();
      }
    }, 30000); // 30 second timeout
  });
});

// Simple seeded RNG for test reproducibility
function createTestRng(seed: number): () => number {
  return () => {
    seed |= 0;
    seed = (seed + 0x6d2b79f5) | 0;
    let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}
