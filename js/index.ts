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

// Types for Emscripten module
interface EmscriptenModule {
  cwrap: (
    name: string,
    returnType: string | null,
    argTypes: string[],
  ) => (...args: unknown[]) => unknown;
  UTF8ToString: (ptr: number) => string;
  _malloc: (size: number) => number;
  _free: (ptr: number) => void;
  HEAPU8: Uint8Array;
  HEAPU32: Uint32Array;
}

interface Api {
  create: () => number;
  destroy: (handle: number) => void;
  getMidi: (handle: number) => number;
  freeMidi: (ptr: number) => void;
  getEvents: (handle: number) => number;
  freeEvents: (ptr: number) => void;
  structureCount: () => number;
  moodCount: () => number;
  chordCount: () => number;
  structureName: (id: number) => string;
  moodName: (id: number) => string;
  chordName: (id: number) => string;
  chordDisplay: (id: number) => string;
  moodDefaultBpm: (id: number) => number;
  version: () => string;
  stylePresetCount: () => number;
  stylePresetName: (id: number) => string;
  stylePresetDisplayName: (id: number) => string;
  stylePresetDescription: (id: number) => string;
  stylePresetTempoDefault: (id: number) => number;
  stylePresetAllowedAttitudes: (id: number) => number;
  getProgressionsByStylePtr: (styleId: number) => number;
  getFormsByStylePtr: (styleId: number) => number;
  createDefaultConfigPtr: (styleId: number) => number;
  validateConfig: (configPtr: number) => number;
  generateFromConfig: (handle: number, configPtr: number) => number;
  configErrorString: (error: number) => string;
  // Vocal-first generation APIs
  generateVocal: (handle: number, configPtr: number) => number;
  regenerateVocal: (handle: number, configPtr: number) => number;
  generateAccompaniment: (handle: number) => number;
  generateAccompanimentWithConfig: (handle: number, configPtr: number) => number;
  regenerateAccompaniment: (handle: number, seed: number) => number;
  regenerateAccompanimentWithConfig: (handle: number, configPtr: number) => number;
  generateWithVocal: (handle: number, configPtr: number) => number;
  setVocalNotes: (handle: number, configPtr: number, notesPtr: number, count: number) => number;
  // Piano Roll Safety API
  getPianoRollSafety: (handle: number, startTick: number, endTick: number, step: number) => number;
  getPianoRollSafetyAt: (handle: number, tick: number) => number;
  getPianoRollSafetyWithContext: (handle: number, tick: number, prevPitch: number) => number;
  freePianoRollData: (ptr: number) => void;
  reasonToString: (reason: number) => string;
  collisionToString: (collisionPtr: number) => string;
}

/**
 * Config validation error codes
 */
export const ConfigError = {
  OK: 0,
  InvalidStyle: 1,
  InvalidChord: 2,
  InvalidForm: 3,
  InvalidAttitude: 4,
  InvalidVocalRange: 5,
  InvalidBpm: 6,
  DurationTooShort: 7,
  InvalidModulation: 8,
  InvalidKey: 9,
  InvalidCompositionStyle: 10,
  InvalidArpeggioPattern: 11,
  InvalidArpeggioSpeed: 12,
  InvalidVocalStyle: 13,
  InvalidMelodyTemplate: 14,
  InvalidMelodicComplexity: 15,
  InvalidHookIntensity: 16,
  InvalidVocalGroove: 17,
  InvalidCallDensity: 18,
  InvalidIntroChant: 19,
  InvalidMixPattern: 20,
  InvalidMotifRepeatScope: 21,
  InvalidArrangementGrowth: 22,
  InvalidModulationTiming: 23,
} as const;

export type ConfigErrorCode = (typeof ConfigError)[keyof typeof ConfigError];

/**
 * Custom error class for MidiSketch configuration errors
 */
export class MidiSketchConfigError extends Error {
  /** Numeric error code */
  readonly code: ConfigErrorCode;
  /** Human-readable error message from native library */
  readonly nativeMessage: string;

  constructor(code: number, nativeMessage: string) {
    super(`MidiSketch config error [${code}]: ${nativeMessage}`);
    this.name = 'MidiSketchConfigError';
    this.code = code as ConfigErrorCode;
    this.nativeMessage = nativeMessage;
  }
}

/**
 * Custom error class for MidiSketch generation errors
 */
export class MidiSketchGenerationError extends Error {
  /** Numeric error code */
  readonly code: number;

  constructor(code: number, message: string) {
    super(message);
    this.name = 'MidiSketchGenerationError';
    this.code = code;
  }
}

/**
 * Song configuration for style-based generation
 */
export interface SongConfig {
  // Basic settings
  /** Style preset ID */
  stylePresetId: number;
  /** Key (0-11) */
  key: number;
  /** BPM (0 = use style default) */
  bpm: number;
  /** Random seed (0 = random) */
  seed: number;
  /** Chord progression ID */
  chordProgressionId: number;
  /** Form/structure pattern ID */
  formId: number;
  /** Vocal attitude: 0=Clean, 1=Expressive, 2=Raw */
  vocalAttitude: number;
  /** Enable drums */
  drumsEnabled: boolean;

  // Arpeggio settings
  /** Enable arpeggio */
  arpeggioEnabled: boolean;
  /** Arpeggio pattern: 0=Up, 1=Down, 2=UpDown, 3=Random */
  arpeggioPattern: number;
  /** Arpeggio speed: 0=Eighth, 1=Sixteenth, 2=Triplet */
  arpeggioSpeed: number;
  /** Arpeggio octave range (1-3) */
  arpeggioOctaveRange: number;
  /** Arpeggio gate length (0-100) */
  arpeggioGate: number;

  // Vocal settings
  /** Vocal range lower bound (MIDI note) */
  vocalLow: number;
  /** Vocal range upper bound (MIDI note) */
  vocalHigh: number;
  /** Skip vocal generation (for BGM-first workflow) */
  skipVocal: boolean;

  // Humanization
  /** Enable humanization */
  humanize: boolean;
  /** Timing variation (0-100) */
  humanizeTiming: number;
  /** Velocity variation (0-100) */
  humanizeVelocity: number;

  // Chord extensions
  /** Enable sus2/sus4 chords */
  chordExtSus: boolean;
  /** Enable 7th chords */
  chordExt7th: boolean;
  /** Enable 9th chords */
  chordExt9th: boolean;
  /** Sus chord probability (0-100) */
  chordExtSusProb: number;
  /** 7th chord probability (0-100) */
  chordExt7thProb: number;
  /** 9th chord probability (0-100) */
  chordExt9thProb: number;

  // Composition style
  /** Composition style: 0=MelodyLead, 1=BackgroundMotif, 2=SynthDriven */
  compositionStyle: number;

  // Duration
  /** Target duration in seconds (0 = use formId) */
  targetDurationSeconds: number;

  // Modulation settings
  /** Modulation timing: 0=None, 1=LastChorus, 2=AfterBridge, 3=EachChorus, 4=Random */
  modulationTiming: number;
  /** Modulation semitones (+1 to +4) */
  modulationSemitones: number;

  // SE/Call settings
  /** Enable SE track */
  seEnabled: boolean;
  /** Enable call feature */
  callEnabled: boolean;
  /** Output calls as notes */
  callNotesEnabled: boolean;
  /** Intro chant: 0=None, 1=Gachikoi, 2=Shouting */
  introChant: number;
  /** Mix pattern: 0=None, 1=Standard, 2=Tiger */
  mixPattern: number;
  /** Call density: 0=None, 1=Minimal, 2=Standard, 3=Intense */
  callDensity: number;

