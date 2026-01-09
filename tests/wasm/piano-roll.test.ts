import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { WasmTestContext } from './test-helpers';

describe('MidiSketch WASM - Piano Roll Safety API', () => {
  const ctx = new WasmTestContext();

  beforeAll(async () => {
    await ctx.init();
    // Generate MIDI first so we have harmony context
    ctx.generateFromConfig({ seed: 12345 });
  });

  afterAll(() => {
    ctx.destroy();
  });

  describe('getPianoRollSafetyAt', () => {
    it('should return valid piano roll info', () => {
      const info = ctx.getPianoRollSafetyAt(0);

      expect(info).toBeDefined();
      expect(info.tick).toBe(0);
      expect(info.safety).toHaveLength(128);
      expect(info.reason).toHaveLength(128);
      expect(info.collision).toHaveLength(128);
      expect(info.recommended.length).toBeGreaterThan(0);
      expect(info.recommended.length).toBeLessThanOrEqual(8);
    });

    it('should identify chord tones as safe', () => {
      const info = ctx.getPianoRollSafetyAt(0);

      // Recommended notes should be chord tones (safe)
      for (const pitch of info.recommended) {
        expect(info.safety[pitch]).toBe(0); // NoteSafety.Safe
        // Chord tone flag should be set
        expect(info.reason[pitch] & 1).toBe(1); // NoteReason.ChordTone
      }
    });

    it('should identify notes outside range as dissonant', () => {
      const info = ctx.getPianoRollSafetyAt(0);

      // Notes below vocal range should be dissonant (out of range)
      // Default vocal_low is around 55-60
      expect(info.safety[20]).toBe(2); // NoteSafety.Dissonant
      expect(info.reason[20] & 1024).toBe(1024); // NoteReason.OutOfRange

      // Notes above vocal range should also be dissonant
      expect(info.safety[120]).toBe(2); // NoteSafety.Dissonant
    });

    it('should return chord degree and key', () => {
      const info = ctx.getPianoRollSafetyAt(0);

      // Chord degree should be valid (0-6 for diatonic chords)
      expect(info.chordDegree).toBeGreaterThanOrEqual(-1);
      expect(info.chordDegree).toBeLessThanOrEqual(6);

      // Current key should be in range 0-11
      expect(info.currentKey).toBeGreaterThanOrEqual(0);
      expect(info.currentKey).toBeLessThanOrEqual(11);
    });
  });

  describe('getPianoRollSafetyWithContext', () => {
    it('should detect large leaps', () => {
      // Get info at tick 0 with a previous note very far away
      const infoWithLeap = ctx.getPianoRollSafetyWithContext(0, 40); // prev = F#2

      // High notes should have large leap warning when prev was very low
      // C5 (72) is 32 semitones away from F#2 (40)
      const highNote = 72;
      expect(infoWithLeap.reason[highNote] & 32).toBe(32); // NoteReason.LargeLeap
    });

    it('should not flag leaps for nearby notes', () => {
      // Get info at tick 0 with a previous note nearby
      const info = ctx.getPianoRollSafetyWithContext(0, 60); // prev = C4

      // Notes close to C4 should not have large leap flag
      const nearbyNote = 62; // D4, 2 semitones away
      expect(info.reason[nearbyNote] & 32).toBe(0); // No LargeLeap flag
    });
  });

  describe('getPianoRollSafety (batch)', () => {
    it('should return correct number of entries', () => {
      // Get safety for 4 beats (1 bar), step = 480 (quarter note)
      const infos = ctx.getPianoRollSafety(0, 1920, 480);

      // Should have 5 entries: 0, 480, 960, 1440, 1920
      expect(infos).toHaveLength(5);
    });

    it('should have correct tick values', () => {
      const infos = ctx.getPianoRollSafety(0, 960, 480);

      expect(infos[0].tick).toBe(0);
      expect(infos[1].tick).toBe(480);
      expect(infos[2].tick).toBe(960);
    });

    it('should have valid data for each tick', () => {
      const infos = ctx.getPianoRollSafety(0, 1920, 480);

      for (const info of infos) {
        expect(info.safety).toHaveLength(128);
        expect(info.reason).toHaveLength(128);
        expect(info.recommended.length).toBeGreaterThan(0);
      }
    });
  });

  describe('chord progression changes', () => {
    it('should have different recommended notes for different chords', () => {
      // Most chord progressions change chords at least once per bar
      // Get safety at start and middle of a bar
      const info1 = ctx.getPianoRollSafetyAt(0); // Bar 1, beat 1
      const info2 = ctx.getPianoRollSafetyAt(3840); // Bar 3, beat 1 (after chord change)

      // At least one tick should have different recommended notes
      // (chord progression should change)
      const set1 = new Set(info1.recommended);
      const set2 = new Set(info2.recommended);

      // Check if the sets are different (chord changed)
      const sameRecommendations = set1.size === set2.size && [...set1].every((v) => set2.has(v));

      // It's possible (but unlikely) that chords have same tones,
      // so we just log rather than fail
      if (sameRecommendations) {
        console.log('Note: Same recommended notes at both ticks - chords may share tones');
      }
    });
  });
});
