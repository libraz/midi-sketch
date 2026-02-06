/**
 * Field mapping table for SongConfig JSON serialization.
 *
 * Single source of truth for JS camelCase <-> C++ snake_case mapping.
 * When adding a new field: add one entry here + update SongConfig type in types.ts.
 */

import type { AccompanimentConfig, SongConfig, VocalConfig } from './types';

interface ConfigField {
  js: keyof SongConfig;
  cpp: string;
  default: number | boolean;
  type: 'number' | 'boolean';
}

interface NestedField {
  js: string;
  cpp: string;
  fields: readonly ConfigField[];
}

// Top-level SongConfig fields
export const CONFIG_FIELDS: readonly ConfigField[] = [
  { js: 'stylePresetId', cpp: 'style_preset_id', default: 0, type: 'number' },
  { js: 'blueprintId', cpp: 'blueprint_id', default: 0, type: 'number' },
  { js: 'mood', cpp: 'mood', default: 0, type: 'number' },
  { js: 'moodExplicit', cpp: 'mood_explicit', default: false, type: 'boolean' },
  { js: 'key', cpp: 'key', default: 0, type: 'number' },
  { js: 'bpm', cpp: 'bpm', default: 0, type: 'number' },
  { js: 'seed', cpp: 'seed', default: 0, type: 'number' },
  { js: 'chordProgressionId', cpp: 'chord_progression_id', default: 0, type: 'number' },
  { js: 'formId', cpp: 'form', default: 0, type: 'number' },
  { js: 'formExplicit', cpp: 'form_explicit', default: false, type: 'boolean' },
  { js: 'targetDurationSeconds', cpp: 'target_duration_seconds', default: 0, type: 'number' },
  { js: 'vocalAttitude', cpp: 'vocal_attitude', default: 0, type: 'number' },
  { js: 'vocalStyle', cpp: 'vocal_style', default: 0, type: 'number' },
  { js: 'driveFeel', cpp: 'drive_feel', default: 50, type: 'number' },
  { js: 'drumsEnabled', cpp: 'drums_enabled', default: true, type: 'boolean' },
  { js: 'arpeggioEnabled', cpp: 'arpeggio_enabled', default: false, type: 'boolean' },
  { js: 'guitarEnabled', cpp: 'guitar_enabled', default: false, type: 'boolean' },
  { js: 'skipVocal', cpp: 'skip_vocal', default: false, type: 'boolean' },
  { js: 'vocalLow', cpp: 'vocal_low', default: 60, type: 'number' },
  { js: 'vocalHigh', cpp: 'vocal_high', default: 79, type: 'number' },
  { js: 'compositionStyle', cpp: 'composition_style', default: 0, type: 'number' },
  { js: 'motifRepeatScope', cpp: 'motif_repeat_scope', default: 0, type: 'number' },
  { js: 'arrangementGrowth', cpp: 'arrangement_growth', default: 0, type: 'number' },
  { js: 'humanize', cpp: 'humanize', default: false, type: 'boolean' },
  { js: 'humanizeTiming', cpp: 'humanize_timing', default: 0.4, type: 'number' },
  { js: 'humanizeVelocity', cpp: 'humanize_velocity', default: 0.3, type: 'number' },
  { js: 'modulationTiming', cpp: 'modulation_timing', default: 0, type: 'number' },
  { js: 'modulationSemitones', cpp: 'modulation_semitones', default: 2, type: 'number' },
  { js: 'seEnabled', cpp: 'se_enabled', default: true, type: 'boolean' },
  { js: 'callEnabled', cpp: 'call_setting', default: 0, type: 'number' },
  { js: 'callNotesEnabled', cpp: 'call_notes_enabled', default: true, type: 'boolean' },
  { js: 'introChant', cpp: 'intro_chant', default: 0, type: 'number' },
  { js: 'mixPattern', cpp: 'mix_pattern', default: 0, type: 'number' },
  { js: 'callDensity', cpp: 'call_density', default: 2, type: 'number' },
  { js: 'melodyTemplate', cpp: 'melody_template', default: 0, type: 'number' },
  { js: 'melodicComplexity', cpp: 'melodic_complexity', default: 1, type: 'number' },
  { js: 'hookIntensity', cpp: 'hook_intensity', default: 2, type: 'number' },
  { js: 'vocalGroove', cpp: 'vocal_groove', default: 0, type: 'number' },
  { js: 'enableSyncopation', cpp: 'enable_syncopation', default: false, type: 'boolean' },
  { js: 'energyCurve', cpp: 'energy_curve', default: 0, type: 'number' },
  { js: 'addictiveMode', cpp: 'addictive_mode', default: false, type: 'boolean' },
  // Melody overrides
  { js: 'melodyMaxLeap', cpp: 'melody_max_leap', default: 0, type: 'number' },
  { js: 'melodySyncopationProb', cpp: 'melody_syncopation_prob', default: 0xff, type: 'number' },
  { js: 'melodyPhraseLength', cpp: 'melody_phrase_length', default: 0, type: 'number' },
  { js: 'melodyLongNoteRatio', cpp: 'melody_long_note_ratio', default: 0xff, type: 'number' },
  {
    js: 'melodyChorusRegisterShift',
    cpp: 'melody_chorus_register_shift',
    default: -128,
    type: 'number',
  },
  { js: 'melodyHookRepetition', cpp: 'melody_hook_repetition', default: 0, type: 'number' },
  { js: 'melodyUseLeadingTone', cpp: 'melody_use_leading_tone', default: 0, type: 'number' },
  // Motif overrides
  { js: 'motifLength', cpp: 'motif_length', default: 0, type: 'number' },
  { js: 'motifNoteCount', cpp: 'motif_note_count', default: 0, type: 'number' },
  { js: 'motifMotion', cpp: 'motif_motion', default: 0xff, type: 'number' },
  { js: 'motifRegisterHigh', cpp: 'motif_register_high', default: 0, type: 'number' },
  { js: 'motifRhythmDensity', cpp: 'motif_rhythm_density', default: 0xff, type: 'number' },
] as const;

