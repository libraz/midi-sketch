import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { ACCOMPANIMENT_FIELDS, CONFIG_FIELDS } from '../../js/src/config-fields';
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

  it('should match C++ accompaniment guitar default', () => {
    const guitar = ACCOMPANIMENT_FIELDS.find((field) => field.js === 'guitarEnabled');
    expect(guitar?.default).toBe(true);
  });

  it('should map every public SongConfig field to a C++ default-config key', () => {
    const getDefaultConfigJson = ctx.module.cwrap(
      'midisketch_create_default_config_json',
      'string',
      ['number'],
    ) as (styleId: number) => string;
    const defaultConfig = JSON.parse(getDefaultConfigJson(0)) as Record<string, unknown>;
    const defaultKeys = new Set(Object.keys(defaultConfig));

    expect(CONFIG_FIELDS.map(({ cpp }) => cpp).filter((cpp) => !defaultKeys.has(cpp))).toEqual([]);
  });
});
