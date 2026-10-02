/**
 * The shared readings every review layer is built on: libcantus's harmonic
 * analysis of the arrangement, a block-level form recovered from
 * self-similarity, the chorus family within it, and the hook.
 *
 * The form is recovered from the notes rather than taken from the generator's
 * section list, so a generated song and a reference are read the same way; a
 * generated song's declared sections are only used to check that reading.
 * Everything assumes 4/4, which is all the generator writes.
 */

import { analyzeArrangement, analyzeReference, melodicSimilarity } from '@libraz/libcantus';

export const BEATS_PER_BAR = 4;
/** Bars per form block: the unit sections are built from in pop writing. */
export const BLOCK_BARS = 4;
/** Libcantus work budget; a full reference arrangement needs more than its default. */
export const BUDGET = 2e8;

/**
 * Expected chord length handed to libcantus. A half-bar prior places chord
 * changes closer to the generator's declared ones than a whole-bar prior does.
 */
const HARMONIC_RHYTHM_BEATS = 2;
/** Two blocks at or above this likeness are read as one section family. */
const FAMILY_THRESHOLD = 0.7;
/** Two hook-length windows at or above this likeness are one statement of the hook. */
const HOOK_THRESHOLD = 0.8;
const HOOK_BARS = 2;
/** A vocal block needs this many notes before its melody is compared. */
const MIN_BLOCK_NOTES = 3;

const ROMAN = ['I', 'bII', 'II', 'bIII', 'III', 'IV', '#IV', 'V', 'bVI', 'VI', 'bVII', 'VII'];
const MINOR_QUALITIES = new Set([
  'min',
  'min7',
  'min6',
  'min9',
  'min11',
  'min13',
  'm7b5',
  'dim',
  'dim7',
]);

const mod12 = (n) => ((n % 12) + 12) % 12;
const mean = (xs) => (xs.length ? xs.reduce((a, b) => a + b, 0) / xs.length : 0);

/**
 * Tonic of the major key a region is read in: a minor region is read from its
 * relative major, as Japanese pop analysis numbers chords.
 */
export function majorTonicPc(key) {
  return key.variant === 'major' ? key.scale.rootPc : mod12(key.scale.rootPc + 3);
}

/** Roman numeral of a chord root relative to a major tonic, lowercased for minor qualities. */
export function romanOf(rootPc, quality, tonicPc) {
  const roman = ROMAN[mod12(rootPc - tonicPc)];
  return MINOR_QUALITIES.has(quality) ? roman.replace(/[IV]+/, (m) => m.toLowerCase()) : roman;
}

/**
 * Infer the harmony of a song with libcantus.
 *
 * Chords are read from the bass and harmony parts only; the vocal is analysed
 * against them, so its non-chord tones cannot vote the chord away.
 */
export function readHarmony(song) {
  const tracks = [
    { name: 'melody', role: 'melody', notes: song.melody },
    { name: 'bass', role: 'bass', notes: song.bass },
    { name: 'harmony', role: 'harmony', notes: song.harmony },
  ].filter((t) => t.notes.length > 0);
  const harmonyTracks = tracks.flatMap((t, i) => (t.role === 'melody' ? [] : [i]));
  const analysis = analyzeArrangement(tracks, {
    harmonyTracks,
    harmonicRhythm: HARMONIC_RHYTHM_BEATS,
    budget: BUDGET,
  });
  const keyAt = (beat) =>
    (analysis.keys.find((k) => beat >= k.startBeat && beat < k.endBeat) ?? analysis.keys[0])?.key ??
    analysis.prevailingKey;
  const chords = analysis.timeline.segments.map((seg) => {
    const tonicPc = majorTonicPc(keyAt(seg.startBeat));
    return {
      startBeat: seg.startBeat,
      endBeat: seg.endBeat,
      rootPc: seg.chord.rootPc,
      quality: seg.chord.quality,
      degree: mod12(seg.chord.rootPc - tonicPc),
      roman: romanOf(seg.chord.rootPc, seg.chord.quality, tonicPc),
    };
  });
  const trackIndex = (name) => tracks.findIndex((t) => t.name === name);
  return { analysis, tracks, chords, keyAt, trackIndex };
}

/** The chord read at a beat, or null. */
export function chordAt(harmony, beat) {
  return harmony.chords.find((c) => beat >= c.startBeat && beat < c.endBeat) ?? null;
}

/** libcantus's structural profile of the song: form, phrases, motifs, harmony. */
export function profileOf(song, harmony) {
  return analyzeReference([...song.bass, ...song.harmony], {
    melody: song.melody,
    timeline: harmony.analysis.timeline,
    totalBeats: song.totalBeats,
    budget: BUDGET,
  });
}

const within = (notes, start, end) =>
  notes.filter((n) => n.startBeat >= start && n.startBeat < end);

