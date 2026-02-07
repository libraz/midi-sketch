/**
 * SongConfigBuilder - Fluent API for building SongConfig with cascade detection
 */

import {
  GenerationParadigm,
  getBlueprintName,
  getBlueprintParadigm,
  getBlueprintRiffPolicy,
  RiffPolicy,
} from './blueprint';
import { createDefaultConfig } from './config';
import { CompositionStyle } from './constants';
import type { SongConfig } from './types';

// ============================================================================
// Types for Change Tracking
// ============================================================================

/**
 * Category of parameter changes
 */
export type ParameterCategory =
  | 'paradigm'
  | 'riffPolicy'
  | 'drums'
  | 'motif'
  | 'bpm'
  | 'hook'
  | 'vocal'
  | 'trackEnable'
  | 'arpeggio'
  | 'chord'
  | 'modulation'
  | 'call'
  | 'basic';

/**
 * Information about a single parameter change
 */
export interface ParameterChange {
  /** Category of the change */
  category: ParameterCategory;
  /** Field name that was changed */
  field: string;
  /** Previous value */
  oldValue: unknown;
  /** New value */
  newValue: unknown;
  /** Reason for the change */
  reason: string;
}

/**
 * Result of a configuration change
 */
export interface ParameterChangeResult {
  /** Number of fields that changed */
  changedCount: number;
  /** Categories of changes */
  changedCategories: ParameterCategory[];
  /** Detailed list of changes */
  changes: ParameterChange[];
  /** Warning messages */
  warnings: string[];
}

// ============================================================================
// ChangeTracker (internal helper for cascade setters)
// ============================================================================

class ChangeTracker {
  private changes: ParameterChange[] = [];
  private warnings: string[] = [];
  private categories = new Set<ParameterCategory>();

  addChange(
    category: ParameterCategory,
    field: string,
    oldValue: unknown,
    newValue: unknown,
    reason: string,
  ): void {
    this.changes.push({ category, field, oldValue, newValue, reason });
    this.categories.add(category);
  }

  addWarning(message: string): void {
    this.warnings.push(message);
  }

  toResult(): ParameterChangeResult {
    return {
      changedCount: this.changes.length,
      changedCategories: Array.from(this.categories),
      changes: this.changes,
      warnings: this.warnings,
    };
  }
}

// ============================================================================
// SongConfigBuilder
// ============================================================================

/**
 * Builder for SongConfig with fluent API and cascade change detection.
 *
 * @example
 * ```typescript
 * const builder = new SongConfigBuilder(0)
 *   .setBpm(165)
 *   .setBlueprint(1)
 *   .setSeed(12345);
 *
 * // Check what changed
 * const changes = builder.getLastChangeResult();
 * if (changes) {
 *   console.log('Auto-changes:', changes.changes);
 * }
 *
 * // Generate
 * sketch.generateFromBuilder(builder);
 * ```
 */
export class SongConfigBuilder {
  private config: SongConfig;
  private explicitFields: Set<string> = new Set();
  private lastChangeResult: ParameterChangeResult | null = null;

  /**
   * Create a new builder with default config for the given style
   * @param styleId Style preset ID (0-12)
   */
  constructor(styleId: number = 0) {
    this.config = createDefaultConfig(styleId);
  }

  // ============================================================================
  // State Management
  // ============================================================================

  /**
   * Get the result of the last change operation
   */
  getLastChangeResult(): ParameterChangeResult | null {
    return this.lastChangeResult;
  }

  /**
   * Get list of explicitly set field names
   */
  getExplicitFields(): string[] {
    return Array.from(this.explicitFields);
  }

  /**
   * Get list of fields that would be derived/auto-set
   */
  getDerivedFields(): string[] {
    const allFields = Object.keys(this.config);
    return allFields.filter((f) => !this.explicitFields.has(f));
  }

  /**
   * Build and return the SongConfig
   */
  build(): SongConfig {
    return { ...this.config };
  }

