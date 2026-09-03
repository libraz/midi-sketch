/**
 * CLI/WASM Parity Test
 *
 * Verifies that the WASM build and the CLI produce equivalent MIDI output for
 * the same logical configuration. Generated event streams must be equivalent
 * at the JSON field level.
 *
 * The WASM side goes through the published TypeScript surface
 * (createDefaultConfig -> SongConfig -> generateFromConfig), so a defect in the
 * package's own camelCase/snake_case serializers fails this test instead of
 * being bypassed by a hand-built JSON payload.
 *
 * Strategy:
 * 1. Build a SongConfig with the public createDefaultConfig()
 * 2. Apply the CLI's unconditional arg defaults to align both paths
 * 3. Apply test-specific overrides
 * 4. Generate via the public MidiSketch API
 * 5. Run the CLI with matching flags
 * 6. Assert both sides actually produced music, then compare note-by-note
 */
import { execSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { createDefaultConfig, init, MidiSketch, type SongConfig } from '../../js/src/index';

const CLI_PATH = path.resolve(__dirname, '../../build/bin/midisketch_cli');

/**
 * Fewest sounding tracks a generated arrangement may have. A pop sketch always
 * carries at least a vocal, a bass and a chord track; anything less means
 * generation collapsed and the note-by-note comparison would be vacuous.
 */
const MIN_SOUNDING_TRACKS = 3;

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
  let sketch: MidiSketch;

  beforeAll(async () => {
    await init({ wasmPath: path.resolve(__dirname, '../../dist/midisketch.wasm') });
    sketch = new MidiSketch();
  });

  afterAll(() => {
    sketch?.destroy();
  });

  /**
   * Apply the same unconditional defaults that CLI's runGenerateMode applies
   * from ParsedArgs defaults. The CLI leaves BPM untouched unless --bpm is
   * supplied, so the style default from createDefaultConfig is retained.
   * Returns a new config object.
   */
  function withCliArgDefaults(config: SongConfig): SongConfig {
    return {
      ...config,
      // CLI unconditionally sets these from ParsedArgs defaults:
      mood: 0,
      moodExplicit: false,
      vocalStyle: 0,
      targetDurationSeconds: 0,
      skipVocal: false,
      addictiveMode: false,
      arpeggioEnabled: false,
      compositionStyle: 0,
      modulationTiming: 0,
      enableSyncopation: false,
      // CLI unconditionally sets the chord extension toggles
      chordExtSus: false,
      chordExt9th: false,
    };
  }

  /**
   * Generate through the published TypeScript surface, which serializes the
   * SongConfig with the package's own serializer.
   */
  function generateViaWasm(config: SongConfig): EventsData {
    sketch.generateFromConfig(config);
    return sketch.getEvents() as unknown as EventsData;
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
   * Build the SongConfig that matches CLI behavior for a given test case.
   */
  function buildWasmConfig(tc: ParityTestCase): SongConfig {
    // Apply CLI's unconditional arg defaults first
    const config = withCliArgDefaults(createDefaultConfig(tc.stylePresetId));

    // Apply test-specific overrides (same as what CLI flags would set)
    config.seed = tc.seed;
    if (tc.blueprintId !== undefined) {
      config.blueprintId = tc.blueprintId;
    }
    if (tc.chordProgressionId !== undefined) {
      config.chordProgressionId = tc.chordProgressionId;
    }
    if (tc.bpm !== undefined) {
      config.bpm = tc.bpm;
    }
    if (tc.formId !== undefined) {
      config.formId = tc.formId;
      config.formExplicit = true;
    }
    if (tc.key !== undefined) {
      config.key = tc.key;
    }
    if (tc.vocalStyle !== undefined) {
      config.vocalStyle = tc.vocalStyle;
    }
    if (tc.vocalLow !== undefined) {
      config.vocalLow = tc.vocalLow;
    }
    if (tc.vocalHigh !== undefined) {
      config.vocalHigh = tc.vocalHigh;
    }

    return config;
  }

  /** Total number of notes across every track. */
  function totalNotes(data: EventsData): number {
    return data.tracks.reduce((sum, track) => sum + track.notes.length, 0);
  }

  /** Names of the tracks that actually carry notes. */
  function soundingTrackNames(data: EventsData): string[] {
    return data.tracks.filter((track) => track.notes.length > 0).map((track) => track.name);
  }

  /**
   * Assert a side produced an actual arrangement. Without this, two silent
   * outputs would satisfy the note-by-note comparison and report parity.
   */
  function expectSoundingOutput(data: EventsData, label: string) {
    expect(totalNotes(data), `[${label}] produced no notes at all`).toBeGreaterThan(0);
    const sounding = soundingTrackNames(data);
    expect(
      sounding.length,
      `[${label}] only these tracks carry notes: ${JSON.stringify(sounding)}`,
    ).toBeGreaterThanOrEqual(MIN_SOUNDING_TRACKS);
    expect(sounding, `[${label}] the vocal track is silent`).toContain('Vocal');
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

  it('treats a silent arrangement as a failure rather than as parity', () => {
    const silent: EventsData = {
      tracks: [
        { name: 'Vocal', notes: [] },
        { name: 'Chord', notes: [] },
        { name: 'Bass', notes: [] },
      ],
    };

    // Note-by-note equality alone accepts two silent outputs as equivalent,
    // which is exactly why every case also asserts that music was produced.
    expect(() => compareEvents(silent, silent, 'silent fixture')).not.toThrow();
    expect(() => expectSoundingOutput(silent, 'silent fixture')).toThrow();
  });

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
      const wasmData = generateViaWasm(buildWasmConfig(tc));
      const cliData = generateViaCli(tc);
      expectSoundingOutput(wasmData, `${tc.name} / WASM`);
      expectSoundingOutput(cliData, `${tc.name} / CLI`);
      compareEvents(wasmData, cliData, tc.name);
    }, 60_000);
  });
});
