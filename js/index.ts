/**
 * midi-sketch - MIDI Auto-Generation Library
 *
 * @example
 * ```typescript
 * import { MidiSketch, init, VocalAttitude } from '@libraz/midi-sketch';
 *
 * await init();
 * const sketch = new MidiSketch();
 * sketch.generate({ seed: 12345 });
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
  generate: (handle: number, paramsPtr: number) => number;
  regenerateMelody: (handle: number, seed: number) => number;
  regenerateMelodyEx: (handle: number, paramsPtr: number) => number;
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
 * Generation parameters for MIDI creation
 */
export interface GeneratorParams {
  /** Structure pattern (0-9) */
  structureId?: number;
  /** Mood preset (0-19) */
  moodId?: number;
  /** Chord progression (0-19) */
  chordId?: number;
  /** Key (0-11: C, C#, D, Eb, E, F, F#, G, Ab, A, Bb, B) */
  key?: number;
  /** Enable drums track */
  drumsEnabled?: boolean;
  /** Enable key modulation */
  modulation?: boolean;
  /** Vocal range lower bound (MIDI note) */
  vocalLow?: number;
  /** Vocal range upper bound (MIDI note) */
  vocalHigh?: number;
  /** Tempo (60-180, 0=use mood default) */
  bpm?: number;
  /** Random seed (0=auto) */
  seed?: number;
  /** Enable timing/velocity humanization */
  humanize?: boolean;
  /** Timing variation (0-100) */
  humanizeTiming?: number;
  /** Velocity variation (0-100) */
  humanizeVelocity?: number;
  /** Enable sus2/sus4 chords */
  chordExtSus?: boolean;
  /** Enable 7th chords */
  chordExt7th?: boolean;
  /** Sus chord probability (0-100) */
  chordExtSusProb?: number;
  /** 7th chord probability (0-100) */
  chordExt7thProb?: number;
  /** Enable 9th chords */
  chordExt9th?: boolean;
  /** 9th chord probability (0-100) */
  chordExt9thProb?: number;
  /** Composition style: 0=MelodyLead, 1=BackgroundMotif, 2=SynthDriven */
  compositionStyle?: number;
  /** Enable arpeggio track */
  arpeggioEnabled?: boolean;
  /** Arpeggio pattern: 0=Up, 1=Down, 2=UpDown, 3=Random */
  arpeggioPattern?: number;
  /** Arpeggio speed: 0=Eighth, 1=Sixteenth, 2=Triplet */
  arpeggioSpeed?: number;
  /** Arpeggio octave range (1-3) */
  arpeggioOctaveRange?: number;
  /** Arpeggio gate length (0-100) */
  arpeggioGate?: number;
  /** Target duration in seconds (0=use structureId, 60-300) */
  targetDurationSeconds?: number;
}

/**
 * Song configuration for style-based generation
 */
export interface SongConfig {
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
  /** Enable arpeggio */
  arpeggioEnabled: boolean;
  /** Vocal range lower bound (MIDI note) */
  vocalLow: number;
  /** Vocal range upper bound (MIDI note) */
  vocalHigh: number;
  /** Enable humanization */
  humanize: boolean;
  /** Timing variation (0-100) */
  humanizeTiming: number;
  /** Velocity variation (0-100) */
  humanizeVelocity: number;
  /** Target duration in seconds (0 = use formId) */
  targetDurationSeconds: number;
}

/**
 * Parameters for melody regeneration
 */
export interface MelodyRegenerateParams {
  /** Random seed (0 = new random) */
  seed: number;
  /** Vocal range lower bound (MIDI note) */
  vocalLow: number;
  /** Vocal range upper bound (MIDI note) */
  vocalHigh: number;
  /** Vocal attitude: 0=Clean, 1=Expressive, 2=Raw */
  vocalAttitude: number;
  /** Composition style: 0=MelodyLead, 1=BackgroundMotif, 2=SynthDriven */
  compositionStyle: number;
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
    generate: m.cwrap('midisketch_generate', 'number', ['number', 'number']) as (
      handle: number,
      paramsPtr: number,
    ) => number,
    regenerateMelody: m.cwrap('midisketch_regenerate_melody', 'number', ['number', 'number']) as (
      handle: number,
      seed: number,
    ) => number,
    regenerateMelodyEx: m.cwrap('midisketch_regenerate_melody_ex', 'number', [
      'number',
      'number',
    ]) as (handle: number, paramsPtr: number) => number,
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
    stylePresetId: view.getUint8(retPtr + 0),
    key: view.getUint8(retPtr + 1),
    bpm: view.getUint16(retPtr + 2, true),
    seed: view.getUint32(retPtr + 4, true),
    chordProgressionId: view.getUint8(retPtr + 8),
    formId: view.getUint8(retPtr + 9),
    vocalAttitude: view.getUint8(retPtr + 10),
    drumsEnabled: view.getUint8(retPtr + 11) !== 0,
    arpeggioEnabled: view.getUint8(retPtr + 12) !== 0,
    vocalLow: view.getUint8(retPtr + 13),
    vocalHigh: view.getUint8(retPtr + 14),
    humanize: view.getUint8(retPtr + 15) !== 0,
    humanizeTiming: view.getUint8(retPtr + 16),
    humanizeVelocity: view.getUint8(retPtr + 17),
    targetDurationSeconds: view.getUint16(retPtr + 18, true),
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
   * Generate MIDI with the given parameters
   */
  generate(params: GeneratorParams = {}): void {
    const a = getApi();
    const m = getModule();
    const paramsPtr = this.allocParams(m, params);
    try {
      const result = a.generate(this.handle, paramsPtr);
      if (result !== 0) {
        throw new Error(`Generation failed with error code: ${result}`);
      }
    } finally {
      m._free(paramsPtr);
    }
  }