  /**
   * Reset all settings to defaults
   * @param styleId Optional new style ID (defaults to current)
   */
  reset(styleId?: number): this {
    const sid = styleId ?? this.config.stylePresetId;
    this.config = createDefaultConfig(sid);
    this.explicitFields.clear();
    this.lastChangeResult = null;
    return this;
  }

  /**
   * Reset to defaults but keep explicitly set values
   * @param styleId Optional new style ID (defaults to current)
   */
  resetKeepExplicit(styleId?: number): this {
    const sid = styleId ?? this.config.stylePresetId;
    const defaultConfig = createDefaultConfig(sid);
    const preserved: Partial<SongConfig> = {};

    // Preserve explicitly set values
    for (const field of this.explicitFields) {
      preserved[field as keyof SongConfig] = this.config[field as keyof SongConfig] as never;
    }

    // Reset to defaults
    this.config = defaultConfig;

    // Restore explicit values
    Object.assign(this.config, preserved);

    this.lastChangeResult = null;
    return this;
  }

  // ============================================================================
  // Basic Setters (No Cascade)
  // ============================================================================

  /**
   * Set random seed
   * @param seed Seed value (0 = random)
   */
  setSeed(seed: number): this {
    this.setField('seed', seed, 'basic');
    return this;
  }

  /**
   * Set key
   * @param key Key (0-11, 0=C, 1=C#, etc.)
   */
  setKey(key: number): this {
    this.setField('key', key, 'basic');
    return this;
  }

  /**
   * Set chord progression
   * @param id Chord progression ID
   */
  setChordProgression(id: number): this {
    this.setField('chordProgressionId', id, 'chord');
    return this;
  }

  /**
   * Set form/structure pattern
   * @param id Form ID
   */
  setForm(id: number): this {
    this.setField('formId', id, 'basic');
    return this;
  }

  /**
   * Set vocal range
   * @param low Lower MIDI note bound
   * @param high Upper MIDI note bound
   */
  setVocalRange(low: number, high: number): this {
    // Normalize range
    const actualLow = Math.min(low, high);
    const actualHigh = Math.max(low, high);
    this.setField('vocalLow', actualLow, 'vocal');
    this.setField('vocalHigh', actualHigh, 'vocal');
    return this;
  }

  /**
   * Set vocal style preset with cascade detection
   *
   * Idol-style vocalStyles (4=Idol, 9=BrightKira, 11=CuteAffected) will
   * auto-enable call system if callEnabled is not explicitly set.
   *
   * @param style Vocal style ID (0=Auto, 1=Standard, 2=Vocaloid, etc.)
   */
  setVocalStyle(style: number): this {
    const tracker = new ChangeTracker();

    const oldStyle = this.config.vocalStyle;
    this.config.vocalStyle = style;
    this.explicitFields.add('vocalStyle');
    tracker.addChange('vocal', 'vocalStyle', oldStyle, style, 'User set vocal style');

    // Idol-style vocalStyles auto-enable call if not explicitly set
    // vocalStyle: 4=Idol, 9=BrightKira, 11=CuteAffected
    const idolStyles = [4, 9, 11];
    if (idolStyles.includes(style) && !this.explicitFields.has('callEnabled')) {
      if (!this.config.callEnabled) {
        const oldCall = this.config.callEnabled;
        this.config.callEnabled = true;
        tracker.addChange(
          'call',
          'callEnabled',
          oldCall,
          true,
          `Idol-style vocalStyle (${style}) auto-enables call system`,
        );
      }
    }

    this.lastChangeResult = tracker.toResult();
    return this;
  }

  /**
   * Set vocal attitude
   * @param attitude 0=Clean, 1=Expressive, 2=Raw
   */
  setVocalAttitude(attitude: number): this {
    this.setField('vocalAttitude', attitude, 'vocal');
    return this;
  }