// Arpeggio nested struct fields
export const ARPEGGIO_FIELDS: readonly ConfigField[] = [
  { js: 'arpeggioPattern' as keyof SongConfig, cpp: 'pattern', default: 0, type: 'number' },
  { js: 'arpeggioSpeed' as keyof SongConfig, cpp: 'speed', default: 1, type: 'number' },
  {
    js: 'arpeggioOctaveRange' as keyof SongConfig,
    cpp: 'octave_range',
    default: 2,
    type: 'number',
  },
  { js: 'arpeggioGate' as keyof SongConfig, cpp: 'gate', default: 0.8, type: 'number' },
  {
    js: 'arpeggioSyncChord' as keyof SongConfig,
    cpp: 'sync_chord',
    default: true,
    type: 'boolean',
  },
] as const;

// ChordExtension nested struct fields
export const CHORD_EXT_FIELDS: readonly ConfigField[] = [
  { js: 'chordExtSus' as keyof SongConfig, cpp: 'enable_sus', default: false, type: 'boolean' },
  { js: 'chordExt7th' as keyof SongConfig, cpp: 'enable_7th', default: false, type: 'boolean' },
  { js: 'chordExt9th' as keyof SongConfig, cpp: 'enable_9th', default: false, type: 'boolean' },
  {
    js: 'chordExtSusProb' as keyof SongConfig,
    cpp: 'sus_probability',
    default: 0.2,
    type: 'number',
  },
  {
    js: 'chordExt7thProb' as keyof SongConfig,
    cpp: 'seventh_probability',
    default: 0.15,
    type: 'number',
  },
  {
    js: 'chordExt9thProb' as keyof SongConfig,
    cpp: 'ninth_probability',
    default: 0.25,
    type: 'number',
  },
] as const;

// MotifChord nested struct fields
export const MOTIF_CHORD_FIELDS: readonly ConfigField[] = [
  {
    js: 'motifFixedProgression' as keyof SongConfig,
    cpp: 'fixed_progression',
    default: true,
    type: 'boolean',
  },
  {
    js: 'motifMaxChordCount' as keyof SongConfig,
    cpp: 'max_chord_count',
    default: 4,
    type: 'number',
  },
] as const;

