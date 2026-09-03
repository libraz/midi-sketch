/**
 * MidiSketch class for MIDI generation
 */

import type { SongConfigBuilder } from './builder';
import {
  serializeAccompanimentConfig,
  serializeConfig,
  serializeVocalConfig,
} from './config-fields';
import { type MidiFormatType, MidiSketchConfigError, MidiSketchGenerationError } from './constants';
import { type EmscriptenModule, getApi, getModule } from './internal'; // getModule still needed for getMidi/getEvents/PianoRoll
import type {
  AccompanimentConfig,
  CollisionInfo,
  DissonanceReport,
  EventData,
  MelodyData,
  NoteInput,
  NoteReasonFlags,
  NoteSafetyLevel,
  PianoRollInfo,
  SongConfig,
  VocalConfig,
} from './types';

/** sizeof(MidiSketchPianoRollInfo) - must match C++ struct layout */
const PIANO_ROLL_INFO_SIZE = 784;

/** sizeof(MidiSketchCollisionInfo) - three uint8_t fields, no padding */
const COLLISION_INFO_SIZE = 3;

/**
 * Largest number of ticks a single getPianoRollSafety() call may sample.
 * Mirrors the batch cap the C API enforces.
 */
export const MAX_PIANO_ROLL_SAMPLES = 100000;

// Most piano-roll slots have no collision. Reuse this immutable value instead
// of allocating 128 identical objects for every sampled tick.
const NO_COLLISION: Readonly<CollisionInfo> = Object.freeze({
  trackRole: 0,
  collidingPitch: 0,
  intervalSemitones: 0,
});

/**
 * MidiSketch instance for MIDI generation
 */
export class MidiSketch {
  private handle: number;

  constructor() {
    const a = getApi();
    this.handle = a.create();
    if (!this.handle) {
      throw new Error('Failed to create MidiSketch instance');
    }
  }

  /**
   * Handle a generation result code, throwing appropriate errors.
   * For config-backed calls, result===1 is resolved through the handle's last config error.
   */
  private handleGenerationResult(result: number, operation: string): void {
    if (result === 0) {
      return;
    }
    const a = getApi();
    if (result === 1) {
      const configError = a.getLastConfigError(this.handle);
      if (configError !== 0) {
        const message = a.configErrorString(configError);
        throw new MidiSketchConfigError(configError, message);
      }
    }
    const errorMessage = a.errorString(result);
    throw new MidiSketchGenerationError(result, `${operation} failed: ${errorMessage}`);
  }

  /**
   * Throw a generation error with a resolved error message.
   * For methods that don't take a full config JSON.
   */
  private throwGenerationError(result: number, operation: string): void {
    const a = getApi();
    const errorMessage = a.errorString(result);
    throw new MidiSketchGenerationError(result, `${operation} failed: ${errorMessage}`);
  }

  /**
   * Generate MIDI from a SongConfig
   * @throws {MidiSketchConfigError} If config validation fails
   * @throws {MidiSketchGenerationError} If generation fails for other reasons
   */
  generateFromConfig(config: SongConfig): void {
    const a = getApi();
    const json = serializeConfig(config);
    const result = a.generateFromJson(this.handle, json, json.length);
    this.handleGenerationResult(result, 'Generation');
  }

  /**
   * Generate MIDI from a SongConfigBuilder
   *
   * @param builder The SongConfigBuilder instance
   * @throws {MidiSketchConfigError} If config validation fails
   * @throws {MidiSketchGenerationError} If generation fails for other reasons
   *
   * @example
   * ```typescript
   * const builder = new SongConfigBuilder(0)
   *   .setBpm(120)
   *   .setBlueprint(1)
   *   .setSeed(12345);
   *
   * sketch.generateFromBuilder(builder);
   * ```
   */
  generateFromBuilder(builder: SongConfigBuilder): void {
    this.generateFromConfig(builder.build());
  }

  /**
   * Select the MIDI format used by subsequent generation calls.
   *
   * The WebAssembly build currently supports SMF1 only. Selecting SMF2 throws
   * MidiSketchGenerationError instead of silently producing SMF1.
   */
  setMidiFormat(format: MidiFormatType): void {
    const result = getApi().setMidiFormat(this.handle, format);
    if (result !== 0) {
      this.throwGenerationError(result, 'Set MIDI format');
    }
  }

  /** Get the selected MIDI output format. */
  getMidiFormat(): MidiFormatType {
    return getApi().getMidiFormat(this.handle) as MidiFormatType;
  }

