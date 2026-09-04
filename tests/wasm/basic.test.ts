import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { ACCOMPANIMENT_FIELDS, CONFIG_FIELDS, NESTED_STRUCTS } from '../../js/src/config-fields';
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

  it('maps every C++ default-config key to a public SongConfig field', () => {
    // The other direction. A field added to the core without its entry in the
    // TypeScript table is invisible to every JS caller and silently absent from
    // a serialized config, which is the direction the three-place edit in
    // CLAUDE.md actually drifts.
    const getDefaultConfigJson = ctx.module.cwrap(
      'midisketch_create_default_config_json',
      'string',
      ['number'],
    ) as (styleId: number) => string;
    const defaultConfig = JSON.parse(getDefaultConfigJson(0)) as Record<string, unknown>;
    const mapped = new Set<string>([
      ...CONFIG_FIELDS.map(({ cpp }) => cpp),
      ...NESTED_STRUCTS.map(({ cpp }) => cpp),
    ]);

    expect(Object.keys(defaultConfig).filter((key) => !mapped.has(key))).toEqual([]);

    for (const group of NESTED_STRUCTS) {
      const nested = defaultConfig[group.cpp] as Record<string, unknown> | undefined;
      expect(nested, `${group.cpp} is missing from the default config`).toBeDefined();
      const known = new Set(group.fields.map(({ cpp }) => cpp));
      expect(
        Object.keys(nested).filter((key) => !known.has(key)),
        group.cpp,
      ).toEqual([]);
    }
  });
});