  // Vocal style settings
  /** Vocal style preset: 0=Auto, 1=Standard, 2=Vocaloid, etc. */
  vocalStyle: number;
  /** Melody template: 0=Auto, 1=PlateauTalk, 2=RunUpTarget, etc. */
  melodyTemplate: number;

  // Arrangement settings
  /** Arrangement growth: 0=LayerAdd, 1=RegisterAdd */
  arrangementGrowth: number;

  // Arpeggio sync settings
  /** Sync arpeggio with chord changes (default=true) */
  arpeggioSyncChord: boolean;

  // Motif settings (for BackgroundMotif style)
  /** Motif repeat scope: 0=FullSong, 1=Section */
  motifRepeatScope: number;
  /** Same progression for all sections (default=true) */
  motifFixedProgression: boolean;
  /** Max chord count (0=no limit, 2-8) */
  motifMaxChordCount: number;

  // Melodic complexity and hook control
  /** Melodic complexity: 0=Simple, 1=Standard, 2=Complex */
  melodicComplexity: number;
  /** Hook intensity: 0=Off, 1=Light, 2=Normal, 3=Strong */
  hookIntensity: number;
  /** Vocal groove feel: 0=Straight, 1=OffBeat, 2=Swing, 3=Syncopated, 4=Driving16th, 5=Bouncy8th */
  vocalGroove: number;
}

/**
 * Note input for custom vocal track
 */
export interface NoteInput {
  /** Note start time in ticks */
  startTick: number;
  /** Note duration in ticks */
  duration: number;
  /** MIDI note number (0-127) */
  pitch: number;
  /** Note velocity (0-127) */
  velocity: number;
}

// ============================================================================
// Piano Roll Safety API Types
// ============================================================================

/**
 * Note safety level for piano roll visualization
 */
export const NoteSafety = {
  /** Green: chord tone, safe to use */
  Safe: 0,
  /** Yellow: tension, low register, or passing tone */
  Warning: 1,
  /** Red: dissonant or out of range */
  Dissonant: 2,
} as const;

export type NoteSafetyLevel = (typeof NoteSafety)[keyof typeof NoteSafety];

/**
 * Reason flags for note safety (bitfield, can be combined)
 */
export const NoteReason = {
  None: 0,
  // Positive reasons (green)
  ChordTone: 1, // Chord tone (root, 3rd, 5th, 7th)
  Tension: 2, // Tension (9th, 11th, 13th)
  ScaleTone: 4, // Scale tone (not chord but in scale)
  // Warning reasons (yellow)
  LowRegister: 8, // Low register (below C4), may sound muddy
  Tritone: 16, // Tritone interval (unstable except on V7)
  LargeLeap: 32, // Large leap (6+ semitones from prev note)
  // Dissonant reasons (red)
  Minor2nd: 64, // Minor 2nd (1 semitone) collision
  Major7th: 128, // Major 7th (11 semitones) collision
  NonScale: 256, // Non-scale tone (chromatic)
  PassingTone: 512, // Can be used as passing tone
  // Out of range reasons (red)
  OutOfRange: 1024, // Outside vocal range
  TooHigh: 2048, // Too high to sing
  TooLow: 4096, // Too low to sing
} as const;

export type NoteReasonFlags = number;

/**
 * Collision info for a note that collides with BGM
 */
export interface CollisionInfo {
  /** Track role of colliding track */
  trackRole: number;
  /** MIDI pitch of colliding note */
  collidingPitch: number;
  /** Collision interval in semitones (1, 6, or 11) */
  intervalSemitones: number;
}

/**
 * Piano roll safety info for a single tick
 */
export interface PianoRollInfo {
  /** Tick position */
  tick: number;
  /** Current chord degree (0=I, 1=ii, etc.) */
  chordDegree: number;
  /** Current key (0-11, considering modulation) */
  currentKey: number;
  /** Safety level for each MIDI note (0-127) */
  safety: NoteSafetyLevel[];
  /** Reason flags for each note (0-127) */
  reason: NoteReasonFlags[];
  /** Collision details for each note */
  collision: CollisionInfo[];
  /** Recommended notes (priority order, max 8) */
  recommended: number[];
}

/**
 * Vocal regeneration configuration
 */
export interface VocalConfig {
  /** Random seed (0 = new random) */
  seed?: number;
  /** Vocal range lower bound (MIDI note, 36-96) */
  vocalLow?: number;
  /** Vocal range upper bound (MIDI note, 36-96) */
  vocalHigh?: number;
  /** Vocal attitude: 0=Clean, 1=Expressive, 2=Raw */
  vocalAttitude?: number;
  /** Vocal style preset: 0=Auto, 1=Standard, 2=Vocaloid, etc. */
  vocalStyle?: number;
  /** Melody template: 0=Auto, 1=PlateauTalk, 2=RunUpTarget, etc. */
  melodyTemplate?: number;
  /** Melodic complexity: 0=Simple, 1=Standard, 2=Complex */
  melodicComplexity?: number;
  /** Hook intensity: 0=Off, 1=Light, 2=Normal, 3=Strong */
  hookIntensity?: number;
  /** Vocal groove feel: 0=Straight, 1=OffBeat, 2=Swing, etc. */
  vocalGroove?: number;
  /** Composition style: 0=MelodyLead, 1=BackgroundMotif, 2=SynthDriven */
  compositionStyle?: number;
}

/**
 * Configuration for accompaniment generation/regeneration.
 */
export interface AccompanimentConfig {
  /** Random seed for BGM (0 = auto-generate) */
  seed?: number;

  // Drums
  /** Enable drums */
  drumsEnabled?: boolean;

  // Arpeggio
  /** Enable arpeggio */
  arpeggioEnabled?: boolean;
  /** Arpeggio pattern: 0=Up, 1=Down, 2=UpDown, 3=Random */
  arpeggioPattern?: number;
  /** Arpeggio speed: 0=Eighth, 1=Sixteenth, 2=Triplet */
  arpeggioSpeed?: number;
  /** Arpeggio octave range: 1-3 */
  arpeggioOctaveRange?: number;
  /** Arpeggio gate length: 0-100 */
  arpeggioGate?: number;
  /** Sync arpeggio with chord changes */
  arpeggioSyncChord?: boolean;

  // Chord Extensions
  /** Enable sus chord extension */
  chordExtSus?: boolean;
  /** Enable 7th chord extension */
  chordExt7th?: boolean;
  /** Enable 9th chord extension */
  chordExt9th?: boolean;
  /** Sus probability: 0-100 */
  chordExtSusProb?: number;
  /** 7th probability: 0-100 */
  chordExt7thProb?: number;
  /** 9th probability: 0-100 */
  chordExt9thProb?: number;

  // Humanization
  /** Enable humanization */
  humanize?: boolean;
  /** Timing variation: 0-100 */
  humanizeTiming?: number;
  /** Velocity variation: 0-100 */
  humanizeVelocity?: number;

  // SE
  /** Enable SE track */
  seEnabled?: boolean;

  // Call System
  /** Enable call system */
  callEnabled?: boolean;
  /** Call density: 0=Sparse, 1=Light, 2=Standard, 3=Dense */
  callDensity?: number;
  /** Intro chant: 0=None, 1=Gachikoi, 2=Mix */
  introChant?: number;
  /** Mix pattern: 0=None, 1=Standard, 2=Tiger */
  mixPattern?: number;
  /** Output call as MIDI notes */
  callNotesEnabled?: boolean;
}

/**
 * Preset information
 */
export interface PresetInfo {
  /** Preset name */
  name: string;
  /** Display string (for chords) */
  display?: string;
  /** Default BPM (for moods) */
  defaultBpm?: number;
}

/**
 * Style preset information
 */
