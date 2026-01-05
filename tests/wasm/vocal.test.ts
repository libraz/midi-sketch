import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { WasmTestContext } from './test-helpers';

describe('MidiSketch WASM - Vocal', () => {
  const ctx = new WasmTestContext();

  beforeAll(async () => {
    await ctx.init();
  });

  afterAll(() => {
    ctx.destroy();
  });

  describe('skipVocal', () => {
    it('should generate BGM without vocal when skipVocal is true', () => {
      const result = ctx.generateFromConfig({ seed: 12345, skipVocal: true });
      expect(result).toBe(0);

      const { data, cleanup } = ctx.getEventsJson();
      const tracks = (data as { tracks: { name: string; notes: unknown[] }[] }).tracks;

      const vocalTrack = tracks.find((t) => t.name === 'Vocal');
      expect(vocalTrack?.notes.length).toBe(0);

      const chordTrack = tracks.find((t) => t.name === 'Chord');
      expect(chordTrack?.notes.length).toBeGreaterThan(0);

      cleanup();
    });
  });

  describe('regenerateVocal', () => {
    it('should regenerate vocal with params', () => {
      // First generate BGM without vocal
      ctx.generateFromConfig({ seed: 12345, skipVocal: true });

      // Then regenerate vocal
      const regenerateVocal = ctx.module.cwrap('midisketch_regenerate_vocal', 'number', [
        'number',
        'number',
      ]) as (h: number, paramsPtr: number) => number;

      const paramsPtr = ctx.allocVocalParams({
        seed: 54321,
        vocalLow: 55,
        vocalHigh: 74,
        vocalAttitude: 1, // Expressive
      });

      const result = regenerateVocal(ctx.handle, paramsPtr);
      ctx.module._free(paramsPtr);

      expect(result).toBe(0); // MIDISKETCH_OK
    });

    it('should change vocal range after regeneration', () => {
      // Generate BGM without vocal
      ctx.generateFromConfig({ seed: 12345, skipVocal: true });

      // Regenerate with restricted range
      const regenerateVocal = ctx.module.cwrap('midisketch_regenerate_vocal', 'number', [
        'number',
        'number',
      ]) as (h: number, paramsPtr: number) => number;

      const paramsPtr = ctx.allocVocalParams({
        seed: 11111,
        vocalLow: 65,
        vocalHigh: 72,
        vocalAttitude: 0, // Clean
      });

      const result = regenerateVocal(ctx.handle, paramsPtr);
      ctx.module._free(paramsPtr);
      expect(result).toBe(0);

      // Get events and check vocal notes
      const { data, cleanup } = ctx.getEventsJson();
      const tracks = (data as { tracks: { name: string; notes: { pitch: number }[] }[] }).tracks;

      const vocalTrack = tracks.find((t) => t.name === 'Vocal');
      expect(vocalTrack).toBeDefined();
      expect(vocalTrack!.notes.length).toBeGreaterThan(0);

      // Most notes should be within range
      const inRangeNotes = vocalTrack!.notes.filter((note) => note.pitch >= 65 && note.pitch <= 72);
      const inRangeRatio = inRangeNotes.length / vocalTrack!.notes.length;
      expect(inRangeRatio).toBeGreaterThan(0.8); // At least 80% within range

      cleanup();
    });

    it('should apply vocal density parameters', () => {
      // Generate BGM without vocal
      ctx.generateFromConfig({ seed: 12345, skipVocal: true });

      const regenerateVocal = ctx.module.cwrap('midisketch_regenerate_vocal', 'number', [
        'number',
        'number',
      ]) as (h: number, paramsPtr: number) => number;

      // Regenerate with high density
      const highDensityParams = ctx.allocVocalParams({
        seed: 33333,
        vocalLow: 55,
        vocalHigh: 74,
        vocalAttitude: 0,
        vocalNoteDensity: 150, // 1.5 * 100
        vocalMinNoteDivision: 16,
        vocalRestRatio: 5, // 0.05 * 100
        vocalAllowExtremLeap: true,
      });

      let result = regenerateVocal(ctx.handle, highDensityParams);
      ctx.module._free(highDensityParams);
      expect(result).toBe(0);

      const { data: highData, cleanup: cleanupHigh } = ctx.getEventsJson();
      const highTracks = (highData as { tracks: { name: string; notes: unknown[] }[] }).tracks;
      const highVocalNotes = highTracks.find((t) => t.name === 'Vocal')?.notes.length ?? 0;
      cleanupHigh();

      // Regenerate with low density
      const lowDensityParams = ctx.allocVocalParams({
        seed: 33333, // Same seed
        vocalLow: 55,
        vocalHigh: 74,
        vocalAttitude: 0,
        vocalNoteDensity: 40, // 0.4 * 100
        vocalMinNoteDivision: 4,
        vocalRestRatio: 40, // 0.4 * 100
        vocalAllowExtremLeap: false,
      });

      result = regenerateVocal(ctx.handle, lowDensityParams);
      ctx.module._free(lowDensityParams);
      expect(result).toBe(0);

      const { data: lowData, cleanup: cleanupLow } = ctx.getEventsJson();
      const lowTracks = (lowData as { tracks: { name: string; notes: unknown[] }[] }).tracks;
      const lowVocalNotes = lowTracks.find((t) => t.name === 'Vocal')?.notes.length ?? 0;
      cleanupLow();

      // High density should produce more notes
      expect(highVocalNotes).toBeGreaterThan(lowVocalNotes);
    });

    it('should use default density when parameters are zero', () => {
      // Generate BGM without vocal
      ctx.generateFromConfig({ seed: 44444, skipVocal: true });

      const regenerateVocal = ctx.module.cwrap('midisketch_regenerate_vocal', 'number', [
        'number',
        'number',
      ]) as (h: number, paramsPtr: number) => number;

      // Regenerate with default density (all zeros = use style default)
      const defaultParams = ctx.allocVocalParams({
        seed: 55555,
        vocalLow: 55,
        vocalHigh: 74,
        vocalAttitude: 0,
        vocalNoteDensity: 0, // Use style default
        vocalMinNoteDivision: 0, // Use style default
        vocalRestRatio: 15, // Default
        vocalAllowExtremLeap: false,
      });

      const result = regenerateVocal(ctx.handle, defaultParams);
      ctx.module._free(defaultParams);
      expect(result).toBe(0);

      const { data, cleanup } = ctx.getEventsJson();
      const tracks = (data as { tracks: { name: string; notes: unknown[] }[] }).tracks;
      const vocalTrack = tracks.find((t) => t.name === 'Vocal');

      expect(vocalTrack?.notes.length).toBeGreaterThan(0);
      cleanup();
    });
  });
});
