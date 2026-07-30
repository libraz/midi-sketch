/**
 * CLI/WASM Parity Test
 *
 * Verifies that the WASM (JSON API) and CLI produce equivalent MIDI output
 * for the same logical configuration. Generated event streams must be
 * byte-for-byte equivalent at the JSON field level.
 *
 * Strategy:
 * 1. Get full default config JSON from WASM C API (createDefaultSongConfig)
 * 2. Apply CLI's unconditional arg defaults (bpm=0, etc.) to align paths
 * 3. Apply test-specific overrides
 * 4. Generate via WASM with the full JSON
 * 5. Run CLI with matching flags
 * 6. Compare events JSON note-by-note
 */
import { execSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import createModule from '../../dist/midisketch.js';

const CLI_PATH = path.resolve(__dirname, '../../build/bin/midisketch_cli');

interface WasmModule {
  cwrap: (
    name: string,
    returnType: string | null,
    argTypes: string[],
  ) => (...args: unknown[]) => unknown;
  UTF8ToString: (ptr: number) => string;
  HEAPU8: Uint8Array;
  HEAPU32: Uint32Array;
}

interface ParityTestCase {
  name: string;
  stylePresetId: number;
  seed: number;
  blueprintId?: number;
  chordProgressionId?: number;
  bpm?: number;
  formId?: number;
  key?: number;
  vocalStyle?: number;
  vocalLow?: number;
  vocalHigh?: number;
}

interface NoteData {
  pitch: number;
  velocity: number;
  start_ticks: number;
  duration_ticks: number;
}

interface TrackData {
  name: string;
  notes: NoteData[];
}

interface EventsData {
  tracks: TrackData[];
}

describe('CLI/WASM Parity', () => {
  let module: WasmModule;
  let handle: number;
  let destroyFn: (h: number) => void;
  let generateFromJsonFn: (h: number, json: string, len: number) => number;
  let getEventsFn: (h: number) => number;
  let freeEventsFn: (ptr: number) => void;
  let createDefaultConfigJsonFn: (styleId: number) => number;

  beforeAll(async () => {
    module = (await createModule()) as WasmModule;

    const createFn = module.cwrap('midisketch_create', 'number', []) as () => number;
    destroyFn = module.cwrap('midisketch_destroy', null, ['number']) as (h: number) => void;
    generateFromJsonFn = module.cwrap('midisketch_generate_from_json', 'number', [
      'number',
      'string',
      'number',
    ]) as (h: number, json: string, len: number) => number;
    getEventsFn = module.cwrap('midisketch_get_events', 'number', ['number']) as (
      h: number,
    ) => number;
    freeEventsFn = module.cwrap('midisketch_free_events', null, ['number']) as (
      ptr: number,
    ) => void;
    createDefaultConfigJsonFn = module.cwrap('midisketch_create_default_config_json', 'number', [
      'number',
    ]) as (styleId: number) => number;

    handle = createFn();
  });

  afterAll(() => {
    if (handle && module) {
      destroyFn(handle);
    }
  });

  /**
   * Get full default SongConfig JSON from C API (matches createDefaultSongConfig).
   */
  function getDefaultConfigJson(styleId: number): Record<string, unknown> {
    const ptr = createDefaultConfigJsonFn(styleId);
    const jsonStr = module.UTF8ToString(ptr);
    return JSON.parse(jsonStr);
  }

  /**
   * Apply the same unconditional defaults that CLI's runGenerateMode applies
   * from ParsedArgs defaults. The CLI leaves BPM untouched unless --bpm is
   * supplied, so the style default from createDefaultSongConfig is retained.
   * Returns a new config object.
   */
  function withCliArgDefaults(config: Record<string, unknown>): Record<string, unknown> {
    return {
      ...config,
      // CLI unconditionally sets these from ParsedArgs defaults:
      mood: 0,
      mood_explicit: false,
      vocal_style: 0,
      target_duration_seconds: 0,
      skip_vocal: false,
      addictive_mode: false,
      arpeggio_enabled: false,
      composition_style: 0,
      modulation_timing: 0,
      enable_syncopation: false,
      // CLI unconditionally sets chord_extension sub-fields
      chord_extension: {
        ...((config.chord_extension ?? {}) as Record<string, unknown>),
        enable_sus: false,
        enable_9th: false,
      },
    };
  }

  /**
   * Generate via WASM using raw JSON config string.
   */
  function generateViaWasm(configJson: string): EventsData {
    const result = generateFromJsonFn(handle, configJson, configJson.length);
    expect(result).toBe(0);

    const eventDataPtr = getEventsFn(handle);
    const jsonPtr = module.HEAPU32[eventDataPtr >> 2];
    const json = module.UTF8ToString(jsonPtr);
    const data = JSON.parse(json) as EventsData;
    freeEventsFn(eventDataPtr);
    return data;
  }

  /**
   * Generate via CLI binary, returns parsed events JSON.
   */
  function generateViaCli(tc: ParityTestCase): EventsData {
    const args: string[] = [`--seed ${tc.seed}`, `--style ${tc.stylePresetId}`];
    if (tc.blueprintId !== undefined) {
      args.push(`--blueprint ${tc.blueprintId}`);
    }
    if (tc.chordProgressionId !== undefined) {
      args.push(`--chord ${tc.chordProgressionId}`);
    }
    if (tc.bpm !== undefined) {
      args.push(`--bpm ${tc.bpm}`);
    }
    if (tc.formId !== undefined) {
      args.push(`--form ${tc.formId}`);
    }
    if (tc.key !== undefined) {
      args.push(`--key ${tc.key}`);
    }
    if (tc.vocalStyle !== undefined) {
      args.push(`--vocal-style ${tc.vocalStyle}`);
    }
    if (tc.vocalLow !== undefined) {
      args.push(`--vocal-low ${tc.vocalLow}`);
    }
    if (tc.vocalHigh !== undefined) {
      args.push(`--vocal-high ${tc.vocalHigh}`);
    }

    const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'midisketch-parity-'));
    try {
      execSync(`${CLI_PATH} ${args.join(' ')}`, { cwd: tmpDir, stdio: 'pipe', timeout: 30000 });
      const jsonPath = path.join(tmpDir, 'output.json');
      return JSON.parse(fs.readFileSync(jsonPath, 'utf-8')) as EventsData;
    } finally {
      fs.rmSync(tmpDir, { recursive: true, force: true });
    }
  }

  /**
   * Build WASM config JSON that matches CLI behavior for a given test case.
   */
  function buildWasmConfig(tc: ParityTestCase): string {
    // Apply CLI's unconditional arg defaults first
    const config = withCliArgDefaults(getDefaultConfigJson(tc.stylePresetId));

    // Apply test-specific overrides (same as what CLI flags would set)
    config.seed = tc.seed;
    if (tc.blueprintId !== undefined) {
      config.blueprint_id = tc.blueprintId;
    }
    if (tc.chordProgressionId !== undefined) {
      config.chord_progression_id = tc.chordProgressionId;
    }
    if (tc.bpm !== undefined) {
      config.bpm = tc.bpm;
    }
    if (tc.formId !== undefined) {
      config.form = tc.formId;
      config.form_explicit = true;
    }
    if (tc.key !== undefined) {
      config.key = tc.key;
    }
    if (tc.vocalStyle !== undefined) {
      config.vocal_style = tc.vocalStyle;
    }
    if (tc.vocalLow !== undefined) {
      config.vocal_low = tc.vocalLow;
    }
    if (tc.vocalHigh !== undefined) {
      config.vocal_high = tc.vocalHigh;
    }

    return JSON.stringify(config);
  }

  /**
   * Compare events data from WASM and CLI, note-by-note.
   */
  function compareEvents(wasmData: EventsData, cliData: EventsData, label: string) {
    expect(wasmData.tracks.length).toBe(cliData.tracks.length);

    for (let i = 0; i < wasmData.tracks.length; i++) {
      const wt = wasmData.tracks[i];
      const ct = cliData.tracks[i];

      expect(wt.name).toBe(ct.name);

      const eventFields = (notes: NoteData[]) =>
        notes.map(({ pitch, velocity, start_ticks, duration_ticks }) => ({
          pitch,
          velocity,
          start_ticks,
          duration_ticks,
        }));
      expect(eventFields(wt.notes), `[${label}] Track "${wt.name}" events differ`).toEqual(
        eventFields(ct.notes),
      );
    }
  }

  // =========================================================================
  // Test cases: sweep each parameter axis independently
  // =========================================================================

  const testCases: ParityTestCase[] = [];

  // All 15 styles (0-14)
  for (let s = 0; s <= 14; s++) {
    testCases.push({ name: `style=${s}`, stylePresetId: s, seed: 42 });
  }

  // Preserve the public default-config path: no BPM flag is passed to the
  // CLI and createDefaultConfig() supplies the WASM-side setting.
  testCases.push({
    name: 'default-config: bpm omitted',
    stylePresetId: 0,
    seed: 42,
  });

  // All 10 blueprints (with style=0 for consistency)
  for (let b = 0; b <= 9; b++) {
    testCases.push({ name: `blueprint=${b}`, stylePresetId: 0, seed: 42, blueprintId: b });
  }
  testCases.push({
    name: 'blueprint=255 (random)',
    stylePresetId: 0,
    seed: 42,
    blueprintId: 255,
  });

  // Selected chord progressions
  for (const chord of [0, 5, 10, 15, 19]) {
    testCases.push({
      name: `chord=${chord}`,
      stylePresetId: 0,
      seed: 42,
      chordProgressionId: chord,
    });
  }

  // Selected forms
  for (const form of [0, 3, 5, 10, 15]) {
    testCases.push({ name: `form=${form}`, stylePresetId: 0, seed: 42, formId: form });
  }

  // All 12 keys
  for (let k = 0; k <= 11; k++) {
    testCases.push({ name: `key=${k}`, stylePresetId: 0, seed: 42, key: k });
  }

  // BPM values
  for (const bpm of [80, 120, 160, 200]) {
    testCases.push({ name: `bpm=${bpm}`, stylePresetId: 0, seed: 42, bpm });
  }

  // Different seeds
  for (const seed of [1, 999, 12345, 99999, 1000000]) {
    testCases.push({ name: `seed=${seed}`, stylePresetId: 0, seed });
  }

  // Vocal styles (explicit)
  for (const vs of [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13]) {
    testCases.push({ name: `vocal_style=${vs}`, stylePresetId: 0, seed: 42, vocalStyle: vs });
  }

  // Vocal range overrides
  testCases.push({
    name: 'vocal_low=57',
    stylePresetId: 0,
    seed: 42,
    vocalLow: 57,
  });
  testCases.push({
    name: 'vocal_range=55-84',
    stylePresetId: 0,
    seed: 42,
    vocalLow: 55,
    vocalHigh: 84,
  });

  // Style 13-14 with all blueprints
  for (let b = 0; b <= 9; b++) {
    testCases.push({
      name: `style=13,bp=${b}`,
      stylePresetId: 13,
      seed: 42,
      blueprintId: b,
    });
    testCases.push({
      name: `style=14,bp=${b}`,
      stylePresetId: 14,
      seed: 42,
      blueprintId: b,
    });
  }

  // WASM crash reproduction: exact config from backup metadata
  testCases.push({
    name: 'wasm-crash-repro: style=14 bp=1 vs=9 low=57',
    stylePresetId: 14,
    seed: 3072680300,
    blueprintId: 1,
    chordProgressionId: 2,
    bpm: 170,
    formId: 5,
    vocalStyle: 9,
    vocalLow: 57,
  });

  // Multi-parameter combinations
  testCases.push({
    name: 'combo: style=5 bp=3 bpm=140 key=4',
    stylePresetId: 5,
    seed: 99999,
    blueprintId: 3,
    bpm: 140,
    key: 4,
  });
  testCases.push({
    name: 'combo: style=8 chord=12 form=7 key=7',
    stylePresetId: 8,
    seed: 12345,
    chordProgressionId: 12,
    formId: 7,
    key: 7,
  });
  testCases.push({
    name: 'combo: style=2 bp=1 bpm=100 form=3 key=11',
    stylePresetId: 2,
    seed: 54321,
    blueprintId: 1,
    bpm: 100,
    formId: 3,
    key: 11,
  });
  testCases.push({
    name: 'combo: style=10 bp=7 chord=8 bpm=175 key=6',
    stylePresetId: 10,
    seed: 77777,
    blueprintId: 7,
    chordProgressionId: 8,
    bpm: 175,
    key: 6,
  });
  testCases.push({
    name: 'combo: style=12 bp=0 chord=18 form=12 key=9',
    stylePresetId: 12,
    seed: 31415,
    blueprintId: 0,
    chordProgressionId: 18,
    formId: 12,
    key: 9,
  });

  // Vocal style + style combos
  testCases.push({
    name: 'combo: style=13 vs=9 bp=5 bpm=160',
    stylePresetId: 13,
    seed: 88888,
    vocalStyle: 9,
    blueprintId: 5,
    bpm: 160,
  });
  testCases.push({
    name: 'combo: style=14 vs=3 bp=7 key=3',
    stylePresetId: 14,
    seed: 55555,
    vocalStyle: 3,
    blueprintId: 7,
    key: 3,
  });
  testCases.push({
    name: 'combo: style=0 vs=13 bpm=130 form=10',
    stylePresetId: 0,
    seed: 11111,
    vocalStyle: 13,
    bpm: 130,
    formId: 10,
  });

  // Run all test cases
  describe.each(testCases)('$name', (tc) => {
    it('WASM and CLI produce identical output', () => {
      const configJson = buildWasmConfig(tc);
      const wasmData = generateViaWasm(configJson);
      const cliData = generateViaCli(tc);
      compareEvents(wasmData, cliData, tc.name);
    }, 60_000);
  });
});
