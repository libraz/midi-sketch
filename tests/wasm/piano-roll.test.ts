/**
 * Piano Roll Safety API.
 *
 * These tests drive the published MidiSketch methods so that the cwrap
 * bindings and the struct offsets the package ships with are the ones under
 * test; a signature or layout mistake surfaces here rather than in production.
 */
import path from 'node:path';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import {
  createDefaultConfig,
  init,
  MAX_PIANO_ROLL_SAMPLES,
  MidiSketch,
  NoteReason,
  NoteSafety,
} from '../../js/src/index';

describe('MidiSketch WASM - Piano Roll Safety API', () => {
  let sketch: MidiSketch;

  beforeAll(async () => {
    await init({ wasmPath: path.resolve(__dirname, '../../dist/midisketch.wasm') });
    sketch = new MidiSketch();
    // Generate MIDI first so we have harmony context
    sketch.generateFromConfig({ ...createDefaultConfig(0), seed: 12345 });
  });

  afterAll(() => {
    sketch?.destroy();
  });

  describe('getPianoRollSafetyAt', () => {
    it('should return valid piano roll info', () => {
      const info = sketch.getPianoRollSafetyAt(0);

      expect(info).toBeDefined();
      expect(info.tick).toBe(0);
      expect(info.safety).toHaveLength(128);
      expect(info.reason).toHaveLength(128);
      expect(info.collision).toHaveLength(128);
      expect(info.recommended.length).toBeGreaterThan(0);
      expect(info.recommended.length).toBeLessThanOrEqual(8);
    });

    it('reuses the no-collision value across empty slots', () => {
      const info = sketch.getPianoRollSafetyAt(0);
      const emptySlots = info.collision.filter((entry) => entry.intervalSemitones === 0);

      expect(emptySlots.length).toBeGreaterThan(0);
      expect(new Set(emptySlots).size).toBe(1);
    });

    it('should identify chord tones as safe', () => {
      const info = sketch.getPianoRollSafetyAt(0);

      // Recommended notes should be chord tones (safe)
      for (const pitch of info.recommended) {
        expect(info.safety[pitch]).toBe(NoteSafety.Safe);
        expect(info.reason[pitch] & NoteReason.ChordTone).toBe(NoteReason.ChordTone);
      }
    });

    it('should identify notes outside range as dissonant', () => {
      const info = sketch.getPianoRollSafetyAt(0);

      // Notes below vocal range should be dissonant (out of range)
      // Default vocal_low is around 55-60
      expect(info.safety[20]).toBe(NoteSafety.Dissonant);
      expect(info.reason[20] & NoteReason.OutOfRange).toBe(NoteReason.OutOfRange);

      // Notes above vocal range should also be dissonant
      expect(info.safety[120]).toBe(NoteSafety.Dissonant);
    });

    it('should return chord degree and key', () => {
      const info = sketch.getPianoRollSafetyAt(0);

      // Chord degree should be valid (0-6 for diatonic chords)
      expect(info.chordDegree).toBeGreaterThanOrEqual(-1);
      expect(info.chordDegree).toBeLessThanOrEqual(6);

      // Current key should be in range 0-11
      expect(info.currentKey).toBeGreaterThanOrEqual(0);
      expect(info.currentKey).toBeLessThanOrEqual(11);
    });
  });

  describe('getPianoRollSafetyAt with a previous pitch', () => {
    it('should detect large leaps', () => {
      // Get info at tick 0 with a previous note very far away
      const infoWithLeap = sketch.getPianoRollSafetyAt(0, 40); // prev = F#2

      // High notes should have large leap warning when prev was very low
      // C5 (72) is 32 semitones away from F#2 (40)
      const highNote = 72;
      expect(infoWithLeap.reason[highNote] & NoteReason.LargeLeap).toBe(NoteReason.LargeLeap);
    });

    it('should not flag leaps for nearby notes', () => {
      // Get info at tick 0 with a previous note nearby
      const info = sketch.getPianoRollSafetyAt(0, 60); // prev = C4

      // Notes close to C4 should not have large leap flag
      const nearbyNote = 62; // D4, 2 semitones away
      expect(info.reason[nearbyNote] & NoteReason.LargeLeap).toBe(0);
    });
  });

  describe('reasonToString', () => {
    it('names the reason flags of a recommended note', () => {
      const info = sketch.getPianoRollSafetyAt(0);
      const pitch = info.recommended[0];

      expect(sketch.reasonToString(info.reason[pitch])).toContain('Chord tone');
    });
  });

  describe('collisionToString', () => {
    it('names the track, pitch and interval of a collision', () => {
      // TrackRole 2 is Bass; F3 (53) a minor 2nd away from the queried note.
      const text = sketch.collisionToString({
        trackRole: 2,
        collidingPitch: 53,
        intervalSemitones: 1,
      });

      expect(text).toContain('Bass');
      expect(text).toContain('F3');
      expect(text).toContain('minor 2nd');
    });

    it('returns an empty string for a slot that records no collision', () => {
      expect(
        sketch.collisionToString({ trackRole: 0, collidingPitch: 0, intervalSemitones: 0 }),
      ).toBe('');
    });

    it('describes the collisions the piano roll reports', () => {
      const colliding = sketch
        .getPianoRollSafety(0, 1920 * 16, 240)
        .flatMap((info) => info.collision)
        .filter((entry) => entry.intervalSemitones !== 0);

      expect(colliding.length).toBeGreaterThan(0);
      for (const entry of colliding) {
        expect(sketch.collisionToString(entry)).not.toBe('');
      }
    });
  });

  describe('getPianoRollSafety (batch)', () => {
    it('should return correct number of entries', () => {
      // Get safety for 4 beats (1 bar), step = 480 (quarter note)
      const infos = sketch.getPianoRollSafety(0, 1920, 480);

      // Should have 5 entries: 0, 480, 960, 1440, 1920
      expect(infos).toHaveLength(5);
    });

    it('should have correct tick values', () => {
      const infos = sketch.getPianoRollSafety(0, 960, 480);

      expect(infos[0].tick).toBe(0);
      expect(infos[1].tick).toBe(480);
      expect(infos[2].tick).toBe(960);
    });

    it('should have valid data for each tick', () => {
      const infos = sketch.getPianoRollSafety(0, 1920, 480);

      for (const info of infos) {
        expect(info.safety).toHaveLength(128);
        expect(info.reason).toHaveLength(128);
        expect(info.recommended.length).toBeGreaterThan(0);
      }
    });

    it('agrees with the single-tick query', () => {
      const batch = sketch.getPianoRollSafety(0, 1920, 480);

      for (const info of batch) {
        expect(sketch.getPianoRollSafetyAt(info.tick)).toEqual(info);
      }
    });

    it('rejects an oversized request without doing the work', () => {
      const oversized = MAX_PIANO_ROLL_SAMPLES + 50000;
      const started = performance.now();

      expect(() => sketch.getPianoRollSafety(0, oversized, 1)).toThrow(RangeError);

      // A request rejected up front costs nothing; one that reaches the core
      // fills and allocates a full capped batch before being discarded.
      expect(performance.now() - started).toBeLessThan(10);
    });

    it('rejects a non-positive step', () => {
      expect(() => sketch.getPianoRollSafety(0, 1920, 0)).toThrow(RangeError);
      expect(() => sketch.getPianoRollSafety(0, 1920, -480)).toThrow(RangeError);
    });

    it('rejects an inverted range', () => {
      expect(() => sketch.getPianoRollSafety(1920, 0, 480)).toThrow(RangeError);
    });

    it('draws the limit at the requested sample count, not one below it', () => {
      // The core clamps endTick to the song length, so these span the guard's
      // boundary without asking it to sample a whole song at tick resolution.
      const atLimit = (MAX_PIANO_ROLL_SAMPLES - 1) * 480;

      expect(() => sketch.getPianoRollSafety(0, atLimit, 480)).not.toThrow();
      expect(() => sketch.getPianoRollSafety(0, atLimit + 480, 480)).toThrow(RangeError);
    });
  });

  describe('chord progression changes', () => {
    it('reports more than one chord across the first eight bars', () => {
      // A progression that never changes would make every safety query
      // identical, so the recommended notes have to vary across bars.
      const bars = sketch.getPianoRollSafety(0, 1920 * 7, 1920);
      const recommendedSets = new Set(bars.map((info) => info.recommended.join(',')));
      const degrees = new Set(bars.map((info) => info.chordDegree));

      expect(bars).toHaveLength(8);
      expect(degrees.size).toBeGreaterThan(1);
      expect(recommendedSets.size).toBeGreaterThan(1);
    });
  });
});