  /**
   * Regenerate only the melody track
   */
  regenerateMelody(seed = 0): void {
    const a = getApi();
    const result = a.regenerateMelody(this.handle, seed);
    if (result !== 0) {
      throw new Error(`Regeneration failed with error code: ${result}`);
    }
  }

  /**
   * Regenerate only the melody track with full parameter control.
   * BGM tracks (chord, bass, drums, arpeggio) remain unchanged.
   */
  regenerateMelodyEx(params: MelodyRegenerateParams): void {
    const a = getApi();
    const m = getModule();
    const paramsPtr = this.allocMelodyParams(m, params);
    try {
      const result = a.regenerateMelodyEx(this.handle, paramsPtr);
      if (result !== 0) {
        throw new Error(`Regeneration failed with error code: ${result}`);
      }
    } finally {
      m._free(paramsPtr);
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

  private allocParams(m: EmscriptenModule, params: GeneratorParams): number {
    const ptr = m._malloc(34);
    const view = new DataView(m.HEAPU8.buffer);

    view.setUint8(ptr + 0, params.structureId ?? 0);
    view.setUint8(ptr + 1, params.moodId ?? 0);
    view.setUint8(ptr + 2, params.chordId ?? 0);
    view.setUint8(ptr + 3, params.key ?? 0);
    view.setUint8(ptr + 4, params.drumsEnabled !== false ? 1 : 0);
    view.setUint8(ptr + 5, params.modulation ? 1 : 0);
    view.setUint8(ptr + 6, params.vocalLow ?? 60);
    view.setUint8(ptr + 7, params.vocalHigh ?? 79);
    view.setUint16(ptr + 8, params.bpm ?? 0, true);
    view.setUint32(ptr + 12, params.seed ?? 0, true);
    view.setUint8(ptr + 16, params.humanize ? 1 : 0);
    view.setUint8(ptr + 17, params.humanizeTiming ?? 50);
    view.setUint8(ptr + 18, params.humanizeVelocity ?? 50);
    view.setUint8(ptr + 19, params.chordExtSus ? 1 : 0);
    view.setUint8(ptr + 20, params.chordExt7th ? 1 : 0);
    view.setUint8(ptr + 21, params.chordExtSusProb ?? 20);
    view.setUint8(ptr + 22, params.chordExt7thProb ?? 30);
    view.setUint8(ptr + 23, params.chordExt9th ? 1 : 0);
    view.setUint8(ptr + 24, params.chordExt9thProb ?? 25);
    view.setUint8(ptr + 25, params.compositionStyle ?? 0);
    view.setUint8(ptr + 26, params.arpeggioEnabled ? 1 : 0);
    view.setUint8(ptr + 27, params.arpeggioPattern ?? 0);
    view.setUint8(ptr + 28, params.arpeggioSpeed ?? 1);
    view.setUint8(ptr + 29, params.arpeggioOctaveRange ?? 2);
    view.setUint8(ptr + 30, params.arpeggioGate ?? 80);
    // offset 31 is padding for alignment
    view.setUint16(ptr + 32, params.targetDurationSeconds ?? 0, true);

    return ptr;
  }

  private allocSongConfig(m: EmscriptenModule, config: SongConfig): number {
    const ptr = m._malloc(20);
    const view = new DataView(m.HEAPU8.buffer);

    view.setUint8(ptr + 0, config.stylePresetId ?? 0);
    view.setUint8(ptr + 1, config.key ?? 0);
    view.setUint16(ptr + 2, config.bpm ?? 0, true);
    view.setUint32(ptr + 4, config.seed ?? 0, true);
    view.setUint8(ptr + 8, config.chordProgressionId ?? 0);
    view.setUint8(ptr + 9, config.formId ?? 0);
    view.setUint8(ptr + 10, config.vocalAttitude ?? 0);
    view.setUint8(ptr + 11, config.drumsEnabled !== false ? 1 : 0);
    view.setUint8(ptr + 12, config.arpeggioEnabled ? 1 : 0);
    view.setUint8(ptr + 13, config.vocalLow ?? 55);
    view.setUint8(ptr + 14, config.vocalHigh ?? 74);
    view.setUint8(ptr + 15, config.humanize ? 1 : 0);
    view.setUint8(ptr + 16, config.humanizeTiming ?? 50);
    view.setUint8(ptr + 17, config.humanizeVelocity ?? 50);
    view.setUint16(ptr + 18, config.targetDurationSeconds ?? 0, true);

    return ptr;
  }

  private allocMelodyParams(m: EmscriptenModule, params: MelodyRegenerateParams): number {
    const ptr = m._malloc(8);
    const view = new DataView(m.HEAPU8.buffer);

    view.setUint32(ptr + 0, params.seed ?? 0, true);
    view.setUint8(ptr + 4, params.vocalLow ?? 60);
    view.setUint8(ptr + 5, params.vocalHigh ?? 79);
    view.setUint8(ptr + 6, params.vocalAttitude ?? 0);
    view.setUint8(ptr + 7, params.compositionStyle ?? 0);

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
