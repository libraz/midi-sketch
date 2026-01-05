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
  });
});
