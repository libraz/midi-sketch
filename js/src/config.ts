/**
 * Configuration utilities for SongConfig
 */

import type { ConfigErrorCode } from './constants';
import { type EmscriptenModule, getApi, getModule } from './internal';
import type { SongConfig } from './types';

/**
 * Create a default song config for a style
 */
export function createDefaultConfig(styleId: number): SongConfig {
  const a = getApi();
  const m = getModule();
  const retPtr = a.createDefaultConfigPtr(styleId);
  const view = new DataView(m.HEAPU8.buffer);
  return {
    // Basic settings (offset 0-12)
    stylePresetId: view.getUint8(retPtr + 0),
    key: view.getUint8(retPtr + 1),
    bpm: view.getUint16(retPtr + 2, true),
    seed: view.getUint32(retPtr + 4, true),
    chordProgressionId: view.getUint8(retPtr + 8),
    formId: view.getUint8(retPtr + 9),
    vocalAttitude: view.getUint8(retPtr + 10),
    drumsEnabled: view.getUint8(retPtr + 11) !== 0,
    blueprintId: view.getUint8(retPtr + 12),

    // Arpeggio settings (offset 13-17)
    arpeggioEnabled: view.getUint8(retPtr + 13) !== 0,
    arpeggioPattern: view.getUint8(retPtr + 14),
    arpeggioSpeed: view.getUint8(retPtr + 15),
    arpeggioOctaveRange: view.getUint8(retPtr + 16),
    arpeggioGate: view.getUint8(retPtr + 17),

    // Vocal settings (offset 18-20)
    vocalLow: view.getUint8(retPtr + 18),
    vocalHigh: view.getUint8(retPtr + 19),
    skipVocal: view.getUint8(retPtr + 20) !== 0,

    // Humanization (offset 21-23)
    humanize: view.getUint8(retPtr + 21) !== 0,
    humanizeTiming: view.getUint8(retPtr + 22),
    humanizeVelocity: view.getUint8(retPtr + 23),

    // Chord extensions (offset 24-31, skip tritone_sub at 27 and 31)
    chordExtSus: view.getUint8(retPtr + 24) !== 0,
    chordExt7th: view.getUint8(retPtr + 25) !== 0,
    chordExt9th: view.getUint8(retPtr + 26) !== 0,
    // offset 27 = chord_ext_tritone_sub (not exposed)
    chordExtSusProb: view.getUint8(retPtr + 28),
    chordExt7thProb: view.getUint8(retPtr + 29),
    chordExt9thProb: view.getUint8(retPtr + 30),
    // offset 31 = chord_ext_tritone_sub_prob (not exposed)

    // Composition style (offset 32)
    compositionStyle: view.getUint8(retPtr + 32),

    // Duration (offset 34-35)
    targetDurationSeconds: view.getUint16(retPtr + 34, true),

    // Modulation settings (offset 36-37)
    modulationTiming: view.getUint8(retPtr + 36),
    modulationSemitones: view.getInt8(retPtr + 37),

    // SE/Call settings (offset 38-43)
    seEnabled: view.getUint8(retPtr + 38) !== 0,
    callEnabled: view.getUint8(retPtr + 39) !== 0,
    callNotesEnabled: view.getUint8(retPtr + 40) !== 0,
    introChant: view.getUint8(retPtr + 41),
    mixPattern: view.getUint8(retPtr + 42),
    callDensity: view.getUint8(retPtr + 43),

    // Vocal style settings (offset 44-45)
    vocalStyle: view.getUint8(retPtr + 44),
    melodyTemplate: view.getUint8(retPtr + 45),

    // Arrangement settings (offset 46)
    arrangementGrowth: view.getUint8(retPtr + 46),

    // Arpeggio sync settings (offset 47)
    arpeggioSyncChord: view.getUint8(retPtr + 47) !== 0,

    // Motif settings (offset 48-50)
    motifRepeatScope: view.getUint8(retPtr + 48),
    motifFixedProgression: view.getUint8(retPtr + 49) !== 0,
    motifMaxChordCount: view.getUint8(retPtr + 50),

    // Melodic complexity and hook control (offset 51-53)
    melodicComplexity: view.getUint8(retPtr + 51),
    hookIntensity: view.getUint8(retPtr + 52),
    vocalGroove: view.getUint8(retPtr + 53),
  };
}

/**
 * Validate a song config before generation.
 * Returns the error code (0 = OK, non-zero = error).
 * Use getConfigErrorMessage() to get human-readable error message.
 */
export function validateConfig(config: SongConfig): ConfigErrorCode {
  const a = getApi();
  const m = getModule();
  const configPtr = allocSongConfig(m, config);
  try {
    return a.validateConfig(configPtr) as ConfigErrorCode;
  } finally {
    m._free(configPtr);
  }
}

/**
 * Get human-readable error message for a config error code.
 */
export function getConfigErrorMessage(errorCode: ConfigErrorCode): string {
  const a = getApi();
  return a.configErrorString(errorCode);
}

