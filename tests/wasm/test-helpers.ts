import createModule from '../../dist/midisketch.js';
import { serializeConfig } from '../../js/src/config-fields';
import type { SongConfig } from '../../js/src/types';

const NO_COLLISION: Readonly<CollisionInfo> = Object.freeze({
  trackRole: 0,
  collidingPitch: 0,
  intervalSemitones: 0,
});

export interface WasmModule {
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

export interface SongConfigOptions {
  stylePresetId?: number;
  key?: number;
  bpm?: number;
  seed?: number;
  chordProgressionId?: number;
  formId?: number;
  vocalAttitude?: number;
  drumsEnabled?: boolean;
  blueprintId?: number;
  arpeggioEnabled?: boolean;
  arpeggioPattern?: number;
  arpeggioSpeed?: number;
  arpeggioOctaveRange?: number;
  arpeggioGate?: number;
  vocalLow?: number;
  vocalHigh?: number;
  skipVocal?: boolean;
  humanize?: boolean;
  humanizeTiming?: number;
  humanizeVelocity?: number;
  chordExtSus?: boolean;
  chordExt7th?: boolean;
  chordExt9th?: boolean;
  chordExtSusProb?: number;
  chordExt7thProb?: number;
  chordExt9thProb?: number;
  compositionStyle?: number;
  targetDurationSeconds?: number;
  modulationTiming?: number;
  modulationSemitones?: number;
  seEnabled?: boolean;
  callEnabled?: boolean;
  callNotesEnabled?: boolean;
  introChant?: number;
  mixPattern?: number;
  callDensity?: number;
  // Vocal style settings
  vocalStyle?: number;
  melodyTemplate?: number;
  // Arrangement settings
  arrangementGrowth?: number;
  // Arpeggio sync settings
  arpeggioSyncChord?: boolean;
  // Motif settings
  motifRepeatScope?: number;
  motifFixedProgression?: boolean;
  motifMaxChordCount?: number;
  // Melodic complexity and hook control
  melodicComplexity?: number;
  hookIntensity?: number;
  vocalGroove?: number;
}

export interface AccompanimentConfigOptions {
  seed?: number;
  drumsEnabled?: boolean;
  arpeggioEnabled?: boolean;
  arpeggioPattern?: number;
  arpeggioSpeed?: number;
  arpeggioOctaveRange?: number;
  arpeggioGate?: number;
  arpeggioSyncChord?: boolean;
  chordExtSus?: boolean;
  chordExt7th?: boolean;
  chordExt9th?: boolean;
  chordExtSusProb?: number;
  chordExt7thProb?: number;
  chordExt9thProb?: number;
  humanize?: boolean;
  humanizeTiming?: number;
  humanizeVelocity?: number;
  seEnabled?: boolean;
  callEnabled?: boolean;
  callDensity?: number;
  introChant?: number;
  mixPattern?: number;
  callNotesEnabled?: boolean;
}

/**
 * Serialize test options through the public SongConfig serializer.
 *
 * The legacy test inputs express the five fractional controls as percentages;
 * normalize only those values before delegating every field mapping to the
 * package code that users receive.
 */
function serializeSongConfig(config: SongConfigOptions): string {
  const normalized: SongConfigOptions = {
    ...config,
    arpeggioGate: config.arpeggioGate === undefined ? undefined : config.arpeggioGate / 100,
    humanizeTiming: config.humanizeTiming === undefined ? undefined : config.humanizeTiming / 100,
    humanizeVelocity:
      config.humanizeVelocity === undefined ? undefined : config.humanizeVelocity / 100,
    chordExtSusProb:
      config.chordExtSusProb === undefined ? undefined : config.chordExtSusProb / 100,
    chordExt7thProb:
      config.chordExt7thProb === undefined ? undefined : config.chordExt7thProb / 100,
    chordExt9thProb:
      config.chordExt9thProb === undefined ? undefined : config.chordExt9thProb / 100,
  };
  return serializeConfig(normalized as SongConfig);
}

/**
 * Serialize an AccompanimentConfigOptions object to C++ AccompanimentConfig JSON string.
 *
 * Maps JS camelCase field names to C++ snake_case field names.
 * AccompanimentConfig uses float timing/probability values in the 0.0-1.0 range.
 */
function serializeAccompanimentConfig(config: AccompanimentConfigOptions): string {
  const obj: Record<string, unknown> = {};

  if (config.seed !== undefined) {
    obj.seed = config.seed;
  }
  if (config.drumsEnabled !== undefined) {
    obj.drums_enabled = config.drumsEnabled;
  }
  if (config.arpeggioEnabled !== undefined) {
    obj.arpeggio_enabled = config.arpeggioEnabled;
  }
  if (config.arpeggioPattern !== undefined) {
    obj.arpeggio_pattern = config.arpeggioPattern;
  }
  if (config.arpeggioSpeed !== undefined) {
    obj.arpeggio_speed = config.arpeggioSpeed;
  }
  if (config.arpeggioOctaveRange !== undefined) {
    obj.arpeggio_octave_range = config.arpeggioOctaveRange;
  }
  if (config.arpeggioGate !== undefined) {
    obj.arpeggio_gate = config.arpeggioGate;
  }
  if (config.arpeggioSyncChord !== undefined) {
    obj.arpeggio_sync_chord = config.arpeggioSyncChord;
  }
  if (config.chordExtSus !== undefined) {
    obj.chord_ext_sus = config.chordExtSus;
  }
  if (config.chordExt7th !== undefined) {
    obj.chord_ext_7th = config.chordExt7th;
  }
  if (config.chordExt9th !== undefined) {
    obj.chord_ext_9th = config.chordExt9th;
  }
  if (config.chordExtSusProb !== undefined) {
    obj.chord_ext_sus_prob = config.chordExtSusProb;
  }
  if (config.chordExt7thProb !== undefined) {
    obj.chord_ext_7th_prob = config.chordExt7thProb;
  }
  if (config.chordExt9thProb !== undefined) {
    obj.chord_ext_9th_prob = config.chordExt9thProb;
  }
  if (config.humanize !== undefined) {
    obj.humanize = config.humanize;
  }
  if (config.humanizeTiming !== undefined) {
    obj.humanize_timing = config.humanizeTiming;
  }
  if (config.humanizeVelocity !== undefined) {
    obj.humanize_velocity = config.humanizeVelocity;
  }
  if (config.seEnabled !== undefined) {
    obj.se_enabled = config.seEnabled;
  }
  if (config.callEnabled !== undefined) {
    obj.call_enabled = config.callEnabled;
  }
  if (config.callDensity !== undefined) {
    obj.call_density = config.callDensity;
  }
  if (config.introChant !== undefined) {
    obj.intro_chant = config.introChant;
  }
  if (config.mixPattern !== undefined) {
    obj.mix_pattern = config.mixPattern;
  }
  if (config.callNotesEnabled !== undefined) {
    obj.call_notes_enabled = config.callNotesEnabled;
  }

  return JSON.stringify(obj);
}

export class WasmTestContext {
  module!: WasmModule;
  handle!: number;