  /**
   * Set humanization settings
   * @param enabled Enable humanization
   * @param timing Timing variation (0-100)
   * @param velocity Velocity variation (0-100)
   */
  setHumanize(enabled: boolean, timing?: number, velocity?: number): this {
    this.setField('humanize', enabled, 'basic');
    if (timing !== undefined) {
      this.setField('humanizeTiming', timing, 'basic');
    }
    if (velocity !== undefined) {
      this.setField('humanizeVelocity', velocity, 'basic');
    }
    return this;
  }

  /**
   * Set modulation settings with validation
   *
   * Warning: If timing≠0 and semitones=0, validation will fail.
   * When modulation is enabled, semitones must be 1-4.
   *
   * @param timing Modulation timing (0=None, 1=LastChorus, 2=AfterBridge, 3=EachChorus, 4=Random)
   * @param semitones Modulation amount (+1 to +4), required when timing≠0
   */
  setModulation(timing: number, semitones?: number): this {
    const tracker = new ChangeTracker();

    const oldTiming = this.config.modulationTiming;
    this.config.modulationTiming = timing;
    this.explicitFields.add('modulationTiming');
    tracker.addChange(
      'modulation',
      'modulationTiming',
      oldTiming,
      timing,
      'User set modulation timing',
    );

    if (semitones !== undefined) {
      const oldSemitones = this.config.modulationSemitones;
      this.config.modulationSemitones = semitones;
      this.explicitFields.add('modulationSemitones');
      tracker.addChange(
        'modulation',
        'modulationSemitones',
        oldSemitones,
        semitones,
        'User set modulation semitones',
      );
    }

    // Warn about invalid combination: timing!=0 but semitones=0 or not set
    if (timing !== 0) {
      const currentSemitones = semitones ?? this.config.modulationSemitones;
      if (currentSemitones === 0 || currentSemitones < 1 || currentSemitones > 4) {
        tracker.addWarning(
          `Modulation timing=${timing} requires modulationSemitones to be 1-4. Current value: ${currentSemitones}`,
        );
      }
    }

    this.lastChangeResult = tracker.toResult();
    return this;
  }

  /**
   * Set chord extension settings
   * @param opts Chord extension options
   */
  setChordExtensions(opts: {
    sus?: boolean;
    seventh?: boolean;
    ninth?: boolean;
    susProb?: number;
    seventhProb?: number;
    ninthProb?: number;
  }): this {
    if (opts.sus !== undefined) {
      this.setField('chordExtSus', opts.sus, 'chord');
    }
    if (opts.seventh !== undefined) {
      this.setField('chordExt7th', opts.seventh, 'chord');
    }
    if (opts.ninth !== undefined) {
      this.setField('chordExt9th', opts.ninth, 'chord');
    }
    if (opts.susProb !== undefined) {
      this.setField('chordExtSusProb', opts.susProb, 'chord');
    }
    if (opts.seventhProb !== undefined) {
      this.setField('chordExt7thProb', opts.seventhProb, 'chord');
    }
    if (opts.ninthProb !== undefined) {
      this.setField('chordExt9thProb', opts.ninthProb, 'chord');
    }
    // Mark explicit if any probability was set (prevents mood override)
    if (
      opts.susProb !== undefined ||
      opts.seventhProb !== undefined ||
      opts.ninthProb !== undefined
    ) {
      this.config.chordExtProbExplicit = true;
    }
    return this;
  }

