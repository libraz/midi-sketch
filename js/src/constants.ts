/**
 * Constants and error classes for midi-sketch
 */

// ============================================================================
// Config Error Codes
// ============================================================================

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

// ============================================================================
// Error Classes
// ============================================================================

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

// ============================================================================
// Vocal and Composition Style Constants
// ============================================================================

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
