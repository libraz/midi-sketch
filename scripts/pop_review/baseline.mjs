#!/usr/bin/env node
/**
 * Measure every labelled reference song and write the per-metric bands a
 * review is read against.
 *
 *   node scripts/pop_review/baseline.mjs <reference-dir>... --out baseline.json
 *
 * Each directory carries its own `track_roles.json`; songs are taken from those
 * labels, not the directory listing, so an unlabelled file never enters the
 * band. A song without accompaniment (a melody-only file) contributes only
 * the metrics a melody decides alone, so it widens the vocal bands without
 * putting harmony readings of an empty harmony into the others. A song whose
 * vocal does not follow its accompaniment (a vocal from another edit) keeps
 * everything but the readings of the vocal against the harmony and the ones
 * taken through a form inferred from the two together. A song that
 * cannot be read is reported and skipped rather than failing the baseline.
 */

import { readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { parseArgs } from 'node:util';
import { UNALIGNED_VOCAL_METRICS, vocalFollowsAccompaniment } from './correctness.mjs';
import { MELODY_ONLY_METRICS } from './pop.mjs';
import { bandsOf } from './report.mjs';
import { reviewSong } from './review.mjs';

const { values, positionals } = parseArgs({
  allowPositionals: true,
  options: { out: { type: 'string' } },
});
if (!positionals.length || (positionals.length > 1 && !values.out)) {
  console.error('usage: baseline.mjs <reference-dir>... [--out baseline.json]');
  process.exit(2);
}

const songs = [];
const records = [];
const perSong = {};
const vocalUnaligned = [];
for (const dir of positionals) {
  const roles = join(dir, 'track_roles.json');
  const files = Object.keys(JSON.parse(readFileSync(roles, 'utf8'))).filter(
    (k) => !k.startsWith('_'),
  );
  for (const file of files) {
    try {
      const { reading, correctness, pop } = reviewSong(join(dir, file), { roles });
      const { progressions, ...flatPop } = pop;
      const accompanied = reading.song.harmony.length > 0 || reading.song.bass.length > 0;
      const aligned = vocalFollowsAccompaniment(reading.song) !== false;
      const keep = (k) =>
        accompanied ? aligned || !UNALIGNED_VOCAL_METRICS.has(k) : MELODY_ONLY_METRICS.has(k);
      const record = Object.fromEntries(
        Object.entries({ ...correctness, ...flatPop }).filter(([k]) => keep(k)),
      );
      records.push(record);
      songs.push(file);
      if (!aligned) {
        vocalUnaligned.push(file);
      }
      perSong[file] = accompanied ? { ...record, progressions } : record;
      const melodyOnly = accompanied ? '' : ' (melody only)';
      console.error(`measured ${file}${melodyOnly}${aligned ? '' : ' (vocal unaligned)'}`);
    } catch (error) {
      console.error(`skipped ${file}: ${error.message}`);
    }
  }
}
const baseline = { songs, vocalUnaligned, metrics: bandsOf(records), perSong };
const out = values.out ?? join(positionals[0], 'pop_baseline.json');
writeFileSync(out, `${JSON.stringify(baseline, null, 2)}\n`);
console.error(`wrote ${out} (${songs.length} songs)`);
