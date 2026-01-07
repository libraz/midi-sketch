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

  describe('SongConfig offset 42+ fields', () => {
    it('should accept vocalStyle parameter (offset 42)', () => {
      const result = ctx.generateFromConfig({
        seed: 12345,
        vocalStyle: 3, // CityPop
      });
      expect(result).toBe(0);
    });

    it('should accept melodyTemplate parameter (offset 43)', () => {
      const result = ctx.generateFromConfig({
        seed: 12345,
        melodyTemplate: 1, // PlateauTalk
      });
      expect(result).toBe(0);
    });

    it('should accept arrangementGrowth parameter (offset 44)', () => {
      const result = ctx.generateFromConfig({
        seed: 12345,
        arrangementGrowth: 1, // RegisterAdd
      });
      expect(result).toBe(0);
    });

    it('should accept arpeggioSyncChord parameter (offset 45)', () => {
      const result = ctx.generateFromConfig({
        seed: 12345,
        arpeggioEnabled: true,
        arpeggioSyncChord: false,
      });
      expect(result).toBe(0);
    });

    it('should accept motifRepeatScope parameter (offset 46)', () => {
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

    it('should regenerate vocal with melodyTemplate (offset 8)', () => {
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
        melodyTemplate: 1, // PlateauTalk
      });

      const result = regenerateVocal(ctx.handle, paramsPtr);
      ctx.module._free(paramsPtr);
      expect(result).toBe(0);
    });

    it('should regenerate vocal with melodicComplexity (offset 9)', () => {
      ctx.generateFromConfig({ seed: 11111, skipVocal: true });

      const regenerateVocal = ctx.module.cwrap('midisketch_regenerate_vocal', 'number', [
        'number',
        'number',
      ]) as (h: number, p: number) => number;

      const paramsPtr = ctx.allocVocalParams({
        seed: 55555,
        vocalLow: 55,
        vocalHigh: 75,
        vocalAttitude: 0,
        melodicComplexity: 2, // Complex
      });

      const result = regenerateVocal(ctx.handle, paramsPtr);
      ctx.module._free(paramsPtr);
      expect(result).toBe(0);
    });

    it('should regenerate vocal with hookIntensity (offset 10)', () => {
      ctx.generateFromConfig({ seed: 11111, skipVocal: true });

      const regenerateVocal = ctx.module.cwrap('midisketch_regenerate_vocal', 'number', [
        'number',
        'number',
      ]) as (h: number, p: number) => number;

      const paramsPtr = ctx.allocVocalParams({
        seed: 66666,
        vocalLow: 55,
        vocalHigh: 75,
        vocalAttitude: 0,
        hookIntensity: 3, // Strong
      });

      const result = regenerateVocal(ctx.handle, paramsPtr);
      ctx.module._free(paramsPtr);
      expect(result).toBe(0);
    });

    it('should regenerate vocal with vocalGroove (offset 11)', () => {
      ctx.generateFromConfig({ seed: 11111, skipVocal: true });

      const regenerateVocal = ctx.module.cwrap('midisketch_regenerate_vocal', 'number', [
        'number',
        'number',
      ]) as (h: number, p: number) => number;

      const paramsPtr = ctx.allocVocalParams({
        seed: 77777,
        vocalLow: 55,
        vocalHigh: 75,
        vocalAttitude: 0,
        vocalGroove: 2, // Swing
      });

      const result = regenerateVocal(ctx.handle, paramsPtr);
      ctx.module._free(paramsPtr);
      expect(result).toBe(0);
    });

    it('should regenerate vocal with compositionStyle (offset 12)', () => {
      ctx.generateFromConfig({ seed: 11111, skipVocal: true });

      const regenerateVocal = ctx.module.cwrap('midisketch_regenerate_vocal', 'number', [
        'number',
        'number',
      ]) as (h: number, p: number) => number;

      const paramsPtr = ctx.allocVocalParams({
        seed: 88888,
        vocalLow: 55,
        vocalHigh: 75,
        vocalAttitude: 0,
        compositionStyle: 1, // BackgroundMotif
      });

      const result = regenerateVocal(ctx.handle, paramsPtr);
      ctx.module._free(paramsPtr);
      expect(result).toBe(0);
    });

    it('should regenerate vocal with all params including new fields', () => {
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
        melodyTemplate: 2, // RunUpTarget
        melodicComplexity: 2, // Complex
        hookIntensity: 3, // Strong
        vocalGroove: 4, // Driving16th
        compositionStyle: 0, // MelodyLead
      });

      const result = regenerateVocal(ctx.handle, paramsPtr);
      ctx.module._free(paramsPtr);
      expect(result).toBe(0);
    });
  });

  describe('Struct offset validation', () => {
    it('should accept all HookIntensity values at offset 50', () => {
      // Test all hook intensity values generate successfully
      for (const intensity of [0, 1, 2, 3]) {
        const result = ctx.generateFromConfig({
          seed: 77777 + intensity,
          hookIntensity: intensity,
        });
        expect(result).toBe(0);
      }
    });

    it('should accept all VocalGrooveFeel values at offset 51', () => {
      // Test all groove values generate successfully
      for (const groove of [0, 1, 2, 3, 4, 5]) {
        const result = ctx.generateFromConfig({
          seed: 88888 + groove,
          vocalGroove: groove,
        });
        expect(result).toBe(0);
      }
    });

    it('should correctly apply melodyTemplate at offset 43', () => {
      // Generate with melody_template = Auto (0)
      const result1 = ctx.generateFromConfig({
        seed: 99999,
        melodyTemplate: 0, // Auto
      });
      expect(result1).toBe(0);

      const { data: data1, cleanup: cleanup1 } = ctx.getEventsJson();
      const notes1 =
        (data1 as { tracks: { name: string; notes: { pitch: number }[] }[] }).tracks.find(
          (t) => t.name === 'Vocal',
        )?.notes ?? [];
      cleanup1();

      // Generate with melody_template = JumpAccent (7)
      const result2 = ctx.generateFromConfig({
        seed: 99999, // Same seed
        melodyTemplate: 7, // JumpAccent
      });
      expect(result2).toBe(0);

      const { data: data2, cleanup: cleanup2 } = ctx.getEventsJson();
      const notes2 =
        (data2 as { tracks: { name: string; notes: { pitch: number }[] }[] }).tracks.find(
          (t) => t.name === 'Vocal',
        )?.notes ?? [];
      cleanup2();

      // Different templates should produce different melodies
      let hasDifference = notes1.length !== notes2.length;
      if (!hasDifference) {
        for (let i = 0; i < notes1.length; i++) {
          if (notes1[i].pitch !== notes2[i].pitch) {
            hasDifference = true;
            break;
          }
        }
      }
      expect(hasDifference).toBe(true);
    });
  });
});