export interface StylePresetInfo {
  /** Style preset ID */
  id: number;
  /** Internal name */
  name: string;
  /** Display name */
  displayName: string;
  /** Description */
  description: string;
  /** Default tempo */
  tempoDefault: number;
  /** Bit flags for allowed vocal attitudes */
  allowedAttitudes: number;
}

/**
 * Event data from generation
 */
export interface EventData {
  bpm: number;
  division: number;
  duration_ticks: number;
  duration_seconds: number;
  tracks: Array<{
    name: string;
    channel: number;
    program: number;
    notes: Array<{
      pitch: number;
      velocity: number;
      start_ticks: number;
      duration_ticks: number;
      start_seconds: number;
      duration_seconds: number;
    }>;
  }>;
  sections: Array<{
    name: string;
    type: string;
    startTick: number;
    endTick: number;
    start_bar: number;
    bars: number;
    start_seconds: number;
    end_seconds: number;
  }>;
}

// Vocal attitude constants
export const VocalAttitude = {
  Clean: 0,
  Expressive: 1,
  Raw: 2,
} as const;

// Composition style constants
export const CompositionStyle = {
  MelodyLead: 0,
  BackgroundMotif: 1,
  SynthDriven: 2,
} as const;

// Attitude bit flags
export const ATTITUDE_CLEAN = 1 << 0;
export const ATTITUDE_EXPRESSIVE = 1 << 1;
export const ATTITUDE_RAW = 1 << 2;

// Modulation timing constants
export const ModulationTiming = {
  None: 0,
  LastChorus: 1,
  AfterBridge: 2,
  EachChorus: 3,
  Random: 4,
} as const;

// Intro chant constants
export const IntroChant = {
  None: 0,
  Gachikoi: 1,
  Shouting: 2,
} as const;

// Mix pattern constants
export const MixPattern = {
  None: 0,
  Standard: 1,
  Tiger: 2,
} as const;

// Call density constants
export const CallDensity = {
  None: 0,
  Minimal: 1,
  Standard: 2,
  Intense: 3,
} as const;

// Arrangement growth constants
export const ArrangementGrowth = {
  LayerAdd: 0,
  RegisterAdd: 1,
} as const;

// Motif repeat scope constants
export const MotifRepeatScope = {
  FullSong: 0,
  Section: 1,
} as const;

// Melodic complexity constants
export const MelodicComplexity = {
  Simple: 0,
  Standard: 1,
  Complex: 2,
} as const;

// Hook intensity constants
export const HookIntensity = {
  Off: 0,
  Light: 1,
  Normal: 2,
  Strong: 3,
} as const;

// Vocal groove feel constants
export const VocalGrooveFeel = {
  Straight: 0,
  OffBeat: 1,
  Swing: 2,
  Syncopated: 3,
  Driving16th: 4,
  Bouncy8th: 5,
} as const;

// Vocal style preset constants
export const VocalStylePreset = {
  Auto: 0,
  Standard: 1,
  Vocaloid: 2,
  UltraVocaloid: 3,
  Idol: 4,
  Ballad: 5,
  Rock: 6,
  CityPop: 7,
  Anime: 8,
  // Extended styles (9-12)
  BrightKira: 9,
  CoolSynth: 10,
  CuteAffected: 11,
  PowerfulShout: 12,
} as const;

let moduleInstance: EmscriptenModule | null = null;
let api: Api | null = null;

function getModule(): EmscriptenModule {
  if (!moduleInstance) {
    throw new Error('Module not initialized. Call init() first.');
  }
  return moduleInstance;
}

function getApi(): Api {
  if (!api) {
    throw new Error('Module not initialized. Call init() first.');
  }
  return api;
}

/**
 * Initialize the WASM module
 */
export async function init(options?: { wasmPath?: string }): Promise<void> {
  if (moduleInstance) {
    return;
  }

  const createModule = await import('./midisketch.js');
  moduleInstance = await createModule.default({
    locateFile: (path: string) => {
      if (path.endsWith('.wasm') && options?.wasmPath) {
        return options.wasmPath;
      }
      return path;
    },
  });

  const m = moduleInstance;

  // Setup cwrap bindings with proper types
  api = {
    create: m.cwrap('midisketch_create', 'number', []) as () => number,
    destroy: m.cwrap('midisketch_destroy', null, ['number']) as (handle: number) => void,
    getMidi: m.cwrap('midisketch_get_midi', 'number', ['number']) as (handle: number) => number,
    freeMidi: m.cwrap('midisketch_free_midi', null, ['number']) as (ptr: number) => void,
    getEvents: m.cwrap('midisketch_get_events', 'number', ['number']) as (handle: number) => number,
    freeEvents: m.cwrap('midisketch_free_events', null, ['number']) as (ptr: number) => void,
    structureCount: m.cwrap('midisketch_structure_count', 'number', []) as () => number,
    moodCount: m.cwrap('midisketch_mood_count', 'number', []) as () => number,
    chordCount: m.cwrap('midisketch_chord_count', 'number', []) as () => number,
    structureName: m.cwrap('midisketch_structure_name', 'string', ['number']) as (
      id: number,
    ) => string,
    moodName: m.cwrap('midisketch_mood_name', 'string', ['number']) as (id: number) => string,
    chordName: m.cwrap('midisketch_chord_name', 'string', ['number']) as (id: number) => string,
    chordDisplay: m.cwrap('midisketch_chord_display', 'string', ['number']) as (
      id: number,
    ) => string,
    moodDefaultBpm: m.cwrap('midisketch_mood_default_bpm', 'number', ['number']) as (
      id: number,
    ) => number,
    version: m.cwrap('midisketch_version', 'string', []) as () => string,
    stylePresetCount: m.cwrap('midisketch_style_preset_count', 'number', []) as () => number,
    stylePresetName: m.cwrap('midisketch_style_preset_name', 'string', ['number']) as (
      id: number,
    ) => string,
    stylePresetDisplayName: m.cwrap('midisketch_style_preset_display_name', 'string', [
      'number',
    ]) as (id: number) => string,
    stylePresetDescription: m.cwrap('midisketch_style_preset_description', 'string', [
      'number',
    ]) as (id: number) => string,
    stylePresetTempoDefault: m.cwrap('midisketch_style_preset_tempo_default', 'number', [
      'number',
    ]) as (id: number) => number,
    stylePresetAllowedAttitudes: m.cwrap('midisketch_style_preset_allowed_attitudes', 'number', [
      'number',
    ]) as (id: number) => number,
    getProgressionsByStylePtr: m.cwrap('midisketch_get_progressions_by_style_ptr', 'number', [
      'number',
    ]) as (styleId: number) => number,
    getFormsByStylePtr: m.cwrap('midisketch_get_forms_by_style_ptr', 'number', ['number']) as (
      styleId: number,
    ) => number,
    createDefaultConfigPtr: m.cwrap('midisketch_create_default_config_ptr', 'number', [
      'number',
    ]) as (styleId: number) => number,
    validateConfig: m.cwrap('midisketch_validate_config', 'number', ['number']) as (
      configPtr: number,
    ) => number,
    generateFromConfig: m.cwrap('midisketch_generate_from_config', 'number', [
      'number',
      'number',
    ]) as (handle: number, configPtr: number) => number,
    configErrorString: m.cwrap('midisketch_config_error_string', 'string', ['number']) as (
      error: number,
    ) => string,
    // Vocal-first generation APIs
    generateVocal: m.cwrap('midisketch_generate_vocal', 'number', ['number', 'number']) as (
      handle: number,
      configPtr: number,
    ) => number,
    regenerateVocal: m.cwrap('midisketch_regenerate_vocal', 'number', ['number', 'number']) as (
      handle: number,
      configPtr: number,
    ) => number,
    generateAccompaniment: m.cwrap('midisketch_generate_accompaniment', 'number', ['number']) as (
      handle: number,
    ) => number,
    generateAccompanimentWithConfig: m.cwrap(
      'midisketch_generate_accompaniment_with_config',
      'number',
      ['number', 'number'],
    ) as (handle: number, configPtr: number) => number,
    regenerateAccompaniment: m.cwrap('midisketch_regenerate_accompaniment', 'number', [
      'number',
      'number',
    ]) as (handle: number, seed: number) => number,
    regenerateAccompanimentWithConfig: m.cwrap(
      'midisketch_regenerate_accompaniment_with_config',
      'number',
      ['number', 'number'],
    ) as (handle: number, configPtr: number) => number,
    generateWithVocal: m.cwrap('midisketch_generate_with_vocal', 'number', [
      'number',
      'number',
    ]) as (handle: number, configPtr: number) => number,
    setVocalNotes: m.cwrap('midisketch_set_vocal_notes', 'number', [
      'number',
      'number',
      'number',
      'number',
    ]) as (handle: number, configPtr: number, notesPtr: number, count: number) => number,
    // Piano Roll Safety API
    getPianoRollSafety: m.cwrap('midisketch_get_piano_roll_safety', 'number', [
      'number',
      'number',
      'number',
      'number',
    ]) as (handle: number, startTick: number, endTick: number, step: number) => number,
    getPianoRollSafetyAt: m.cwrap('midisketch_get_piano_roll_safety_at', 'number', [
      'number',
      'number',
    ]) as (handle: number, tick: number) => number,
    getPianoRollSafetyWithContext: m.cwrap(
      'midisketch_get_piano_roll_safety_with_context',
      'number',
      ['number', 'number', 'number'],
    ) as (handle: number, tick: number, prevPitch: number) => number,
    freePianoRollData: m.cwrap('midisketch_free_piano_roll_data', null, ['number']) as (
      ptr: number,
    ) => void,
    reasonToString: m.cwrap('midisketch_reason_to_string', 'string', ['number']) as (
      reason: number,
    ) => string,
    collisionToString: m.cwrap('midisketch_collision_to_string', 'string', ['number']) as (
      collisionPtr: number,
    ) => string,
  };
}

