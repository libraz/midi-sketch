/**
 * SongConfigBuilder - Fluent API for building SongConfig with cascade detection
 */

import {
  GenerationParadigm,
  getBlueprintDrumsRequired,
  getBlueprintName,
  getBlueprintParadigm,
  getBlueprintRiffPolicy,
  getBlueprintTempoRange,
  RiffPolicy,
} from './blueprint';
import { createDefaultConfig } from './config';
import { CompositionStyle, HookIntensity } from './constants';
import type { SongConfig } from './types';

function clampUnitInterval(value: number): number {
  return Math.max(0, Math.min(1, value));
}

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
// Explicitness
// ============================================================================

/**
 * The core flag that tells it a field was the caller's choice, for the fields
 * the core reads one for. Setting the field sets its flag.
 */
const EXPLICIT_FLAGS: Partial<Record<keyof SongConfig, keyof SongConfig>> = {
  drumsEnabled: 'drumsEnabledExplicit',
  compositionStyle: 'compositionStyleExplicit',
  mood: 'moodExplicit',
  formId: 'formExplicit',
  hookIntensity: 'hookIntensityExplicit',
  vocalGroove: 'vocalGrooveExplicit',
  chordExtSusProb: 'chordExtProbExplicit',
  chordExt7thProb: 'chordExtProbExplicit',
  chordExt9thProb: 'chordExtProbExplicit',
  chordExtTritoneSubProb: 'chordExtProbExplicit',
};

/** A value a cascade wrote, with what it replaced, so the cascade can be undone. */
interface DerivedValue {
  trigger: keyof SongConfig;
  category: ParameterCategory;
  previous: unknown;
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
  private derivedFields = new Map<keyof SongConfig, DerivedValue>();
  private lastChangeResult: ParameterChangeResult | null = null;

