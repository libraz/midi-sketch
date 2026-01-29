/**
 * MidiSketch class for MIDI generation
 */

import type { SongConfigBuilder } from './builder';
import { allocSongConfig } from './config';
import { MidiSketchConfigError, MidiSketchGenerationError } from './constants';
import { type EmscriptenModule, getApi, getModule } from './internal';
import type {
  AccompanimentConfig,
  CollisionInfo,
  EventData,
  NoteInput,
  NoteReasonFlags,
  NoteSafetyLevel,
  PianoRollInfo,
  SongConfig,
  VocalConfig,
} from './types';

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
   * Generate MIDI from a SongConfig
   * @throws {MidiSketchConfigError} If config validation fails
   * @throws {MidiSketchGenerationError} If generation fails for other reasons
   */
  generateFromConfig(config: SongConfig): void {
    const a = getApi();
    const m = getModule();
    const configPtr = allocSongConfig(m, config);
    try {
      const result = a.generateFromConfig(this.handle, configPtr);
      if (result !== 0) {
        // Error code 1 = INVALID_PARAM, which includes config validation errors
        // Try to get detailed error message
        const errorMessage = a.configErrorString(result);
        if (result === 1) {
          // Config validation error - get more specific error from validation
          const validationResult = a.validateConfig(configPtr);
          if (validationResult !== 0) {
            const validationMessage = a.configErrorString(validationResult);
            throw new MidiSketchConfigError(validationResult, validationMessage);
          }
        }
        throw new MidiSketchGenerationError(result, `Generation failed: ${errorMessage}`);
      }
    } finally {
      m._free(configPtr);
    }
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
   * Generate only the vocal track without accompaniment.
   * Use for trial-and-error workflow: generate vocal, listen, regenerate if needed.
   * Call generateAccompaniment() when satisfied with the vocal.
   * @throws {MidiSketchConfigError} If config validation fails
   * @throws {MidiSketchGenerationError} If generation fails
   */
  generateVocal(config: SongConfig): void {
    const a = getApi();
    const m = getModule();
    const configPtr = allocSongConfig(m, config);
    try {
      const result = a.generateVocal(this.handle, configPtr);
      if (result !== 0) {
        const validationResult = a.validateConfig(configPtr);
        if (validationResult !== 0) {
          const validationMessage = a.configErrorString(validationResult);
          throw new MidiSketchConfigError(validationResult, validationMessage);
        }
        throw new MidiSketchGenerationError(
          result,
          `Vocal generation failed with error code: ${result}`,
        );
      }
    } finally {
      m._free(configPtr);
    }
  }

  /**
   * Regenerate vocal track with new configuration or seed.
   * Keeps the same chord progression and structure.
   * @param configOrSeed VocalConfig object or seed number (default: 0 = new random)
   * @throws {MidiSketchGenerationError} If regeneration fails
   */
  regenerateVocal(configOrSeed: VocalConfig | number = 0): void {
    const a = getApi();
    const m = getModule();

    let configPtr = 0;
    if (typeof configOrSeed === 'number') {
      // Seed only - create minimal config with just seed
      configPtr = this.allocVocalConfig(m, { seed: configOrSeed });
    } else {
      // Full config
      configPtr = this.allocVocalConfig(m, configOrSeed);
    }

    try {
      const result = a.regenerateVocal(this.handle, configPtr);
      if (result !== 0) {
        throw new MidiSketchGenerationError(
          result,
          `Vocal regeneration failed with error code: ${result}`,
        );
      }
    } finally {
      m._free(configPtr);
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
        throw new MidiSketchGenerationError(
          result,
          `Accompaniment generation failed with error code: ${result}`,
        );
      }
    } else {
      const m = getModule();
      const configPtr = this.allocAccompanimentConfig(m, config);
      try {
        const result = a.generateAccompanimentWithConfig(this.handle, configPtr);
        if (result !== 0) {
          throw new MidiSketchGenerationError(
            result,
            `Accompaniment generation failed with error code: ${result}`,
          );
        }
      } finally {
        m._free(configPtr);
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
        throw new MidiSketchGenerationError(
          result,
          `Accompaniment regeneration failed with error code: ${result}`,
        );
      }
    } else {
      const m = getModule();
      const configPtr = this.allocAccompanimentConfig(m, seedOrConfig);
      try {
        const result = a.regenerateAccompanimentWithConfig(this.handle, configPtr);
        if (result !== 0) {
          throw new MidiSketchGenerationError(
            result,
            `Accompaniment regeneration failed with error code: ${result}`,
          );
        }
      } finally {
        m._free(configPtr);
      }
    }
  }

  /**
   * Allocate and populate AccompanimentConfig in WASM memory.
   */
  private allocAccompanimentConfig(m: EmscriptenModule, config: AccompanimentConfig): number {
    // MidiSketchAccompanimentConfig struct size: 28 bytes
    const configPtr = m._malloc(28);
    const view = new DataView(m.HEAPU8.buffer, configPtr, 28);

    view.setUint32(0, config.seed ?? 0, true); // seed

    view.setUint8(4, config.drumsEnabled !== false ? 1 : 0); // drums_enabled

    view.setUint8(5, config.arpeggioEnabled ? 1 : 0); // arpeggio_enabled
    view.setUint8(6, config.arpeggioPattern ?? 0); // arpeggio_pattern
    view.setUint8(7, config.arpeggioSpeed ?? 1); // arpeggio_speed
    view.setUint8(8, config.arpeggioOctaveRange ?? 2); // arpeggio_octave_range
    view.setUint8(9, config.arpeggioGate ?? 80); // arpeggio_gate
    view.setUint8(10, config.arpeggioSyncChord !== false ? 1 : 0); // arpeggio_sync_chord

    view.setUint8(11, config.chordExtSus ? 1 : 0); // chord_ext_sus
    view.setUint8(12, config.chordExt7th ? 1 : 0); // chord_ext_7th
    view.setUint8(13, config.chordExt9th ? 1 : 0); // chord_ext_9th
    view.setUint8(14, config.chordExtSusProb ?? 20); // chord_ext_sus_prob
    view.setUint8(15, config.chordExt7thProb ?? 30); // chord_ext_7th_prob
    view.setUint8(16, config.chordExt9thProb ?? 25); // chord_ext_9th_prob

    view.setUint8(17, config.humanize ? 1 : 0); // humanize
    view.setUint8(18, config.humanizeTiming ?? 50); // humanize_timing
    view.setUint8(19, config.humanizeVelocity ?? 50); // humanize_velocity

    view.setUint8(20, config.seEnabled !== false ? 1 : 0); // se_enabled

    view.setUint8(21, config.callEnabled ? 1 : 0); // call_enabled
    view.setUint8(22, config.callDensity ?? 2); // call_density
    view.setUint8(23, config.introChant ?? 0); // intro_chant
    view.setUint8(24, config.mixPattern ?? 0); // mix_pattern
    view.setUint8(25, config.callNotesEnabled !== false ? 1 : 0); // call_notes_enabled

    // Reserved padding
    view.setUint8(26, 0);
    view.setUint8(27, 0);

    return configPtr;
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
    const m = getModule();
    const configPtr = allocSongConfig(m, config);
    try {
      const result = a.generateWithVocal(this.handle, configPtr);
      if (result !== 0) {
        const validationResult = a.validateConfig(configPtr);
        if (validationResult !== 0) {
          const validationMessage = a.configErrorString(validationResult);
          throw new MidiSketchConfigError(validationResult, validationMessage);
        }
        throw new MidiSketchGenerationError(result, `Generation failed with error code: ${result}`);
      }
    } finally {
      m._free(configPtr);
    }
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
    const m = getModule();
    const configPtr = allocSongConfig(m, config);
    const notesPtr = this.allocNoteInputArray(m, notes);

    try {
      const result = a.setVocalNotes(this.handle, configPtr, notesPtr, notes.length);
      if (result !== 0) {
        const validationResult = a.validateConfig(configPtr);
        if (validationResult !== 0) {
          const validationMessage = a.configErrorString(validationResult);
          throw new MidiSketchConfigError(validationResult, validationMessage);
        }
        throw new MidiSketchGenerationError(
          result,
          `Set vocal notes failed with error code: ${result}`,
        );
      }
    } finally {
      m._free(configPtr);
      m._free(notesPtr);
    }
  }

  /**
   * Allocate and populate NoteInput array in WASM memory.
   */
  private allocNoteInputArray(m: EmscriptenModule, notes: NoteInput[]): number {
    // MidiSketchNoteInput struct size: 12 bytes (uint32 + uint32 + uint8 + uint8 + 2 padding)
    const structSize = 12;
    const ptr = m._malloc(notes.length * structSize);
    const view = new DataView(m.HEAPU8.buffer);

    for (let i = 0; i < notes.length; i++) {
      const offset = ptr + i * structSize;
      view.setUint32(offset + 0, notes[i].startTick, true); // start_tick
      view.setUint32(offset + 4, notes[i].duration, true); // duration
      view.setUint8(offset + 8, notes[i].pitch); // pitch
      view.setUint8(offset + 9, notes[i].velocity); // velocity
      // 2 bytes padding (10-11)
    }

    return ptr;
  }

  /**
   * Get the generated MIDI data
   */
  getMidi(): Uint8Array {
    const a = getApi();
    const m = getModule();
    const midiDataPtr = a.getMidi(this.handle);
    if (!midiDataPtr) {
      throw new Error('No MIDI data available');
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
   * @param startTick Start tick
   * @param endTick End tick
   * @param step Step size in ticks (e.g., 120 for 16th notes, 480 for quarter notes)
   * @returns Array of piano roll safety info for each step
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

    const dataPtr = a.getPianoRollSafety(this.handle, startTick, endTick, step);
    if (!dataPtr) {
      throw new Error('Failed to get piano roll safety data. Generate MIDI first.');
    }

    try {
      // MidiSketchPianoRollData: { data: ptr, count: size_t }
      const infoArrayPtr = m.HEAPU32[dataPtr >> 2];
      const count = m.HEAPU32[(dataPtr + 4) >> 2];

      const results: PianoRollInfo[] = [];
      const infoSize = 784; // sizeof(MidiSketchPianoRollInfo)

      for (let i = 0; i < count; i++) {
        const infoPtr = infoArrayPtr + i * infoSize;
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
    for (let i = 0; i < 128; i++) {
      safety.push(view.getUint8(ptr + 6 + i) as NoteSafetyLevel);
    }

    // Parse reason array (128 * 2 bytes at offset 134)
    const reason: NoteReasonFlags[] = [];
    for (let i = 0; i < 128; i++) {
      reason.push(view.getUint16(ptr + 134 + i * 2, true));
    }

    // Parse collision array (128 * 3 bytes at offset 390)
    const collision: CollisionInfo[] = [];
    for (let i = 0; i < 128; i++) {
      const collisionOffset = ptr + 390 + i * 3;
      collision.push({
        trackRole: view.getUint8(collisionOffset),
        collidingPitch: view.getUint8(collisionOffset + 1),
        intervalSemitones: view.getUint8(collisionOffset + 2),
      });
    }

    // Parse recommended array (up to 8 bytes at offset 774)
    const recommendedCount = view.getUint8(ptr + 782);
    const recommended: number[] = [];
    for (let i = 0; i < recommendedCount && i < 8; i++) {
      recommended.push(view.getUint8(ptr + 774 + i));
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
   * @returns Resolved blueprint ID (0-3), or 255 if not generated
   */
  getResolvedBlueprintId(): number {
    const a = getApi();
    return a.getResolvedBlueprintId(this.handle);
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

  private allocVocalConfig(m: EmscriptenModule, config: VocalConfig): number {
    const ptr = m._malloc(16); // MidiSketchVocalConfig size (16 bytes with padding)
    const view = new DataView(m.HEAPU8.buffer);

    // Layout: seed(4) + vocal_low(1) + vocal_high(1) + vocal_attitude(1)
    //         + vocal_style(1) + melody_template(1) + melodic_complexity(1)
    //         + hook_intensity(1) + vocal_groove(1) + composition_style(1)
    //         + reserved(2) = 16 bytes
    view.setUint32(ptr + 0, config.seed ?? 0, true);
    view.setUint8(ptr + 4, config.vocalLow ?? 60);
    view.setUint8(ptr + 5, config.vocalHigh ?? 79);
    view.setUint8(ptr + 6, config.vocalAttitude ?? 0);
    view.setUint8(ptr + 7, config.vocalStyle ?? 0);
    view.setUint8(ptr + 8, config.melodyTemplate ?? 0);
    view.setUint8(ptr + 9, config.melodicComplexity ?? 1); // Default: Standard
    view.setUint8(ptr + 10, config.hookIntensity ?? 2); // Default: Normal
    view.setUint8(ptr + 11, config.vocalGroove ?? 0); // Default: Straight
    view.setUint8(ptr + 12, config.compositionStyle ?? 0);
    view.setUint8(ptr + 13, 0); // Reserved
    view.setUint8(ptr + 14, 0); // Reserved
    view.setUint8(ptr + 15, 0); // Padding (explicit for clarity)

    return ptr;
  }
}

export default MidiSketch;