/**
 * Get library version
 */
export function getVersion(): string {
  return getApi().version();
}

/**
 * Get structure presets
 */
export function getStructures(): PresetInfo[] {
  const a = getApi();
  const count = a.structureCount();
  const result: PresetInfo[] = [];
  for (let i = 0; i < count; i++) {
    result.push({ name: a.structureName(i) });
  }
  return result;
}

/**
 * Get mood presets
 */
export function getMoods(): PresetInfo[] {
  const a = getApi();
  const count = a.moodCount();
  const result: PresetInfo[] = [];
  for (let i = 0; i < count; i++) {
    result.push({
      name: a.moodName(i),
      defaultBpm: a.moodDefaultBpm(i),
    });
  }
  return result;
}

/**
 * Get chord progression presets
 */
export function getChords(): PresetInfo[] {
  const a = getApi();
  const count = a.chordCount();
  const result: PresetInfo[] = [];
  for (let i = 0; i < count; i++) {
    result.push({
      name: a.chordName(i),
      display: a.chordDisplay(i),
    });
  }
  return result;
}

/**
 * Get style presets
 */
export function getStylePresets(): StylePresetInfo[] {
  const a = getApi();
  const count = a.stylePresetCount();
  const result: StylePresetInfo[] = [];
  for (let i = 0; i < count; i++) {
    result.push({
      id: i,
      name: a.stylePresetName(i),
      displayName: a.stylePresetDisplayName(i),
      description: a.stylePresetDescription(i),
      tempoDefault: a.stylePresetTempoDefault(i),
      allowedAttitudes: a.stylePresetAllowedAttitudes(i),
    });
  }
  return result;
}

/**
 * Get chord progressions compatible with a style
 */
export function getProgressionsByStyle(styleId: number): number[] {
  const a = getApi();
  const m = getModule();
  const retPtr = a.getProgressionsByStylePtr(styleId);
  const view = new DataView(m.HEAPU8.buffer);
  const count = view.getUint8(retPtr);
  const result: number[] = [];
  for (let i = 0; i < count; i++) {
    result.push(view.getUint8(retPtr + 1 + i));
  }
  return result;
}

/**
 * Get forms compatible with a style
 */
export function getFormsByStyle(styleId: number): number[] {
  const a = getApi();
  const m = getModule();
  const retPtr = a.getFormsByStylePtr(styleId);
  const view = new DataView(m.HEAPU8.buffer);
  const count = view.getUint8(retPtr);
  const result: number[] = [];
  for (let i = 0; i < count; i++) {
    result.push(view.getUint8(retPtr + 1 + i));
  }
  return result;
}

/**
 * Create a default song config for a style
 */
export function createDefaultConfig(styleId: number): SongConfig {
  const a = getApi();
  const m = getModule();
  const retPtr = a.createDefaultConfigPtr(styleId);
  const view = new DataView(m.HEAPU8.buffer);
  return {
    // Basic settings
    stylePresetId: view.getUint8(retPtr + 0),
    key: view.getUint8(retPtr + 1),
    bpm: view.getUint16(retPtr + 2, true),
    seed: view.getUint32(retPtr + 4, true),
    chordProgressionId: view.getUint8(retPtr + 8),
    formId: view.getUint8(retPtr + 9),
    vocalAttitude: view.getUint8(retPtr + 10),
    drumsEnabled: view.getUint8(retPtr + 11) !== 0,

    // Arpeggio settings
    arpeggioEnabled: view.getUint8(retPtr + 12) !== 0,
    arpeggioPattern: view.getUint8(retPtr + 13),
    arpeggioSpeed: view.getUint8(retPtr + 14),
    arpeggioOctaveRange: view.getUint8(retPtr + 15),
    arpeggioGate: view.getUint8(retPtr + 16),

    // Vocal settings
    vocalLow: view.getUint8(retPtr + 17),
    vocalHigh: view.getUint8(retPtr + 18),
    skipVocal: view.getUint8(retPtr + 19) !== 0,

    // Humanization
    humanize: view.getUint8(retPtr + 20) !== 0,
    humanizeTiming: view.getUint8(retPtr + 21),
    humanizeVelocity: view.getUint8(retPtr + 22),

    // Chord extensions
    chordExtSus: view.getUint8(retPtr + 23) !== 0,
    chordExt7th: view.getUint8(retPtr + 24) !== 0,
    chordExt9th: view.getUint8(retPtr + 25) !== 0,
    chordExtSusProb: view.getUint8(retPtr + 26),
    chordExt7thProb: view.getUint8(retPtr + 27),
    chordExt9thProb: view.getUint8(retPtr + 28),

    // Composition style
    compositionStyle: view.getUint8(retPtr + 29),

    // Duration
    targetDurationSeconds: view.getUint16(retPtr + 32, true),

    // Modulation settings
    modulationTiming: view.getUint8(retPtr + 34),
    modulationSemitones: view.getInt8(retPtr + 35),

    // SE/Call settings (offset 36-41)
    seEnabled: view.getUint8(retPtr + 36) !== 0,
    callEnabled: view.getUint8(retPtr + 37) !== 0,
    callNotesEnabled: view.getUint8(retPtr + 38) !== 0,
    introChant: view.getUint8(retPtr + 39),
    mixPattern: view.getUint8(retPtr + 40),
    callDensity: view.getUint8(retPtr + 41),

    // Vocal style settings (offset 42-43)
    vocalStyle: view.getUint8(retPtr + 42),
    melodyTemplate: view.getUint8(retPtr + 43),

    // Arrangement settings (offset 44)
    arrangementGrowth: view.getUint8(retPtr + 44),

    // Arpeggio sync settings (offset 45)
    arpeggioSyncChord: view.getUint8(retPtr + 45) !== 0,

    // Motif settings (offset 46-48)
    motifRepeatScope: view.getUint8(retPtr + 46),
    motifFixedProgression: view.getUint8(retPtr + 47) !== 0,
    motifMaxChordCount: view.getUint8(retPtr + 48),

    // Melodic complexity and hook control (offset 49-51)
    melodicComplexity: view.getUint8(retPtr + 49),
    hookIntensity: view.getUint8(retPtr + 50),
    vocalGroove: view.getUint8(retPtr + 51),
  };
}

