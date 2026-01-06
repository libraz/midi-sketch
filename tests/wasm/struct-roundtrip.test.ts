import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { WasmTestContext } from './test-helpers';

// Tests for struct offset alignment after the vocalStyle field fix.
// These tests verify that offset 46+ (SongConfig) and offset 7+ (VocalParams) fields work correctly.
describe('Struct Roundtrip Tests', () => {
  const ctx = new WasmTestContext();

  beforeAll(async () => {
    await ctx.init();
  });

  afterAll(() => {
    ctx.destroy();
  });

  describe('SongConfig offset 46+ fields', () => {
    it('should accept vocalStyle parameter', () => {
      const result = ctx.generateFromConfig({
        seed: 12345,
        vocalStyle: 3, // CityPop
      });
      expect(result).toBe(0);
    });

    it('should accept arrangementGrowth parameter (offset 47)', () => {
      const result = ctx.generateFromConfig({
        seed: 12345,
        arrangementGrowth: 1, // RegisterAdd
      });
      expect(result).toBe(0);
    });

    it('should accept arpeggioSyncChord parameter (offset 48)', () => {
      const result = ctx.generateFromConfig({
        seed: 12345,
        arpeggioEnabled: true,
        arpeggioSyncChord: false,
      });
      expect(result).toBe(0);
    });

    it('should accept motifRepeatScope parameter (offset 49)', () => {
      const result = ctx.generateFromConfig({
        seed: 12345,
        compositionStyle: 1, // BackgroundMotif
        motifRepeatScope: 1, // Section
      });
      expect(result).toBe(0);
    });

    it('should accept melodicComplexity parameter (offset 52)', () => {
      const result = ctx.generateFromConfig({
        seed: 12345,
        melodicComplexity: 2, // Complex
      });
      expect(result).toBe(0);
    });

    it('should accept hookIntensity parameter (offset 53)', () => {
      const result = ctx.generateFromConfig({
        seed: 12345,
        hookIntensity: 3, // Strong
      });
      expect(result).toBe(0);
    });

    it('should accept vocalGroove parameter (offset 54)', () => {
      const result = ctx.generateFromConfig({
        seed: 12345,
        vocalGroove: 3, // Syncopated
      });
      expect(result).toBe(0);
    });

    it('should accept all offset 46+ parameters together', () => {
      const result = ctx.generateFromConfig({
        seed: 12345,
        vocalStyle: 2, // Vocaloid
        arrangementGrowth: 1,
        arpeggioEnabled: true,
        arpeggioSyncChord: false,
        compositionStyle: 1,
        motifRepeatScope: 1,
        motifFixedProgression: true,
        motifMaxChordCount: 3,
        melodicComplexity: 2,
        hookIntensity: 3,
        vocalGroove: 4,
      });
      expect(result).toBe(0);
    });
  });

  describe('VocalParams offset 7+ fields', () => {
    it('should regenerate vocal with vocalStyle parameter (offset 7)', () => {
      // First generate BGM only
      ctx.generateFromConfig({ seed: 11111, skipVocal: true });

      // Then regenerate vocal with vocalStyle
      const regenerateVocal = ctx.module.cwrap('midisketch_regenerate_vocal', 'number', [
        'number',
        'number',
      ]) as (h: number, p: number) => number;

      const paramsPtr = ctx.allocVocalParams({
        seed: 22222,
        vocalLow: 55,
        vocalHigh: 75,
        vocalAttitude: 1,
        vocalStyle: 2, // Vocaloid
      });

      const result = regenerateVocal(ctx.handle, paramsPtr);
      ctx.module._free(paramsPtr);
      expect(result).toBe(0);
    });

    it('should regenerate vocal with vocalNoteDensity (offset 8)', () => {
      ctx.generateFromConfig({ seed: 11111, skipVocal: true });

      const regenerateVocal = ctx.module.cwrap('midisketch_regenerate_vocal', 'number', [
        'number',
        'number',
      ]) as (h: number, p: number) => number;

      const paramsPtr = ctx.allocVocalParams({
        seed: 33333,
        vocalLow: 55,
        vocalHigh: 75,
        vocalAttitude: 0,
        vocalNoteDensity: 100, // Higher density
      });

      const result = regenerateVocal(ctx.handle, paramsPtr);
      ctx.module._free(paramsPtr);
      expect(result).toBe(0);
    });

    it('should regenerate vocal with all offset 7+ params', () => {
      ctx.generateFromConfig({ seed: 11111, skipVocal: true });

      const regenerateVocal = ctx.module.cwrap('midisketch_regenerate_vocal', 'number', [
        'number',
        'number',
      ]) as (h: number, p: number) => number;

      const paramsPtr = ctx.allocVocalParams({
        seed: 44444,
        vocalLow: 55,
        vocalHigh: 75,
        vocalAttitude: 1,
        vocalStyle: 3, // UltraVocaloid
        vocalNoteDensity: 150,
        vocalMinNoteDivision: 16,
        vocalRestRatio: 20,
        vocalAllowExtremLeap: true,
      });

      const result = regenerateVocal(ctx.handle, paramsPtr);
      ctx.module._free(paramsPtr);
      expect(result).toBe(0);
    });
  });
});