  async init(): Promise<void> {
    this.module = (await createModule()) as WasmModule;
    const create = this.module.cwrap('midisketch_create', 'number', []) as () => number;
    this.handle = create();
  }

  destroy(): void {
    if (this.handle && this.module) {
      const destroyFn = this.module.cwrap('midisketch_destroy', null, ['number']) as (
        h: number,
      ) => void;
      destroyFn(this.handle);
    }
  }

  generateFromConfig(config: SongConfigOptions): number {
    const generateFn = this.module.cwrap('midisketch_generate_from_json', 'number', [
      'number',
      'string',
      'number',
    ]) as (h: number, json: string, len: number) => number;

    const json = serializeSongConfig(config);
    return generateFn(this.handle, json, json.length);
  }

  generateVocal(config: SongConfigOptions): number {
    const generateFn = this.module.cwrap('midisketch_generate_vocal_from_json', 'number', [
      'number',
      'string',
      'number',
    ]) as (h: number, json: string, len: number) => number;

    const json = serializeSongConfig(config);
    return generateFn(this.handle, json, json.length);
  }

  regenerateVocal(newSeed: number): number {
    const regenerateFn = this.module.cwrap('midisketch_regenerate_vocal_from_json', 'number', [
      'number',
      'string',
      'number',
    ]) as (h: number, json: string, len: number) => number;

    const json = JSON.stringify({ seed: newSeed });
    return regenerateFn(this.handle, json, json.length);
  }

