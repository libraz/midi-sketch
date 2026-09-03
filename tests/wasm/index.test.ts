import path from 'node:path';
import { beforeAll, describe, expect, it } from 'vitest';
import {
  ArpeggioPattern,
  ArpeggioSpeed,
  createDefaultConfig,
  GenerationParadigm,
  getBlueprintCount,
  getBlueprintName,
  getBlueprintParadigm,
  getBlueprintRiffPolicy,
  getBlueprints,
  getBlueprintTempoRange,
  getBlueprintWeight,
  getVersion,
  init,
  MidiFormat,
  MidiSketch,
  RiffPolicy,
} from '../../js/src/index';

describe('MidiSketch JS API', () => {
  beforeAll(async () => {
    const wasmPath = path.resolve(__dirname, '../../dist/midisketch.wasm');
    await Promise.all([init({ wasmPath }), init({ wasmPath })]);
  });

  describe('getVersion', () => {
    it('should return a valid semver version string', () => {
      const version = getVersion();
      expect(version).toMatch(/^\d+\.\d+\.\d+(\+.+)?$/);
    });

    it('should return consistent version across multiple calls', () => {
      const version1 = getVersion();
      const version2 = getVersion();
      expect(version1).toBe(version2);
    });
  });

  describe('public constants', () => {
    it('should expose every implemented arpeggio pattern', () => {
      expect(ArpeggioPattern).toEqual({
        Up: 0,
        Down: 1,
        UpDown: 2,
        Random: 3,
        Pinwheel: 4,
        PedalRoot: 5,
        Alberti: 6,
        BrokenChord: 7,
        Auto: 255,
      });
    });

    it('should expose every implemented arpeggio speed', () => {
      expect(ArpeggioSpeed).toEqual({
        Eighth: 0,
        Sixteenth: 1,
        Triplet: 2,
        Auto: 255,
      });
    });

    it('should expose MIDI output format values', () => {
      expect(MidiFormat).toEqual({
        SMF1: 1,
        SMF2: 2,
      });
    });
  });

  describe('MIDI output format', () => {
    it('should expose SMF1 and reject unsupported SMF2 explicitly', () => {
      const sketch = new MidiSketch();
      try {
        expect(sketch.getMidiFormat()).toBe(MidiFormat.SMF1);
        sketch.setMidiFormat(MidiFormat.SMF1);
        expect(sketch.getMidiFormat()).toBe(MidiFormat.SMF1);
        expect(() => sketch.setMidiFormat(MidiFormat.SMF2)).toThrow(
          'Set MIDI format failed: MIDI format is not supported by this build',
        );
        expect(sketch.getMidiFormat()).toBe(MidiFormat.SMF1);
      } finally {
        sketch.destroy();
      }
    });
  });

  // ============================================================================
  // Production Blueprint API Tests
  // ============================================================================

  describe('ProductionBlueprint API', () => {
    describe('getBlueprintCount', () => {
      it('should return 10 blueprints', () => {
        expect(getBlueprintCount()).toBe(10);
      });
    });

    describe('getBlueprintName', () => {
      it('should return correct names for each blueprint', () => {
        expect(getBlueprintName(0)).toBe('Traditional');
        expect(getBlueprintName(1)).toBe('RhythmLock');
        expect(getBlueprintName(2)).toBe('StoryPop');
        expect(getBlueprintName(3)).toBe('Ballad');
        expect(getBlueprintName(4)).toBe('IdolStandard');
        expect(getBlueprintName(5)).toBe('IdolHyper');
        expect(getBlueprintName(6)).toBe('IdolKawaii');
        expect(getBlueprintName(7)).toBe('IdolCoolPop');
        expect(getBlueprintName(8)).toBe('IdolEmo');
        expect(getBlueprintName(9)).toBe('BehavioralLoop');
      });

      it('should return "unknown" for invalid ID', () => {
        expect(getBlueprintName(255)).toBe('unknown');
      });
    });

    describe('getBlueprintParadigm', () => {
      it('should return correct paradigm for each blueprint', () => {
        expect(getBlueprintParadigm(0)).toBe(GenerationParadigm.Traditional);
        expect(getBlueprintParadigm(1)).toBe(GenerationParadigm.RhythmSync);
        expect(getBlueprintParadigm(2)).toBe(GenerationParadigm.MelodyDriven);
        expect(getBlueprintParadigm(3)).toBe(GenerationParadigm.MelodyDriven);
        expect(getBlueprintParadigm(4)).toBe(GenerationParadigm.MelodyDriven);
        expect(getBlueprintParadigm(5)).toBe(GenerationParadigm.RhythmSync);
        expect(getBlueprintParadigm(6)).toBe(GenerationParadigm.MelodyDriven);
        expect(getBlueprintParadigm(7)).toBe(GenerationParadigm.RhythmSync);
        expect(getBlueprintParadigm(8)).toBe(GenerationParadigm.MelodyDriven);
        // BehavioralLoop is built around a fixed riff, and Traditional skips the Motif
        // track outright, so its LockedPitch policy only means anything under RhythmSync.
        expect(getBlueprintParadigm(9)).toBe(GenerationParadigm.RhythmSync);
      });
    });

    describe('getBlueprintRiffPolicy', () => {
      it('should return correct riff policy for each blueprint', () => {
        expect(getBlueprintRiffPolicy(0)).toBe(RiffPolicy.Free);
        expect(getBlueprintRiffPolicy(1)).toBe(RiffPolicy.Locked);
        expect(getBlueprintRiffPolicy(2)).toBe(RiffPolicy.Evolving);
        expect(getBlueprintRiffPolicy(3)).toBe(RiffPolicy.Free);
        expect(getBlueprintRiffPolicy(4)).toBe(RiffPolicy.Evolving);
        expect(getBlueprintRiffPolicy(5)).toBe(RiffPolicy.Locked);
        expect(getBlueprintRiffPolicy(6)).toBe(RiffPolicy.Locked);
        expect(getBlueprintRiffPolicy(7)).toBe(RiffPolicy.Locked);
        expect(getBlueprintRiffPolicy(8)).toBe(RiffPolicy.Locked);
        expect(getBlueprintRiffPolicy(9)).toBe(RiffPolicy.LockedPitch);
      });
    });

    describe('getBlueprintWeight', () => {
      it('should return correct weights for each blueprint', () => {
        expect(getBlueprintWeight(0)).toBe(42); // Traditional: 42%
        expect(getBlueprintWeight(1)).toBe(14); // RhythmLock: 14%
        expect(getBlueprintWeight(2)).toBe(10); // StoryPop: 10%
        expect(getBlueprintWeight(3)).toBe(4); // Ballad: 4%
        expect(getBlueprintWeight(4)).toBe(10); // IdolStandard: 10%
        expect(getBlueprintWeight(5)).toBe(6); // IdolHyper: 6%
        expect(getBlueprintWeight(6)).toBe(5); // IdolKawaii: 5%
        expect(getBlueprintWeight(7)).toBe(5); // IdolCoolPop: 5%
        expect(getBlueprintWeight(8)).toBe(4); // IdolEmo: 4%
        expect(getBlueprintWeight(9)).toBe(0); // BehavioralLoop: explicit selection only
      });

      it('weights should sum to 100', () => {
        let total = 0;
        for (let i = 0; i < getBlueprintCount(); i++) {
          total += getBlueprintWeight(i);
        }
        expect(total).toBe(100);
      });
    });

    describe('getBlueprints', () => {
      it('should return array of all blueprints', () => {
        const blueprints = getBlueprints();
        expect(blueprints).toHaveLength(10);

        // Verify first blueprint (Traditional)
        expect(blueprints[0]).toEqual({
          id: 0,
          name: 'Traditional',
          paradigm: GenerationParadigm.Traditional,
          riffPolicy: RiffPolicy.Free,
          weight: 42,
          tempoMin: 96,
          tempoMax: 150,
        });

        // Verify second blueprint (RhythmLock)
        expect(blueprints[1]).toEqual({
          id: 1,
          name: 'RhythmLock',
          paradigm: GenerationParadigm.RhythmSync,
          riffPolicy: RiffPolicy.Locked,
          weight: 14,
          tempoMin: 160,
          tempoMax: 175,
        });

        expect(blueprints[9]).toEqual({
          id: 9,
          name: 'BehavioralLoop',
          paradigm: GenerationParadigm.RhythmSync,
          riffPolicy: RiffPolicy.LockedPitch,
          weight: 0,
          tempoMin: 100,
          tempoMax: 170,
        });
      });
    });

    it('should expose each blueprint tempo range', () => {
      expect(getBlueprintTempoRange(7)).toEqual({ min: 160, max: 178 });
    });

    describe('createDefaultConfig with blueprintId', () => {
      it('should include blueprintId in default config', () => {
        const config = createDefaultConfig(0);
        expect(config.blueprintId).toBeDefined();
        expect(typeof config.blueprintId).toBe('number');
      });
    });

    describe('MidiSketch.getResolvedBlueprintId', () => {
      it('should return resolved blueprint ID after generation', () => {
        const sketch = new MidiSketch();
        try {
          const config = createDefaultConfig(0);
          config.seed = 12345;
          config.blueprintId = 1; // RhythmSync
          sketch.generateFromConfig(config);

          const resolvedId = sketch.getResolvedBlueprintId();
          expect(resolvedId).toBe(1);
        } finally {
          sketch.destroy();
        }
      });

      it('should return selected blueprint when blueprintId is 255 (random)', () => {
        const sketch = new MidiSketch();
        try {
          const config = createDefaultConfig(0);
          config.seed = 12345;
          config.blueprintId = 255; // Random selection
          sketch.generateFromConfig(config);

          const resolvedId = sketch.getResolvedBlueprintId();
          expect(resolvedId).toBeGreaterThanOrEqual(0);
          expect(resolvedId).toBeLessThanOrEqual(8);
        } finally {
          sketch.destroy();
        }
      });

      it('should return 0 (default) before generation', () => {
        const sketch = new MidiSketch();
        try {
          const resolvedId = sketch.getResolvedBlueprintId();
          expect(resolvedId).toBe(0); // Default is Traditional (0)
        } finally {
          sketch.destroy();
        }
      });
    });

    describe('MidiSketch.getWarnings', () => {
      it('returns generator warnings instead of builder-only validation warnings', () => {
        const sketch = new MidiSketch();
        try {
          const config = createDefaultConfig(0);
          config.seed = 12345;
          config.blueprintId = 3;
          config.mood = 14;
          sketch.generateFromConfig(config);

          expect(sketch.getWarnings().some((warning) => warning.includes('Mood'))).toBe(true);
        } finally {
          sketch.destroy();
        }
      });
    });
  });

  describe('MidiSketch melody persistence', () => {
    it('round-trips a saved melody through the public JS API', () => {
      const sketch = new MidiSketch();
      try {
        const config = createDefaultConfig(0);
        config.seed = 12345;
        sketch.generateFromConfig(config);

        const original = sketch.getMelody();
        expect(original.notes.length).toBeGreaterThan(0);

        const replacement = {
          seed: 9876,
          notes: [
            { startTick: 0, duration: 480, pitch: 60, velocity: 100 },
            { startTick: 480, duration: 240, pitch: 64, velocity: 90 },
          ],
        };
        sketch.setMelody(replacement);
        expect(sketch.getMelody()).toEqual(replacement);

        sketch.setMelody(original);
        expect(sketch.getMelody()).toEqual(original);
      } finally {
        sketch.destroy();
      }
    });

    it('reports an unusable handle instead of failing to parse an empty payload', () => {
      const sketch = new MidiSketch();
      sketch.generateFromConfig({ ...createDefaultConfig(0), seed: 12345 });
      sketch.destroy();

      // A destroyed handle makes the C API hand back a null string; the wrapper
      // has to name that cause rather than surface a JSON syntax error.
      expect(() => sketch.getMelody()).toThrow(/No melody data available/);
      expect(() => sketch.getMelody()).not.toThrow(SyntaxError);
    });
  });

  describe('MidiSketch.getVocalPreviewMidi', () => {
    it('should return a compact vocal-and-bass preview after generation', () => {
      const sketch = new MidiSketch();
      try {
        const config = createDefaultConfig(0);
        config.seed = 12345;
        sketch.generateFromConfig(config);

        const fullMidi = sketch.getMidi();
        const previewMidi = sketch.getVocalPreviewMidi();
        expect(previewMidi).toBeInstanceOf(Uint8Array);
        expect(Array.from(previewMidi.subarray(0, 4))).toEqual([0x4d, 0x54, 0x68, 0x64]);
        expect(previewMidi.length).toBeLessThan(fullMidi.length);
      } finally {
        sketch.destroy();
      }
    });
  });

  describe('MidiSketch.getEvents', () => {
    it('should expose metadata, tempo changes, vocal style, and SE text events', () => {
      const sketch = new MidiSketch();
      try {
        const config = createDefaultConfig(0);
        config.seed = 12345;
        config.vocalStyle = 3;
        sketch.generateFromConfig(config);

        const events = sketch.getEvents();
        expect(events.metadata).toMatchObject({ style: 0, seed: 12345 });
        expect(events.metadata.blueprint).toBeGreaterThanOrEqual(0);
        expect(events.vocal_style).toBe(3);
        expect(events.tempo_map.length).toBeGreaterThan(0);
        expect(events.tracks.find((track) => track.name === 'SE')?.textEvents).toBeInstanceOf(
          Array,
        );
      } finally {
        sketch.destroy();
      }
    });
  });

  describe('generation errors', () => {
    it('uses the generation error message for a destroyed handle', () => {
      const sketch = new MidiSketch();
      sketch.destroy();

      expect(() => sketch.regenerateVocal(12345)).toThrow(
        'Vocal regeneration failed: Invalid parameter or invalid handle',
      );
    });
  });
});
