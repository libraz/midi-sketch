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

  it('should generate MIDI successfully', () => {
    const generate = module.cwrap('midisketch_generate', 'number', ['number', 'number']) as (
      h: number,
      paramsPtr: number,
    ) => number;

    // Allocate params struct (34 bytes)
    const paramsPtr = module._malloc(34);
    const view = new DataView(module.HEAPU8.buffer);

    // Set basic params
    view.setUint8(paramsPtr + 0, 0); // structureId
    view.setUint8(paramsPtr + 1, 0); // moodId
    view.setUint8(paramsPtr + 2, 0); // chordId
    view.setUint8(paramsPtr + 3, 0); // key
    view.setUint8(paramsPtr + 4, 1); // drumsEnabled
    view.setUint8(paramsPtr + 5, 0); // modulation
    view.setUint8(paramsPtr + 6, 60); // vocalLow
    view.setUint8(paramsPtr + 7, 79); // vocalHigh
    view.setUint16(paramsPtr + 8, 0, true); // bpm
    view.setUint32(paramsPtr + 12, 12345, true); // seed
    // offset 31 is padding
    view.setUint16(paramsPtr + 32, 0, true); // targetDurationSeconds (0 = use structureId)

    const result = generate(handle, paramsPtr);
    module._free(paramsPtr);

    expect(result).toBe(0); // MIDISKETCH_OK
  });

  describe('targetDurationSeconds', () => {
    it('should reject duration below 60 seconds', () => {
      const generate = module.cwrap('midisketch_generate', 'number', ['number', 'number']) as (
        h: number,
        paramsPtr: number,
      ) => number;

      const paramsPtr = module._malloc(34);
      const view = new DataView(module.HEAPU8.buffer);

      // Set minimal valid params
      view.setUint8(paramsPtr + 0, 0); // structureId
      view.setUint8(paramsPtr + 1, 0); // moodId
      view.setUint8(paramsPtr + 2, 0); // chordId
      view.setUint8(paramsPtr + 3, 0); // key
      view.setUint8(paramsPtr + 4, 1); // drumsEnabled
      view.setUint8(paramsPtr + 5, 0); // modulation
      view.setUint8(paramsPtr + 6, 60); // vocalLow
      view.setUint8(paramsPtr + 7, 79); // vocalHigh
      view.setUint16(paramsPtr + 8, 120, true); // bpm
      view.setUint32(paramsPtr + 12, 12345, true); // seed
      view.setUint16(paramsPtr + 32, 30, true); // targetDurationSeconds (invalid: < 60)

      const result = generate(handle, paramsPtr);
      module._free(paramsPtr);

      expect(result).toBe(1); // MIDISKETCH_ERROR_INVALID_PARAM
    });

    it('should reject duration above 300 seconds', () => {
      const generate = module.cwrap('midisketch_generate', 'number', ['number', 'number']) as (
        h: number,
        paramsPtr: number,
      ) => number;

      const paramsPtr = module._malloc(34);
      const view = new DataView(module.HEAPU8.buffer);

      view.setUint8(paramsPtr + 0, 0);
      view.setUint8(paramsPtr + 1, 0);
      view.setUint8(paramsPtr + 2, 0);
      view.setUint8(paramsPtr + 3, 0);
      view.setUint8(paramsPtr + 4, 1);
      view.setUint8(paramsPtr + 5, 0);
      view.setUint8(paramsPtr + 6, 60);
      view.setUint8(paramsPtr + 7, 79);
      view.setUint16(paramsPtr + 8, 120, true);
      view.setUint32(paramsPtr + 12, 12345, true);
      view.setUint16(paramsPtr + 32, 400, true); // targetDurationSeconds (invalid: > 300)

      const result = generate(handle, paramsPtr);
      module._free(paramsPtr);

      expect(result).toBe(1); // MIDISKETCH_ERROR_INVALID_PARAM
    });

    it('should accept valid duration and generate song', () => {
      const generate = module.cwrap('midisketch_generate', 'number', ['number', 'number']) as (
        h: number,
        paramsPtr: number,
      ) => number;
      const getEvents = module.cwrap('midisketch_get_events', 'number', ['number']) as (
        h: number,
      ) => number;
      const freeEvents = module.cwrap('midisketch_free_events', null, ['number']) as (
        ptr: number,
      ) => void;

      const paramsPtr = module._malloc(34);
      const view = new DataView(module.HEAPU8.buffer);

      view.setUint8(paramsPtr + 0, 0);
      view.setUint8(paramsPtr + 1, 0);
      view.setUint8(paramsPtr + 2, 0);
      view.setUint8(paramsPtr + 3, 0);
      view.setUint8(paramsPtr + 4, 1);
      view.setUint8(paramsPtr + 5, 0);
      view.setUint8(paramsPtr + 6, 60);
      view.setUint8(paramsPtr + 7, 79);
      view.setUint16(paramsPtr + 8, 120, true); // bpm = 120
      view.setUint32(paramsPtr + 12, 12345, true);
      view.setUint16(paramsPtr + 32, 180, true); // targetDurationSeconds = 180 (3 minutes)

      const result = generate(handle, paramsPtr);
      module._free(paramsPtr);

      expect(result).toBe(0); // MIDISKETCH_OK

      // Verify generation produced valid output by checking events
      const eventDataPtr = getEvents(handle);
      const jsonPtr = module.HEAPU32[eventDataPtr >> 2];
      const json = module.UTF8ToString(jsonPtr);
      const data = JSON.parse(json);

      // Check that we have tracks and notes
      expect(data.tracks.length).toBeGreaterThan(0);

      // At 120 BPM, 180 seconds should produce ~90 bars worth of content
      // Verify there are a significant number of notes (duration-based songs are longer)
      const totalNotes = data.tracks.reduce(
        (sum: number, track: { notes: unknown[] }) => sum + track.notes.length,
        0,
      );
      expect(totalNotes).toBeGreaterThan(100); // Longer song = more notes

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

  describe('regenerateMelody', () => {
    it('should regenerate melody with new seed', () => {
      const regenerateMelody = module.cwrap('midisketch_regenerate_melody', 'number', [
        'number',
        'number',
      ]) as (h: number, seed: number) => number;

      const result = regenerateMelody(handle, 99999);
      expect(result).toBe(0); // MIDISKETCH_OK
    });
  });

  describe('regenerateMelodyEx', () => {
    it('should regenerate melody with full params', () => {
      const regenerateMelodyEx = module.cwrap('midisketch_regenerate_melody_ex', 'number', [
        'number',
        'number',
      ]) as (h: number, paramsPtr: number) => number;

      // Allocate MidiSketchMelodyParams struct (8 bytes)
      const paramsPtr = module._malloc(8);
      const view = new DataView(module.HEAPU8.buffer);

      view.setUint32(paramsPtr + 0, 54321, true); // seed
      view.setUint8(paramsPtr + 4, 55); // vocalLow
      view.setUint8(paramsPtr + 5, 74); // vocalHigh
      view.setUint8(paramsPtr + 6, 1); // vocalAttitude (Expressive)
      view.setUint8(paramsPtr + 7, 0); // compositionStyle (MelodyLead)

      const result = regenerateMelodyEx(handle, paramsPtr);
      module._free(paramsPtr);

      expect(result).toBe(0); // MIDISKETCH_OK
    });

    it('should change vocal range after regeneration', () => {
      const regenerateMelodyEx = module.cwrap('midisketch_regenerate_melody_ex', 'number', [
        'number',
        'number',
      ]) as (h: number, paramsPtr: number) => number;
      const getEvents = module.cwrap('midisketch_get_events', 'number', ['number']) as (
        h: number,
      ) => number;
      const freeEvents = module.cwrap('midisketch_free_events', null, ['number']) as (
        ptr: number,
      ) => void;

      // Regenerate with restricted range
      const paramsPtr = module._malloc(8);
      const view = new DataView(module.HEAPU8.buffer);

      view.setUint32(paramsPtr + 0, 11111, true); // seed
      view.setUint8(paramsPtr + 4, 65); // vocalLow (higher)
      view.setUint8(paramsPtr + 5, 72); // vocalHigh (lower)
      view.setUint8(paramsPtr + 6, 0); // vocalAttitude (Clean)
      view.setUint8(paramsPtr + 7, 0); // compositionStyle

      const result = regenerateMelodyEx(handle, paramsPtr);
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

      // All notes should be within range
      for (const note of vocalTrack.notes) {
        expect(note.pitch).toBeGreaterThanOrEqual(65);
        expect(note.pitch).toBeLessThanOrEqual(72);
      }

      freeEvents(eventDataPtr);
    });
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