  getStylePresetCount(): number {
    const countFn = this.module.cwrap(
      'midisketch_style_preset_count',
      'number',
      [],
    ) as () => number;
    return countFn();
  }

  getStylePresetAllowedAttitudes(styleId: number): number[] {
    const allowedFn = this.module.cwrap('midisketch_style_preset_allowed_attitudes', 'number', [
      'number',
    ]) as (id: number) => number;
    const flags = allowedFn(styleId);
    return [0, 1, 2].filter((attitude) => (flags & (1 << attitude)) !== 0);
  }

  getStructureCount(): number {
    const countFn = this.module.cwrap('midisketch_structure_count', 'number', []) as () => number;
    return countFn();
  }

  getBlueprintCount(): number {
    const countFn = this.module.cwrap('midisketch_blueprint_count', 'number', []) as () => number;
    return countFn();
  }

  generateAccompaniment(config?: AccompanimentConfigOptions): number {
    if (config) {
      const generateFn = this.module.cwrap(
        'midisketch_generate_accompaniment_from_json',
        'number',
        ['number', 'string', 'number'],
      ) as (h: number, json: string, len: number) => number;

      const json = serializeAccompanimentConfig(config);
      return generateFn(this.handle, json, json.length);
    }
    const generateFn = this.module.cwrap('midisketch_generate_accompaniment', 'number', [
      'number',
    ]) as (h: number) => number;
    return generateFn(this.handle);
  }

  regenerateAccompaniment(seedOrConfig?: number | AccompanimentConfigOptions): number {
    if (typeof seedOrConfig === 'object') {
      const regenerateFn = this.module.cwrap(
        'midisketch_regenerate_accompaniment_from_json',
        'number',
        ['number', 'string', 'number'],
      ) as (h: number, json: string, len: number) => number;

      const json = serializeAccompanimentConfig(seedOrConfig);
      return regenerateFn(this.handle, json, json.length);
    }
    const regenerateFn = this.module.cwrap('midisketch_regenerate_accompaniment', 'number', [
      'number',
      'number',
    ]) as (h: number, seed: number) => number;
    return regenerateFn(this.handle, seedOrConfig ?? 0);
  }

  generateWithVocal(config: SongConfigOptions): number {
    const generateFn = this.module.cwrap('midisketch_generate_with_vocal_from_json', 'number', [
      'number',
      'string',
      'number',
    ]) as (h: number, json: string, len: number) => number;

    const json = serializeSongConfig(config);
    return generateFn(this.handle, json, json.length);
  }

  setVocalNotes(
    config: SongConfigOptions,
    notes: { startTick: number; duration: number; pitch: number; velocity: number }[],
  ): number {
    const setNotesFn = this.module.cwrap('midisketch_set_vocal_notes_from_json', 'number', [
      'number',
      'string',
      'number',
    ]) as (h: number, json: string, len: number) => number;

    // Build the combined JSON: {"config": {...SongConfig...}, "notes": [...]}
    const configObj = JSON.parse(serializeSongConfig(config));
    const notesArr = notes.map((note) => ({
      start_tick: note.startTick,
      duration: note.duration,
      pitch: note.pitch,
      velocity: note.velocity,
    }));

    const json = JSON.stringify({ config: configObj, notes: notesArr });
    return setNotesFn(this.handle, json, json.length);
  }

  getEventsJson(): { data: unknown; cleanup: () => void } {
    const getEvents = this.module.cwrap('midisketch_get_events', 'number', ['number']) as (
      h: number,
    ) => number;
    const freeEvents = this.module.cwrap('midisketch_free_events', null, ['number']) as (
      ptr: number,
    ) => void;

    const eventDataPtr = getEvents(this.handle);
    const jsonPtr = this.module.HEAPU32[eventDataPtr >> 2];
    const json = this.module.UTF8ToString(jsonPtr);
    const data = JSON.parse(json);

    return {
      data,
      cleanup: () => freeEvents(eventDataPtr),
    };
  }

  // Piano Roll Safety API

  getPianoRollSafetyAt(tick: number): PianoRollInfo {
    const getSafetyAt = this.module.cwrap('midisketch_get_piano_roll_safety_at', 'number', [
      'number',
      'number',
    ]) as (h: number, tick: number) => number;

    const infoPtr = getSafetyAt(this.handle, tick);
    if (!infoPtr) {
      throw new Error('Failed to get piano roll safety info');
    }

    return this.parsePianoRollInfo(infoPtr);
  }