/**
 * Cut the song into blocks of {@link BLOCK_BARS} bars, aligned so a block
 * boundary falls on the bar where the vocal first enters.
 */
export function cutBlocks(song, harmony) {
  const span = BLOCK_BARS * BEATS_PER_BAR;
  const entryBar = song.melody.length ? Math.floor(song.melody[0].startBeat / BEATS_PER_BAR) : 0;
  const origin = (entryBar % BLOCK_BARS) * BEATS_PER_BAR;
  const starts = origin > 0 ? [0] : [];
  for (let s = origin; s < song.totalBeats - BEATS_PER_BAR; s += span) {
    starts.push(s);
  }
  const allNotes = song.parts.flatMap((p) => p.notes);
  return starts.map((start, index) => {
    const end = Math.min(
      index + 1 < starts.length ? starts[index + 1] : start + span,
      song.totalBeats,
    );
    const bars = (end - start) / BEATS_PER_BAR;
    const vocal = within(song.melody, start, end);
    const slots = [];
    for (let b = start; b < end; b += BEATS_PER_BAR / 2) {
      slots.push(chordAt(harmony, b)?.degree ?? -1);
    }
    return {
      index,
      startBeat: start,
      endBeat: end,
      bars,
      vocal,
      degrees: slots,
      vocalMean: vocal.length ? mean(vocal.map((n) => n.pitch)) : null,
      vocalPeak: vocal.length ? Math.max(...vocal.map((n) => n.pitch)) : null,
      onsetsPerBar: within(allNotes, start, end).length / bars,
      velocity: mean(within(allNotes, start, end).map((n) => n.velocity ?? 0)),
    };
  });
}

function harmonicLikeness(a, b) {
  const n = Math.min(a.degrees.length, b.degrees.length);
  if (n === 0) {
    return 0;
  }
  let same = 0;
  for (let i = 0; i < n; i += 1) {
    if (a.degrees[i] >= 0 && a.degrees[i] === b.degrees[i]) {
      same += 1;
    }
  }
  return same / Math.max(a.degrees.length, b.degrees.length);
}

/** How alike two blocks are, melody first and harmony second. */
export function blockLikeness(a, b) {
  const av = a.vocal.length >= MIN_BLOCK_NOTES;
  const bv = b.vocal.length >= MIN_BLOCK_NOTES;
  if (av !== bv) {
    return 0;
  }
  const harmonic = harmonicLikeness(a, b);
  return av ? 0.65 * melodicSimilarity(a.vocal, b.vocal) + 0.35 * harmonic : harmonic;
}

/**
 * Group blocks into section families: each block joins the earliest family
 * whose first block it resembles, or starts a new one.
 */
export function familiesOf(blocks) {
  const heads = [];
  const familyOf = blocks.map((block) => {
    for (let f = 0; f < heads.length; f += 1) {
      if (blockLikeness(blocks[heads[f]], block) >= FAMILY_THRESHOLD) {
        return f;
      }
    }
    heads.push(block.index);
    return heads.length - 1;
  });
  return { familyOf, count: heads.length };
}

const zscores = (values) => {
  const m = mean(values);
  const sd = Math.sqrt(mean(values.map((v) => (v - m) ** 2))) || 1;
  return values.map((v) => (v - m) / sd);
};

/**
 * The chorus family: among sung families heard at least twice, the one that
 * sits highest, plays densest and loudest, weighted by how often it recurs.
 * Recurring families that only ever directly follow a chorus block join it, so a chorus
 * whose two halves differ is read as one section. Returns null when no sung
 * family recurs.
 */
export function chorusFamily(blocks, families) {
  const sung = blocks.filter((b) => b.vocal.length >= MIN_BLOCK_NOTES);
  if (!sung.length) {
    return null;
  }
  const energy = new Map();
  const zs = [
    zscores(sung.map((x) => x.vocalMean)),
    zscores(sung.map((x) => x.onsetsPerBar)),
    zscores(sung.map((x) => x.velocity)),
  ];
  sung.forEach((b, i) => {
    energy.set(b.index, zs[0][i] + zs[1][i] + zs[2][i]);
  });
  let best = null;
  for (let f = 0; f < families.count; f += 1) {
    const members = sung.filter((b) => families.familyOf[b.index] === f);
    if (members.length < 2) {
      continue;
    }
    const score = mean(members.map((b) => energy.get(b.index))) + 0.5 * members.length;
    if (!best || score > best.score) {
      best = { family: f, score };
    }
  }
  if (!best) {
    return null;
  }
  const inChorus = new Set(
    blocks.filter((b) => families.familyOf[b.index] === best.family).map((b) => b.index),
  );
  for (let grew = true; grew; ) {
    grew = false;
    for (let f = 0; f < families.count; f += 1) {
      const members = blocks.filter((b) => families.familyOf[b.index] === f);
      if (members.some((b) => inChorus.has(b.index))) {
        continue;
      }
      const companion =
        members.length >= 2 &&
        members.every((b) => b.vocal.length >= MIN_BLOCK_NOTES && inChorus.has(b.index - 1));
      if (companion) {
        for (const b of members) {
          inChorus.add(b.index);
        }
        grew = true;
      }
    }
  }
  return { family: best.family, score: best.score, blocks: [...inChorus].sort((a, b) => a - b) };
}

