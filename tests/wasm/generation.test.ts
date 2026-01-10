import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { WasmTestContext } from './test-helpers';

describe('MidiSketch WASM - Generation', () => {
  const ctx = new WasmTestContext();

  beforeAll(async () => {
    await ctx.init();
  });

  afterAll(() => {
    ctx.destroy();
  });

  it('should generate MIDI from config successfully', () => {
    const result = ctx.generateFromConfig({ seed: 12345 });
    expect(result).toBe(0); // MIDISKETCH_OK
  });

  it('should get MIDI data after generation', () => {
    const getMidi = ctx.module.cwrap('midisketch_get_midi', 'number', ['number']) as (
      h: number,
    ) => number;
    const freeMidi = ctx.module.cwrap('midisketch_free_midi', null, ['number']) as (
      ptr: number,
    ) => void;

    const midiDataPtr = getMidi(ctx.handle);
    expect(midiDataPtr).toBeGreaterThan(0);

    // MidiSketchMidiData: { data: ptr, size: size_t }
    const dataPtr = ctx.module.HEAPU32[midiDataPtr >> 2];
    const size = ctx.module.HEAPU32[(midiDataPtr + 4) >> 2];

    expect(dataPtr).toBeGreaterThan(0);
    expect(size).toBeGreaterThan(0);

    // Check MIDI header (MThd)
    const header = new Uint8Array(ctx.module.HEAPU8.buffer, dataPtr, 4);
    expect(String.fromCharCode(...header)).toBe('MThd');

    freeMidi(midiDataPtr);
  });

  it('should get events JSON after generation', () => {
    const { data, cleanup } = ctx.getEventsJson();

    expect(data).toHaveProperty('bpm');
    expect(data).toHaveProperty('tracks');
    expect((data as { tracks: unknown[] }).tracks.length).toBeGreaterThan(0);

    cleanup();
  });

  it('should generate all 8 tracks', () => {
    // Generate with all features enabled
    ctx.generateFromConfig({
      seed: 99999,
      arpeggioEnabled: true,
    });

    const { data, cleanup } = ctx.getEventsJson();
    const tracks = (data as { tracks: { name: string }[] }).tracks;

    // Verify expected track names exist
    const trackNames = tracks.map((t) => t.name);
    expect(trackNames).toContain('SE');
    expect(trackNames).toContain('Vocal');
    expect(trackNames).toContain('Chord');
    expect(trackNames).toContain('Bass');
    expect(trackNames).toContain('Drums');
    expect(trackNames).toContain('Arpeggio');
    // Motif and Aux may or may not be present depending on generation

    cleanup();
  });

  it('should generate tracks with notes', () => {
    ctx.generateFromConfig({
      seed: 54321,
      drumsEnabled: true,
    });

    const { data, cleanup } = ctx.getEventsJson();
    const tracks = (data as { tracks: { name: string; notes: unknown[] }[] }).tracks;

    // Core tracks must have notes
    const vocalTrack = tracks.find((t) => t.name === 'Vocal');
    const chordTrack = tracks.find((t) => t.name === 'Chord');
    const bassTrack = tracks.find((t) => t.name === 'Bass');
    const drumsTrack = tracks.find((t) => t.name === 'Drums');

    expect(vocalTrack).toBeDefined();
    expect(chordTrack).toBeDefined();
    expect(bassTrack).toBeDefined();
    expect(drumsTrack).toBeDefined();

    expect(vocalTrack!.notes.length).toBeGreaterThan(0);
    expect(chordTrack!.notes.length).toBeGreaterThan(0);
    expect(bassTrack!.notes.length).toBeGreaterThan(0);
    expect(drumsTrack!.notes.length).toBeGreaterThan(0);

    cleanup();
  });

  it('should generate valid note data', () => {
    ctx.generateFromConfig({ seed: 11111 });

    const { data, cleanup } = ctx.getEventsJson();
    const tracks = (
      data as {
        tracks: {
          name: string;
          notes: {
            start_ticks: number;
            duration_ticks: number;
            pitch: number;
            velocity: number;
          }[];
        }[];
      }
    ).tracks;

    const vocalTrack = tracks.find((t) => t.name === 'Vocal');
    expect(vocalTrack).toBeDefined();
    expect(vocalTrack!.notes.length).toBeGreaterThan(0);

    // Verify note structure
    const firstNote = vocalTrack!.notes[0];
    expect(firstNote).toHaveProperty('start_ticks');
    expect(firstNote).toHaveProperty('duration_ticks');
    expect(firstNote).toHaveProperty('pitch');
    expect(firstNote).toHaveProperty('velocity');

    // Verify note values are reasonable
    expect(firstNote.start_ticks).toBeGreaterThanOrEqual(0);
    expect(firstNote.duration_ticks).toBeGreaterThan(0);
    expect(firstNote.pitch).toBeGreaterThanOrEqual(0);
    expect(firstNote.pitch).toBeLessThanOrEqual(127);
    expect(firstNote.velocity).toBeGreaterThan(0);
    expect(firstNote.velocity).toBeLessThanOrEqual(127);

    cleanup();
  });

  it('should generate Aux track', () => {
    // Generate with settings that should produce Aux track
    ctx.generateFromConfig({
      seed: 22222,
    });

    const { data, cleanup } = ctx.getEventsJson();
    const tracks = (data as { tracks: { name: string; notes: unknown[] }[] }).tracks;

    // Verify Aux track exists
    const auxTrack = tracks.find((t) => t.name === 'Aux');
    expect(auxTrack).toBeDefined();
    expect(auxTrack!.notes.length).toBeGreaterThan(0);

    cleanup();
  });

  it('should get vocal preview MIDI (vocal + root bass)', () => {
    ctx.generateFromConfig({ seed: 33333 });

    const getMidi = ctx.module.cwrap('midisketch_get_midi', 'number', ['number']) as (
      h: number,
    ) => number;
    const getVocalPreviewMidi = ctx.module.cwrap('midisketch_get_vocal_preview_midi', 'number', [
      'number',
    ]) as (h: number) => number;
    const freeMidi = ctx.module.cwrap('midisketch_free_midi', null, ['number']) as (
      ptr: number,
    ) => void;

    // Get full MIDI
    const fullMidiPtr = getMidi(ctx.handle);
    expect(fullMidiPtr).toBeGreaterThan(0);
    const fullSize = ctx.module.HEAPU32[(fullMidiPtr + 4) >> 2];

    // Get vocal preview MIDI
    const previewMidiPtr = getVocalPreviewMidi(ctx.handle);
    expect(previewMidiPtr).toBeGreaterThan(0);

    const previewDataPtr = ctx.module.HEAPU32[previewMidiPtr >> 2];
    const previewSize = ctx.module.HEAPU32[(previewMidiPtr + 4) >> 2];

    expect(previewDataPtr).toBeGreaterThan(0);
    expect(previewSize).toBeGreaterThan(0);

    // Check MIDI header (MThd)
    const header = new Uint8Array(ctx.module.HEAPU8.buffer, previewDataPtr, 4);
    expect(String.fromCharCode(...header)).toBe('MThd');

    // Preview should be smaller than full MIDI (fewer tracks)
    expect(previewSize).toBeLessThan(fullSize);

    freeMidi(previewMidiPtr);
    freeMidi(fullMidiPtr);
  });
});
