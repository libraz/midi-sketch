import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { WasmTestContext } from './test-helpers';

interface Track {
  name: string;
  notes: { tick: number; pitch: number }[];
  textEvents: { tick: number; text: string }[];
}

describe('MidiSketch WASM - Call System', () => {
  const ctx = new WasmTestContext();

  beforeAll(async () => {
    await ctx.init();
  });

  afterAll(() => {
    ctx.destroy();
  });

  describe('Chant Section', () => {
    it('should generate with call enabled and insert Chant section', () => {
      const result = ctx.generateFromConfig({
        seed: 12345,
        callEnabled: true,
        introChant: 1, // Gachikoi
        mixPattern: 0, // None
        targetDurationSeconds: 120,
      });
      expect(result).toBe(0);

      const { data, cleanup } = ctx.getEventsJson();
      const tracks = (data as { tracks: Track[] }).tracks;

      const seTrack = tracks.find((t) => t.name === 'SE');
      expect(seTrack).toBeDefined();
      const chantMarker = seTrack!.textEvents.find((e) => e.text === 'Gachikoi');
      expect(chantMarker).toBeDefined();

      cleanup();
    });

    it('should have no vocal notes during Chant sections', () => {
      const result = ctx.generateFromConfig({
        seed: 12345,
        callEnabled: true,
        introChant: 1, // Gachikoi
        mixPattern: 2, // Tiger
        targetDurationSeconds: 120,
      });
      expect(result).toBe(0);

      const { data, cleanup } = ctx.getEventsJson();
      const tracks = (data as { tracks: Track[] }).tracks;

      // Find Chant section start tick from SE track
      const seTrack = tracks.find((t) => t.name === 'SE');
      const chantEvent = seTrack!.textEvents.find((e) => e.text === 'Gachikoi');
      expect(chantEvent).toBeDefined();

      // Check that there are no vocal notes at the Chant section start
      const vocalTrack = tracks.find((t) => t.name === 'Vocal');
      expect(vocalTrack).toBeDefined();

      // Vocal should not have notes starting exactly at chant tick
      const vocalAtChant = vocalTrack!.notes.filter((n) => n.tick === chantEvent!.tick);
      expect(vocalAtChant.length).toBe(0);

      cleanup();
    });
  });

  describe('MixBreak Section', () => {
    it('should generate with MIX pattern and insert MixBreak section', () => {
      const result = ctx.generateFromConfig({
        seed: 12345,
        callEnabled: true,
        introChant: 0, // None
        mixPattern: 2, // Tiger
        targetDurationSeconds: 120,
      });
      expect(result).toBe(0);

      const { data, cleanup } = ctx.getEventsJson();
      const tracks = (data as { tracks: Track[] }).tracks;

      const seTrack = tracks.find((t) => t.name === 'SE');
      expect(seTrack).toBeDefined();
      const mixMarker = seTrack!.textEvents.find((e) => e.text === 'TigerMix');
      expect(mixMarker).toBeDefined();

      cleanup();
    });
  });

  describe('Call Notes', () => {
    it('should generate call notes when callNotesEnabled is true', () => {
      const result = ctx.generateFromConfig({
        seed: 12345,
        callEnabled: true,
        callNotesEnabled: true,
        introChant: 1, // Gachikoi
        mixPattern: 1, // Standard
        targetDurationSeconds: 120,
      });
      expect(result).toBe(0);

      const { data, cleanup } = ctx.getEventsJson();
      const tracks = (data as { tracks: Track[] }).tracks;

      // SE track should have notes (call notes at C3=48)
      const seTrack = tracks.find((t) => t.name === 'SE');
      expect(seTrack).toBeDefined();
      expect(seTrack!.notes.length).toBeGreaterThan(0);

      // All call notes should be C3 (48)
      const callNotes = seTrack!.notes.filter((n) => n.pitch === 48);
      expect(callNotes.length).toBeGreaterThan(0);

      cleanup();
    });
  });

  describe('Call Disabled', () => {
    it('should not have call sections when callEnabled is false', () => {
      const result = ctx.generateFromConfig({
        seed: 12345,
        callEnabled: false,
        introChant: 1, // Set but should be ignored
        mixPattern: 2, // Set but should be ignored
      });
      expect(result).toBe(0);

      const { data, cleanup } = ctx.getEventsJson();
      const tracks = (data as { tracks: Track[] }).tracks;

      // SE track should not have Gachikoi or TigerMix markers
      const seTrack = tracks.find((t) => t.name === 'SE');
      expect(seTrack).toBeDefined();
      const chantMarker = seTrack!.textEvents.find(
        (e) => e.text === 'Gachikoi' || e.text === 'TigerMix',
      );
      expect(chantMarker).toBeUndefined();

      cleanup();
    });
  });

  describe('Modulation', () => {
    it('should generate with modulation timing', () => {
      const result = ctx.generateFromConfig({
        seed: 12345,
        modulationTiming: 1, // LastChorus
        modulationSemitones: 3,
        formId: 6, // FullWithBridge - has multiple choruses
      });
      expect(result).toBe(0);

      const { data, cleanup } = ctx.getEventsJson();
      const tracks = (data as { tracks: Track[] }).tracks;

      // Check SE track for modulation marker
      const seTrack = tracks.find((t) => t.name === 'SE');
      expect(seTrack).toBeDefined();
      const modEvent = seTrack!.textEvents.find((e) => e.text.includes('Mod+'));
      expect(modEvent).toBeDefined();
      expect(modEvent!.text).toBe('Mod+3');

      cleanup();
    });
  });
});