/**
 * Allocate SongConfig in WASM memory
 * @internal
 */
export function allocSongConfig(m: EmscriptenModule, config: SongConfig): number {
  const ptr = m._malloc(54); // MidiSketchSongConfig size
  const view = new DataView(m.HEAPU8.buffer);

  // Basic settings (offset 0-12)
  view.setUint8(ptr + 0, config.stylePresetId ?? 0);
  view.setUint8(ptr + 1, config.key ?? 0);
  view.setUint16(ptr + 2, config.bpm ?? 0, true);
  view.setUint32(ptr + 4, config.seed ?? 0, true);
  view.setUint8(ptr + 8, config.chordProgressionId ?? 0);
  view.setUint8(ptr + 9, config.formId ?? 0);
  view.setUint8(ptr + 10, config.vocalAttitude ?? 0);
  view.setUint8(ptr + 11, config.drumsEnabled !== false ? 1 : 0);
  view.setUint8(ptr + 12, config.blueprintId ?? 0);

  // Arpeggio settings (offset 13-17)
  view.setUint8(ptr + 13, config.arpeggioEnabled ? 1 : 0);
  view.setUint8(ptr + 14, config.arpeggioPattern ?? 0);
  view.setUint8(ptr + 15, config.arpeggioSpeed ?? 1);
  view.setUint8(ptr + 16, config.arpeggioOctaveRange ?? 2);
  view.setUint8(ptr + 17, config.arpeggioGate ?? 80);

  // Vocal settings (offset 18-20)
  view.setUint8(ptr + 18, config.vocalLow ?? 55);
  view.setUint8(ptr + 19, config.vocalHigh ?? 74);
  view.setUint8(ptr + 20, config.skipVocal ? 1 : 0);

  // Humanization (offset 21-23)
  view.setUint8(ptr + 21, config.humanize ? 1 : 0);
  view.setUint8(ptr + 22, config.humanizeTiming ?? 50);
  view.setUint8(ptr + 23, config.humanizeVelocity ?? 50);

  // Chord extensions (offset 24-31)
  view.setUint8(ptr + 24, config.chordExtSus ? 1 : 0);
  view.setUint8(ptr + 25, config.chordExt7th ? 1 : 0);
  view.setUint8(ptr + 26, config.chordExt9th ? 1 : 0);
  view.setUint8(ptr + 27, 0); // chord_ext_tritone_sub (not exposed in JS)
  view.setUint8(ptr + 28, config.chordExtSusProb ?? 20);
  view.setUint8(ptr + 29, config.chordExt7thProb ?? 30);
  view.setUint8(ptr + 30, config.chordExt9thProb ?? 25);
  view.setUint8(ptr + 31, 0); // chord_ext_tritone_sub_prob (not exposed in JS)

  // Composition style (offset 32)
  view.setUint8(ptr + 32, config.compositionStyle ?? 0);

  // Reserved + padding (offset 33)
  view.setUint8(ptr + 33, 0);

  // Duration (offset 34-35)
  view.setUint16(ptr + 34, config.targetDurationSeconds ?? 0, true);

  // Modulation settings (offset 36-37)
  view.setUint8(ptr + 36, config.modulationTiming ?? 0);
  view.setInt8(ptr + 37, config.modulationSemitones ?? 2);

  // SE/Call settings (offset 38-43)
  view.setUint8(ptr + 38, config.seEnabled !== false ? 1 : 0);
  view.setUint8(ptr + 39, config.callEnabled ? 1 : 0);
  view.setUint8(ptr + 40, config.callNotesEnabled !== false ? 1 : 0);
  view.setUint8(ptr + 41, config.introChant ?? 0);
  view.setUint8(ptr + 42, config.mixPattern ?? 0);
  view.setUint8(ptr + 43, config.callDensity ?? 2);

  // Vocal style settings (offset 44-45)
  view.setUint8(ptr + 44, config.vocalStyle ?? 0);
  view.setUint8(ptr + 45, config.melodyTemplate ?? 0);

  // Arrangement settings (offset 46)
  view.setUint8(ptr + 46, config.arrangementGrowth ?? 0);

  // Arpeggio sync settings (offset 47)
  view.setUint8(ptr + 47, config.arpeggioSyncChord !== false ? 1 : 0);

  // Motif settings (offset 48-50)
  view.setUint8(ptr + 48, config.motifRepeatScope ?? 0);
  view.setUint8(ptr + 49, config.motifFixedProgression !== false ? 1 : 0);
  view.setUint8(ptr + 50, config.motifMaxChordCount ?? 4);

  // Melodic complexity and hook control (offset 51-53)
  view.setUint8(ptr + 51, config.melodicComplexity ?? 1);
  view.setUint8(ptr + 52, config.hookIntensity ?? 2);
  view.setUint8(ptr + 53, config.vocalGroove ?? 0);

  return ptr;
}
