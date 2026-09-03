/**
 * Zero-argument init() must locate the WASM binary next to the shipped module.
 *
 * These tests run the published bundle from a temporary working directory that
 * contains no copy of midisketch.wasm, so a resolution that depends on the
 * process CWD fails here instead of only failing for package consumers.
 */
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';

const DIST_ENTRY = path.resolve(__dirname, '../../dist/index.mjs');

/** Run a script in a directory holding no WASM binary and return its stdout. */
function runInForeignCwd(cwd: string, source: string): string {
  const scriptPath = path.join(cwd, 'probe.mjs');
  fs.writeFileSync(scriptPath, source);
  return execFileSync(process.execPath, [scriptPath], {
    cwd,
    encoding: 'utf-8',
    timeout: 60_000,
  }).trim();
}

describe('init() WASM resolution', () => {
  let foreignCwd: string;

  beforeAll(() => {
    foreignCwd = fs.mkdtempSync(path.join(os.tmpdir(), 'midisketch-init-'));
  });

  afterAll(() => {
    fs.rmSync(foreignCwd, { recursive: true, force: true });
  });

  it('has no WASM binary in the working directory used by these tests', () => {
    expect(fs.existsSync(path.join(foreignCwd, 'midisketch.wasm'))).toBe(false);
    expect(fs.existsSync(DIST_ENTRY)).toBe(true);
  });

  it('resolves the WASM binary without a wasmPath option', () => {
    const output = runInForeignCwd(
      foreignCwd,
      `import { init } from ${JSON.stringify(pathToFileURL(DIST_ENTRY).href)};
await init();
process.stdout.write('initialized');
`,
    );
    expect(output).toBe('initialized');
  });

  it('runs the documented quick start and produces MIDI', () => {
    // Mirrors the JavaScript/TypeScript quick start in the README.
    const output = runInForeignCwd(
      foreignCwd,
      `import { createDefaultConfig, init, MidiSketch } from ${JSON.stringify(
        pathToFileURL(DIST_ENTRY).href,
      )};
await init();
const sketch = new MidiSketch();
const config = createDefaultConfig(0);
config.seed = 12345;

sketch.generateFromConfig(config);

const midiData = sketch.getMidi();
const noteCount = sketch.getEvents().tracks.reduce((sum, track) => sum + track.notes.length, 0);
sketch.destroy();
process.stdout.write(JSON.stringify({ midiBytes: midiData.length, noteCount }));
`,
    );
    const { midiBytes, noteCount } = JSON.parse(output) as {
      midiBytes: number;
      noteCount: number;
    };
    expect(midiBytes).toBeGreaterThan(0);
    expect(noteCount).toBeGreaterThan(0);
  });

  it('still honors an explicit wasmPath option', () => {
    const wasmPath = path.resolve(__dirname, '../../dist/midisketch.wasm');
    const output = runInForeignCwd(
      foreignCwd,
      `import { init } from ${JSON.stringify(pathToFileURL(DIST_ENTRY).href)};
await init({ wasmPath: ${JSON.stringify(wasmPath)} });
process.stdout.write('initialized');
`,
    );
    expect(output).toBe('initialized');
  });
});
