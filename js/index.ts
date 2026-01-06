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
  regenerateVocal: (handle: number, paramsPtr: number) => number;
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

  // Vocal density settings
  /** Note density (0-200, where 0=use style default, 70=standard, 100=idol, 150=vocaloid) */
  vocalNoteDensity: number;
  /** Min note division (0=default, 4=quarter, 8=eighth, 16=sixteenth) */
  vocalMinNoteDivision: number;
  /** Rest ratio (0-50, percentage of phrase rest time) */
  vocalRestRatio: number;
  /** Allow extreme leaps for vocaloid-style melodies */
  vocalAllowExtremLeap: boolean;

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
 * Vocal regeneration parameters
 */
export interface VocalParams {
  /** Random seed (0 = new random) */
  seed: number;
  /** Vocal range lower bound (MIDI note) */
  vocalLow: number;
  /** Vocal range upper bound (MIDI note) */
  vocalHigh: number;
  /** Vocal attitude: 0=Clean, 1=Expressive, 2=Raw */
  vocalAttitude: number;
  /** Note density (0-200, where 0=use style default, 70=standard, 100=idol, 150=vocaloid) */
  vocalNoteDensity?: number;
  /** Min note division (0=default, 4=quarter, 8=eighth, 16=sixteenth) */
  vocalMinNoteDivision?: number;
  /** Rest ratio (0-50, percentage of phrase rest time) */
  vocalRestRatio?: number;
  /** Allow extreme leaps for vocaloid-style melodies */
  vocalAllowExtremLeap?: boolean;
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
    regenerateVocal: m.cwrap('midisketch_regenerate_vocal', 'number', ['number', 'number']) as (
      handle: number,
      paramsPtr: number,
    ) => number,
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

    // SE/Call settings
    seEnabled: view.getUint8(retPtr + 36) !== 0,
    callEnabled: view.getUint8(retPtr + 37) !== 0,
    callNotesEnabled: view.getUint8(retPtr + 38) !== 0,
    introChant: view.getUint8(retPtr + 39),
    mixPattern: view.getUint8(retPtr + 40),
    callDensity: view.getUint8(retPtr + 41),

    // Vocal density settings
    vocalNoteDensity: view.getUint8(retPtr + 42),
    vocalMinNoteDivision: view.getUint8(retPtr + 43),
    vocalRestRatio: view.getUint8(retPtr + 44),
    vocalAllowExtremLeap: view.getUint8(retPtr + 45) !== 0,

    // Arrangement settings
    arrangementGrowth: view.getUint8(retPtr + 46),

    // Arpeggio sync settings
    arpeggioSyncChord: view.getUint8(retPtr + 47) !== 0,

    // Motif settings
    motifRepeatScope: view.getUint8(retPtr + 48),
    motifFixedProgression: view.getUint8(retPtr + 49) !== 0,
    motifMaxChordCount: view.getUint8(retPtr + 50),

