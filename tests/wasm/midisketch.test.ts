import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import createModule from '../../dist/midisketch.js';

interface WasmModule {
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

describe('MidiSketch WASM', () => {
  let module: WasmModule;
  let handle: number;

  beforeAll(async () => {
    module = (await createModule()) as WasmModule;
    const create = module.cwrap('midisketch_create', 'number', []) as () => number;
    handle = create();
  });

  afterAll(() => {
    if (handle && module) {
      const destroy = module.cwrap('midisketch_destroy', null, ['number']) as (h: number) => void;
      destroy(handle);
    }
  });

  it('should create a valid handle', () => {
    expect(handle).toBeGreaterThan(0);
  });

  it('should return version string', () => {
    const version = module.cwrap('midisketch_version', 'string', []) as () => string;
    const versionStr = version();
    expect(versionStr).toMatch(/^\d+\.\d+\.\d+$/);
  });

  it('should return structure count', () => {
    const structureCount = module.cwrap('midisketch_structure_count', 'number', []) as () => number;
    expect(structureCount()).toBeGreaterThan(0);
  });

  it('should return mood count', () => {
    const moodCount = module.cwrap('midisketch_mood_count', 'number', []) as () => number;
    expect(moodCount()).toBeGreaterThan(0);
  });

  it('should return chord count', () => {
    const chordCount = module.cwrap('midisketch_chord_count', 'number', []) as () => number;
    expect(chordCount()).toBeGreaterThan(0);
  });

  // Helper to allocate SongConfig struct (36 bytes with padding)
  function allocSongConfig(config: {
    stylePresetId?: number;
    key?: number;
    bpm?: number;
    seed?: number;
    chordProgressionId?: number;
    formId?: number;
    vocalAttitude?: number;
    drumsEnabled?: boolean;
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
  }): number {
    const ptr = module._malloc(36);
    const view = new DataView(module.HEAPU8.buffer);

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
    view.setUint8(ptr + 17, config.vocalLow ?? 60);
    view.setUint8(ptr + 18, config.vocalHigh ?? 79);
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

    return ptr;
  }

  // Helper to allocate VocalParams struct (8 bytes)
  function allocVocalParams(params: {
    seed?: number;
    vocalLow?: number;
    vocalHigh?: number;
    vocalAttitude?: number;
  }): number {
    const ptr = module._malloc(8);
    const view = new DataView(module.HEAPU8.buffer);

    view.setUint32(ptr + 0, params.seed ?? 0, true);
    view.setUint8(ptr + 4, params.vocalLow ?? 60);
    view.setUint8(ptr + 5, params.vocalHigh ?? 79);
    view.setUint8(ptr + 6, params.vocalAttitude ?? 0);

    return ptr;
  }

  it('should generate MIDI from config successfully', () => {
    const generateFromConfig = module.cwrap('midisketch_generate_from_config', 'number', [
      'number',
      'number',
    ]) as (h: number, configPtr: number) => number;

    const configPtr = allocSongConfig({ seed: 12345 });
    const result = generateFromConfig(handle, configPtr);
    module._free(configPtr);

    expect(result).toBe(0); // MIDISKETCH_OK
  });

  describe('skipVocal', () => {
    it('should generate BGM without vocal when skipVocal is true', () => {
      const generateFromConfig = module.cwrap('midisketch_generate_from_config', 'number', [
        'number',
        'number',
      ]) as (h: number, configPtr: number) => number;
      const getEvents = module.cwrap('midisketch_get_events', 'number', ['number']) as (
        h: number,
      ) => number;
      const freeEvents = module.cwrap('midisketch_free_events', null, ['number']) as (
        ptr: number,
      ) => void;

      const configPtr = allocSongConfig({ seed: 12345, skipVocal: true });
      const result = generateFromConfig(handle, configPtr);
      module._free(configPtr);

      expect(result).toBe(0);

      // Check that vocal track is empty
      const eventDataPtr = getEvents(handle);
      const jsonPtr = module.HEAPU32[eventDataPtr >> 2];
      const json = module.UTF8ToString(jsonPtr);
      const data = JSON.parse(json);

      const vocalTrack = data.tracks.find((t: { name: string }) => t.name === 'Vocal');
      expect(vocalTrack.notes.length).toBe(0);

      // Other tracks should have notes
      const chordTrack = data.tracks.find((t: { name: string }) => t.name === 'Chord');
      expect(chordTrack.notes.length).toBeGreaterThan(0);

      freeEvents(eventDataPtr);
    });
  });

  describe('regenerateVocal', () => {
    it('should regenerate vocal with params', () => {
      const generateFromConfig = module.cwrap('midisketch_generate_from_config', 'number', [
        'number',
        'number',
      ]) as (h: number, configPtr: number) => number;
      const regenerateVocal = module.cwrap('midisketch_regenerate_vocal', 'number', [
        'number',
        'number',
      ]) as (h: number, paramsPtr: number) => number;

      // First generate BGM without vocal
      const configPtr = allocSongConfig({ seed: 12345, skipVocal: true });
      generateFromConfig(handle, configPtr);
      module._free(configPtr);

      // Then regenerate vocal
      const paramsPtr = allocVocalParams({
        seed: 54321,
        vocalLow: 55,
        vocalHigh: 74,
        vocalAttitude: 1, // Expressive
      });

      const result = regenerateVocal(handle, paramsPtr);
      module._free(paramsPtr);

      expect(result).toBe(0); // MIDISKETCH_OK
    });

    it('should change vocal range after regeneration', () => {
      const generateFromConfig = module.cwrap('midisketch_generate_from_config', 'number', [
        'number',
        'number',
      ]) as (h: number, configPtr: number) => number;
      const regenerateVocal = module.cwrap('midisketch_regenerate_vocal', 'number', [
        'number',
        'number',
      ]) as (h: number, paramsPtr: number) => number;
      const getEvents = module.cwrap('midisketch_get_events', 'number', ['number']) as (
        h: number,
      ) => number;
      const freeEvents = module.cwrap('midisketch_free_events', null, ['number']) as (
        ptr: number,
      ) => void;

      // Generate BGM without vocal
      const configPtr = allocSongConfig({ seed: 12345, skipVocal: true });
      generateFromConfig(handle, configPtr);
      module._free(configPtr);

      // Regenerate with restricted range
      const paramsPtr = allocVocalParams({
        seed: 11111,
        vocalLow: 65,
        vocalHigh: 72,
        vocalAttitude: 0, // Clean
      });

      const result = regenerateVocal(handle, paramsPtr);
      module._free(paramsPtr);
      expect(result).toBe(0);

      // Get events and check vocal notes
      const eventDataPtr = getEvents(handle);
      const jsonPtr = module.HEAPU32[eventDataPtr >> 2];
      const json = module.UTF8ToString(jsonPtr);
      const data = JSON.parse(json);

      // Find vocal track
      const vocalTrack = data.tracks.find((t: { name: string }) => t.name === 'Vocal');
      expect(vocalTrack).toBeDefined();
      expect(vocalTrack.notes.length).toBeGreaterThan(0);

      // Most notes should be within range (allow some flexibility for melodic movement)
      const inRangeNotes = vocalTrack.notes.filter(
        (note: { pitch: number }) => note.pitch >= 65 && note.pitch <= 72,
      );
      const inRangeRatio = inRangeNotes.length / vocalTrack.notes.length;
      expect(inRangeRatio).toBeGreaterThan(0.8); // At least 80% within range

      freeEvents(eventDataPtr);
    });
  });

  it('should get MIDI data after generation', () => {
    const getMidi = module.cwrap('midisketch_get_midi', 'number', ['number']) as (
      h: number,
    ) => number;
    const freeMidi = module.cwrap('midisketch_free_midi', null, ['number']) as (
      ptr: number,
    ) => void;

    const midiDataPtr = getMidi(handle);
    expect(midiDataPtr).toBeGreaterThan(0);

    // MidiSketchMidiData: { data: ptr, size: size_t }
    const dataPtr = module.HEAPU32[midiDataPtr >> 2];
    const size = module.HEAPU32[(midiDataPtr + 4) >> 2];

    expect(dataPtr).toBeGreaterThan(0);
    expect(size).toBeGreaterThan(0);

    // Check MIDI header (MThd)
    const header = new Uint8Array(module.HEAPU8.buffer, dataPtr, 4);
    expect(String.fromCharCode(...header)).toBe('MThd');

    freeMidi(midiDataPtr);
  });

  it('should get events JSON after generation', () => {
    const getEvents = module.cwrap('midisketch_get_events', 'number', ['number']) as (
      h: number,
    ) => number;
    const freeEvents = module.cwrap('midisketch_free_events', null, ['number']) as (
      ptr: number,
    ) => void;

    const eventDataPtr = getEvents(handle);
    expect(eventDataPtr).toBeGreaterThan(0);

    // MidiSketchEventData: { json: char*, length: size_t }
    const jsonPtr = module.HEAPU32[eventDataPtr >> 2];
    const json = module.UTF8ToString(jsonPtr);

    const data = JSON.parse(json);
    expect(data).toHaveProperty('bpm');
    expect(data).toHaveProperty('tracks');
    expect(data.tracks.length).toBeGreaterThan(0);

    freeEvents(eventDataPtr);
  });

  describe('StylePreset API', () => {
    it('should return style preset count', () => {
      const stylePresetCount = module.cwrap(
        'midisketch_style_preset_count',
        'number',
        [],
      ) as () => number;
      expect(stylePresetCount()).toBeGreaterThan(0);
    });

    it('should return style preset name', () => {
      const stylePresetName = module.cwrap('midisketch_style_preset_name', 'string', [
        'number',
      ]) as (id: number) => string;
      const name = stylePresetName(0);
      expect(name.length).toBeGreaterThan(0);
    });

    it('should return progressions for style', () => {
      const getProgressionsPtr = module.cwrap(
        'midisketch_get_progressions_by_style_ptr',
        'number',
        ['number'],
      ) as (styleId: number) => number;

      const retPtr = getProgressionsPtr(0);
      const view = new DataView(module.HEAPU8.buffer);
      const count = view.getUint8(retPtr);
      expect(count).toBeGreaterThan(0);
    });
  });
});
