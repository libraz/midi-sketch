import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { WasmTestContext } from './test-helpers';

describe('MidiSketch WASM - New Parameters', () => {
  const ctx = new WasmTestContext();

  beforeAll(async () => {
    await ctx.init();
  });

  afterAll(() => {
    ctx.destroy();
  });

  describe('SE Enabled', () => {
    it('should generate with SE enabled (default)', () => {
      const result = ctx.generateFromConfig({
        seed: 12345,
        seEnabled: true,
        callEnabled: true,
      });
      expect(result).toBe(0);
    });

    it('should generate with SE disabled', () => {
      const result = ctx.generateFromConfig({
        seed: 12345,
        seEnabled: false,
        callEnabled: true,
      });
      expect(result).toBe(0);
    });
  });

  describe('Arrangement Growth', () => {
    it('should generate with LayerAdd (default)', () => {
      const result = ctx.generateFromConfig({
        seed: 55555,
        arrangementGrowth: 0, // LayerAdd
      });
      expect(result).toBe(0);
    });

    it('should generate with RegisterAdd', () => {
      const result = ctx.generateFromConfig({
        seed: 55555,
        arrangementGrowth: 1, // RegisterAdd
      });
      expect(result).toBe(0);
    });
  });

  describe('Arpeggio Sync Chord', () => {
    it('should generate with sync_chord enabled (default)', () => {
      const result = ctx.generateFromConfig({
        seed: 33333,
        arpeggioEnabled: true,
        arpeggioSyncChord: true,
      });
      expect(result).toBe(0);
    });

    it('should generate with sync_chord disabled', () => {
      const result = ctx.generateFromConfig({
        seed: 33333,
        arpeggioEnabled: true,
        arpeggioSyncChord: false,
      });
      expect(result).toBe(0);
    });
  });

  describe('Motif Parameters', () => {
    it('should generate with FullSong repeat scope', () => {
      const result = ctx.generateFromConfig({
        seed: 88888,
        compositionStyle: 1, // BackgroundMotif
        motifRepeatScope: 0, // FullSong
      });
      expect(result).toBe(0);
    });

    it('should generate with Section repeat scope', () => {
      const result = ctx.generateFromConfig({
        seed: 88888,
        compositionStyle: 1, // BackgroundMotif
        motifRepeatScope: 1, // Section
      });
      expect(result).toBe(0);
    });

    it('should generate with max chord count limit', () => {
      const result = ctx.generateFromConfig({
        seed: 77777,
        compositionStyle: 1, // BackgroundMotif
        motifMaxChordCount: 2,
      });
      expect(result).toBe(0);
    });

    it('should generate with fixed progression', () => {
      const result = ctx.generateFromConfig({
        seed: 77777,
        compositionStyle: 1, // BackgroundMotif
        motifFixedProgression: true,
      });
      expect(result).toBe(0);
    });
  });

  describe('Vocal Density Parameters', () => {
    it('should generate with min note division = quarter notes', () => {
      const result = ctx.generateFromConfig({
        seed: 44444,
        vocalMinNoteDivision: 4, // Quarter notes only
      });
      expect(result).toBe(0);
    });

    it('should generate with high rest ratio', () => {
      const result = ctx.generateFromConfig({
        seed: 44444,
        vocalRestRatio: 40, // 40% rest
      });
      expect(result).toBe(0);
    });

    it('should generate with extreme leap allowed', () => {
      const result = ctx.generateFromConfig({
        seed: 44444,
        vocalAllowExtremLeap: true,
      });
      expect(result).toBe(0);
    });

    it('should generate with high note density', () => {
      const result = ctx.generateFromConfig({
        seed: 44444,
        vocalNoteDensity: 150, // High density (vocaloid-like)
      });
      expect(result).toBe(0);
    });
  });

  describe('Combined Parameters', () => {
    it('should generate with multiple new parameters combined', () => {
      const result = ctx.generateFromConfig({
        seed: 99999,
        compositionStyle: 1, // BackgroundMotif
        arrangementGrowth: 1, // RegisterAdd
        arpeggioEnabled: true,
        arpeggioSyncChord: false,
        motifRepeatScope: 1, // Section
        motifMaxChordCount: 3,
        vocalMinNoteDivision: 8,
        vocalRestRatio: 20,
      });
      expect(result).toBe(0);
    });
  });
});