    // Melodic complexity and hook control
    melodicComplexity: view.getUint8(retPtr + 51),
    hookIntensity: view.getUint8(retPtr + 52),
    vocalGroove: view.getUint8(retPtr + 53),
  };
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
   */
  generateFromConfig(config: SongConfig): void {
    const a = getApi();
    const m = getModule();
    const configPtr = this.allocSongConfig(m, config);
    try {
      const result = a.generateFromConfig(this.handle, configPtr);
      if (result !== 0) {
        throw new Error(`Generation failed with error code: ${result}`);
      }
    } finally {
      m._free(configPtr);
    }
  }

  /**
   * Regenerate only the vocal track with the given parameters.
   * BGM tracks (chord, bass, drums, arpeggio) remain unchanged.
   * Use after generateFromConfig with skipVocal=true.
   */
  regenerateVocal(params: VocalParams): void {
    const a = getApi();
    const m = getModule();
    const paramsPtr = this.allocVocalParams(m, params);
    try {
      const result = a.regenerateVocal(this.handle, paramsPtr);
      if (result !== 0) {
        throw new Error(`Vocal regeneration failed with error code: ${result}`);
      }
    } finally {
      m._free(paramsPtr);
    }
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
    const ptr = m._malloc(56); // Struct is 51 bytes, aligned to 56
    const view = new DataView(m.HEAPU8.buffer);

    // Basic settings
    view.setUint8(ptr + 0, config.stylePresetId ?? 0);
    view.setUint8(ptr + 1, config.key ?? 0);
    view.setUint16(ptr + 2, config.bpm ?? 0, true);
    view.setUint32(ptr + 4, config.seed ?? 0, true);
    view.setUint8(ptr + 8, config.chordProgressionId ?? 0);
    view.setUint8(ptr + 9, config.formId ?? 0);
    view.setUint8(ptr + 10, config.vocalAttitude ?? 0);
    view.setUint8(ptr + 11, config.drumsEnabled !== false ? 1 : 0);

    // Arpeggio settings
    view.setUint8(ptr + 12, config.arpeggioEnabled ? 1 : 0);
    view.setUint8(ptr + 13, config.arpeggioPattern ?? 0);
    view.setUint8(ptr + 14, config.arpeggioSpeed ?? 1);
    view.setUint8(ptr + 15, config.arpeggioOctaveRange ?? 2);
    view.setUint8(ptr + 16, config.arpeggioGate ?? 80);

    // Vocal settings
    view.setUint8(ptr + 17, config.vocalLow ?? 55);
    view.setUint8(ptr + 18, config.vocalHigh ?? 74);
    view.setUint8(ptr + 19, config.skipVocal ? 1 : 0);

    // Humanization
    view.setUint8(ptr + 20, config.humanize ? 1 : 0);
    view.setUint8(ptr + 21, config.humanizeTiming ?? 50);
    view.setUint8(ptr + 22, config.humanizeVelocity ?? 50);

    // Chord extensions
    view.setUint8(ptr + 23, config.chordExtSus ? 1 : 0);
    view.setUint8(ptr + 24, config.chordExt7th ? 1 : 0);
    view.setUint8(ptr + 25, config.chordExt9th ? 1 : 0);
    view.setUint8(ptr + 26, config.chordExtSusProb ?? 20);
    view.setUint8(ptr + 27, config.chordExt7thProb ?? 30);
    view.setUint8(ptr + 28, config.chordExt9thProb ?? 25);

    // Composition style
    view.setUint8(ptr + 29, config.compositionStyle ?? 0);

    // Reserved + padding
    view.setUint8(ptr + 30, 0);
    view.setUint8(ptr + 31, 0);

    // Duration
    view.setUint16(ptr + 32, config.targetDurationSeconds ?? 0, true);

    // Modulation settings
    view.setUint8(ptr + 34, config.modulationTiming ?? 0);
    view.setInt8(ptr + 35, config.modulationSemitones ?? 2);

    // SE/Call settings
    view.setUint8(ptr + 36, config.seEnabled !== false ? 1 : 0);
    view.setUint8(ptr + 37, config.callEnabled ? 1 : 0);
    view.setUint8(ptr + 38, config.callNotesEnabled !== false ? 1 : 0);
    view.setUint8(ptr + 39, config.introChant ?? 0);
    view.setUint8(ptr + 40, config.mixPattern ?? 0);
    view.setUint8(ptr + 41, config.callDensity ?? 2);

    // Vocal density settings
    view.setUint8(ptr + 42, config.vocalNoteDensity ?? 0);
    view.setUint8(ptr + 43, config.vocalMinNoteDivision ?? 0);
    view.setUint8(ptr + 44, config.vocalRestRatio ?? 15);
    view.setUint8(ptr + 45, config.vocalAllowExtremLeap ? 1 : 0);

    // Arrangement settings
    view.setUint8(ptr + 46, config.arrangementGrowth ?? 0);

    // Arpeggio sync settings
    view.setUint8(ptr + 47, config.arpeggioSyncChord !== false ? 1 : 0);

    // Motif settings
    view.setUint8(ptr + 48, config.motifRepeatScope ?? 0);
    view.setUint8(ptr + 49, config.motifFixedProgression !== false ? 1 : 0);
    view.setUint8(ptr + 50, config.motifMaxChordCount ?? 4);

    // Melodic complexity and hook control
    view.setUint8(ptr + 51, config.melodicComplexity ?? 1); // Default: Standard
    view.setUint8(ptr + 52, config.hookIntensity ?? 2); // Default: Normal
    view.setUint8(ptr + 53, config.vocalGroove ?? 0); // Default: Straight

    return ptr;
  }

  private allocVocalParams(m: EmscriptenModule, params: VocalParams): number {
    const ptr = m._malloc(12); // 11 bytes + padding
    const view = new DataView(m.HEAPU8.buffer);

    view.setUint32(ptr + 0, params.seed ?? 0, true);
    view.setUint8(ptr + 4, params.vocalLow ?? 60);
    view.setUint8(ptr + 5, params.vocalHigh ?? 79);
    view.setUint8(ptr + 6, params.vocalAttitude ?? 0);
    // Vocal density parameters
    view.setUint8(ptr + 7, params.vocalNoteDensity ?? 0);
    view.setUint8(ptr + 8, params.vocalMinNoteDivision ?? 0);
    view.setUint8(ptr + 9, params.vocalRestRatio ?? 15);
    view.setUint8(ptr + 10, params.vocalAllowExtremLeap ? 1 : 0);

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
