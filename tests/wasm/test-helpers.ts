import createModule from '../../dist/midisketch.js';

export interface WasmModule {
  cwrap: (
    name: string,
    returnType: string | null,
    argTypes: string[],
  ) => (...args: unknown[]) => unknown;
  UTF8ToString: (ptr: number) => string;
  _malloc: (size: number) => number;
  _free: (ptr: number) => void;
  HEAPU8: Uint8Array;
  HEAPU32: Uint32Array;
}

export interface SongConfigOptions {
  stylePresetId?: number;
  key?: number;
  bpm?: number;
  seed?: number;
  chordProgressionId?: number;
  formId?: number;
  vocalAttitude?: number;
  drumsEnabled?: boolean;
  arpeggioEnabled?: boolean;
  arpeggioPattern?: number;
  arpeggioSpeed?: number;
  arpeggioOctaveRange?: number;
  arpeggioGate?: number;
  vocalLow?: number;
  vocalHigh?: number;
  skipVocal?: boolean;
  humanize?: boolean;
  humanizeTiming?: number;
  humanizeVelocity?: number;
  chordExtSus?: boolean;
  chordExt7th?: boolean;
  chordExt9th?: boolean;
  chordExtSusProb?: number;
  chordExt7thProb?: number;
  chordExt9thProb?: number;
  compositionStyle?: number;
  targetDurationSeconds?: number;
  modulationTiming?: number;
  modulationSemitones?: number;
  seEnabled?: boolean;
  callEnabled?: boolean;
  callNotesEnabled?: boolean;
  introChant?: number;
  mixPattern?: number;
  callDensity?: number;
  // Vocal style settings
  vocalStyle?: number;
  melodyTemplate?: number;
  // Arrangement settings
  arrangementGrowth?: number;
  // Arpeggio sync settings
  arpeggioSyncChord?: boolean;
  // Motif settings
  motifRepeatScope?: number;
  motifFixedProgression?: boolean;
  motifMaxChordCount?: number;
  // Melodic complexity and hook control
  melodicComplexity?: number;
  hookIntensity?: number;
  vocalGroove?: number;
}

export class WasmTestContext {
  module!: WasmModule;
  handle!: number;

  async init(): Promise<void> {
    this.module = (await createModule()) as WasmModule;
    const create = this.module.cwrap('midisketch_create', 'number', []) as () => number;
    this.handle = create();
  }

  destroy(): void {
    if (this.handle && this.module) {
      const destroyFn = this.module.cwrap('midisketch_destroy', null, ['number']) as (
        h: number,
      ) => void;
      destroyFn(this.handle);
    }
  }

  allocSongConfig(config: SongConfigOptions): number {
    const ptr = this.module._malloc(52); // MidiSketchSongConfig size
    const view = new DataView(this.module.HEAPU8.buffer);

    // Basic settings
    view.setUint8(ptr + 0, config.stylePresetId ?? 0);
    view.setUint8(ptr + 1, config.key ?? 0);
    view.setUint16(ptr + 2, config.bpm ?? 0, true);
    view.setUint32(ptr + 4, config.seed ?? 0, true);
    view.setUint8(ptr + 8, config.chordProgressionId ?? 0);
    view.setUint8(ptr + 9, config.formId ?? 0);
    view.setUint8(ptr + 10, config.vocalAttitude ?? 0);
    view.setUint8(ptr + 11, config.drumsEnabled !== false ? 1 : 0);

    // Arpeggio settings
    view.setUint8(ptr + 12, config.arpeggioEnabled ? 1 : 0);
    view.setUint8(ptr + 13, config.arpeggioPattern ?? 0);
    view.setUint8(ptr + 14, config.arpeggioSpeed ?? 1);
    view.setUint8(ptr + 15, config.arpeggioOctaveRange ?? 2);
    view.setUint8(ptr + 16, config.arpeggioGate ?? 80);

    // Vocal settings
    view.setUint8(ptr + 17, config.vocalLow ?? 60);
    view.setUint8(ptr + 18, config.vocalHigh ?? 79);
    view.setUint8(ptr + 19, config.skipVocal ? 1 : 0);

    // Humanization
    view.setUint8(ptr + 20, config.humanize ? 1 : 0);
    view.setUint8(ptr + 21, config.humanizeTiming ?? 50);
    view.setUint8(ptr + 22, config.humanizeVelocity ?? 50);

    // Chord extensions
    view.setUint8(ptr + 23, config.chordExtSus ? 1 : 0);
    view.setUint8(ptr + 24, config.chordExt7th ? 1 : 0);
    view.setUint8(ptr + 25, config.chordExt9th ? 1 : 0);
    view.setUint8(ptr + 26, config.chordExtSusProb ?? 20);
    view.setUint8(ptr + 27, config.chordExt7thProb ?? 30);
    view.setUint8(ptr + 28, config.chordExt9thProb ?? 25);

    // Composition style
    view.setUint8(ptr + 29, config.compositionStyle ?? 0);

    // Reserved + padding
    view.setUint8(ptr + 30, 0);
    view.setUint8(ptr + 31, 0);

    // Duration
    view.setUint16(ptr + 32, config.targetDurationSeconds ?? 0, true);

    // Modulation settings
    view.setUint8(ptr + 34, config.modulationTiming ?? 0);
    view.setInt8(ptr + 35, config.modulationSemitones ?? 2);

    // Call settings
    view.setUint8(ptr + 36, config.seEnabled !== false ? 1 : 0);
    view.setUint8(ptr + 37, config.callEnabled ? 1 : 0);
    view.setUint8(ptr + 38, config.callNotesEnabled !== false ? 1 : 0);
    view.setUint8(ptr + 39, config.introChant ?? 0);
    view.setUint8(ptr + 40, config.mixPattern ?? 0);
    view.setUint8(ptr + 41, config.callDensity ?? 2);

    // Vocal style settings (offset 42-43)
    view.setUint8(ptr + 42, config.vocalStyle ?? 0);
    view.setUint8(ptr + 43, config.melodyTemplate ?? 0);

    // Arrangement settings (offset 44)
    view.setUint8(ptr + 44, config.arrangementGrowth ?? 0);

    // Arpeggio sync settings (offset 45)
    view.setUint8(ptr + 45, config.arpeggioSyncChord !== false ? 1 : 0);

    // Motif settings (offset 46-48)
    view.setUint8(ptr + 46, config.motifRepeatScope ?? 0);
    view.setUint8(ptr + 47, config.motifFixedProgression !== false ? 1 : 0);
    view.setUint8(ptr + 48, config.motifMaxChordCount ?? 4);

    // Melodic complexity and hook control (offset 49-51)
    view.setUint8(ptr + 49, config.melodicComplexity ?? 1);
    view.setUint8(ptr + 50, config.hookIntensity ?? 2);
    view.setUint8(ptr + 51, config.vocalGroove ?? 0);

    return ptr;
  }

