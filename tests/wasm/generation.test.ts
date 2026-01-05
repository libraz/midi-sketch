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
});