  /**
   * Generate only the vocal track without accompaniment.
   * Use for trial-and-error workflow: generate vocal, listen, regenerate if needed.
   * Call generateAccompaniment() when satisfied with the vocal.
   * @throws {MidiSketchConfigError} If config validation fails
   * @throws {MidiSketchGenerationError} If generation fails
   */
  generateVocal(config: SongConfig): void {
    const a = getApi();
    const json = serializeConfig(config);
    const result = a.generateVocalFromJson(this.handle, json, json.length);
    this.handleGenerationResult(result, 'Vocal generation');
  }

  /**
   * Regenerate vocal track with new configuration or seed.
   * Keeps the same chord progression and structure.
   * @param configOrSeed VocalConfig object or seed number (default: 0 = new random)
   * @throws {MidiSketchGenerationError} If regeneration fails
   */
  regenerateVocal(configOrSeed: VocalConfig | number = 0): void {
    const a = getApi();

    if (typeof configOrSeed === 'number' && configOrSeed === 0) {
      const result = a.regenerateVocalFromJson(this.handle, '', 0);
      if (result !== 0) {
        this.throwGenerationError(result, 'Vocal regeneration');
      }
      return;
    }

    const vocalConfig: VocalConfig =
      typeof configOrSeed === 'number' ? { seed: configOrSeed } : configOrSeed;

    const json = serializeVocalConfig(vocalConfig);
    const result = a.regenerateVocalFromJson(this.handle, json, json.length);
    if (result !== 0) {
      this.throwGenerationError(result, 'Vocal regeneration');
    }
  }

  /**
   * Generate accompaniment tracks for existing vocal.
   * Must be called after generateVocal() or generateWithVocal().
   * Generates: Aux -> Bass -> Chord -> Drums (adapting to vocal).
   * @param config Optional accompaniment configuration
   * @throws {MidiSketchGenerationError} If generation fails
   */
  generateAccompaniment(config?: AccompanimentConfig): void {
    const a = getApi();
    if (config === undefined) {
      const result = a.generateAccompaniment(this.handle);
      if (result !== 0) {
        this.throwGenerationError(result, 'Accompaniment generation');
      }
    } else {
      const json = serializeAccompanimentConfig(config);
      const result = a.generateAccompanimentFromJson(this.handle, json, json.length);
      if (result !== 0) {
        this.throwGenerationError(result, 'Accompaniment generation');
      }
    }
  }

  /**
   * Regenerate accompaniment tracks with a new seed or configuration.
   * Keeps current vocal, regenerates all accompaniment tracks
   * (Aux, Bass, Chord, Drums, etc.) with the specified seed/config.
   * Must have existing vocal (call generateVocal() first).
   * @param seedOrConfig Random seed (0 = auto-generate) or AccompanimentConfig
   * @throws {MidiSketchGenerationError} If regeneration fails
   */
  regenerateAccompaniment(seedOrConfig: number | AccompanimentConfig = 0): void {
    const a = getApi();
    if (typeof seedOrConfig === 'number') {
      const result = a.regenerateAccompaniment(this.handle, seedOrConfig);
      if (result !== 0) {
        this.throwGenerationError(result, 'Accompaniment regeneration');
      }
    } else {
      const json = serializeAccompanimentConfig(seedOrConfig);
      const result = a.regenerateAccompanimentFromJson(this.handle, json, json.length);
      if (result !== 0) {
        this.throwGenerationError(result, 'Accompaniment regeneration');
      }
    }
  }

  /**
   * Generate all tracks with vocal-first priority.
   * Generation order: Vocal -> Aux -> Bass -> Chord -> Drums.
   * Accompaniment adapts to vocal melody.
   * @throws {MidiSketchConfigError} If config validation fails
   * @throws {MidiSketchGenerationError} If generation fails
   */
  generateWithVocal(config: SongConfig): void {
    const a = getApi();
    const json = serializeConfig(config);
    const result = a.generateWithVocalFromJson(this.handle, json, json.length);
    this.handleGenerationResult(result, 'Generation');
  }

  /**
   * Get the current vocal melody for saving or comparing candidates.
   *
   * The returned value can be restored later with setMelody().
   */
  getMelody(): MelodyData {
    const json = getApi().getMelodyJson(this.handle);
    // The C API reports an unusable handle as a null string. Distinguish that
    // from a payload that came back but failed to parse.
    if (!json) {
      throw new Error('No melody data available');
    }
    let melody: {
      seed: number;
      notes: Array<{
        start_tick: number;
        duration: number;
        pitch: number;
        velocity: number;
      }>;
    };
    try {
      melody = JSON.parse(json);
    } catch (error) {
      throw new Error(`Malformed melody data: ${(error as Error).message}`);
    }
    return {
      seed: melody.seed,
      notes: melody.notes.map((note) => ({
        startTick: note.start_tick,
        duration: note.duration,
        pitch: note.pitch,
        velocity: note.velocity,
      })),
    };
  }