/**
 * Validate a song config before generation.
 * Returns the error code (0 = OK, non-zero = error).
 * Use getConfigErrorMessage() to get human-readable error message.
 */
export function validateConfig(config: SongConfig): ConfigErrorCode {
  const a = getApi();
  const m = getModule();
  const configPtr = allocSongConfigStatic(m, config);
  try {
    return a.validateConfig(configPtr) as ConfigErrorCode;
  } finally {
    m._free(configPtr);
  }
}

/**
 * Get human-readable error message for a config error code.
 */
export function getConfigErrorMessage(errorCode: ConfigErrorCode): string {
  const a = getApi();
  return a.configErrorString(errorCode);
}

// Helper function to allocate SongConfig without class context
function allocSongConfigStatic(m: EmscriptenModule, config: SongConfig): number {
  const ptr = m._malloc(52);
  const view = new DataView(m.HEAPU8.buffer);

  // Basic settings (offset 0-11)
  view.setUint8(ptr + 0, config.stylePresetId ?? 0);
  view.setUint8(ptr + 1, config.key ?? 0);
  view.setUint16(ptr + 2, config.bpm ?? 0, true);
  view.setUint32(ptr + 4, config.seed ?? 0, true);
  view.setUint8(ptr + 8, config.chordProgressionId ?? 0);
  view.setUint8(ptr + 9, config.formId ?? 0);
  view.setUint8(ptr + 10, config.vocalAttitude ?? 0);
  view.setUint8(ptr + 11, config.drumsEnabled !== false ? 1 : 0);

  // Arpeggio settings (offset 12-16)
  view.setUint8(ptr + 12, config.arpeggioEnabled ? 1 : 0);
  view.setUint8(ptr + 13, config.arpeggioPattern ?? 0);
  view.setUint8(ptr + 14, config.arpeggioSpeed ?? 1);
  view.setUint8(ptr + 15, config.arpeggioOctaveRange ?? 2);
  view.setUint8(ptr + 16, config.arpeggioGate ?? 80);

  // Vocal settings (offset 17-19)
  view.setUint8(ptr + 17, config.vocalLow ?? 55);
  view.setUint8(ptr + 18, config.vocalHigh ?? 74);
  view.setUint8(ptr + 19, config.skipVocal ? 1 : 0);

  // Humanization (offset 20-22)
  view.setUint8(ptr + 20, config.humanize ? 1 : 0);
  view.setUint8(ptr + 21, config.humanizeTiming ?? 50);
  view.setUint8(ptr + 22, config.humanizeVelocity ?? 50);

  // Chord extensions (offset 23-28)
  view.setUint8(ptr + 23, config.chordExtSus ? 1 : 0);
  view.setUint8(ptr + 24, config.chordExt7th ? 1 : 0);
  view.setUint8(ptr + 25, config.chordExt9th ? 1 : 0);
  view.setUint8(ptr + 26, config.chordExtSusProb ?? 20);
  view.setUint8(ptr + 27, config.chordExt7thProb ?? 30);
  view.setUint8(ptr + 28, config.chordExt9thProb ?? 25);

  // Composition style (offset 29)
  view.setUint8(ptr + 29, config.compositionStyle ?? 0);

  // Reserved + padding (offset 30-31)
  view.setUint8(ptr + 30, 0);
  view.setUint8(ptr + 31, 0);

  // Duration (offset 32-33)
  view.setUint16(ptr + 32, config.targetDurationSeconds ?? 0, true);

  // Modulation settings (offset 34-35)
  view.setUint8(ptr + 34, config.modulationTiming ?? 0);
  view.setInt8(ptr + 35, config.modulationSemitones ?? 2);

  // SE/Call settings (offset 36-41)
  view.setUint8(ptr + 36, config.seEnabled !== false ? 1 : 0);
  view.setUint8(ptr + 37, config.callEnabled ? 1 : 0);
  view.setUint8(ptr + 38, config.callNotesEnabled !== false ? 1 : 0);
  view.setUint8(ptr + 39, config.introChant ?? 0);
  view.setUint8(ptr + 40, config.mixPattern ?? 0);
  view.setUint8(ptr + 41, config.callDensity ?? 2);

  // Vocal style settings (offset 42-43)
  view.setUint8(ptr + 42, config.vocalStyle ?? 0);
  view.setUint8(ptr + 43, config.melodyTemplate ?? 0);

  // Arrangement settings (offset 44)
  view.setUint8(ptr + 44, config.arrangementGrowth ?? 0);

  // Arpeggio sync settings (offset 45)
  view.setUint8(ptr + 45, config.arpeggioSyncChord !== false ? 1 : 0);

  // Motif settings (offset 46-48)
  view.setUint8(ptr + 46, config.motifRepeatScope ?? 0);
  view.setUint8(ptr + 47, config.motifFixedProgression !== false ? 1 : 0);
  view.setUint8(ptr + 48, config.motifMaxChordCount ?? 4);

  // Melodic complexity and hook control (offset 49-51)
  view.setUint8(ptr + 49, config.melodicComplexity ?? 1);
  view.setUint8(ptr + 50, config.hookIntensity ?? 2);
  view.setUint8(ptr + 51, config.vocalGroove ?? 0);

  return ptr;
}

/**
 * MidiSketch instance for MIDI generation
 */
export class MidiSketch {
  private handle: number;

  constructor() {
    const a = getApi();
    this.handle = a.create();
    if (!this.handle) {
      throw new Error('Failed to create MidiSketch instance');
    }
  }

  /**
   * Generate MIDI from a SongConfig
   * @throws {MidiSketchConfigError} If config validation fails
   * @throws {MidiSketchGenerationError} If generation fails for other reasons
   */
  generateFromConfig(config: SongConfig): void {
    const a = getApi();
    const m = getModule();
    const configPtr = this.allocSongConfig(m, config);
    try {
      const result = a.generateFromConfig(this.handle, configPtr);
      if (result !== 0) {
        // Error code 1 = INVALID_PARAM, which includes config validation errors
        // Try to get detailed error message
        const errorMessage = a.configErrorString(result);
        if (result === 1) {
          // Config validation error - get more specific error from validation
          const validationResult = a.validateConfig(configPtr);
          if (validationResult !== 0) {
            const validationMessage = a.configErrorString(validationResult);
            throw new MidiSketchConfigError(validationResult, validationMessage);
          }
        }
        throw new MidiSketchGenerationError(result, `Generation failed: ${errorMessage}`);
      }
    } finally {
      m._free(configPtr);
    }
  }

  /**
   * Generate only the vocal track without accompaniment.
   * Use for trial-and-error workflow: generate vocal, listen, regenerate if needed.
   * Call generateAccompaniment() when satisfied with the vocal.
   * @throws {MidiSketchConfigError} If config validation fails
   * @throws {MidiSketchGenerationError} If generation fails
   */
  generateVocal(config: SongConfig): void {
    const a = getApi();
    const m = getModule();
    const configPtr = this.allocSongConfig(m, config);
    try {
      const result = a.generateVocal(this.handle, configPtr);
      if (result !== 0) {
        const validationResult = a.validateConfig(configPtr);
        if (validationResult !== 0) {
          const validationMessage = a.configErrorString(validationResult);
          throw new MidiSketchConfigError(validationResult, validationMessage);
        }
        throw new MidiSketchGenerationError(
          result,
          `Vocal generation failed with error code: ${result}`,
        );
      }
    } finally {
      m._free(configPtr);
    }
  }

