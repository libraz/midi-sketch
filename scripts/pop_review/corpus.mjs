#!/usr/bin/env node
/**
 * Review many generated songs at once and count, per metric, how many fall
 * outside the reference band.
 *
 *   node scripts/pop_review/corpus.mjs --baseline baseline.json <output.json>... [--all] [--json]
 *
 * A metric that leaves the band on most songs, whatever their blueprint, is a
 * property of the generator rather than of a style, and is where a defect is
 * looked for first. --all prints in-band metrics too.
 */

import { readFileSync } from 'node:fs';
import { parseArgs } from 'node:util';
import { reviewSong } from './review.mjs';

const { values, positionals } = parseArgs({
  allowPositionals: true,
  options: {
    baseline: { type: 'string' },
    all: { type: 'boolean', default: false },
    json: { type: 'boolean', default: false },
  },
});
if (!values.baseline || !positionals.length) {
  console.error('usage: corpus.mjs --baseline baseline.json <output.json>... [--all] [--json]');
  process.exit(2);
}
const bands = JSON.parse(readFileSync(values.baseline, 'utf8')).metrics;

const samples = {};
const flags = {};
for (const path of positionals) {
  const { correctness, pop } = reviewSong(path);
  for (const [name, value] of Object.entries({ ...correctness, ...pop })) {
    if (typeof value !== 'number') {
      continue;
    }
    samples[name] = [...(samples[name] ?? []), value];
    const band = bands[name];
    if (band && (value < band.min || value > band.max)) {
      const side = value < band.min ? 'low' : 'high';
      flags[name] = flags[name] ?? { low: 0, high: 0 };
      flags[name][side] += 1;
    }
  }
}

const median = (xs) => {
  const s = [...xs].sort((a, b) => a - b);
  const mid = Math.floor(s.length / 2);
  return s.length % 2 ? s[mid] : (s[mid - 1] + s[mid]) / 2;
};
const rows = Object.entries(samples)
  .filter(([name]) => values.all || flags[name])
  .map(([name, xs]) => ({
    metric: name,
    generatedMedian: median(xs),
    reference: bands[name] ?? null,
    low: flags[name]?.low ?? 0,
    high: flags[name]?.high ?? 0,
    songs: xs.length,
  }))
  .sort((a, b) => b.low + b.high - (a.low + a.high));

if (values.json) {
  console.log(JSON.stringify(rows, null, 2));
} else {
  const f = (v) => (Number.isInteger(v) ? String(v) : v.toFixed(3));
  console.log(`${positionals.length} songs`);
  console.log('| metric | generated median | reference median [min–max] (n) | low | high |');
  console.log('|---|---|---|---|---|');
  for (const r of rows) {
    const ref = r.reference
      ? `${f(r.reference.median)} [${f(r.reference.min)}–${f(r.reference.max)}] (${r.reference.n})`
      : '—';
    console.log(`| ${r.metric} | ${f(r.generatedMedian)} | ${ref} | ${r.low} | ${r.high} |`);
  }
}