  /**
   * Restore a vocal melody previously returned by getMelody().
   */
  setMelody(melody: MelodyData): void {
    const json = JSON.stringify({
      seed: melody.seed,
      notes: melody.notes.map((note) => ({
        start_tick: note.startTick,
        duration: note.duration,
        pitch: note.pitch,
        velocity: note.velocity,
      })),
    });
    const result = getApi().setMelodyFromJson(this.handle, json, json.length);
    this.handleGenerationResult(result, 'Set melody');
  }

  /**
   * Set custom vocal notes for accompaniment generation.
   *
   * Initializes the song structure and chord progression from config,
   * then replaces the vocal track with the provided notes.
   * Call generateAccompaniment() after this to generate
   * accompaniment tracks that fit the custom vocal melody.
   *
   * @param config Song configuration (for structure/chord setup)
   * @param notes Array of note inputs representing the custom vocal
   * @throws {MidiSketchConfigError} If config validation fails
   * @throws {MidiSketchGenerationError} If operation fails
   *
   * @example
   * ```typescript
   * // Set custom vocal notes
   * sketch.setVocalNotes(config, [
   *   { startTick: 0, duration: 480, pitch: 60, velocity: 100 },
   *   { startTick: 480, duration: 480, pitch: 62, velocity: 100 },
   * ]);
   *
   * // Generate accompaniment for the custom vocal
   * sketch.generateAccompaniment();
   *
   * // Get the MIDI data
   * const midi = sketch.getMidi();
   * ```
   */
  setVocalNotes(config: SongConfig, notes: NoteInput[]): void {
    const a = getApi();

    // Build combined JSON with config and notes without double-parsing
    const configJson = serializeConfig(config);
    const notesArray = notes.map((note) => ({
      start_tick: note.startTick,
      duration: note.duration,
      pitch: note.pitch,
      velocity: note.velocity,
    }));
    const combined = `{"config":${configJson},"notes":${JSON.stringify(notesArray)}}`;

    const result = a.setVocalNotesFromJson(this.handle, combined, combined.length);
    this.handleGenerationResult(result, 'Set vocal notes');
  }

  private readMidi(getMidiData: () => number, unavailableMessage: string): Uint8Array {
    const a = getApi();
    const m = getModule();
    const midiDataPtr = getMidiData();
    if (!midiDataPtr) {
      throw new Error(unavailableMessage);
    }

    try {
      const dataPtr = m.HEAPU32[midiDataPtr >> 2];
      const size = m.HEAPU32[(midiDataPtr + 4) >> 2];

      const result = new Uint8Array(size);
      result.set(m.HEAPU8.subarray(dataPtr, dataPtr + size));
      return result;
    } finally {
      a.freeMidi(midiDataPtr);
    }
  }

  /** Get the generated MIDI data. */
  getMidi(): Uint8Array {
    const a = getApi();
    return this.readMidi(() => a.getMidi(this.handle), 'No MIDI data available');
  }

  /**
   * Get a compact vocal-practice preview containing the vocal melody and chord-root bass.
   * Generate a vocal or full song before calling this method.
   */
  getVocalPreviewMidi(): Uint8Array {
    const a = getApi();
    return this.readMidi(
      () => a.getVocalPreviewMidi(this.handle),
      'No vocal preview MIDI data available',
    );
  }

  /**
   * Get the event data as a parsed object
   */
  getEvents(): EventData {
    const a = getApi();
    const m = getModule();
    const eventDataPtr = a.getEvents(this.handle);
    if (!eventDataPtr) {
      throw new Error('No event data available');
    }

    try {
      const jsonPtr = m.HEAPU32[eventDataPtr >> 2];
      const json = m.UTF8ToString(jsonPtr);
      return JSON.parse(json) as EventData;
    } finally {
      a.freeEvents(eventDataPtr);
    }
  }

  /**
   * Analyze the generated song for harmonic dissonance.
   */
  getDissonanceReport(): DissonanceReport {
    const a = getApi();
    const m = getModule();
    const reportPtr = a.getDissonance(this.handle);
    if (!reportPtr) {
      throw new Error('No dissonance report available');
    }

    try {
      const jsonPtr = m.HEAPU32[reportPtr >> 2];
      return JSON.parse(m.UTF8ToString(jsonPtr)) as DissonanceReport;
    } finally {
      a.freeDissonance(reportPtr);
    }
  }

