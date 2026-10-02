#!/usr/bin/env node

/**
 * Review one song as pop music.
 *
 *   node scripts/pop_review/review.mjs <output.json | ref.mid>
 *     [--baseline baseline.json] [--target ref.mid] [--roles track_roles.json] [--json]
 *
 * Prints soundness, pop-craft and (with --target) fit-and-novelty readings.
 * With --baseline each metric is placed against the reference corpus.
 */

import { readFileSync } from 'node:fs';
import { parseArgs } from 'node:util';
import { readSong } from './analysis.mjs';
import { correctnessOf } from './correctness.mjs';
import { popOf } from './pop.mjs';
import { formatReview } from './report.mjs';
import { loadSong } from './song.mjs';
import { targetFit } from './target.mjs';

/** Read a song and take every measurement the review reports. */
export function reviewSong(path, opts = {}) {
  const reading = readSong(loadSong(path, opts));
  return { reading, correctness: correctnessOf(reading), pop: popOf(reading) };
}

/** Review a candidate, optionally against a target. */
export function review(path, opts = {}) {
  const candidate = reviewSong(path, opts);
  let fit = null;
  if (opts.target) {
    const target = opts.targetReview ?? reviewSong(opts.target, opts);
    fit = targetFit(candidate.reading, candidate.pop, target.reading, target.pop);
    candidate.target = target;
  }
  return { ...candidate, fit };
}

/** The serializable part of a review. */
export function summarize(result) {
  const { reading, correctness, pop, fit } = result;
  return {
    label: reading.song.label,
    source: reading.song.source,
    formSource: reading.formSource,
    choruses: reading.runs.map((r) => [r.startBeat / 4, r.endBeat / 4]),
    correctness,
    pop,
    fit: fit && {
      structureFit: fit.structureFit,
      layoutFit: fit.layoutFit,
      hookCopyLikeness: fit.hookCopyLikeness,
      surfaceSimilarity: fit.surfaceSimilarity,
      copyRisk: fit.copyRisk,
      structure: fit.structure,
      layout: fit.layout,
    },
  };
}

if (import.meta.url === `file://${process.argv[1]}`) {
  const { values, positionals } = parseArgs({
    allowPositionals: true,
    options: {
      baseline: { type: 'string' },
      target: { type: 'string' },
      roles: { type: 'string' },
      json: { type: 'boolean', default: false },
    },
  });
  if (positionals.length !== 1) {
    console.error(
      'usage: review.mjs <output.json | ref.mid> [--baseline f] [--target ref.mid] [--roles f] [--json]',
    );
    process.exit(2);
  }
  const result = review(positionals[0], { target: values.target, roles: values.roles });
  const baseline = values.baseline ? JSON.parse(readFileSync(values.baseline, 'utf8')) : null;
  console.log(
    values.json ? JSON.stringify(summarize(result), null, 2) : formatReview(result, baseline),
  );
}
