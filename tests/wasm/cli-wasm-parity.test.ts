/**
 * CLI/WASM Parity Test
 *
 * Verifies that the WASM (JSON API) and CLI produce identical MIDI output
 * for the same logical configuration. Both paths should converge at
 * SongConfig -> generateFromConfig(), producing bitwise identical results.
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
   * from ParsedArgs defaults. This aligns the WASM config with CLI behavior
   * when no explicit flags are passed.
   */
  function applyCliArgDefaults(config: Record<string, unknown>) {
    // CLI unconditionally sets these from ParsedArgs defaults:
    config.mood = 0;
    config.mood_explicit = false;
    config.vocal_style = 0;
    config.bpm = 0; // args.bpm defaults to 0 (= auto, resolved during generation)
    config.target_duration_seconds = 0;
    config.skip_vocal = false;
    config.addictive_mode = false;
    config.arpeggio_enabled = false;
    config.composition_style = 0;
    config.modulation_timing = 0;
    config.enable_syncopation = false;

    // CLI unconditionally sets chord_extension sub-fields
    const chordExt = (config.chord_extension ?? {}) as Record<string, unknown>;
    chordExt.enable_sus = false;
    chordExt.enable_9th = false;
    config.chord_extension = chordExt;
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
    const config = getDefaultConfigJson(tc.stylePresetId);

    // Apply CLI's unconditional arg defaults first
    applyCliArgDefaults(config);

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

      if (wt.notes.length !== ct.notes.length) {
        throw new Error(
          `[${label}] Track "${wt.name}": note count mismatch — ` +
            `WASM=${wt.notes.length}, CLI=${ct.notes.length}`,
        );
      }

      for (let j = 0; j < wt.notes.length; j++) {
        const wn = wt.notes[j];
        const cn = ct.notes[j];

        if (
          wn.pitch !== cn.pitch ||
          wn.velocity !== cn.velocity ||
          wn.start_ticks !== cn.start_ticks ||
          wn.duration_ticks !== cn.duration_ticks
        ) {
          throw new Error(
            `[${label}] Track "${wt.name}" note #${j}: ` +
              `WASM(p=${wn.pitch},v=${wn.velocity},t=${wn.start_ticks},d=${wn.duration_ticks}) !== ` +
              `CLI(p=${cn.pitch},v=${cn.velocity},t=${cn.start_ticks},d=${cn.duration_ticks})`,
          );
        }
      }
    }
  }

  // =========================================================================
  // Test cases: sweep each parameter axis independently
  // =========================================================================

  const testCases: ParityTestCase[] = [];

  // All 13 styles
  for (let s = 0; s <= 12; s++) {
    testCases.push({ name: `style=${s}`, stylePresetId: s, seed: 42 });
  }

  // All 9 blueprints (with style=0 for consistency)
  for (let b = 0; b <= 8; b++) {
    testCases.push({ name: `blueprint=${b}`, stylePresetId: 0, seed: 42, blueprintId: b });
  }

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

  // Run all test cases
  describe.each(testCases)('$name', (tc) => {
    it('WASM and CLI produce identical output', () => {
      const configJson = buildWasmConfig(tc);
      const wasmData = generateViaWasm(configJson);
      const cliData = generateViaCli(tc);
      compareEvents(wasmData, cliData, tc.name);
    });
  });
});