  /**
   * Set arpeggio settings
   * @param enabled Enable arpeggio
   * @param opts Arpeggio options
   */
  setArpeggio(
    enabled: boolean,
    opts?: {
      pattern?: number;
      speed?: number;
      octaveRange?: number;
      gate?: number;
      syncChord?: boolean;
    },
  ): this {
    this.setField('arpeggioEnabled', enabled, 'arpeggio');
    if (opts) {
      if (opts.pattern !== undefined) {
        this.setField('arpeggioPattern', opts.pattern, 'arpeggio');
      }
      if (opts.speed !== undefined) {
        this.setField('arpeggioSpeed', opts.speed, 'arpeggio');
      }
      if (opts.octaveRange !== undefined) {
        this.setField('arpeggioOctaveRange', opts.octaveRange, 'arpeggio');
      }
      if (opts.gate !== undefined) {
        this.setField('arpeggioGate', opts.gate, 'arpeggio');
      }
      if (opts.syncChord !== undefined) {
        this.setField('arpeggioSyncChord', opts.syncChord, 'arpeggio');
      }
    }
    return this;
  }

  /**
   * Set motif settings
   * @param opts Motif options
   */
  setMotif(opts: {
    repeatScope?: number;
    fixedProgression?: boolean;
    maxChordCount?: number;
  }): this {
    if (opts.repeatScope !== undefined) {
      this.setField('motifRepeatScope', opts.repeatScope, 'motif');
    }
    if (opts.fixedProgression !== undefined) {
      this.setField('motifFixedProgression', opts.fixedProgression, 'motif');
    }
    if (opts.maxChordCount !== undefined) {
      this.setField('motifMaxChordCount', opts.maxChordCount, 'motif');
    }
    return this;
  }

  /**
   * Set call/SE settings
   * @param opts Call options
   */
  setCall(opts: {
    enabled?: boolean;
    notesEnabled?: boolean;
    density?: number;
    introChant?: number;
    mixPattern?: number;
    seEnabled?: boolean;
  }): this {
    if (opts.enabled !== undefined) {
      this.setField('callEnabled', opts.enabled, 'call');
    }
    if (opts.notesEnabled !== undefined) {
      this.setField('callNotesEnabled', opts.notesEnabled, 'call');
    }
    if (opts.density !== undefined) {
      this.setField('callDensity', opts.density, 'call');
    }
    if (opts.introChant !== undefined) {
      this.setField('introChant', opts.introChant, 'call');
    }
    if (opts.mixPattern !== undefined) {
      this.setField('mixPattern', opts.mixPattern, 'call');
    }
    if (opts.seEnabled !== undefined) {
      this.setField('seEnabled', opts.seEnabled, 'call');
    }
    return this;
  }

  /**
   * Set melodic complexity
   * @param complexity 0=Simple, 1=Standard, 2=Complex
   */
  setMelodicComplexity(complexity: number): this {
    this.setField('melodicComplexity', complexity, 'vocal');
    return this;
  }

  /**
   * Set hook intensity
   * @param intensity 0=Off, 1=Light, 2=Normal, 3=Strong
   */
  setHookIntensity(intensity: number): this {
    this.setField('hookIntensity', intensity, 'hook');
    return this;
  }

  /**
   * Set vocal groove feel
   * @param groove 0=Straight, 1=OffBeat, 2=Swing, 3=Syncopated, 4=Driving16th, 5=Bouncy8th
   */
  setVocalGroove(groove: number): this {
    this.setField('vocalGroove', groove, 'vocal');
    return this;
  }

  /**
   * Set melody template
   * @param template 0=Auto, 1=PlateauTalk, 2=RunUpTarget, etc.
   */
  setMelodyTemplate(template: number): this {
    this.setField('melodyTemplate', template, 'vocal');
    return this;
  }

  /**
   * Set arrangement growth
   * @param growth 0=LayerAdd, 1=RegisterAdd
   */
  setArrangementGrowth(growth: number): this {
    this.setField('arrangementGrowth', growth, 'basic');
    return this;
  }

  /**
   * Set target duration
   * @param seconds Target duration in seconds (0 = use formId)
   */
  setTargetDuration(seconds: number): this {
    this.setField('targetDurationSeconds', seconds, 'basic');
    return this;
  }

  /**
   * Skip vocal generation
   * @param skip Whether to skip vocal generation
   */
  setSkipVocal(skip: boolean): this {
    this.setField('skipVocal', skip, 'vocal');
    return this;
  }

