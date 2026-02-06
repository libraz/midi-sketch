/**
 * midi-sketch - MIDI Auto-Generation Library
 *
 * @example
 * ```typescript
 * import { MidiSketch, init, createDefaultConfig } from '@libraz/midi-sketch';
 *
 * await init();
 * const sketch = new MidiSketch();
 * const config = createDefaultConfig(0);
 * sketch.generateFromConfig(config);
 * const midiData = sketch.getMidi();
 * ```
 */

// Blueprint
export {
  type BlueprintInfo,
  GenerationParadigm,
  type GenerationParadigmType,
  getBlueprintCount,
  getBlueprintName,
  getBlueprintParadigm,
  getBlueprintRiffPolicy,
  getBlueprints,
  getBlueprintWeight,
  RiffPolicy,
  type RiffPolicyType,
} from './blueprint';
// Builder (will be added later)
export {
  type ParameterCategory,
  type ParameterChange,
  type ParameterChangeResult,
  SongConfigBuilder,
} from './builder';
// Config
export { createDefaultConfig, getConfigErrorMessage, validateConfig } from './config';
export {
  deserializeConfig,
  serializeAccompanimentConfig,
  serializeConfig,
  serializeVocalConfig,
} from './config-fields';
// Constants
export {
  ArrangementGrowth,
  ATTITUDE_CLEAN,
  ATTITUDE_EXPRESSIVE,
  ATTITUDE_RAW,
  CallDensity,
  CompositionStyle,
  ConfigError,
  type ConfigErrorCode,
  HookIntensity,
  IntroChant,
  MelodicComplexity,
  MidiSketchConfigError,
  MidiSketchGenerationError,
  MixPattern,
  ModulationTiming,
  MotifRepeatScope,
  VocalAttitude,
  VocalGrooveFeel,
  VocalStylePreset,
} from './constants';
// Internal (init only)
export { init } from './internal';
// MidiSketch class
export { default, MidiSketch } from './midi-sketch';
// Presets
export {
  getChords,
  getFormsByStyle,
  getMoods,
  getProgressionsByStyle,
  getStructures,
  getStylePresets,
} from './presets';
// Types
export type {
  AccompanimentConfig,
  CollisionInfo,
  EventData,
  NoteInput,
  NoteReasonFlags,
  NoteSafetyLevel,
  PianoRollInfo,
  PresetInfo,
  SongConfig,
  StylePresetInfo,
  VocalConfig,
} from './types';
export { NoteReason, NoteSafety } from './types';
// Utils
export { downloadMidi, getVersion } from './utils';