  generateFromConfig(config: SongConfigOptions): number {
    const generateFn = this.module.cwrap('midisketch_generate_from_config', 'number', [
      'number',
      'number',
    ]) as (h: number, configPtr: number) => number;

    const configPtr = this.allocSongConfig(config);
    const result = generateFn(this.handle, configPtr);
    this.module._free(configPtr);
    return result;
  }

  generateVocal(config: SongConfigOptions): number {
    const generateFn = this.module.cwrap('midisketch_generate_vocal', 'number', [
      'number',
      'number',
    ]) as (h: number, configPtr: number) => number;

    const configPtr = this.allocSongConfig(config);
    const result = generateFn(this.handle, configPtr);
    this.module._free(configPtr);
    return result;
  }

  regenerateVocal(newSeed: number): number {
    const regenerateFn = this.module.cwrap('midisketch_regenerate_vocal', 'number', [
      'number',
      'number',
    ]) as (h: number, seed: number) => number;

    return regenerateFn(this.handle, newSeed);
  }

  generateAccompaniment(): number {
    const generateFn = this.module.cwrap('midisketch_generate_accompaniment', 'number', [
      'number',
    ]) as (h: number) => number;

    return generateFn(this.handle);
  }

  generateWithVocal(config: SongConfigOptions): number {
    const generateFn = this.module.cwrap('midisketch_generate_with_vocal', 'number', [
      'number',
      'number',
    ]) as (h: number, configPtr: number) => number;

    const configPtr = this.allocSongConfig(config);
    const result = generateFn(this.handle, configPtr);
    this.module._free(configPtr);
    return result;
  }

  setVocalNotes(
    config: SongConfigOptions,
    notes: { startTick: number; duration: number; pitch: number; velocity: number }[],
  ): number {
    const setNotesFn = this.module.cwrap('midisketch_set_vocal_notes', 'number', [
      'number',
      'number',
      'number',
      'number',
    ]) as (h: number, configPtr: number, notesPtr: number, count: number) => number;

    const configPtr = this.allocSongConfig(config);
    const notesPtr = this.allocNoteInputArray(notes);

    const result = setNotesFn(this.handle, configPtr, notesPtr, notes.length);

    this.module._free(configPtr);
    this.module._free(notesPtr);
    return result;
  }

  private allocNoteInputArray(
    notes: { startTick: number; duration: number; pitch: number; velocity: number }[],
  ): number {
    // MidiSketchNoteInput struct size: 12 bytes (uint32 + uint32 + uint8 + uint8 + 2 padding)
    const structSize = 12;
    const ptr = this.module._malloc(notes.length * structSize);
    const view = new DataView(this.module.HEAPU8.buffer);

    for (let i = 0; i < notes.length; i++) {
      const offset = ptr + i * structSize;
      view.setUint32(offset + 0, notes[i].startTick, true);
      view.setUint32(offset + 4, notes[i].duration, true);
      view.setUint8(offset + 8, notes[i].pitch);
      view.setUint8(offset + 9, notes[i].velocity);
      // 2 bytes padding (10-11)
    }

    return ptr;
  }