  /**
   * Set drive feel
   * @param feel 0=laid-back, 50=neutral, 100=aggressive
   */
  setDriveFeel(feel: number): this {
    this.setField('driveFeel', feel, 'basic');
    return this;
  }

  /**
   * Set addictive mode (Behavioral Loop)
   * @param enabled Enable addictive mode
   */
  setAddictiveMode(enabled: boolean): this {
    this.setField('addictiveMode', enabled, 'basic');
    return this;
  }

  /**
   * Set mood override
   * @param mood Mood preset ID (0-23)
   */
  setMood(mood: number): this {
    this.setField('mood', mood, 'basic');
    this.setField('moodExplicit', true, 'basic');
    return this;
  }

  /**
   * Set form explicit mode (use formId exactly, no randomization)
   * @param explicit Whether formId should be used exactly
   */
  setFormExplicit(explicit: boolean): this {
    this.setField('formExplicit', explicit, 'basic');
    return this;
  }

  // ============================================================================
  // Cascade Setters
  // ============================================================================

  /**
   * Set blueprint with cascade detection
   *
   * Setting a blueprint may automatically change:
   * - drumsEnabled (if blueprint requires drums: ID 1,5,6,7)
   * - hookIntensity (BehavioralLoop forces Maximum)
   * - BPM clamping for RhythmSync paradigm
   *
   * Blueprint drums_required: IDs 1 (RhythmLock), 5 (IdolHyper), 6 (IdolKawaii), 7 (IdolCoolPop)
   * BehavioralLoop (ID 9): Forces HookIntensity=Maximum, RiffPolicy=LockedPitch
   *
   * @param id Blueprint ID (0-9, 255=random)
   */
  setBlueprint(id: number): this {
    const tracker = new ChangeTracker();

    const oldBlueprint = this.config.blueprintId;

    // Set the blueprint
    this.config.blueprintId = id;
    this.explicitFields.add('blueprintId');
    tracker.addChange('basic', 'blueprintId', oldBlueprint, id, 'User set blueprint');

    // If not random, resolve cascade effects
    if (id !== 255) {
      const paradigm = getBlueprintParadigm(id);
      const riffPolicy = getBlueprintRiffPolicy(id);

      // Blueprint IDs with drums_required=true: 1, 5, 6, 7
      const drumsRequiredBlueprints = [1, 5, 6, 7];
      const isDrumsRequired = drumsRequiredBlueprints.includes(id);

      // Handle drums_required
      if (isDrumsRequired) {
        if (!this.config.drumsEnabled) {
          const oldDrums = this.config.drumsEnabled;
          this.config.drumsEnabled = true;
          tracker.addChange(
            'drums',
            'drumsEnabled',
            oldDrums,
            true,
            `Blueprint ${getBlueprintName(id)} requires drums (drums_required=true)`,
          );
          // Note: drums setting will be hidden in UI for these blueprints
          tracker.addWarning(
            `Blueprint ${getBlueprintName(id)} has drums_required=true; drumsEnabled forced to true`,
          );
        }
      } else if (paradigm === GenerationParadigm.RhythmSync) {
        // RhythmSync without drums_required - still recommend drums
        if (!this.config.drumsEnabled && !this.explicitFields.has('drumsEnabled')) {
          const oldDrums = this.config.drumsEnabled;
          this.config.drumsEnabled = true;
          tracker.addChange(
            'drums',
            'drumsEnabled',
            oldDrums,
            true,
            'RhythmSync blueprint works best with drums',
          );
        } else if (!this.config.drumsEnabled) {
          tracker.addWarning('RhythmSync blueprint works best with drums enabled');
        }
      }

      // RhythmSync prefers BPM in 160-175 range
      if (paradigm === GenerationParadigm.RhythmSync) {
        if (
          this.config.bpm > 0 &&
          (this.config.bpm < 160 || this.config.bpm > 175) &&
          !this.explicitFields.has('bpm')
        ) {
          const oldBpm = this.config.bpm;
          const newBpm = Math.max(160, Math.min(175, this.config.bpm));
          this.config.bpm = newBpm;
          tracker.addChange(
            'bpm',
            'bpm',
            oldBpm,
            newBpm,
            'RhythmSync blueprint prefers BPM 160-175',
          );
        } else if (this.config.bpm > 0 && (this.config.bpm < 160 || this.config.bpm > 175)) {
          tracker.addWarning('RhythmSync blueprint works best with BPM 160-175');
        }
      }

      // BehavioralLoop (ID 9): addictive_mode, forces HookIntensity=Maximum (4)
      if (id === 9) {
        const HOOK_INTENSITY_MAXIMUM = 4;
        if (
          this.config.hookIntensity !== HOOK_INTENSITY_MAXIMUM &&
          !this.explicitFields.has('hookIntensity')
        ) {
          const oldHook = this.config.hookIntensity;
          this.config.hookIntensity = HOOK_INTENSITY_MAXIMUM;
          tracker.addChange(
            'hook',
            'hookIntensity',
            oldHook,
            HOOK_INTENSITY_MAXIMUM,
            'BehavioralLoop blueprint forces HookIntensity=Maximum',
          );
        }
        tracker.addWarning(
          'BehavioralLoop (ID 9) enables addictive_mode with maximum hook repetition and LockedPitch riff policy',
        );
      }

      // Store paradigm and riff policy info for reference
      if (riffPolicy === RiffPolicy.LockedPitch || riffPolicy === RiffPolicy.LockedAll) {
        tracker.addChange(
          'riffPolicy',
          '_riffPolicy',
          null,
          riffPolicy,
          `Blueprint uses ${riffPolicy === RiffPolicy.LockedPitch ? 'LockedPitch' : 'LockedAll'} riff policy`,
        );
      }
    }

    this.lastChangeResult = tracker.toResult();
    return this;
  }

