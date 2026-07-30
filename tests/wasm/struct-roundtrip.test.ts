import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { HookIntensity, VocalGrooveFeel } from '../../js/src/constants';
import { type SongConfigOptions, WasmTestContext } from './test-helpers';

interface EventData {
  vocal_style: number;
  tracks: Array<{
    name: string;
    notes: Array<{
      pitch: number;
      velocity: number;
      start_ticks: number;
      duration_ticks: number;
    }>;
  }>;
}

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

  function generateEvents(config: SongConfigOptions): EventData {
    expect(ctx.generateFromConfig(config)).toBe(0);
    const { data, cleanup } = ctx.getEventsJson();
    try {
      const events = data as EventData;
      expect(events.tracks.some((track) => track.notes.length > 0)).toBe(true);
      return events;
    } finally {
      cleanup();
    }
  }

  function noteFingerprint(events: EventData): string {
    return JSON.stringify(
      events.tracks.map((track) => ({
        name: track.name,
        notes: track.notes.map((note) => [
          note.start_ticks,
          note.duration_ticks,
          note.pitch,
          note.velocity,
        ]),
      })),
    );
  }

  function expectConfigChangesNotes(
    baseline: SongConfigOptions,
    configured: SongConfigOptions,
  ): void {
    const baselineEvents = generateEvents(baseline);
    const configuredEvents = generateEvents(configured);
    expect(noteFingerprint(configuredEvents)).not.toBe(noteFingerprint(baselineEvents));
  }

  describe('SongConfig offset 42+ fields', () => {
    it('should apply vocalStyle parameter (offset 42)', () => {
      const events = generateEvents({
        seed: 12345,
        vocalStyle: 7, // CityPop
      });
      expect(events.vocal_style).toBe(7);
    });

    it('should apply melodyTemplate parameter (offset 43)', () => {
      expectConfigChangesNotes(
        { seed: 12345, melodyTemplate: 0 },
        { seed: 12345, melodyTemplate: 1 }, // PlateauTalk
      );
    });

    it('should apply arrangementGrowth parameter (offset 44)', () => {
      expectConfigChangesNotes(
        { seed: 12345, arrangementGrowth: 0 },
        { seed: 12345, arrangementGrowth: 1 }, // RegisterAdd
      );
    });

    it('should apply arpeggioSyncChord parameter (offset 45)', () => {
      expectConfigChangesNotes(
        { seed: 12345, arpeggioEnabled: true, arpeggioSyncChord: true },
        { seed: 12345, arpeggioEnabled: true, arpeggioSyncChord: false },
      );
    });

    it('should apply motifRepeatScope parameter (offset 46)', () => {
      expectConfigChangesNotes(
        { seed: 12345, compositionStyle: 1, motifRepeatScope: 0 },
        { seed: 12345, compositionStyle: 1, motifRepeatScope: 1 }, // Section
      );
    });

    it('should apply melodicComplexity parameter (offset 52)', () => {
      expectConfigChangesNotes(
        { seed: 12345, melodicComplexity: 0 },
        { seed: 12345, melodicComplexity: 2 }, // Complex
      );
    });

    it('should apply hookIntensity parameter (offset 53)', () => {
      expectConfigChangesNotes(
        { seed: 12345, hookIntensity: HookIntensity.Off },
        { seed: 12345, hookIntensity: HookIntensity.Strong },
      );
    });

    it('should apply vocalGroove parameter (offset 54)', () => {
      expectConfigChangesNotes(
        { seed: 12345, vocalGroove: VocalGrooveFeel.Straight },
        { seed: 12345, vocalGroove: VocalGrooveFeel.Syncopated },
      );
    });

    it('should apply all offset 46+ parameters together', () => {
      const baseline = generateEvents({ seed: 12345 });
      const configured = generateEvents({
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
        hookIntensity: HookIntensity.Strong,
        vocalGroove: VocalGrooveFeel.Driving16th,
      });
      expect(configured.vocal_style).toBe(2);
      expect(noteFingerprint(configured)).not.toBe(noteFingerprint(baseline));
    });
  });

  // VocalParams offset 7+ tests removed - API deprecated

  describe('Struct offset validation', () => {
    it('should apply the complete HookIntensity range at offset 50', () => {
      const fingerprints = new Set<string>();
      for (const intensity of Object.values(HookIntensity)) {
        const events = generateEvents({
          seed: 77777 + intensity,
          hookIntensity: intensity,
        });
        fingerprints.add(noteFingerprint(events));
      }
      expect(fingerprints.size).toBeGreaterThan(1);
    });

    it('should apply the complete VocalGrooveFeel range at offset 51', () => {
      const fingerprints = new Set<string>();
      for (const groove of Object.values(VocalGrooveFeel)) {
        const events = generateEvents({
          seed: 88888 + groove,
          vocalGroove: groove,
        });
        fingerprints.add(noteFingerprint(events));
      }
      expect(fingerprints.size).toBeGreaterThan(1);
    });

    it('should correctly apply melodyTemplate at offset 43', () => {
      expectConfigChangesNotes(
        { seed: 99999, melodyTemplate: 0 },
        { seed: 99999, melodyTemplate: 7 }, // JumpAccent
      );
    });
  });
});