  getEventsJson(): { data: unknown; cleanup: () => void } {
    const getEvents = this.module.cwrap('midisketch_get_events', 'number', ['number']) as (
      h: number,
    ) => number;
    const freeEvents = this.module.cwrap('midisketch_free_events', null, ['number']) as (
      ptr: number,
    ) => void;

    const eventDataPtr = getEvents(this.handle);
    const jsonPtr = this.module.HEAPU32[eventDataPtr >> 2];
    const json = this.module.UTF8ToString(jsonPtr);
    const data = JSON.parse(json);

    return {
      data,
      cleanup: () => freeEvents(eventDataPtr),
    };
  }

  // Piano Roll Safety API

  getPianoRollSafetyAt(tick: number): PianoRollInfo {
    const getSafetyAt = this.module.cwrap('midisketch_get_piano_roll_safety_at', 'number', [
      'number',
      'number',
    ]) as (h: number, tick: number) => number;

    const infoPtr = getSafetyAt(this.handle, tick);
    if (!infoPtr) {
      throw new Error('Failed to get piano roll safety info');
    }

    return this.parsePianoRollInfo(infoPtr);
  }

  getPianoRollSafetyWithContext(tick: number, prevPitch: number): PianoRollInfo {
    const getSafetyWithContext = this.module.cwrap(
      'midisketch_get_piano_roll_safety_with_context',
      'number',
      ['number', 'number', 'number'],
    ) as (h: number, tick: number, prevPitch: number) => number;

    const infoPtr = getSafetyWithContext(this.handle, tick, prevPitch);
    if (!infoPtr) {
      throw new Error('Failed to get piano roll safety info');
    }

    return this.parsePianoRollInfo(infoPtr);
  }

  getPianoRollSafety(startTick: number, endTick: number, step: number): PianoRollInfo[] {
    const getSafety = this.module.cwrap('midisketch_get_piano_roll_safety', 'number', [
      'number',
      'number',
      'number',
      'number',
    ]) as (h: number, startTick: number, endTick: number, step: number) => number;
    const freePianoRollData = this.module.cwrap('midisketch_free_piano_roll_data', null, [
      'number',
    ]) as (ptr: number) => void;

    const dataPtr = getSafety(this.handle, startTick, endTick, step);
    if (!dataPtr) {
      throw new Error('Failed to get piano roll safety data');
    }

    try {
      const infoArrayPtr = this.module.HEAPU32[dataPtr >> 2];
      const count = this.module.HEAPU32[(dataPtr + 4) >> 2];

      const results: PianoRollInfo[] = [];
      const infoSize = 784; // sizeof(MidiSketchPianoRollInfo)

      for (let i = 0; i < count; i++) {
        const infoPtr = infoArrayPtr + i * infoSize;
        results.push(this.parsePianoRollInfo(infoPtr));
      }

      return results;
    } finally {
      freePianoRollData(dataPtr);
    }
  }

  private parsePianoRollInfo(ptr: number): PianoRollInfo {
    const view = new DataView(this.module.HEAPU8.buffer);

    const tick = view.getUint32(ptr + 0, true);
    const chordDegree = view.getInt8(ptr + 4);
    const currentKey = view.getUint8(ptr + 5);

    const safety: number[] = [];
    for (let i = 0; i < 128; i++) {
      safety.push(view.getUint8(ptr + 6 + i));
    }

    const reason: number[] = [];
    for (let i = 0; i < 128; i++) {
      reason.push(view.getUint16(ptr + 134 + i * 2, true));
    }

    const collision: CollisionInfo[] = [];
    for (let i = 0; i < 128; i++) {
      const offset = ptr + 390 + i * 3;
      collision.push({
        trackRole: view.getUint8(offset),
        collidingPitch: view.getUint8(offset + 1),
        intervalSemitones: view.getUint8(offset + 2),
      });
    }

    const recommendedCount = view.getUint8(ptr + 782);
    const recommended: number[] = [];
    for (let i = 0; i < recommendedCount && i < 8; i++) {
      recommended.push(view.getUint8(ptr + 774 + i));
    }

    return { tick, chordDegree, currentKey, safety, reason, collision, recommended };
  }
}

export interface CollisionInfo {
  trackRole: number;
  collidingPitch: number;
  intervalSemitones: number;
}

export interface PianoRollInfo {
  tick: number;
  chordDegree: number;
  currentKey: number;
  safety: number[];
  reason: number[];
  collision: CollisionInfo[];
  recommended: number[];
}