  /**
   * Set BPM with cascade detection
   *
   * For RhythmSync blueprints, warns if BPM is outside 160-175 range.
   * C++ respects explicit BPM and skips clamping.
   *
   * @param bpm BPM value (0 = use style default)
   */
  setBpm(bpm: number): this {
    const tracker = new ChangeTracker();

    const oldBpm = this.config.bpm;

    // Check if we're using a RhythmSync blueprint and warn (but don't clamp)
    // C++ will respect explicit BPM via bpm_explicit flag
    if (this.config.blueprintId !== 255 && bpm > 0) {
      const paradigm = getBlueprintParadigm(this.config.blueprintId);
      if (paradigm === GenerationParadigm.RhythmSync && (bpm < 160 || bpm > 175)) {
        tracker.addWarning(`RhythmSync blueprint works best with BPM 160-175 (set: ${bpm})`);
      }
    }

    this.config.bpm = bpm;
    this.explicitFields.add('bpm');
    tracker.addChange('bpm', 'bpm', oldBpm, bpm, 'User set BPM');

    this.lastChangeResult = tracker.toResult();
    return this;
  }

  /**
   * Set composition style with cascade detection
   *
   * Setting composition style may automatically change:
   * - skipVocal (for BackgroundMotif/SynthDriven)
   * - arpeggioEnabled (for SynthDriven)
   *
   * @param style 0=MelodyLead, 1=BackgroundMotif, 2=SynthDriven
   */
  setCompositionStyle(style: number): this {
    const tracker = new ChangeTracker();

    const oldStyle = this.config.compositionStyle;

    this.config.compositionStyle = style;
    this.explicitFields.add('compositionStyle');
    tracker.addChange('basic', 'compositionStyle', oldStyle, style, 'User set composition style');

    // BackgroundMotif and SynthDriven typically skip vocal
    if (
      (style === CompositionStyle.BackgroundMotif || style === CompositionStyle.SynthDriven) &&
      !this.explicitFields.has('skipVocal')
    ) {
      const oldSkipVocal = this.config.skipVocal;
      if (!oldSkipVocal) {
        this.config.skipVocal = true;
        tracker.addChange(
          'vocal',
          'skipVocal',
          oldSkipVocal,
          true,
          style === CompositionStyle.BackgroundMotif
            ? 'BackgroundMotif style skips vocal'
            : 'SynthDriven style skips vocal',
        );
      }
    }

    // SynthDriven typically enables arpeggio
    if (style === CompositionStyle.SynthDriven && !this.explicitFields.has('arpeggioEnabled')) {
      const oldArpeggio = this.config.arpeggioEnabled;
      if (!oldArpeggio) {
        this.config.arpeggioEnabled = true;
        tracker.addChange(
          'arpeggio',
          'arpeggioEnabled',
          oldArpeggio,
          true,
          'SynthDriven style enables arpeggio',
        );
      }
    }

    this.lastChangeResult = tracker.toResult();
    return this;
  }

