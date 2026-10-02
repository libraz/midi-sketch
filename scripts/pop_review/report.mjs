/**
 * Markdown rendering of a review, and the corpus statistics a baseline holds.
 *
 * A metric is flagged only against the reference band, never against a
 * threshold of this tool's own: below the lowest or above the highest
 * reference value. The band's sample count is always printed beside it,
 * because a band drawn from one or two songs is a fingerprint, not a norm.
 */

import { BEATS_PER_BAR } from './analysis.mjs';

/** Per-metric n/min/median/max over a list of metric records. */
export function bandsOf(records) {
  const names = new Set(records.flatMap((r) => Object.keys(r)));
  const bands = {};
  for (const name of names) {
    const xs = records
      .map((r) => r[name])
      .filter((v) => typeof v === 'number' && Number.isFinite(v))
      .sort((a, b) => a - b);
    if (!xs.length) {
      continue;
    }
    const mid = Math.floor(xs.length / 2);
    bands[name] = {
      n: xs.length,
      min: xs[0],
      median: xs.length % 2 ? xs[mid] : (xs[mid - 1] + xs[mid]) / 2,
      max: xs[xs.length - 1],
    };
  }
  return bands;
}

const fmt = (v) => {
  if (v === null || v === undefined) {
    return '—';
  }
  if (typeof v !== 'number') {
    return String(v);
  }
  return Number.isInteger(v) ? String(v) : v.toFixed(Math.abs(v) >= 10 ? 1 : 3);
};

function metricTable(title, metrics, band) {
  const lines = [`## ${title}`, ''];
  lines.push(band ? '| metric | value | reference median [min–max] (n) | |' : '| metric | value |');
  lines.push(band ? '|---|---|---|---|' : '|---|---|');
  for (const [name, value] of Object.entries(metrics)) {
    if (typeof value === 'object' && value !== null) {
      continue;
    }
    const b = band?.[name];
    if (!band) {
      lines.push(`| ${name} | ${fmt(value)} |`);
      continue;
    }
    let flag = '';
    if (b && typeof value === 'number' && value < b.min) {
      flag = 'LOW';
    } else if (b && typeof value === 'number' && value > b.max) {
      flag = 'HIGH';
    }
    const ref = b ? `${fmt(b.median)} [${fmt(b.min)}–${fmt(b.max)}] (${b.n})` : '—';
    lines.push(`| ${name} | ${fmt(value)} | ${ref} | ${flag} |`);
  }
  return lines.join('\n');
}

function formLine(reading) {
  const letters = reading.blocks.map((b) => {
    const family = String.fromCharCode(65 + (reading.families.familyOf[b.index] % 26));
    const sung = b.vocal.length >= 3 ? '' : '-';
    const chorus = reading.runs.some((r) => b.startBeat >= r.startBeat && b.startBeat < r.endBeat)
      ? '*'
      : '';
    return `${chorus}${family}${sung}`;
  });
  const declared = reading.song.sections
    ? `\nDeclared: ${reading.song.sections.map((s) => `${s.type}@${s.startBeat / BEATS_PER_BAR}`).join(' ')}`
    : '';
  return `Blocks of 4 bars by family (\`*\` chorus, \`-\` unsung): ${letters.join(' ')}${declared}`;
}

/** Render one review as markdown. */
export function formatReview(result, baseline) {
  const { reading, correctness, pop, fit } = result;
  const bands = baseline?.metrics ?? null;
  const out = [`# Pop review: ${reading.song.label}`, ''];
  out.push(formLine(reading));
  out.push(
    `Choruses (${reading.formSource}): ${reading.runs.map((r) => `${r.startBeat / BEATS_PER_BAR}–${r.endBeat / BEATS_PER_BAR}`).join(', ') || 'none'}`,
  );
  const progressions = Object.entries(pop.progressions ?? {}).map(([k, v]) => `${k}×${v}`);
  out.push(`Named progressions: ${progressions.join(', ') || 'none'}`);
  if (reading.hook) {
    out.push(
      `Hook: bar ${reading.hook.startBeat / BEATS_PER_BAR}, pitches ${reading.hook.notes.map((n) => n.pitch).join(' ')}, ` +
        `stated at bars ${reading.hook.statements.map((s) => s / BEATS_PER_BAR).join(', ')}`,
    );
  }
  out.push('', metricTable('Soundness', correctness, bands), '');
  out.push(metricTable('Pop craft', pop, bands), '');
  if (fit) {
    out.push('## Target fit', '');
    out.push(`Structure fit ${fmt(fit.structureFit)}, layout fit ${fmt(fit.layoutFit)}`);
    out.push(
      `Hook likeness to the target's closest passage ${fmt(fit.hookCopyLikeness)}, motif surface similarity ${fmt(fit.surfaceSimilarity)}` +
        (fit.copyRisk ? ' — **likely copy**' : ''),
    );
    out.push(`libcantus: ${fit.structure.rationale}`);
    out.push('', metricTable('Layout closeness', fit.layout, null));
  }
  if (baseline) {
    out.push('', `Reference band: ${baseline.songs.length} songs (${baseline.songs.join(', ')})`);
  }
  return out.join('\n');
}