  /**
   * Create a new builder with default config for the given style
   * @param styleId Style preset ID (0-16)
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
    this.derivedFields.clear();
    this.lastChangeResult = null;
    return this;
  }

  /**
   * Reset to defaults but keep explicitly set values, together with the flags
   * that tell the core they are explicit
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

    // Cascaded values went back to their defaults with everything else.
    this.derivedFields.clear();
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
   * @param id Form ID. Marks the form as explicit, preventing automatic form selection.
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
   * Set vocal style preset
   *
   * Call-oriented vocal styles (Idol, BrightKira, CuteAffected) turn the call
   * system on while callSetting stays Auto: the core resolves Auto from the
   * vocal style, so changing the style again changes the answer with it.
   *
   * @param style Vocal style ID (0=Auto, 1=Standard, 2=Vocaloid, etc.)
   */
  setVocalStyle(style: number): this {
    this.setField('vocalStyle', style, 'vocal');
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
   * @param timing Timing variation (0.0-1.0; values outside the range are clamped)
   * @param velocity Velocity variation (0.0-1.0; values outside the range are clamped)
   */
  setHumanize(enabled: boolean, timing?: number, velocity?: number): this {
    this.setField('humanize', enabled, 'basic');
    if (timing !== undefined) {
      this.setField('humanizeTiming', clampUnitInterval(timing), 'basic');
    }
    if (velocity !== undefined) {
      this.setField('humanizeVelocity', clampUnitInterval(velocity), 'basic');
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
    this.markExplicit('modulationTiming');
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
      this.markExplicit('modulationSemitones');
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
    tritone?: boolean;
    susProb?: number;
    seventhProb?: number;
    ninthProb?: number;
    tritoneProb?: number;
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
    if (opts.tritone !== undefined) {
      this.setField('chordExtTritoneSub', opts.tritone, 'chord');
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
    if (opts.tritoneProb !== undefined) {
      this.setField('chordExtTritoneSubProb', opts.tritoneProb, 'chord');
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
      baseVelocity?: number;
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
      if (opts.baseVelocity !== undefined) {
        this.setField('arpeggioBaseVelocity', opts.baseVelocity, 'arpeggio');
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
    maxChordCount?: number;
    length?: number;
    noteCount?: number;
    motion?: number;
    registerHigh?: number;
    rhythmDensity?: number;
  }): this {
    if (opts.repeatScope !== undefined) {
      this.setField('motifRepeatScope', opts.repeatScope, 'motif');
    }
    if (opts.maxChordCount !== undefined) {
      this.setField('motifMaxChordCount', opts.maxChordCount, 'motif');
    }
    if (opts.length !== undefined) {
      this.setField('motifLength', opts.length, 'motif');
    }
    if (opts.noteCount !== undefined) {
      this.setField('motifNoteCount', opts.noteCount, 'motif');
    }
    if (opts.motion !== undefined) {
      this.setField('motifMotion', opts.motion, 'motif');
    }
    if (opts.registerHigh !== undefined) {
      this.setField('motifRegisterHigh', opts.registerHigh, 'motif');
    }
    if (opts.rhythmDensity !== undefined) {
      this.setField('motifRhythmDensity', opts.rhythmDensity, 'motif');
    }
    return this;
  }

  /**
   * Set call/SE settings
   * @param opts Call options
   */
  setCall(opts: {
    enabled?: boolean;
    setting?: number;
    notesEnabled?: boolean;
    density?: number;
    introChant?: number;
    mixPattern?: number;
    seEnabled?: boolean;
  }): this {
    if (opts.setting !== undefined) {
      this.setField('callSetting', opts.setting, 'call');
      this.config.callEnabled = opts.setting === 1;
      this.markExplicit('callEnabled');
    }
    if (opts.enabled !== undefined) {
      this.setField('callEnabled', opts.enabled, 'call');
      this.config.callSetting = opts.enabled ? 1 : 2;
      this.markExplicit('callSetting');
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
   * @param intensity 0=Off, 1=Light, 2=Normal, 3=Strong, 4=Maximum
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
   * Set target duration.
   *
   * The duration has to reach between 12 and 144 bars at the resolved tempo, so the
   * accepted range in seconds depends on the BPM. A value outside it is rejected when
   * the config is validated rather than being shortened to fit, which is the same
   * answer the native CLI gives for the same config.
   *
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
   * Set mora rhythm mode
   * @param mode 0=Standard, 1=MoraTimed, 2=Auto
   */
  setMoraRhythmMode(mode: number): this {
    this.setField('moraRhythmMode', mode, 'vocal');
    return this;
  }

  /** Set the syllabic subdivision rate (0 = style default, 1-100 = override). */
  setSyllabicSubdivisionRate(rate: number): this {
    this.setField('syllabicSubRate', rate, 'vocal');
    return this;
  }

  /** Enable or disable melodic syncopation. */
  setSyncopation(enabled: boolean): this {
    this.setField('enableSyncopation', enabled, 'vocal');
    return this;
  }

  /** Set the section energy curve (0=GradualBuild through 3=SteadyState). */
  setEnergyCurve(curve: number): this {
    this.setField('energyCurve', curve, 'basic');
    return this;
  }

  /**
   * Set the optional per-song melody overrides. `maxLeap` caps the blueprint's
   * leap budget; left unset, the style's leap value does not narrow it.
   */
  setMelodyOverrides(opts: {
    maxLeap?: number;
    syncopationProb?: number;
    phraseLength?: number;
    longNoteRatio?: number;
    chorusRegisterShift?: number;
    hookRepetition?: number;
    useLeadingTone?: number;
  }): this {
    if (opts.maxLeap !== undefined) {
      this.setField('melodyMaxLeap', opts.maxLeap, 'vocal');
    }
    if (opts.syncopationProb !== undefined) {
      this.setField('melodySyncopationProb', opts.syncopationProb, 'vocal');
    }
    if (opts.phraseLength !== undefined) {
      this.setField('melodyPhraseLength', opts.phraseLength, 'vocal');
    }
    if (opts.longNoteRatio !== undefined) {
      this.setField('melodyLongNoteRatio', opts.longNoteRatio, 'vocal');
    }
    if (opts.chorusRegisterShift !== undefined) {
      this.setField('melodyChorusRegisterShift', opts.chorusRegisterShift, 'vocal');
    }
    if (opts.hookRepetition !== undefined) {
      this.setField('melodyHookRepetition', opts.hookRepetition, 'vocal');
    }
    if (opts.useLeadingTone !== undefined) {
      this.setField('melodyUseLeadingTone', opts.useLeadingTone, 'vocal');
    }
    return this;
  }

  /** Enable or disable the guitar accompaniment track. */
  setGuitar(enabled: boolean): this {
    this.setField('guitarEnabled', enabled, 'trackEnable');
    return this;
  }

  /**
   * Set mood override
   * @param mood Mood preset ID (0-23)
   */
  setMood(mood: number): this {
    this.setField('mood', mood, 'basic');
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
   * Setting a blueprint may automatically change, unless the field was set
   * explicitly (then it only warns):
   * - drumsEnabled (if the blueprint has drums_required=true, per the C++
   *   blueprint table via getBlueprintDrumsRequired)
   * - hookIntensity (BehavioralLoop forces Maximum)
   * - BPM auto-adjustment into the blueprint's tempo range
   *
   * Values a previous blueprint changed this way are restored first, so the
   * result does not depend on which blueprint was set before.
   *
   * BehavioralLoop (ID 9): Forces HookIntensity=Maximum, RiffPolicy=LockedPitch
   *
   * @param id Blueprint ID (0-9, 255=random)
   */
  setBlueprint(id: number): this {
    const tracker = new ChangeTracker();

    const oldBlueprint = this.config.blueprintId;

    // Set the blueprint
    this.config.blueprintId = id;
    this.markExplicit('blueprintId');
    tracker.addChange('basic', 'blueprintId', oldBlueprint, id, 'User set blueprint');
    this.revertDerived('blueprintId', tracker);

    // If not random, resolve cascade effects
    if (id !== 255) {
      const paradigm = getBlueprintParadigm(id);
      const riffPolicy = getBlueprintRiffPolicy(id);

      // Source of truth is the C++ blueprint table (production_blueprint.cpp),
      // queried via midisketch_blueprint_drums_required. setBlueprint already
      // requires an initialized module (getBlueprintParadigm above), so no
      // hardcoded mirror list is needed.
      const isDrumsRequired = getBlueprintDrumsRequired(id);

      // Handle drums_required. An explicit setDrums(false) is how drums are
      // turned off on these blueprints, so it is kept and only warned about.
      if (isDrumsRequired) {
        if (!this.config.drumsEnabled && !this.explicitFields.has('drumsEnabled')) {
          const oldDrums = this.config.drumsEnabled;
          this.deriveField('blueprintId', 'drums', 'drumsEnabled', true);
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
        } else if (!this.config.drumsEnabled) {
          tracker.addWarning(
            `Blueprint ${getBlueprintName(id)} has drums_required=true; drums stay off as explicitly set`,
          );
        }
      } else if (paradigm === GenerationParadigm.RhythmSync) {
        // RhythmSync without drums_required - still recommend drums
        if (!this.config.drumsEnabled && !this.explicitFields.has('drumsEnabled')) {
          const oldDrums = this.config.drumsEnabled;
          this.deriveField('blueprintId', 'drums', 'drumsEnabled', true);
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

      const tempoRange = getBlueprintTempoRange(id);
      if (tempoRange.min > 0 && tempoRange.max > 0) {
        if (
          this.config.bpm > 0 &&
          (this.config.bpm < tempoRange.min || this.config.bpm > tempoRange.max) &&
          !this.explicitFields.has('bpm')
        ) {
          const oldBpm = this.config.bpm;
          const newBpm = Math.max(tempoRange.min, Math.min(tempoRange.max, this.config.bpm));
          this.deriveField('blueprintId', 'bpm', 'bpm', newBpm);
          tracker.addChange(
            'bpm',
            'bpm',
            oldBpm,
            newBpm,
            `${getBlueprintName(id)} blueprint prefers BPM ${tempoRange.min}-${tempoRange.max}`,
          );
        } else if (
          this.config.bpm > 0 &&
          (this.config.bpm < tempoRange.min || this.config.bpm > tempoRange.max)
        ) {
          tracker.addWarning(
            `${getBlueprintName(id)} blueprint works best with BPM ${tempoRange.min}-${tempoRange.max}`,
          );
        }
      }

      // BehavioralLoop (ID 9): addictive_mode, forces HookIntensity=Maximum (4)
      if (id === 9) {
        if (
          this.config.hookIntensity !== HookIntensity.Maximum &&
          !this.explicitFields.has('hookIntensity')
        ) {
          const oldHook = this.config.hookIntensity;
          this.deriveField('blueprintId', 'hook', 'hookIntensity', HookIntensity.Maximum);
          tracker.addChange(
            'hook',
            'hookIntensity',
            oldHook,
            HookIntensity.Maximum,
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
   * Warns if BPM is outside the selected blueprint's declared tempo range.
   * C++ respects explicit BPM and skips clamping.
   *
   * @param bpm BPM value (0 = implicit: a named blueprint's tempo, otherwise the
   *   explicit mood's or the style preset's, with a drawn blueprint chosen to fit it)
   */
  setBpm(bpm: number): this {
    const tracker = new ChangeTracker();

    const oldBpm = this.config.bpm;

    // C++ will respect explicit BPM via bpm_explicit flag.
    if (this.config.blueprintId !== 255 && bpm > 0) {
      const tempoRange = getBlueprintTempoRange(this.config.blueprintId);
      if (
        tempoRange.min > 0 &&
        tempoRange.max > 0 &&
        (bpm < tempoRange.min || bpm > tempoRange.max)
      ) {
        tracker.addWarning(
          `${getBlueprintName(this.config.blueprintId)} blueprint works best with BPM ` +
            `${tempoRange.min}-${tempoRange.max} (set: ${bpm})`,
        );
      }
    }

    this.config.bpm = bpm;
    this.markExplicit('bpm');
    tracker.addChange('bpm', 'bpm', oldBpm, bpm, 'User set BPM');

    this.lastChangeResult = tracker.toResult();
    return this;
  }

  /**
   * Set composition style with cascade detection
   *
   * Setting composition style may automatically change arpeggioEnabled (for
   * SynthDriven), restored when the style changes again. BackgroundMotif and
   * SynthDriven songs generated in one call have no vocal; the core leaves it
   * out itself, so skipVocal is not touched. The vocal-first calls
   * (generateVocal, generateWithVocal) still sing the vocal they are asked for.
   *
   * @param style 0=MelodyLead, 1=BackgroundMotif, 2=SynthDriven
   */
  setCompositionStyle(style: number): this {
    const tracker = new ChangeTracker();

    const oldStyle = this.config.compositionStyle;

    this.config.compositionStyle = style;
    this.markExplicit('compositionStyle');
    tracker.addChange('basic', 'compositionStyle', oldStyle, style, 'User set composition style');
    this.revertDerived('compositionStyle', tracker);

    // SynthDriven typically enables arpeggio
    if (style === CompositionStyle.SynthDriven && !this.explicitFields.has('arpeggioEnabled')) {
      const oldArpeggio = this.config.arpeggioEnabled;
      if (!oldArpeggio) {
        this.deriveField('compositionStyle', 'arpeggio', 'arpeggioEnabled', true);
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
   * Changing style preset resets chord, form, bpm and vocal attitude to the
   * style's defaults unless they were set explicitly. The bpm default is 0,
   * which plays the style's own tempo when the blueprint is drawn at random.
   *
   * @param id Style preset ID (0-16)
   */
  setStylePreset(id: number): this {
    const tracker = new ChangeTracker();

    const oldStyleId = this.config.stylePresetId;
    const defaultConfig = createDefaultConfig(id);

    // Update style preset ID
    this.config.stylePresetId = id;
    this.markExplicit('stylePresetId');
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
    this.markExplicit('drumsEnabled');
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

  /**
   * Record that the user chose a field; the one place explicitness is written.
   * Sets the paired core flag too, so it survives resetKeepExplicit with the value.
   */
  private markExplicit(field: keyof SongConfig): void {
    this.explicitFields.add(field);
    this.derivedFields.delete(field);
    const flag = EXPLICIT_FLAGS[field];
    if (flag !== undefined) {
      this.setConfigValue(flag, true as never);
      this.explicitFields.add(flag);
    }
  }

  /** Write a value a cascade derived from `trigger`, remembering what it replaced. */
  private deriveField<K extends keyof SongConfig>(
    trigger: keyof SongConfig,
    category: ParameterCategory,
    field: K,
    value: SongConfig[K],
  ): void {
    if (!this.derivedFields.has(field)) {
      this.derivedFields.set(field, { trigger, category, previous: this.config[field] });
    }
    this.setConfigValue(field, value);
  }

  /** Undo what the previous value of `trigger` cascaded into fields the user has not set since. */
  private revertDerived(trigger: keyof SongConfig, tracker: ChangeTracker): void {
    for (const [field, derived] of this.derivedFields) {
      if (derived.trigger !== trigger) {
        continue;
      }
      this.derivedFields.delete(field);
      const current = this.config[field];
      if (current === derived.previous) {
        continue;
      }
      this.setConfigValue(field, derived.previous as never);
      tracker.addChange(
        derived.category,
        field,
        current,
        derived.previous,
        `Restored: ${trigger} no longer sets it`,
      );
    }
  }

  private setField(
    field: keyof SongConfig,
    value: SongConfig[keyof SongConfig],
    category: ParameterCategory,
  ): void {
    const oldValue = this.config[field];
    this.setConfigValue(field, value as SongConfig[typeof field]);
    this.markExplicit(field);

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