  // ============================================================================
  // Piano Roll Safety API
  // ============================================================================

  /**
   * Get piano roll safety info for a single tick.
   *
   * Returns safety level, reason flags, and collision info for each MIDI note (0-127).
   * Use this before placing custom vocal notes to see which notes are safe.
   *
   * @param tick Tick position to query
   * @param prevPitch Previous note pitch for leap detection (optional, 255 if none)
   * @returns Piano roll safety info for all 128 MIDI notes
   *
   * @example
   * ```typescript
   * // Get safety info at tick 0
   * const info = sketch.getPianoRollSafetyAt(0);
   *
   * // Check if C4 (pitch 60) is safe
   * if (info.safety[60] === NoteSafety.Safe) {
   *   console.log('C4 is a chord tone, safe to use');
   * }
   *
   * // Get recommended notes
   * console.log('Recommended:', info.recommended);
   * ```
   */
  getPianoRollSafetyAt(tick: number, prevPitch?: number): PianoRollInfo {
    const a = getApi();
    const m = getModule();

    const infoPtr =
      prevPitch !== undefined
        ? a.getPianoRollSafetyWithContext(this.handle, tick, prevPitch)
        : a.getPianoRollSafetyAt(this.handle, tick);

    if (!infoPtr) {
      throw new Error('Failed to get piano roll safety info. Generate MIDI first.');
    }

    return this.parsePianoRollInfo(m, infoPtr);
  }

  /**
   * Get piano roll safety info for a range of ticks.
   *
   * Useful for visualizing safe notes over time in a piano roll editor.
   *
   * At most {@link MAX_PIANO_ROLL_SAMPLES} samples may be requested. The limit is
   * checked against the requested range before any work happens, so an oversized
   * request costs nothing.
   *
   * @param startTick Start tick
   * @param endTick End tick (must be >= startTick)
   * @param step Step size in ticks (e.g., 120 for 16th notes, 480 for quarter notes)
   * @returns Array of piano roll safety info for each step
   * @throws {RangeError} If step is not positive, the range is inverted, or the
   *   request would exceed the sample limit
   *
   * @example
   * ```typescript
   * // Get safety info for first 4 bars, sampled at 16th note resolution
   * const infos = sketch.getPianoRollSafety(0, 1920 * 4, 120);
   *
   * for (const info of infos) {
   *   console.log(`Tick ${info.tick}: chord degree ${info.chordDegree}`);
   *   console.log('Recommended notes:', info.recommended);
   * }
   * ```
   */
  getPianoRollSafety(startTick: number, endTick: number, step: number): PianoRollInfo[] {
    const a = getApi();
    const m = getModule();

    if (!Number.isInteger(step) || step <= 0) {
      throw new RangeError(`Piano roll safety step must be a positive integer, got ${step}`);
    }
    if (endTick < startTick) {
      throw new RangeError(
        `Piano roll safety range is inverted: startTick ${startTick} is after endTick ${endTick}`,
      );
    }
    // Reject before the call rather than after: the C API would otherwise fill
    // and allocate a full capped batch that this wrapper cannot return.
    const requestedSamples = Math.floor((endTick - startTick) / step) + 1;
    if (requestedSamples > MAX_PIANO_ROLL_SAMPLES) {
      throw new RangeError(
        `Piano roll safety requests are limited to ${MAX_PIANO_ROLL_SAMPLES} samples, ` +
          `got ${requestedSamples}; increase the step size or narrow the range.`,
      );
    }

    const dataPtr = a.getPianoRollSafety(this.handle, startTick, endTick, step);
    if (!dataPtr) {
      throw new Error('Failed to get piano roll safety data. Generate MIDI first.');
    }

    try {
      // MidiSketchPianoRollData starts with the data pointer. Count and the
      // truncation state are read through C helpers so this remains correct
      // on both WASM32 and native-size_t builds.
      const infoArrayPtr = m.HEAPU32[dataPtr >> 2];
      const count = a.getPianoRollDataCount(dataPtr);
      if (a.pianoRollDataWasTruncated(dataPtr) !== 0) {
        // Unreachable while the pre-call check holds: the core clamps endTick to
        // the song length, so it can only sample fewer ticks than requested.
        throw new RangeError(
          `Piano roll safety data was truncated to ${MAX_PIANO_ROLL_SAMPLES} samples.`,
        );
      }

      const results: PianoRollInfo[] = [];

      for (let idx = 0; idx < count; idx++) {
        const infoPtr = infoArrayPtr + idx * PIANO_ROLL_INFO_SIZE;
        results.push(this.parsePianoRollInfo(m, infoPtr));
      }

      return results;
    } finally {
      a.freePianoRollData(dataPtr);
    }
  }