// Nested struct definitions
export const NESTED_STRUCTS: readonly NestedField[] = [
  { js: 'arpeggio', cpp: 'arpeggio', fields: ARPEGGIO_FIELDS },
  { js: 'chordExtension', cpp: 'chord_extension', fields: CHORD_EXT_FIELDS },
  { js: 'motifChord', cpp: 'motif_chord', fields: MOTIF_CHORD_FIELDS },
] as const;

// ============================================================================
// VocalConfig fields
// ============================================================================

export const VOCAL_FIELDS: readonly {
  js: string;
  cpp: string;
  default: number | boolean;
  type: 'number' | 'boolean';
}[] = [
  { js: 'seed', cpp: 'seed', default: 0, type: 'number' },
  { js: 'vocalLow', cpp: 'vocal_low', default: 60, type: 'number' },
  { js: 'vocalHigh', cpp: 'vocal_high', default: 79, type: 'number' },
  { js: 'vocalAttitude', cpp: 'vocal_attitude', default: 0, type: 'number' },
  { js: 'vocalStyle', cpp: 'vocal_style', default: 0, type: 'number' },
  { js: 'melodyTemplate', cpp: 'melody_template', default: 0, type: 'number' },
  { js: 'melodicComplexity', cpp: 'melodic_complexity', default: 1, type: 'number' },
  { js: 'hookIntensity', cpp: 'hook_intensity', default: 2, type: 'number' },
  { js: 'vocalGroove', cpp: 'vocal_groove', default: 0, type: 'number' },
  { js: 'compositionStyle', cpp: 'composition_style', default: 0, type: 'number' },
] as const;

// ============================================================================
// AccompanimentConfig fields
// ============================================================================

export const ACCOMPANIMENT_FIELDS: readonly {
  js: string;
  cpp: string;
  default: number | boolean;
  type: 'number' | 'boolean';
}[] = [
  { js: 'seed', cpp: 'seed', default: 0, type: 'number' },
  { js: 'drumsEnabled', cpp: 'drums_enabled', default: true, type: 'boolean' },
  { js: 'arpeggioEnabled', cpp: 'arpeggio_enabled', default: false, type: 'boolean' },
  { js: 'guitarEnabled', cpp: 'guitar_enabled', default: false, type: 'boolean' },
  { js: 'arpeggioPattern', cpp: 'arpeggio_pattern', default: 0, type: 'number' },
  { js: 'arpeggioSpeed', cpp: 'arpeggio_speed', default: 1, type: 'number' },
  { js: 'arpeggioOctaveRange', cpp: 'arpeggio_octave_range', default: 2, type: 'number' },
  { js: 'arpeggioGate', cpp: 'arpeggio_gate', default: 80, type: 'number' },
  { js: 'arpeggioSyncChord', cpp: 'arpeggio_sync_chord', default: true, type: 'boolean' },
  { js: 'chordExtSus', cpp: 'chord_ext_sus', default: false, type: 'boolean' },
  { js: 'chordExt7th', cpp: 'chord_ext_7th', default: false, type: 'boolean' },
  { js: 'chordExt9th', cpp: 'chord_ext_9th', default: false, type: 'boolean' },
  { js: 'chordExtTritoneSub', cpp: 'chord_ext_tritone_sub', default: false, type: 'boolean' },
  { js: 'chordExtSusProb', cpp: 'chord_ext_sus_prob', default: 20, type: 'number' },
  { js: 'chordExt7thProb', cpp: 'chord_ext_7th_prob', default: 30, type: 'number' },
  { js: 'chordExt9thProb', cpp: 'chord_ext_9th_prob', default: 25, type: 'number' },
  { js: 'chordExtTritoneSubProb', cpp: 'chord_ext_tritone_sub_prob', default: 50, type: 'number' },
  { js: 'humanize', cpp: 'humanize', default: false, type: 'boolean' },
  { js: 'humanizeTiming', cpp: 'humanize_timing', default: 50, type: 'number' },
  { js: 'humanizeVelocity', cpp: 'humanize_velocity', default: 50, type: 'number' },
  { js: 'seEnabled', cpp: 'se_enabled', default: true, type: 'boolean' },
  { js: 'callEnabled', cpp: 'call_enabled', default: false, type: 'boolean' },
  { js: 'callDensity', cpp: 'call_density', default: 2, type: 'number' },
  { js: 'introChant', cpp: 'intro_chant', default: 0, type: 'number' },
  { js: 'mixPattern', cpp: 'mix_pattern', default: 0, type: 'number' },
  { js: 'callNotesEnabled', cpp: 'call_notes_enabled', default: true, type: 'boolean' },
] as const;

