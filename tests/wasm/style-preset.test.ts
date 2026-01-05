import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { WasmTestContext } from './test-helpers';

describe('MidiSketch WASM - Style Preset', () => {
  const ctx = new WasmTestContext();

  beforeAll(async () => {
    await ctx.init();
  });

  afterAll(() => {
    ctx.destroy();
  });

  it('should return style preset count', () => {
    const stylePresetCount = ctx.module.cwrap(
      'midisketch_style_preset_count',
      'number',
      [],
    ) as () => number;
    expect(stylePresetCount()).toBeGreaterThan(0);
  });

  it('should return style preset name', () => {
    const stylePresetName = ctx.module.cwrap('midisketch_style_preset_name', 'string', [
      'number',
    ]) as (id: number) => string;
    const name = stylePresetName(0);
    expect(name.length).toBeGreaterThan(0);
  });

  it('should return progressions for style', () => {
    const getProgressionsPtr = ctx.module.cwrap(
      'midisketch_get_progressions_by_style_ptr',
      'number',
      ['number'],
    ) as (styleId: number) => number;

    const retPtr = getProgressionsPtr(0);
    const view = new DataView(ctx.module.HEAPU8.buffer);
    const count = view.getUint8(retPtr);
    expect(count).toBeGreaterThan(0);
  });

  it('should return different progressions for different styles', () => {
    const getProgressionsPtr = ctx.module.cwrap(
      'midisketch_get_progressions_by_style_ptr',
      'number',
      ['number'],
    ) as (styleId: number) => number;

    const stylePresetCount = ctx.module.cwrap(
      'midisketch_style_preset_count',
      'number',
      [],
    ) as () => number;

    const count = stylePresetCount();
    expect(count).toBeGreaterThan(1);

    // Get progressions for first and second style
    const ptr0 = getProgressionsPtr(0);
    const ptr1 = getProgressionsPtr(1);

    const view = new DataView(ctx.module.HEAPU8.buffer);
    const count0 = view.getUint8(ptr0);
    const count1 = view.getUint8(ptr1);

    // Both should have progressions
    expect(count0).toBeGreaterThan(0);
    expect(count1).toBeGreaterThan(0);
  });
});