  /**
   * Regenerate vocal track with new configuration or seed.
   * Keeps the same chord progression and structure.
   * @param configOrSeed VocalConfig object or seed number (default: 0 = new random)
   * @throws {MidiSketchGenerationError} If regeneration fails
   */
  regenerateVocal(configOrSeed: VocalConfig | number = 0): void {
    const a = getApi();
    const m = getModule();

    let configPtr = 0;
    if (typeof configOrSeed === 'number') {
      // Seed only - create minimal config with just seed
      configPtr = this.allocVocalConfig(m, { seed: configOrSeed });
    } else {
      // Full config
      configPtr = this.allocVocalConfig(m, configOrSeed);
    }

    try {
      const result = a.regenerateVocal(this.handle, configPtr);
      if (result !== 0) {
        throw new MidiSketchGenerationError(
          result,
          `Vocal regeneration failed with error code: ${result}`,
        );
      }
    } finally {
      m._free(configPtr);
    }
  }

  /**
   * Generate accompaniment tracks for existing vocal.
   * Must be called after generateVocal() or generateWithVocal().
   * Generates: Aux → Bass → Chord → Drums (adapting to vocal).
   * @param config Optional accompaniment configuration
   * @throws {MidiSketchGenerationError} If generation fails
   */
  generateAccompaniment(config?: AccompanimentConfig): void {
    const a = getApi();
    if (config === undefined) {
      const result = a.generateAccompaniment(this.handle);
      if (result !== 0) {
        throw new MidiSketchGenerationError(
          result,
          `Accompaniment generation failed with error code: ${result}`,
        );
      }
    } else {
      const m = getModule();
      const configPtr = this.allocAccompanimentConfig(m, config);
      try {
        const result = a.generateAccompanimentWithConfig(this.handle, configPtr);
        if (result !== 0) {
          throw new MidiSketchGenerationError(
            result,
            `Accompaniment generation failed with error code: ${result}`,
          );
        }
      } finally {
        m._free(configPtr);
      }
    }
  }

  /**
   * Regenerate accompaniment tracks with a new seed or configuration.
   * Keeps current vocal, regenerates all accompaniment tracks
   * (Aux, Bass, Chord, Drums, etc.) with the specified seed/config.
   * Must have existing vocal (call generateVocal() first).
   * @param seedOrConfig Random seed (0 = auto-generate) or AccompanimentConfig
   * @throws {MidiSketchGenerationError} If regeneration fails
   */
  regenerateAccompaniment(seedOrConfig: number | AccompanimentConfig = 0): void {
    const a = getApi();
    if (typeof seedOrConfig === 'number') {
      const result = a.regenerateAccompaniment(this.handle, seedOrConfig);
      if (result !== 0) {
        throw new MidiSketchGenerationError(
          result,
          `Accompaniment regeneration failed with error code: ${result}`,
        );
      }
    } else {
      const m = getModule();
      const configPtr = this.allocAccompanimentConfig(m, seedOrConfig);
      try {
        const result = a.regenerateAccompanimentWithConfig(this.handle, configPtr);
        if (result !== 0) {
          throw new MidiSketchGenerationError(
            result,
            `Accompaniment regeneration failed with error code: ${result}`,
          );
        }
      } finally {
        m._free(configPtr);
      }
    }
  }

  /**
   * Allocate and populate AccompanimentConfig in WASM memory.
   */
  private allocAccompanimentConfig(m: EmscriptenModule, config: AccompanimentConfig): number {
    // MidiSketchAccompanimentConfig struct size: 28 bytes
    const configPtr = m._malloc(28);
    const view = new DataView(m.HEAPU8.buffer, configPtr, 28);

    view.setUint32(0, config.seed ?? 0, true); // seed

    view.setUint8(4, config.drumsEnabled !== false ? 1 : 0); // drums_enabled

    view.setUint8(5, config.arpeggioEnabled ? 1 : 0); // arpeggio_enabled
    view.setUint8(6, config.arpeggioPattern ?? 0); // arpeggio_pattern
    view.setUint8(7, config.arpeggioSpeed ?? 1); // arpeggio_speed
    view.setUint8(8, config.arpeggioOctaveRange ?? 2); // arpeggio_octave_range
    view.setUint8(9, config.arpeggioGate ?? 80); // arpeggio_gate
    view.setUint8(10, config.arpeggioSyncChord !== false ? 1 : 0); // arpeggio_sync_chord

    view.setUint8(11, config.chordExtSus ? 1 : 0); // chord_ext_sus
    view.setUint8(12, config.chordExt7th ? 1 : 0); // chord_ext_7th
    view.setUint8(13, config.chordExt9th ? 1 : 0); // chord_ext_9th
    view.setUint8(14, config.chordExtSusProb ?? 20); // chord_ext_sus_prob
    view.setUint8(15, config.chordExt7thProb ?? 30); // chord_ext_7th_prob
    view.setUint8(16, config.chordExt9thProb ?? 25); // chord_ext_9th_prob

    view.setUint8(17, config.humanize ? 1 : 0); // humanize
    view.setUint8(18, config.humanizeTiming ?? 50); // humanize_timing
    view.setUint8(19, config.humanizeVelocity ?? 50); // humanize_velocity

    view.setUint8(20, config.seEnabled !== false ? 1 : 0); // se_enabled

    view.setUint8(21, config.callEnabled ? 1 : 0); // call_enabled
    view.setUint8(22, config.callDensity ?? 2); // call_density
    view.setUint8(23, config.introChant ?? 0); // intro_chant
    view.setUint8(24, config.mixPattern ?? 0); // mix_pattern
    view.setUint8(25, config.callNotesEnabled !== false ? 1 : 0); // call_notes_enabled

    // Reserved padding
    view.setUint8(26, 0);
    view.setUint8(27, 0);

    return configPtr;
  }

  /**
   * Generate all tracks with vocal-first priority.
   * Generation order: Vocal → Aux → Bass → Chord → Drums.
   * Accompaniment adapts to vocal melody.
   * @throws {MidiSketchConfigError} If config validation fails
   * @throws {MidiSketchGenerationError} If generation fails
   */
  generateWithVocal(config: SongConfig): void {
    const a = getApi();
    const m = getModule();
    const configPtr = this.allocSongConfig(m, config);
    try {
      const result = a.generateWithVocal(this.handle, configPtr);
      if (result !== 0) {
        const validationResult = a.validateConfig(configPtr);
        if (validationResult !== 0) {
          const validationMessage = a.configErrorString(validationResult);
          throw new MidiSketchConfigError(validationResult, validationMessage);
        }
        throw new MidiSketchGenerationError(result, `Generation failed with error code: ${result}`);
      }
    } finally {
      m._free(configPtr);
    }
  }