// ============================================================================
// Serialize VocalConfig / AccompanimentConfig
// ============================================================================

/**
 * Serialize a JS VocalConfig to a C++ snake_case JSON string.
 */
export function serializeVocalConfig(config: VocalConfig): string {
  const obj: Record<string, unknown> = {};
  const c = config as unknown as Record<string, unknown>;
  for (const { js, cpp } of VOCAL_FIELDS) {
    const val = c[js];
    if (val !== undefined) {
      obj[cpp] = val;
    }
  }
  return JSON.stringify(obj);
}

/**
 * Serialize a JS AccompanimentConfig to a C++ snake_case JSON string.
 */
export function serializeAccompanimentConfig(config: AccompanimentConfig): string {
  const obj: Record<string, unknown> = {};
  const c = config as unknown as Record<string, unknown>;
  for (const { js, cpp } of ACCOMPANIMENT_FIELDS) {
    const val = c[js];
    if (val !== undefined) {
      obj[cpp] = val;
    }
  }
  return JSON.stringify(obj);
}

// ============================================================================
// Serialize / Deserialize SongConfig
// ============================================================================

/**
 * Serialize a JS SongConfig to a C++ snake_case JSON string.
 */
export function serializeConfig(config: SongConfig): string {
  const obj: Record<string, unknown> = {};
  const c = config as unknown as Record<string, unknown>;

  for (const { js, cpp } of CONFIG_FIELDS) {
    const val = c[js];
    if (val !== undefined) {
      // callEnabled (bool) -> call_setting (number): true=1(Enabled), false=0(Auto)
      if (js === 'callEnabled') {
        obj[cpp] = val ? 1 : 0;
      } else {
        obj[cpp] = val;
      }
    }
  }

  // Nested structs: flatten from JS top-level fields into C++ nested objects
  for (const { cpp: nestedKey, fields } of NESTED_STRUCTS) {
    const nested: Record<string, unknown> = {};
    for (const { js, cpp } of fields) {
      const val = c[js];
      if (val !== undefined) {
        nested[cpp] = val;
      }
    }
    if (Object.keys(nested).length > 0) {
      obj[nestedKey] = nested;
    }
  }

  return JSON.stringify(obj);
}

/**
 * Deserialize a C++ snake_case JSON string to a JS SongConfig.
 */
export function deserializeConfig(json: string): SongConfig {
  const obj = JSON.parse(json) as Record<string, unknown>;
  const config: Record<string, unknown> = {};

  for (const { js, cpp, default: def } of CONFIG_FIELDS) {
    // call_setting (number) -> callEnabled (bool): 0=Auto(false), 1=Enabled(true)
    if (js === 'callEnabled') {
      config[js] = (obj[cpp] ?? def) !== 0;
    } else {
      config[js] = obj[cpp] ?? def;
    }
  }

  // Nested structs: unflatten from C++ nested objects to JS top-level fields
  for (const { cpp: nestedKey, fields } of NESTED_STRUCTS) {
    const nested = obj[nestedKey] as Record<string, unknown> | undefined;
    if (nested) {
      for (const { js, cpp, default: def } of fields) {
        config[js] = nested[cpp] ?? def;
      }
    }
  }

  return config as unknown as SongConfig;
}