/** Runs of consecutive chorus blocks: each run is one chorus. */
export function chorusRuns(blocks, chorus) {
  if (!chorus) {
    return [];
  }
  const inChorus = new Set(chorus.blocks);
  const runs = [];
  for (const block of blocks) {
    const previous = runs[runs.length - 1];
    if (!inChorus.has(block.index)) {
      continue;
    }
    if (previous && previous.endBlock === block.index - 1) {
      previous.endBlock = block.index;
      previous.endBeat = block.endBeat;
    } else {
      runs.push({
        startBlock: block.index,
        endBlock: block.index,
        startBeat: block.startBeat,
        endBeat: block.endBeat,
      });
    }
  }
  return runs.map(({ startBeat, endBeat }) => ({ startBeat, endBeat }));
}

/** Choruses as the song's own section list states them: consecutive Chorus sections merged. */
export function declaredChorusRuns(sections) {
  const runs = [];
  for (const s of sections) {
    if (s.type !== 'Chorus') {
      continue;
    }
    const previous = runs[runs.length - 1];
    if (previous && previous.endBeat === s.startBeat) {
      previous.endBeat = s.endBeat;
    } else {
      runs.push({ startBeat: s.startBeat, endBeat: s.endBeat });
    }
  }
  return runs;
}

/** Share of declared chorus beats the inferred choruses cover, and vice versa. */
export function runAgreement(declared, inferred) {
  const overlap = (xs, ys) =>
    xs.reduce(
      (sum, x) =>
        sum +
        ys.reduce(
          (acc, y) =>
            acc + Math.max(0, Math.min(x.endBeat, y.endBeat) - Math.max(x.startBeat, y.startBeat)),
          0,
        ),
      0,
    );
  const length = (xs) => xs.reduce((sum, x) => sum + x.endBeat - x.startBeat, 0);
  const shared = overlap(declared, inferred);
  return {
    recall: length(declared) ? shared / length(declared) : null,
    precision: length(inferred) ? shared / length(inferred) : null,
  };
}

/**
 * Find the hook: the {@link HOOK_BARS}-bar sung window, starting inside the
 * first chorus, that the song restates most often. Statements are counted
 * without overlap across the whole song.
 */
export function findHook(song, runs) {
  if (!runs.length) {
    return null;
  }
  const span = HOOK_BARS * BEATS_PER_BAR;
  const windows = [];
  const firstBar = Math.floor((song.melody[0]?.startBeat ?? 0) / BEATS_PER_BAR) * BEATS_PER_BAR;
  for (let s = firstBar; s + span <= song.totalBeats; s += BEATS_PER_BAR) {
    const notes = within(song.melody, s, s + span);
    if (notes.length >= MIN_BLOCK_NOTES) {
      windows.push({ startBeat: s, notes });
    }
  }
  const first = runs[0];
  const candidates = windows.filter(
    (w) => w.startBeat >= first.startBeat && w.startBeat + span <= first.endBeat,
  );
  let best = null;
  for (const candidate of candidates) {
    const statements = [];
    let freeFrom = Number.NEGATIVE_INFINITY;
    for (const w of windows) {
      if (w.startBeat < freeFrom) {
        continue;
      }
      if (melodicSimilarity(candidate.notes, w.notes) >= HOOK_THRESHOLD) {
        statements.push(w.startBeat);
        freeFrom = w.startBeat + span;
      }
    }
    if (!best || statements.length > best.statements.length) {
      best = { startBeat: candidate.startBeat, notes: candidate.notes, statements };
    }
  }
  return best;
}

/** Everything the review layers read, computed once per song. */
export function readSong(song) {
  const harmony = readHarmony(song);
  const blocks = cutBlocks(song, harmony);
  const families = familiesOf(blocks);
  const chorus = chorusFamily(blocks, families);
  const inferredRuns = chorusRuns(blocks, chorus);
  const declaredRuns = song.sections ? declaredChorusRuns(song.sections) : [];
  const formSource = declaredRuns.length ? 'declared' : 'inferred';
  const runs = formSource === 'declared' ? declaredRuns : inferredRuns;
  const agreement = declaredRuns.length ? runAgreement(declaredRuns, inferredRuns) : null;
  const hook = findHook(song, runs);
  const profile = profileOf(song, harmony);
  return {
    song,
    harmony,
    blocks,
    families,
    chorus,
    runs,
    inferredRuns,
    formSource,
    agreement,
    hook,
    profile,
  };
}