  /**
   * Set custom vocal notes for accompaniment generation.
   *
   * Initializes the song structure and chord progression from config,
   * then replaces the vocal track with the provided notes.
   * Call generateAccompaniment() after this to generate
   * accompaniment tracks that fit the custom vocal melody.
   *
   * @param config Song configuration (for structure/chord setup)
   * @param notes Array of note inputs representing the custom vocal
   * @throws {MidiSketchConfigError} If config validation fails
   * @throws {MidiSketchGenerationError} If operation fails
   *
   * @example
   * ```typescript
   * // Set custom vocal notes
   * sketch.setVocalNotes(config, [
   *   { startTick: 0, duration: 480, pitch: 60, velocity: 100 },
   *   { startTick: 480, duration: 480, pitch: 62, velocity: 100 },
   * ]);
   *
   * // Generate accompaniment for the custom vocal
   * sketch.generateAccompaniment();
   *
   * // Get the MIDI data
   * const midi = sketch.getMidi();
   * ```
   */
  setVocalNotes(config: SongConfig, notes: NoteInput[]): void {
    const a = getApi();
    const m = getModule();
    const configPtr = this.allocSongConfig(m, config);
    const notesPtr = this.allocNoteInputArray(m, notes);

    try {
      const result = a.setVocalNotes(this.handle, configPtr, notesPtr, notes.length);
      if (result !== 0) {
        const validationResult = a.validateConfig(configPtr);
        if (validationResult !== 0) {
          const validationMessage = a.configErrorString(validationResult);
          throw new MidiSketchConfigError(validationResult, validationMessage);
        }
        throw new MidiSketchGenerationError(
          result,
          `Set vocal notes failed with error code: ${result}`,
        );
      }
    } finally {
      m._free(configPtr);
      m._free(notesPtr);
    }
  }

  /**
   * Allocate and populate NoteInput array in WASM memory.
   */
  private allocNoteInputArray(m: EmscriptenModule, notes: NoteInput[]): number {
    // MidiSketchNoteInput struct size: 12 bytes (uint32 + uint32 + uint8 + uint8 + 2 padding)
    const structSize = 12;
    const ptr = m._malloc(notes.length * structSize);
    const view = new DataView(m.HEAPU8.buffer);

    for (let i = 0; i < notes.length; i++) {
      const offset = ptr + i * structSize;
      view.setUint32(offset + 0, notes[i].startTick, true); // start_tick
      view.setUint32(offset + 4, notes[i].duration, true); // duration
      view.setUint8(offset + 8, notes[i].pitch); // pitch
      view.setUint8(offset + 9, notes[i].velocity); // velocity
      // 2 bytes padding (10-11)
    }

    return ptr;
  }

  /**
   * Get the generated MIDI data
   */
  getMidi(): Uint8Array {
    const a = getApi();
    const m = getModule();
    const midiDataPtr = a.getMidi(this.handle);
    if (!midiDataPtr) {
      throw new Error('No MIDI data available');
    }

    try {
      const dataPtr = m.HEAPU32[midiDataPtr >> 2];
      const size = m.HEAPU32[(midiDataPtr + 4) >> 2];

      const result = new Uint8Array(size);
      result.set(m.HEAPU8.subarray(dataPtr, dataPtr + size));
      return result;
    } finally {
      a.freeMidi(midiDataPtr);
    }
  }

  /**
   * Get the event data as a parsed object
   */
  getEvents(): EventData {
    const a = getApi();
    const m = getModule();
    const eventDataPtr = a.getEvents(this.handle);
    if (!eventDataPtr) {
      throw new Error('No event data available');
    }

    try {
      const jsonPtr = m.HEAPU32[eventDataPtr >> 2];
      const json = m.UTF8ToString(jsonPtr);
      return JSON.parse(json) as EventData;
    } finally {
      a.freeEvents(eventDataPtr);
    }
  }

  // ============================================================================
  // Piano Roll Safety API
  // ============================================================================

  /**
   * Get piano roll safety info for a single tick.
   *
   * Returns safety level, reason flags, and collision info for each MIDI note (0-127).
   * Use this before placing custom vocal notes to see which notes are safe.
   *
   * @param tick Tick position to query
   * @param prevPitch Previous note pitch for leap detection (optional, 255 if none)
   * @returns Piano roll safety info for all 128 MIDI notes
   *
   * @example
   * ```typescript
   * // Get safety info at tick 0
   * const info = sketch.getPianoRollSafetyAt(0);
   *
   * // Check if C4 (pitch 60) is safe
   * if (info.safety[60] === NoteSafety.Safe) {
   *   console.log('C4 is a chord tone, safe to use');
   * }
   *
   * // Get recommended notes
   * console.log('Recommended:', info.recommended);
   * ```
   */
  getPianoRollSafetyAt(tick: number, prevPitch?: number): PianoRollInfo {
    const a = getApi();
    const m = getModule();

    const infoPtr =
      prevPitch !== undefined
        ? a.getPianoRollSafetyWithContext(this.handle, tick, prevPitch)
        : a.getPianoRollSafetyAt(this.handle, tick);

    if (!infoPtr) {
      throw new Error('Failed to get piano roll safety info. Generate MIDI first.');
    }

    return this.parsePianoRollInfo(m, infoPtr);
  }

  /**
   * Get piano roll safety info for a range of ticks.
   *
   * Useful for visualizing safe notes over time in a piano roll editor.
   *
   * @param startTick Start tick
   * @param endTick End tick
   * @param step Step size in ticks (e.g., 120 for 16th notes, 480 for quarter notes)
   * @returns Array of piano roll safety info for each step
   *
   * @example
   * ```typescript
   * // Get safety info for first 4 bars, sampled at 16th note resolution
   * const infos = sketch.getPianoRollSafety(0, 1920 * 4, 120);
   *
   * for (const info of infos) {
   *   console.log(`Tick ${info.tick}: chord degree ${info.chordDegree}`);
   *   console.log('Recommended notes:', info.recommended);
   * }
   * ```
   */
  getPianoRollSafety(startTick: number, endTick: number, step: number): PianoRollInfo[] {
    const a = getApi();
    const m = getModule();

    const dataPtr = a.getPianoRollSafety(this.handle, startTick, endTick, step);
    if (!dataPtr) {
      throw new Error('Failed to get piano roll safety data. Generate MIDI first.');
    }

    try {
      // MidiSketchPianoRollData: { data: ptr, count: size_t }
      const infoArrayPtr = m.HEAPU32[dataPtr >> 2];
      const count = m.HEAPU32[(dataPtr + 4) >> 2];

      const results: PianoRollInfo[] = [];
      const infoSize = 784; // sizeof(MidiSketchPianoRollInfo)

      for (let i = 0; i < count; i++) {
        const infoPtr = infoArrayPtr + i * infoSize;
        results.push(this.parsePianoRollInfo(m, infoPtr));
      }

      return results;
    } finally {
      a.freePianoRollData(dataPtr);
    }
  }

  /**
   * Convert reason flags to human-readable string.
   *
   * @param reason Reason flags from PianoRollInfo
   * @returns Human-readable string like "ChordTone" or "LowRegister, Tritone"
   */
  reasonToString(reason: NoteReasonFlags): string {
    const a = getApi();
    return a.reasonToString(reason);
  }

  /**
   * Parse MidiSketchPianoRollInfo from WASM memory.
   * @internal
   */
  private parsePianoRollInfo(m: EmscriptenModule, ptr: number): PianoRollInfo {
    const view = new DataView(m.HEAPU8.buffer);

    // Offsets from struct_layout_test.cpp:
    // tick: 0, chord_degree: 4, current_key: 5, safety: 6, reason: 134,
    // collision: 390, recommended: 774, recommended_count: 782
    const tick = view.getUint32(ptr + 0, true);
    const chordDegree = view.getInt8(ptr + 4);
    const currentKey = view.getUint8(ptr + 5);

    // Parse safety array (128 bytes at offset 6)
    const safety: NoteSafetyLevel[] = [];
    for (let i = 0; i < 128; i++) {
      safety.push(view.getUint8(ptr + 6 + i) as NoteSafetyLevel);
    }

    // Parse reason array (128 * 2 bytes at offset 134)
    const reason: NoteReasonFlags[] = [];
    for (let i = 0; i < 128; i++) {
      reason.push(view.getUint16(ptr + 134 + i * 2, true));
    }

    // Parse collision array (128 * 3 bytes at offset 390)
    const collision: CollisionInfo[] = [];
    for (let i = 0; i < 128; i++) {
      const collisionOffset = ptr + 390 + i * 3;
      collision.push({
        trackRole: view.getUint8(collisionOffset),
        collidingPitch: view.getUint8(collisionOffset + 1),
        intervalSemitones: view.getUint8(collisionOffset + 2),
      });
    }

    // Parse recommended array (up to 8 bytes at offset 774)
    const recommendedCount = view.getUint8(ptr + 782);
    const recommended: number[] = [];
    for (let i = 0; i < recommendedCount && i < 8; i++) {
      recommended.push(view.getUint8(ptr + 774 + i));
    }

    return {
      tick,
      chordDegree,
      currentKey,
      safety,
      reason,
      collision,
      recommended,
    };
  }

