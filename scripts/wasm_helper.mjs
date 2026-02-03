#!/usr/bin/env node
/**
 * wasm_helper.mjs - Run WASM module and output events JSON or MIDI binary.
 *
 * Usage:
 *   node wasm_helper.mjs --seed 1 --style 0 --bpm 120 --blueprint 0 [--midi out.mid]
 *
 * Outputs events JSON to stdout. If --midi is specified, writes MIDI binary to that path.
 * Config fields not specified on the command line use createDefaultConfig() defaults.
 */

import { init, MidiSketch, createDefaultConfig } from '../dist/index.mjs';
import { writeFileSync } from 'node:fs';
import { parseArgs } from 'node:util';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';

const { values } = parseArgs({
  options: {
    seed:             { type: 'string' },
    style:            { type: 'string' },
    chord:            { type: 'string' },
    form:             { type: 'string' },
    bpm:              { type: 'string' },
    blueprint:        { type: 'string' },
    key:              { type: 'string' },
    duration:         { type: 'string' },
    mood:             { type: 'string' },
    'vocal-attitude': { type: 'string' },
    'vocal-low':      { type: 'string' },
    'vocal-high':     { type: 'string' },
    'vocal-style':    { type: 'string' },
    humanize:         { type: 'boolean', default: false },
    'humanize-timing':   { type: 'string' },
    'humanize-velocity': { type: 'string' },
    midi:             { type: 'string' },
    'dump-config':    { type: 'boolean', default: false },
  },
  strict: false,
});

const __dirname = dirname(fileURLToPath(import.meta.url));
const wasmPath = resolve(__dirname, '..', 'dist', 'midisketch.wasm');

await init({ wasmPath });

const styleId = parseInt(values.style ?? '1', 10);  // Default to style 1 (same as CLI)
const config = createDefaultConfig(styleId);

// Override only explicitly specified fields
if (values.seed !== undefined)              config.seed = parseInt(values.seed, 10);
if (values.chord !== undefined)             config.chordProgressionId = parseInt(values.chord, 10);
if (values.form !== undefined) {
  config.formId = parseInt(values.form, 10);
  config.formExplicit = true;
}
if (values.bpm !== undefined)               config.bpm = parseInt(values.bpm, 10);
if (values.blueprint !== undefined)         config.blueprintId = parseInt(values.blueprint, 10);
if (values.key !== undefined)               config.key = parseInt(values.key, 10);
if (values['vocal-attitude'] !== undefined) config.vocalAttitude = parseInt(values['vocal-attitude'], 10);
if (values['vocal-low'] !== undefined)      config.vocalLow = parseInt(values['vocal-low'], 10);
if (values['vocal-high'] !== undefined)     config.vocalHigh = parseInt(values['vocal-high'], 10);
if (values.duration !== undefined)          config.targetDurationSeconds = parseInt(values.duration, 10);
if (values.mood !== undefined) {
  config.mood = parseInt(values.mood, 10);
  config.moodExplicit = true;
}
if (values['vocal-style'] !== undefined)    config.vocalStyle = parseInt(values['vocal-style'], 10);
if (values.humanize)                        config.humanize = true;
if (values['humanize-timing'] !== undefined)   config.humanizeTiming = parseInt(values['humanize-timing'], 10);
if (values['humanize-velocity'] !== undefined) config.humanizeVelocity = parseInt(values['humanize-velocity'], 10);

if (values['dump-config']) {
  // Output effective config as JSON to stdout and exit (no generation)
  process.stdout.write(JSON.stringify(config));
  process.exit(0);
}

const sketch = new MidiSketch();
sketch.generateFromConfig(config);

// Output events JSON to stdout
const events = sketch.getEvents();
process.stdout.write(JSON.stringify(events));

// Optionally write MIDI binary
if (values.midi) {
  const midi = sketch.getMidi();
  writeFileSync(values.midi, midi);
}