  /**
   * Convert reason flags to human-readable string.
   *
   * @param reason Reason flags from PianoRollInfo
   * @returns Human-readable string like "ChordTone" or "LowRegister, Tritone"
   */
  reasonToString(reason: NoteReasonFlags): string {
    const a = getApi();
    return a.reasonToString(reason);
  }

  /**
   * Convert collision info to human-readable string.
   *
   * @param collision Collision entry from PianoRollInfo.collision
   * @returns Human-readable string like "Bass F3 minor 2nd", or an empty
   *   string when the entry records no collision
   */
  collisionToString(collision: CollisionInfo): string {
    const a = getApi();
    const m = getModule();

    const ptr = m._malloc(COLLISION_INFO_SIZE);
    if (!ptr) {
      throw new Error('Failed to allocate memory for collision info');
    }
    try {
      m.HEAPU8[ptr] = collision.trackRole;
      m.HEAPU8[ptr + 1] = collision.collidingPitch;
      m.HEAPU8[ptr + 2] = collision.intervalSemitones;
      return a.collisionToString(ptr);
    } finally {
      m._free(ptr);
    }
  }

  /**
   * Parse MidiSketchPianoRollInfo from WASM memory.
   * @internal
   */
  private parsePianoRollInfo(m: EmscriptenModule, ptr: number): PianoRollInfo {
    const view = new DataView(m.HEAPU8.buffer);

    // Offsets from struct_layout_test.cpp:
    // tick: 0, chord_degree: 4, current_key: 5, safety: 6, reason: 134,
    // collision: 390, recommended: 774, recommended_count: 782
    const tick = view.getUint32(ptr + 0, true);
    const chordDegree = view.getInt8(ptr + 4);
    const currentKey = view.getUint8(ptr + 5);

    // Parse safety array (128 bytes at offset 6)
    const safety: NoteSafetyLevel[] = [];
    for (let idx = 0; idx < 128; idx++) {
      safety.push(view.getUint8(ptr + 6 + idx) as NoteSafetyLevel);
    }

    // Parse reason array (128 * 2 bytes at offset 134)
    const reason: NoteReasonFlags[] = [];
    for (let idx = 0; idx < 128; idx++) {
      reason.push(view.getUint16(ptr + 134 + idx * 2, true));
    }

    // Parse collision array (128 * 3 bytes at offset 390)
    const collision: CollisionInfo[] = Array<CollisionInfo>(128).fill(NO_COLLISION);
    for (let idx = 0; idx < 128; idx++) {
      const collisionOffset = ptr + 390 + idx * 3;
      const intervalSemitones = view.getUint8(collisionOffset + 2);
      if (intervalSemitones !== 0) {
        collision[idx] = {
          trackRole: view.getUint8(collisionOffset),
          collidingPitch: view.getUint8(collisionOffset + 1),
          intervalSemitones,
        };
      }
    }

    // Parse recommended array (up to 8 bytes at offset 774)
    const recommendedCount = view.getUint8(ptr + 782);
    const recommended: number[] = [];
    for (let idx = 0; idx < recommendedCount && idx < 8; idx++) {
      recommended.push(view.getUint8(ptr + 774 + idx));
    }

    return {
      tick,
      chordDegree,
      currentKey,
      safety,
      reason,
      collision,
      recommended,
    };
  }

  /**
   * Get the resolved blueprint ID after generation.
   *
   * Returns the actual blueprint ID used for generation.
   * If blueprintId was set to 255 (random), this returns the selected ID.
   *
   * @returns Resolved blueprint ID (0-9), or 255 if not generated
   */
  getResolvedBlueprintId(): number {
    const a = getApi();
    return a.getResolvedBlueprintId(this.handle);
  }

  /** Get non-fatal warnings produced by the latest generation operation. */
  getWarnings(): string[] {
    const a = getApi();
    return JSON.parse(a.getWarningsJson(this.handle)) as string[];
  }

  /**
   * Destroy the instance and free resources
   */
  destroy(): void {
    if (this.handle) {
      const a = getApi();
      a.destroy(this.handle);
      this.handle = 0;
    }
  }
}

export default MidiSketch;