  /**
   * Set style preset with cascade detection
   *
   * Changing style preset resets mood, chord, form, bpm to style defaults.
   *
   * @param id Style preset ID (0-12)
   */
  setStylePreset(id: number): this {
    const tracker = new ChangeTracker();

    const oldStyleId = this.config.stylePresetId;
    const defaultConfig = createDefaultConfig(id);

    // Update style preset ID
    this.config.stylePresetId = id;
    this.explicitFields.add('stylePresetId');
    tracker.addChange('basic', 'stylePresetId', oldStyleId, id, 'User set style preset');

    // Reset non-explicit fields to new style defaults
    const fieldsToReset: (keyof SongConfig)[] = [
      'chordProgressionId',
      'formId',
      'bpm',
      'vocalAttitude',
    ];

    for (const field of fieldsToReset) {
      if (!this.explicitFields.has(field) && this.config[field] !== defaultConfig[field]) {
        const oldValue = this.config[field];
        this.setConfigValue(field, defaultConfig[field]);
        tracker.addChange('basic', field, oldValue, defaultConfig[field], 'Reset to style default');
      }
    }

    this.lastChangeResult = tracker.toResult();
    return this;
  }

  /**
   * Set drums enabled with cascade detection
   *
   * Disabling drums may trigger warnings for blueprints that require drums.
   *
   * @param enabled Whether drums are enabled
   */
  setDrums(enabled: boolean): this {
    const tracker = new ChangeTracker();

    const oldDrums = this.config.drumsEnabled;

    this.config.drumsEnabled = enabled;
    this.config.drumsEnabledExplicit = true;
    this.explicitFields.add('drumsEnabled');
    tracker.addChange('drums', 'drumsEnabled', oldDrums, enabled, 'User set drums');

    // Check if blueprint requires drums
    if (!enabled && this.config.blueprintId !== 255) {
      const paradigm = getBlueprintParadigm(this.config.blueprintId);
      if (paradigm === GenerationParadigm.RhythmSync) {
        tracker.addWarning('RhythmSync blueprint works best with drums enabled');
      }
    }

    this.lastChangeResult = tracker.toResult();
    return this;
  }

  // ============================================================================
  // Private Helpers
  // ============================================================================

  private setConfigValue<K extends keyof SongConfig>(field: K, value: SongConfig[K]): void {
    this.config[field] = value;
  }

  private setField(
    field: keyof SongConfig,
    value: SongConfig[keyof SongConfig],
    category: ParameterCategory,
  ): void {
    const oldValue = this.config[field];
    this.setConfigValue(field, value as SongConfig[typeof field]);
    this.explicitFields.add(field);

    this.lastChangeResult = {
      changedCount: 1,
      changedCategories: [category],
      changes: [
        {
          category,
          field,
          oldValue,
          newValue: value,
          reason: 'User set value',
        },
      ],
      warnings: [],
    };
  }
}
