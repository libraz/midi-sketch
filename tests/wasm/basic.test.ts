import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { WasmTestContext } from './test-helpers';

describe('MidiSketch WASM - Basic', () => {
  const ctx = new WasmTestContext();

  beforeAll(async () => {
    await ctx.init();
  });

  afterAll(() => {
    ctx.destroy();
  });

  it('should create a valid handle', () => {
    expect(ctx.handle).toBeGreaterThan(0);
  });

  it('should return version string', () => {
    const version = ctx.module.cwrap('midisketch_version', 'string', []) as () => string;
    const versionStr = version();
    expect(versionStr).toMatch(/^\d+\.\d+\.\d+(\+.+)?$/);
  });

  it('should return structure count', () => {
    const structureCount = ctx.module.cwrap(
      'midisketch_structure_count',
      'number',
      [],
    ) as () => number;
    expect(structureCount()).toBeGreaterThan(0);
  });

  it('should return mood count', () => {
    const moodCount = ctx.module.cwrap('midisketch_mood_count', 'number', []) as () => number;
    expect(moodCount()).toBeGreaterThan(0);
  });

  it('should return chord count', () => {
    const chordCount = ctx.module.cwrap('midisketch_chord_count', 'number', []) as () => number;
    expect(chordCount()).toBeGreaterThan(0);
  });
});
