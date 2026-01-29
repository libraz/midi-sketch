import path from 'node:path';
import { beforeAll, describe, expect, it } from 'vitest';
import { CompositionStyle, init, MidiSketch, SongConfigBuilder } from '../../js/src/index';

describe('SongConfigBuilder', () => {
  beforeAll(async () => {
    const wasmPath = path.resolve(__dirname, '../../dist/midisketch.wasm');
    await init({ wasmPath });
  });

  describe('constructor', () => {
    it('should create builder with default config for style 0', () => {
      const builder = new SongConfigBuilder(0);
      const config = builder.build();
      expect(config.stylePresetId).toBe(0);
    });

    it('should create builder with default config for style 1', () => {
      const builder = new SongConfigBuilder(1);
      const config = builder.build();
      expect(config.stylePresetId).toBe(1);
    });
  });

  describe('basic setters', () => {
    it('should set seed', () => {
      const builder = new SongConfigBuilder(0).setSeed(12345);
      const config = builder.build();
      expect(config.seed).toBe(12345);
    });

    it('should set key', () => {
      const builder = new SongConfigBuilder(0).setKey(5);
      const config = builder.build();
      expect(config.key).toBe(5);
    });

    it('should set chord progression', () => {
      const builder = new SongConfigBuilder(0).setChordProgression(3);
      const config = builder.build();
      expect(config.chordProgressionId).toBe(3);
    });

    it('should set form', () => {
      const builder = new SongConfigBuilder(0).setForm(2);
      const config = builder.build();
      expect(config.formId).toBe(2);
    });

    it('should set vocal range and normalize', () => {
      // Normal order
      const builder1 = new SongConfigBuilder(0).setVocalRange(55, 75);
      const config1 = builder1.build();
      expect(config1.vocalLow).toBe(55);
      expect(config1.vocalHigh).toBe(75);

      // Reversed order - should normalize
      const builder2 = new SongConfigBuilder(0).setVocalRange(75, 55);
      const config2 = builder2.build();
      expect(config2.vocalLow).toBe(55);
      expect(config2.vocalHigh).toBe(75);
    });

    it('should set vocal style', () => {
      const builder = new SongConfigBuilder(0).setVocalStyle(2);
      const config = builder.build();
      expect(config.vocalStyle).toBe(2);
    });

    it('should set humanize settings', () => {
      const builder = new SongConfigBuilder(0).setHumanize(true, 30, 40);
      const config = builder.build();
      expect(config.humanize).toBe(true);
      expect(config.humanizeTiming).toBe(30);
      expect(config.humanizeVelocity).toBe(40);
    });

    it('should set modulation settings', () => {
      const builder = new SongConfigBuilder(0).setModulation(1, 3);
      const config = builder.build();
      expect(config.modulationTiming).toBe(1);
      expect(config.modulationSemitones).toBe(3);
    });

    it('should set arpeggio settings', () => {
      const builder = new SongConfigBuilder(0).setArpeggio(true, {
        pattern: 2,
        speed: 1,
        octaveRange: 2,
        gate: 70,
        syncChord: true,
      });
      const config = builder.build();
      expect(config.arpeggioEnabled).toBe(true);
      expect(config.arpeggioPattern).toBe(2);
      expect(config.arpeggioSpeed).toBe(1);
      expect(config.arpeggioOctaveRange).toBe(2);
      expect(config.arpeggioGate).toBe(70);
      expect(config.arpeggioSyncChord).toBe(true);
    });

    it('should set chord extensions', () => {
      const builder = new SongConfigBuilder(0).setChordExtensions({
        sus: true,
        seventh: true,
        ninth: false,
        susProb: 30,
        seventhProb: 40,
      });
      const config = builder.build();
      expect(config.chordExtSus).toBe(true);
      expect(config.chordExt7th).toBe(true);
      expect(config.chordExt9th).toBe(false);
      expect(config.chordExtSusProb).toBe(30);
      expect(config.chordExt7thProb).toBe(40);
    });

    it('should set motif settings', () => {
      const builder = new SongConfigBuilder(0).setMotif({
        repeatScope: 1,
        fixedProgression: false,
        maxChordCount: 6,
      });
      const config = builder.build();
      expect(config.motifRepeatScope).toBe(1);
      expect(config.motifFixedProgression).toBe(false);
      expect(config.motifMaxChordCount).toBe(6);
    });

    it('should set call settings', () => {
      const builder = new SongConfigBuilder(0).setCall({
        enabled: true,
        notesEnabled: true,
        density: 2,
        introChant: 1,
        mixPattern: 1,
        seEnabled: true,
      });
      const config = builder.build();
      expect(config.callEnabled).toBe(true);
      expect(config.callNotesEnabled).toBe(true);
      expect(config.callDensity).toBe(2);
      expect(config.introChant).toBe(1);
      expect(config.mixPattern).toBe(1);
      expect(config.seEnabled).toBe(true);
    });
  });

  describe('fluent API chaining', () => {
    it('should support method chaining', () => {
      const config = new SongConfigBuilder(0)
        .setSeed(12345)
        .setKey(2)
        .setBpm(120)
        .setBlueprint(0)
        .setVocalRange(55, 75)
        .build();

      expect(config.seed).toBe(12345);
      expect(config.key).toBe(2);
      expect(config.bpm).toBe(120);
      expect(config.blueprintId).toBe(0);
      expect(config.vocalLow).toBe(55);
      expect(config.vocalHigh).toBe(75);
    });
  });

  describe('cascade detection - setBlueprint', () => {
    it('should detect RhythmSync blueprint enabling drums', () => {
      const builder = new SongConfigBuilder(0);
      // Start with drums disabled
      builder.setDrums(false);
      // Clear explicit flag for drums to allow cascade
      (builder as unknown as { explicitFields: Set<string> }).explicitFields.delete('drumsEnabled');

      builder.setBlueprint(1); // RhythmLock (RhythmSync paradigm)

      const result = builder.getLastChangeResult();
      expect(result).not.toBeNull();
      expect(result?.changes.some((c) => c.field === 'drumsEnabled')).toBe(true);

      const config = builder.build();
      expect(config.drumsEnabled).toBe(true);
    });

    it('should warn when RhythmSync blueprint used without drums', () => {
      const builder = new SongConfigBuilder(0);
      builder.setDrums(false); // Explicitly disable drums
      builder.setBlueprint(1); // RhythmLock (RhythmSync paradigm)

      const result = builder.getLastChangeResult();
      expect(result).not.toBeNull();
      expect(result?.warnings.length).toBeGreaterThan(0);
      expect(result?.warnings.some((w) => w.includes('drums'))).toBe(true);
    });

    it('should detect BPM clamping for RhythmSync blueprints', () => {
      const builder = new SongConfigBuilder(0);
      builder.setBlueprint(1); // RhythmLock (RhythmSync paradigm)

      // Set BPM outside RhythmSync range
      builder.setBpm(200);

      const result = builder.getLastChangeResult();
      expect(result).not.toBeNull();
      // BPM should be clamped to 175
      expect(result?.changes.some((c) => c.field === 'bpm' && c.newValue === 175)).toBe(true);

      const config = builder.build();
      expect(config.bpm).toBe(175);
    });
  });

  describe('cascade detection - setCompositionStyle', () => {
    it('should set skipVocal for BackgroundMotif style', () => {
      const builder = new SongConfigBuilder(0);
      builder.setCompositionStyle(CompositionStyle.BackgroundMotif);

      const result = builder.getLastChangeResult();
      expect(result).not.toBeNull();
      expect(result?.changes.some((c) => c.field === 'skipVocal' && c.newValue === true)).toBe(
        true,
      );

      const config = builder.build();
      expect(config.skipVocal).toBe(true);
    });

    it('should set skipVocal and arpeggioEnabled for SynthDriven style', () => {
      const builder = new SongConfigBuilder(0);
      builder.setCompositionStyle(CompositionStyle.SynthDriven);

      const result = builder.getLastChangeResult();
      expect(result).not.toBeNull();
      expect(result?.changes.some((c) => c.field === 'skipVocal' && c.newValue === true)).toBe(
        true,
      );
      expect(
        result?.changes.some((c) => c.field === 'arpeggioEnabled' && c.newValue === true),
      ).toBe(true);

      const config = builder.build();
      expect(config.skipVocal).toBe(true);
      expect(config.arpeggioEnabled).toBe(true);
    });

    it('should not override explicitly set skipVocal', () => {
      const builder = new SongConfigBuilder(0);
      builder.setSkipVocal(false); // Explicitly set
      builder.setCompositionStyle(CompositionStyle.BackgroundMotif);

      const config = builder.build();
      // skipVocal should remain false because it was explicitly set
      expect(config.skipVocal).toBe(false);
    });
  });

  describe('cascade detection - setDrums', () => {
    it('should warn when disabling drums with RhythmSync blueprint', () => {
      const builder = new SongConfigBuilder(0);
      builder.setBlueprint(1); // RhythmLock (RhythmSync paradigm)
      builder.setDrums(false);

      const result = builder.getLastChangeResult();
      expect(result).not.toBeNull();
      expect(result?.warnings.length).toBeGreaterThan(0);
    });
  });

  describe('cascade detection - drums_required blueprints', () => {
    it('should force drums enabled for blueprint ID 1 (RhythmLock)', () => {
      const builder = new SongConfigBuilder(0);
      // Clear drums to test forcing
      (builder as unknown as { config: { drumsEnabled: boolean } }).config.drumsEnabled = false;

      builder.setBlueprint(1);

      const result = builder.getLastChangeResult();
      expect(result).not.toBeNull();
      expect(result?.changes.some((c) => c.field === 'drumsEnabled' && c.newValue === true)).toBe(
        true,
      );
      expect(result?.warnings.some((w) => w.includes('drums_required'))).toBe(true);

      const config = builder.build();
      expect(config.drumsEnabled).toBe(true);
    });

    it('should force drums enabled for blueprint ID 5 (IdolHyper)', () => {
      const builder = new SongConfigBuilder(0);
      (builder as unknown as { config: { drumsEnabled: boolean } }).config.drumsEnabled = false;

      builder.setBlueprint(5);

      const config = builder.build();
      expect(config.drumsEnabled).toBe(true);
    });

    it('should force drums enabled for blueprint ID 6 (IdolKawaii)', () => {
      const builder = new SongConfigBuilder(0);
      (builder as unknown as { config: { drumsEnabled: boolean } }).config.drumsEnabled = false;

      builder.setBlueprint(6);

      const config = builder.build();
      expect(config.drumsEnabled).toBe(true);
    });

    it('should force drums enabled for blueprint ID 7 (IdolCoolPop)', () => {
      const builder = new SongConfigBuilder(0);
      (builder as unknown as { config: { drumsEnabled: boolean } }).config.drumsEnabled = false;

      builder.setBlueprint(7);

      const config = builder.build();
      expect(config.drumsEnabled).toBe(true);
    });
  });

  describe('cascade detection - BehavioralLoop (blueprint ID 9)', () => {
    it('should force HookIntensity=Maximum (4) for BehavioralLoop', () => {
      const builder = new SongConfigBuilder(0);
      builder.setBlueprint(9);

      const result = builder.getLastChangeResult();
      expect(result).not.toBeNull();
      expect(result?.changes.some((c) => c.field === 'hookIntensity' && c.newValue === 4)).toBe(
        true,
      );
      expect(result?.warnings.some((w) => w.includes('BehavioralLoop'))).toBe(true);

      const config = builder.build();
      expect(config.hookIntensity).toBe(4); // Maximum
    });
  });

  describe('cascade detection - setVocalStyle with Idol styles', () => {
    it('should auto-enable call for vocalStyle=4 (Idol)', () => {
      const builder = new SongConfigBuilder(0);
      builder.setVocalStyle(4);

      const result = builder.getLastChangeResult();
      expect(result).not.toBeNull();
      expect(result?.changes.some((c) => c.field === 'callEnabled' && c.newValue === true)).toBe(
        true,
      );

      const config = builder.build();
      expect(config.callEnabled).toBe(true);
    });

    it('should auto-enable call for vocalStyle=9 (BrightKira)', () => {
      const builder = new SongConfigBuilder(0);
      builder.setVocalStyle(9);

      const config = builder.build();
      expect(config.callEnabled).toBe(true);
    });

    it('should auto-enable call for vocalStyle=11 (CuteAffected)', () => {
      const builder = new SongConfigBuilder(0);
      builder.setVocalStyle(11);

      const config = builder.build();
      expect(config.callEnabled).toBe(true);
    });

    it('should not override explicitly set callEnabled=false', () => {
      const builder = new SongConfigBuilder(0);
      builder.setCall({ enabled: false }); // Explicitly disable
      builder.setVocalStyle(4); // Idol style

      const config = builder.build();
      expect(config.callEnabled).toBe(false); // Should remain false
    });
  });

  describe('cascade detection - setModulation validation', () => {
    it('should warn when modulationTiming≠0 but modulationSemitones=0', () => {
      const builder = new SongConfigBuilder(0);
      builder.setModulation(1, 0); // timing=LastChorus, semitones=0 (invalid)

      const result = builder.getLastChangeResult();
      expect(result).not.toBeNull();
      expect(result?.warnings.length).toBeGreaterThan(0);
      expect(result?.warnings.some((w) => w.includes('modulationSemitones'))).toBe(true);
    });

    it('should warn when modulationTiming set without semitones', () => {
      const builder = new SongConfigBuilder(0);
      // First set semitones to 0
      (
        builder as unknown as { config: { modulationSemitones: number } }
      ).config.modulationSemitones = 0;

      builder.setModulation(2); // timing=AfterBridge, no semitones provided

      const result = builder.getLastChangeResult();
      expect(result).not.toBeNull();
      expect(result?.warnings.some((w) => w.includes('1-4'))).toBe(true);
    });

    it('should not warn when modulationTiming=0 (disabled)', () => {
      const builder = new SongConfigBuilder(0);
      builder.setModulation(0); // Disabled

      const result = builder.getLastChangeResult();
      expect(result).not.toBeNull();
      expect(result?.warnings.length).toBe(0);
    });

    it('should not warn when valid modulation settings', () => {
      const builder = new SongConfigBuilder(0);
      builder.setModulation(1, 2); // timing=LastChorus, semitones=2 (valid)

      const result = builder.getLastChangeResult();
      expect(result).not.toBeNull();
      expect(result?.warnings.length).toBe(0);
    });
  });

  describe('explicit fields tracking', () => {
    it('should track explicitly set fields', () => {
      const builder = new SongConfigBuilder(0).setSeed(12345).setKey(5).setBpm(120);

      const explicit = builder.getExplicitFields();
      expect(explicit).toContain('seed');
      expect(explicit).toContain('key');
      expect(explicit).toContain('bpm');
    });

    it('should list derived fields', () => {
      const builder = new SongConfigBuilder(0).setSeed(12345);

      const derived = builder.getDerivedFields();
      expect(derived).not.toContain('seed');
      expect(derived).toContain('key');
      expect(derived).toContain('bpm');
    });
  });

  describe('reset', () => {
    it('should reset all fields to defaults', () => {
      const builder = new SongConfigBuilder(0).setSeed(12345).setKey(5).setBpm(120);

      builder.reset();

      const config = builder.build();
      expect(config.seed).toBe(0); // Default
      expect(builder.getExplicitFields()).toHaveLength(0);
    });

    it('should reset to new style', () => {
      const builder = new SongConfigBuilder(0);
      builder.reset(1);

      const config = builder.build();
      expect(config.stylePresetId).toBe(1);
    });
  });

  describe('resetKeepExplicit', () => {
    it('should keep explicitly set values', () => {
      const builder = new SongConfigBuilder(0).setSeed(12345).setBpm(150);

      builder.resetKeepExplicit();

      const config = builder.build();
      expect(config.seed).toBe(12345); // Kept
      expect(config.bpm).toBe(150); // Kept
      expect(builder.getExplicitFields()).toContain('seed');
      expect(builder.getExplicitFields()).toContain('bpm');
    });
  });

  describe('generateFromBuilder integration', () => {
    it('should generate MIDI from builder', () => {
      const sketch = new MidiSketch();
      try {
        const builder = new SongConfigBuilder(0).setSeed(12345).setBlueprint(0);

        sketch.generateFromBuilder(builder);

        const midi = sketch.getMidi();
        expect(midi).toBeInstanceOf(Uint8Array);
        expect(midi.length).toBeGreaterThan(0);
        // Check MIDI header
        expect(midi[0]).toBe(0x4d); // 'M'
        expect(midi[1]).toBe(0x54); // 'T'
        expect(midi[2]).toBe(0x68); // 'h'
        expect(midi[3]).toBe(0x64); // 'd'
      } finally {
        sketch.destroy();
      }
    });

    it('should produce same result as generateFromConfig with same settings', () => {
      const sketch1 = new MidiSketch();
      const sketch2 = new MidiSketch();
      try {
        const builder = new SongConfigBuilder(0).setSeed(12345).setBlueprint(0);

        // Generate from builder
        sketch1.generateFromBuilder(builder);
        const midi1 = sketch1.getMidi();

        // Generate from config
        const config = builder.build();
        sketch2.generateFromConfig(config);
        const midi2 = sketch2.getMidi();

        // Results should be identical
        expect(midi1.length).toBe(midi2.length);
        expect(Array.from(midi1)).toEqual(Array.from(midi2));
      } finally {
        sketch1.destroy();
        sketch2.destroy();
      }
    });
  });
});