  /**
   * Destroy the instance and free resources
   */
  destroy(): void {
    if (this.handle) {
      const a = getApi();
      a.destroy(this.handle);
      this.handle = 0;
    }
  }

  private allocSongConfig(m: EmscriptenModule, config: SongConfig): number {
    const ptr = m._malloc(52); // MidiSketchSongConfig size
    const view = new DataView(m.HEAPU8.buffer);

    // Basic settings (offset 0-11)
    view.setUint8(ptr + 0, config.stylePresetId ?? 0);
    view.setUint8(ptr + 1, config.key ?? 0);
    view.setUint16(ptr + 2, config.bpm ?? 0, true);
    view.setUint32(ptr + 4, config.seed ?? 0, true);
    view.setUint8(ptr + 8, config.chordProgressionId ?? 0);
    view.setUint8(ptr + 9, config.formId ?? 0);
    view.setUint8(ptr + 10, config.vocalAttitude ?? 0);
    view.setUint8(ptr + 11, config.drumsEnabled !== false ? 1 : 0);

    // Arpeggio settings (offset 12-16)
    view.setUint8(ptr + 12, config.arpeggioEnabled ? 1 : 0);
    view.setUint8(ptr + 13, config.arpeggioPattern ?? 0);
    view.setUint8(ptr + 14, config.arpeggioSpeed ?? 1);
    view.setUint8(ptr + 15, config.arpeggioOctaveRange ?? 2);
    view.setUint8(ptr + 16, config.arpeggioGate ?? 80);

    // Vocal settings (offset 17-19)
    view.setUint8(ptr + 17, config.vocalLow ?? 55);
    view.setUint8(ptr + 18, config.vocalHigh ?? 74);
    view.setUint8(ptr + 19, config.skipVocal ? 1 : 0);

    // Humanization (offset 20-22)
    view.setUint8(ptr + 20, config.humanize ? 1 : 0);
    view.setUint8(ptr + 21, config.humanizeTiming ?? 50);
    view.setUint8(ptr + 22, config.humanizeVelocity ?? 50);

    // Chord extensions (offset 23-28)
    view.setUint8(ptr + 23, config.chordExtSus ? 1 : 0);
    view.setUint8(ptr + 24, config.chordExt7th ? 1 : 0);
    view.setUint8(ptr + 25, config.chordExt9th ? 1 : 0);
    view.setUint8(ptr + 26, config.chordExtSusProb ?? 20);
    view.setUint8(ptr + 27, config.chordExt7thProb ?? 30);
    view.setUint8(ptr + 28, config.chordExt9thProb ?? 25);

    // Composition style (offset 29)
    view.setUint8(ptr + 29, config.compositionStyle ?? 0);

    // Reserved + padding (offset 30-31)
    view.setUint8(ptr + 30, 0);
    view.setUint8(ptr + 31, 0);

    // Duration (offset 32-33)
    view.setUint16(ptr + 32, config.targetDurationSeconds ?? 0, true);

    // Modulation settings (offset 34-35)
    view.setUint8(ptr + 34, config.modulationTiming ?? 0);
    view.setInt8(ptr + 35, config.modulationSemitones ?? 2);

    // SE/Call settings (offset 36-41)
    view.setUint8(ptr + 36, config.seEnabled !== false ? 1 : 0);
    view.setUint8(ptr + 37, config.callEnabled ? 1 : 0);
    view.setUint8(ptr + 38, config.callNotesEnabled !== false ? 1 : 0);
    view.setUint8(ptr + 39, config.introChant ?? 0);
    view.setUint8(ptr + 40, config.mixPattern ?? 0);
    view.setUint8(ptr + 41, config.callDensity ?? 2);

    // Vocal style settings (offset 42-43)
    view.setUint8(ptr + 42, config.vocalStyle ?? 0);
    view.setUint8(ptr + 43, config.melodyTemplate ?? 0);

    // Arrangement settings (offset 44)
    view.setUint8(ptr + 44, config.arrangementGrowth ?? 0);

    // Arpeggio sync settings (offset 45)
    view.setUint8(ptr + 45, config.arpeggioSyncChord !== false ? 1 : 0);

    // Motif settings (offset 46-48)
    view.setUint8(ptr + 46, config.motifRepeatScope ?? 0);
    view.setUint8(ptr + 47, config.motifFixedProgression !== false ? 1 : 0);
    view.setUint8(ptr + 48, config.motifMaxChordCount ?? 4);

    // Melodic complexity and hook control (offset 49-51)
    view.setUint8(ptr + 49, config.melodicComplexity ?? 1); // Default: Standard
    view.setUint8(ptr + 50, config.hookIntensity ?? 2); // Default: Normal
    view.setUint8(ptr + 51, config.vocalGroove ?? 0); // Default: Straight

    return ptr;
  }

  private allocVocalConfig(m: EmscriptenModule, config: VocalConfig): number {
    const ptr = m._malloc(16); // MidiSketchVocalConfig size (16 bytes with padding)
    const view = new DataView(m.HEAPU8.buffer);

    // Layout: seed(4) + vocal_low(1) + vocal_high(1) + vocal_attitude(1)
    //         + vocal_style(1) + melody_template(1) + melodic_complexity(1)
    //         + hook_intensity(1) + vocal_groove(1) + composition_style(1)
    //         + reserved(2) = 16 bytes
    view.setUint32(ptr + 0, config.seed ?? 0, true);
    view.setUint8(ptr + 4, config.vocalLow ?? 60);
    view.setUint8(ptr + 5, config.vocalHigh ?? 79);
    view.setUint8(ptr + 6, config.vocalAttitude ?? 0);
    view.setUint8(ptr + 7, config.vocalStyle ?? 0);
    view.setUint8(ptr + 8, config.melodyTemplate ?? 0);
    view.setUint8(ptr + 9, config.melodicComplexity ?? 1); // Default: Standard
    view.setUint8(ptr + 10, config.hookIntensity ?? 2); // Default: Normal
    view.setUint8(ptr + 11, config.vocalGroove ?? 0); // Default: Straight
    view.setUint8(ptr + 12, config.compositionStyle ?? 0);
    view.setUint8(ptr + 13, 0); // Reserved
    view.setUint8(ptr + 14, 0); // Reserved
    view.setUint8(ptr + 15, 0); // Padding (explicit for clarity)

    return ptr;
  }
}

/**
 * Download MIDI data as a file (browser only)
 */
export function downloadMidi(midiData: Uint8Array, filename = 'output.mid'): void {
  // Copy to regular ArrayBuffer to ensure compatibility
  const buffer = new ArrayBuffer(midiData.length);
  new Uint8Array(buffer).set(midiData);
  const blob = new Blob([buffer], { type: 'audio/midi' });
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement('a');
  anchor.href = url;
  anchor.download = filename;
  anchor.click();
  URL.revokeObjectURL(url);
}

export default MidiSketch;