  getPianoRollSafetyWithContext(tick: number, prevPitch: number): PianoRollInfo {
    const getSafetyWithContext = this.module.cwrap(
      'midisketch_get_piano_roll_safety_with_context',
      'number',
      ['number', 'number', 'number'],
    ) as (h: number, tick: number, prevPitch: number) => number;

    const infoPtr = getSafetyWithContext(this.handle, tick, prevPitch);
    if (!infoPtr) {
      throw new Error('Failed to get piano roll safety info');
    }

    return this.parsePianoRollInfo(infoPtr);
  }

  getPianoRollSafety(startTick: number, endTick: number, step: number): PianoRollInfo[] {
    const getSafety = this.module.cwrap('midisketch_get_piano_roll_safety', 'number', [
      'number',
      'number',
      'number',
      'number',
    ]) as (h: number, startTick: number, endTick: number, step: number) => number;
    const freePianoRollData = this.module.cwrap('midisketch_free_piano_roll_data', null, [
      'number',
    ]) as (ptr: number) => void;
    const getPianoRollDataCount = this.module.cwrap('midisketch_piano_roll_data_count', 'number', [
      'number',
    ]) as (ptr: number) => number;
    const pianoRollDataWasTruncated = this.module.cwrap(
      'midisketch_piano_roll_data_was_truncated',
      'number',
      ['number'],
    ) as (ptr: number) => number;

    const dataPtr = getSafety(this.handle, startTick, endTick, step);
    if (!dataPtr) {
      throw new Error('Failed to get piano roll safety data');
    }

    try {
      const infoArrayPtr = this.module.HEAPU32[dataPtr >> 2];
      const count = getPianoRollDataCount(dataPtr);
      if (pianoRollDataWasTruncated(dataPtr) !== 0) {
        throw new RangeError(
          'Piano roll safety requests are limited to 100,000 samples; increase the step size.',
        );
      }

      const results: PianoRollInfo[] = [];
      const infoSize = 784; // sizeof(MidiSketchPianoRollInfo)

      for (let idx = 0; idx < count; idx++) {
        const infoPtr = infoArrayPtr + idx * infoSize;
        results.push(this.parsePianoRollInfo(infoPtr));
      }

      return results;
    } finally {
      freePianoRollData(dataPtr);
    }
  }

  private parsePianoRollInfo(ptr: number): PianoRollInfo {
    const view = new DataView(this.module.HEAPU8.buffer);

    const tick = view.getUint32(ptr + 0, true);
    const chordDegree = view.getInt8(ptr + 4);
    const currentKey = view.getUint8(ptr + 5);

    const safety: number[] = [];
    for (let idx = 0; idx < 128; idx++) {
      safety.push(view.getUint8(ptr + 6 + idx));
    }

    const reason: number[] = [];
    for (let idx = 0; idx < 128; idx++) {
      reason.push(view.getUint16(ptr + 134 + idx * 2, true));
    }

    const collision: CollisionInfo[] = Array<CollisionInfo>(128).fill(NO_COLLISION);
    for (let idx = 0; idx < 128; idx++) {
      const offset = ptr + 390 + idx * 3;
      const intervalSemitones = view.getUint8(offset + 2);
      if (intervalSemitones !== 0) {
        collision[idx] = {
          trackRole: view.getUint8(offset),
          collidingPitch: view.getUint8(offset + 1),
          intervalSemitones,
        };
      }
    }

    const recommendedCount = view.getUint8(ptr + 782);
    const recommended: number[] = [];
    for (let idx = 0; idx < recommendedCount && idx < 8; idx++) {
      recommended.push(view.getUint8(ptr + 774 + idx));
    }

    return { tick, chordDegree, currentKey, safety, reason, collision, recommended };
  }
}

export interface CollisionInfo {
  trackRole: number;
  collidingPitch: number;
  intervalSemitones: number;
}

export interface PianoRollInfo {
  tick: number;
  chordDegree: number;
  currentKey: number;
  safety: number[];
  reason: number[];
  collision: CollisionInfo[];
  recommended: number[];
}
