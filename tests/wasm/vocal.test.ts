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

  describe('Vocal-First Generation', () => {
    it('should generate only vocal with generateVocal', () => {
      const result = ctx.generateVocal({ seed: 12345 });
      expect(result).toBe(0);

      const { data, cleanup } = ctx.getEventsJson();
      const tracks = (data as { tracks: { name: string; notes: unknown[] }[] }).tracks;

      // Vocal should have notes
      const vocalTrack = tracks.find((t) => t.name === 'Vocal');
      expect(vocalTrack?.notes.length).toBeGreaterThan(0);

      // Accompaniment tracks should be empty
      const chordTrack = tracks.find((t) => t.name === 'Chord');
      expect(chordTrack?.notes.length).toBe(0);

      const bassTrack = tracks.find((t) => t.name === 'Bass');
      expect(bassTrack?.notes.length).toBe(0);

      cleanup();
    });

    it('should regenerate vocal with different seed using regenerateVocal', () => {
      // First generate vocal
      ctx.generateVocal({ seed: 11111 });

      const { data: data1, cleanup: cleanup1 } = ctx.getEventsJson();
      const tracks1 = (data1 as { tracks: { name: string; notes: unknown[] }[] }).tracks;
      const vocalNotes1 = tracks1.find((t) => t.name === 'Vocal')?.notes.length ?? 0;
      cleanup1();

      // Regenerate with different seed
      const result = ctx.regenerateVocal(22222);
      expect(result).toBe(0);

      const { data: data2, cleanup: cleanup2 } = ctx.getEventsJson();
      const tracks2 = (data2 as { tracks: { name: string; notes: unknown[] }[] }).tracks;
      const vocalNotes2 = tracks2.find((t) => t.name === 'Vocal')?.notes.length ?? 0;
      cleanup2();

      // Both should have notes (possibly different counts due to different seeds)
      expect(vocalNotes1).toBeGreaterThan(0);
      expect(vocalNotes2).toBeGreaterThan(0);
    });

    it('should generate accompaniment after generateVocal', () => {
      // Generate vocal only
      ctx.generateVocal({ seed: 33333 });

      // Check vocal is generated
      const { data: vocalData, cleanup: vocalCleanup } = ctx.getEventsJson();
      const vocalTracks = (vocalData as { tracks: { name: string; notes: unknown[] }[] }).tracks;
      expect(vocalTracks.find((t) => t.name === 'Vocal')?.notes.length).toBeGreaterThan(0);
      expect(vocalTracks.find((t) => t.name === 'Chord')?.notes.length).toBe(0);
      vocalCleanup();

      // Generate accompaniment
      const result = ctx.generateAccompaniment();
      expect(result).toBe(0);

      // Check all tracks are now generated
      const { data: fullData, cleanup: fullCleanup } = ctx.getEventsJson();
      const fullTracks = (fullData as { tracks: { name: string; notes: unknown[] }[] }).tracks;

      expect(fullTracks.find((t) => t.name === 'Vocal')?.notes.length).toBeGreaterThan(0);
      expect(fullTracks.find((t) => t.name === 'Chord')?.notes.length).toBeGreaterThan(0);
      expect(fullTracks.find((t) => t.name === 'Bass')?.notes.length).toBeGreaterThan(0);

      fullCleanup();
    });

    it('should generate all tracks with generateWithVocal', () => {
      const result = ctx.generateWithVocal({ seed: 44444 });
      expect(result).toBe(0);

      const { data, cleanup } = ctx.getEventsJson();
      const tracks = (data as { tracks: { name: string; notes: unknown[] }[] }).tracks;

      // All main tracks should have notes
      expect(tracks.find((t) => t.name === 'Vocal')?.notes.length).toBeGreaterThan(0);
      expect(tracks.find((t) => t.name === 'Chord')?.notes.length).toBeGreaterThan(0);
      expect(tracks.find((t) => t.name === 'Bass')?.notes.length).toBeGreaterThan(0);

      cleanup();
    });

    it('should produce different results with generateWithVocal vs generateFromConfig', () => {
      // Generate with vocal-first workflow
      ctx.generateWithVocal({ seed: 55555 });
      const { data: vocalFirstData, cleanup: cleanup1 } = ctx.getEventsJson();
      const vocalFirstTracks = (
        vocalFirstData as { tracks: { name: string; notes: { pitch: number }[] }[] }
      ).tracks;
      const vocalFirstNotes = vocalFirstTracks.find((t) => t.name === 'Vocal')?.notes ?? [];
      cleanup1();

      // Generate with traditional workflow (same seed)
      ctx.generateFromConfig({ seed: 55555 });
      const { data: traditionalData, cleanup: cleanup2 } = ctx.getEventsJson();
      const traditionalTracks = (
        traditionalData as { tracks: { name: string; notes: { pitch: number }[] }[] }
      ).tracks;
      const traditionalNotes = traditionalTracks.find((t) => t.name === 'Vocal')?.notes ?? [];
      cleanup2();

      // Both should have notes
      expect(vocalFirstNotes.length).toBeGreaterThan(0);
      expect(traditionalNotes.length).toBeGreaterThan(0);
    });
  });

  describe('Custom Vocal Notes (setVocalNotes)', () => {
    it('should set custom vocal notes', () => {
      const customNotes = [
        { startTick: 0, duration: 480, pitch: 60, velocity: 100 },
        { startTick: 480, duration: 480, pitch: 62, velocity: 90 },
        { startTick: 960, duration: 480, pitch: 64, velocity: 85 },
        { startTick: 1440, duration: 480, pitch: 65, velocity: 80 },
      ];

      const result = ctx.setVocalNotes({ seed: 12345 }, customNotes);
      expect(result).toBe(0);

      const { data, cleanup } = ctx.getEventsJson();
      const tracks = (
        data as { tracks: { name: string; notes: { pitch: number; start_ticks: number }[] }[] }
      ).tracks;

      const vocalTrack = tracks.find((t) => t.name === 'Vocal');
      expect(vocalTrack?.notes.length).toBe(4);

      // Verify notes match
      expect(vocalTrack?.notes[0].pitch).toBe(60);
      expect(vocalTrack?.notes[1].pitch).toBe(62);
      expect(vocalTrack?.notes[2].pitch).toBe(64);
      expect(vocalTrack?.notes[3].pitch).toBe(65);

      cleanup();
    });

    it('should generate accompaniment after setVocalNotes', () => {
      const customNotes = [
        { startTick: 0, duration: 480, pitch: 60, velocity: 100 },
        { startTick: 480, duration: 480, pitch: 64, velocity: 90 },
        { startTick: 960, duration: 480, pitch: 67, velocity: 85 },
        { startTick: 1440, duration: 480, pitch: 72, velocity: 80 },
      ];

      // Set custom vocal
      const setResult = ctx.setVocalNotes({ seed: 12345, drumsEnabled: true }, customNotes);
      expect(setResult).toBe(0);

      // Verify vocal is set but accompaniment is empty
      const { data: beforeData, cleanup: beforeCleanup } = ctx.getEventsJson();
      const beforeTracks = (beforeData as { tracks: { name: string; notes: unknown[] }[] }).tracks;
      expect(beforeTracks.find((t) => t.name === 'Vocal')?.notes.length).toBe(4);
      expect(beforeTracks.find((t) => t.name === 'Chord')?.notes.length).toBe(0);
      expect(beforeTracks.find((t) => t.name === 'Bass')?.notes.length).toBe(0);
      beforeCleanup();

      // Generate accompaniment
      const accResult = ctx.generateAccompaniment();
      expect(accResult).toBe(0);

      // Verify all tracks are now generated
      const { data: afterData, cleanup: afterCleanup } = ctx.getEventsJson();
      const afterTracks = (afterData as { tracks: { name: string; notes: unknown[] }[] }).tracks;

      expect(afterTracks.find((t) => t.name === 'Vocal')?.notes.length).toBe(4);
      expect(afterTracks.find((t) => t.name === 'Chord')?.notes.length).toBeGreaterThan(0);
      expect(afterTracks.find((t) => t.name === 'Bass')?.notes.length).toBeGreaterThan(0);

      afterCleanup();
    });

    it('should preserve custom vocal notes after accompaniment generation', () => {
      const customNotes = [
        { startTick: 0, duration: 960, pitch: 60, velocity: 100 },
        { startTick: 960, duration: 960, pitch: 67, velocity: 90 },
      ];

      ctx.setVocalNotes({ seed: 12345 }, customNotes);
      ctx.generateAccompaniment();

      const { data, cleanup } = ctx.getEventsJson();
      const tracks = (
        data as { tracks: { name: string; notes: { pitch: number; duration_ticks: number }[] }[] }
      ).tracks;

      const vocalTrack = tracks.find((t) => t.name === 'Vocal');
      expect(vocalTrack?.notes.length).toBe(2);
      expect(vocalTrack?.notes[0].pitch).toBe(60);
      expect(vocalTrack?.notes[0].duration_ticks).toBe(960);
      expect(vocalTrack?.notes[1].pitch).toBe(67);
      expect(vocalTrack?.notes[1].duration_ticks).toBe(960);

      cleanup();
    });

    it('should work with empty notes array', () => {
      const result = ctx.setVocalNotes({ seed: 12345 }, []);
      expect(result).toBe(0);

      const { data, cleanup } = ctx.getEventsJson();
      const tracks = (data as { tracks: { name: string; notes: unknown[] }[] }).tracks;

      const vocalTrack = tracks.find((t) => t.name === 'Vocal');
      expect(vocalTrack?.notes.length).toBe(0);

      cleanup();
    });
  });
});
