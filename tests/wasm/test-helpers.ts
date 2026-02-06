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
  blueprintId?: number;
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

export interface AccompanimentConfigOptions {
  seed?: number;
  drumsEnabled?: boolean;
  arpeggioEnabled?: boolean;
  arpeggioPattern?: number;
  arpeggioSpeed?: number;
  arpeggioOctaveRange?: number;
  arpeggioGate?: number;
  arpeggioSyncChord?: boolean;
  chordExtSus?: boolean;
  chordExt7th?: boolean;
  chordExt9th?: boolean;
  chordExtSusProb?: number;
  chordExt7thProb?: number;
  chordExt9thProb?: number;
  humanize?: boolean;
  humanizeTiming?: number;
  humanizeVelocity?: number;
  seEnabled?: boolean;
  callEnabled?: boolean;
  callDensity?: number;
  introChant?: number;
  mixPattern?: number;
  callNotesEnabled?: boolean;
}

/**
 * Serialize a SongConfigOptions object to a C++ SongConfig JSON string.
 *
 * Maps JS camelCase field names to C++ snake_case field names.
 * Handles nested structs (arpeggio, chord_extension, motif_chord) and
 * value conversions (gate/probability from 0-100 integer to 0-1 float,
 * humanize timing/velocity from 0-100 integer to 0-1 float).
 */
function serializeSongConfig(config: SongConfigOptions): string {
  const obj: Record<string, unknown> = {};

  // Top-level flat fields
  if (config.stylePresetId !== undefined) {
    obj.style_preset_id = config.stylePresetId;
  }
  if (config.key !== undefined) {
    obj.key = config.key;
  }
  if (config.bpm !== undefined) {
    obj.bpm = config.bpm;
  }
  if (config.seed !== undefined) {
    obj.seed = config.seed;
  }
  if (config.chordProgressionId !== undefined) {
    obj.chord_progression_id = config.chordProgressionId;
  }
  if (config.formId !== undefined) {
    obj.form = config.formId;
  }
  if (config.vocalAttitude !== undefined) {
    obj.vocal_attitude = config.vocalAttitude;
  }
  if (config.drumsEnabled !== undefined) {
    obj.drums_enabled = config.drumsEnabled;
  }
  if (config.blueprintId !== undefined) {
    obj.blueprint_id = config.blueprintId;
  }
  if (config.arpeggioEnabled !== undefined) {
    obj.arpeggio_enabled = config.arpeggioEnabled;
  }
  if (config.vocalLow !== undefined) {
    obj.vocal_low = config.vocalLow;
  }
  if (config.vocalHigh !== undefined) {
    obj.vocal_high = config.vocalHigh;
  }
  if (config.skipVocal !== undefined) {
    obj.skip_vocal = config.skipVocal;
  }
  if (config.humanize !== undefined) {
    obj.humanize = config.humanize;
  }
  if (config.compositionStyle !== undefined) {
    obj.composition_style = config.compositionStyle;
  }
  if (config.targetDurationSeconds !== undefined) {
    obj.target_duration_seconds = config.targetDurationSeconds;
  }
  if (config.modulationTiming !== undefined) {
    obj.modulation_timing = config.modulationTiming;
  }
  if (config.modulationSemitones !== undefined) {
    obj.modulation_semitones = config.modulationSemitones;
  }
  if (config.seEnabled !== undefined) {
    obj.se_enabled = config.seEnabled;
  }
  if (config.callNotesEnabled !== undefined) {
    obj.call_notes_enabled = config.callNotesEnabled;
  }
  if (config.introChant !== undefined) {
    obj.intro_chant = config.introChant;
  }
  if (config.mixPattern !== undefined) {
    obj.mix_pattern = config.mixPattern;
  }
  if (config.callDensity !== undefined) {
    obj.call_density = config.callDensity;
  }
  if (config.vocalStyle !== undefined) {
    obj.vocal_style = config.vocalStyle;
  }
  if (config.melodyTemplate !== undefined) {
    obj.melody_template = config.melodyTemplate;
  }
  if (config.arrangementGrowth !== undefined) {
    obj.arrangement_growth = config.arrangementGrowth;
  }
  if (config.motifRepeatScope !== undefined) {
    obj.motif_repeat_scope = config.motifRepeatScope;
  }
  if (config.melodicComplexity !== undefined) {
    obj.melodic_complexity = config.melodicComplexity;
  }
  if (config.hookIntensity !== undefined) {
    obj.hook_intensity = config.hookIntensity;
  }
  if (config.vocalGroove !== undefined) {
    obj.vocal_groove = config.vocalGroove;
  }

  // Special mapping: callEnabled (bool) -> call_setting (enum)
  // CallSetting: 0=Auto, 1=Enabled, 2=Disabled
  // For predictable tests: true -> Enabled(1), false/undefined -> Disabled(2)
  if (config.callEnabled !== undefined) {
    obj.call_setting = config.callEnabled === true ? 1 : 2;
  } else {
    obj.call_setting = 2; // Disabled for predictable tests
  }

  // Humanize timing/velocity: test uses 0-100 integer, SongConfig uses 0-1 float
  if (config.humanizeTiming !== undefined) {
    obj.humanize_timing = config.humanizeTiming / 100;
  }
  if (config.humanizeVelocity !== undefined) {
    obj.humanize_velocity = config.humanizeVelocity / 100;
  }

  // Nested struct: arpeggio
  const arpeggio: Record<string, unknown> = {};
  if (config.arpeggioPattern !== undefined) {
    arpeggio.pattern = config.arpeggioPattern;
  }
  if (config.arpeggioSpeed !== undefined) {
    arpeggio.speed = config.arpeggioSpeed;
  }
  if (config.arpeggioOctaveRange !== undefined) {
    arpeggio.octave_range = config.arpeggioOctaveRange;
  }
  if (config.arpeggioSyncChord !== undefined) {
    arpeggio.sync_chord = config.arpeggioSyncChord;
  }
  // Gate: test uses 0-100 integer, SongConfig arpeggio.gate is 0-1 float
  if (config.arpeggioGate !== undefined) {
    arpeggio.gate = config.arpeggioGate / 100;
  }
  if (Object.keys(arpeggio).length > 0) {
    obj.arpeggio = arpeggio;
  }

  // Nested struct: chord_extension
  const chordExt: Record<string, unknown> = {};
  if (config.chordExtSus !== undefined) {
    chordExt.enable_sus = config.chordExtSus;
  }
  if (config.chordExt7th !== undefined) {
    chordExt.enable_7th = config.chordExt7th;
  }
  if (config.chordExt9th !== undefined) {
    chordExt.enable_9th = config.chordExt9th;
  }
  // Probabilities: test uses 0-100 integer, SongConfig uses 0-1 float
  if (config.chordExtSusProb !== undefined) {
    chordExt.sus_probability = config.chordExtSusProb / 100;
  }
  if (config.chordExt7thProb !== undefined) {
    chordExt.seventh_probability = config.chordExt7thProb / 100;
  }
  if (config.chordExt9thProb !== undefined) {
    chordExt.ninth_probability = config.chordExt9thProb / 100;
  }
  if (Object.keys(chordExt).length > 0) {
    obj.chord_extension = chordExt;
  }

  // Nested struct: motif_chord
  const motifChord: Record<string, unknown> = {};
  if (config.motifFixedProgression !== undefined) {
    motifChord.fixed_progression = config.motifFixedProgression;
  }
  if (config.motifMaxChordCount !== undefined) {
    motifChord.max_chord_count = config.motifMaxChordCount;
  }
  if (Object.keys(motifChord).length > 0) {
    obj.motif_chord = motifChord;
  }

  return JSON.stringify(obj);
}

/**
 * Serialize an AccompanimentConfigOptions object to C++ AccompanimentConfig JSON string.
 *
 * Maps JS camelCase field names to C++ snake_case field names.
 * AccompanimentConfig uses uint8_t for timing/gate/probability values (0-100),
 * so no float conversion is needed.
 */
function serializeAccompanimentConfig(config: AccompanimentConfigOptions): string {
  const obj: Record<string, unknown> = {};

  if (config.seed !== undefined) {
    obj.seed = config.seed;
  }
  if (config.drumsEnabled !== undefined) {
    obj.drums_enabled = config.drumsEnabled;
  }
  if (config.arpeggioEnabled !== undefined) {
    obj.arpeggio_enabled = config.arpeggioEnabled;
  }
  if (config.arpeggioPattern !== undefined) {
    obj.arpeggio_pattern = config.arpeggioPattern;
  }
  if (config.arpeggioSpeed !== undefined) {
    obj.arpeggio_speed = config.arpeggioSpeed;
  }
  if (config.arpeggioOctaveRange !== undefined) {
    obj.arpeggio_octave_range = config.arpeggioOctaveRange;
  }
  if (config.arpeggioGate !== undefined) {
    obj.arpeggio_gate = config.arpeggioGate;
  }
  if (config.arpeggioSyncChord !== undefined) {
    obj.arpeggio_sync_chord = config.arpeggioSyncChord;
  }
  if (config.chordExtSus !== undefined) {
    obj.chord_ext_sus = config.chordExtSus;
  }
  if (config.chordExt7th !== undefined) {
    obj.chord_ext_7th = config.chordExt7th;
  }
  if (config.chordExt9th !== undefined) {
    obj.chord_ext_9th = config.chordExt9th;
  }
  if (config.chordExtSusProb !== undefined) {
    obj.chord_ext_sus_prob = config.chordExtSusProb;
  }
  if (config.chordExt7thProb !== undefined) {
    obj.chord_ext_7th_prob = config.chordExt7thProb;
  }
  if (config.chordExt9thProb !== undefined) {
    obj.chord_ext_9th_prob = config.chordExt9thProb;
  }
  if (config.humanize !== undefined) {
    obj.humanize = config.humanize;
  }
  if (config.humanizeTiming !== undefined) {
    obj.humanize_timing = config.humanizeTiming;
  }
  if (config.humanizeVelocity !== undefined) {
    obj.humanize_velocity = config.humanizeVelocity;
  }
  if (config.seEnabled !== undefined) {
    obj.se_enabled = config.seEnabled;
  }
  if (config.callEnabled !== undefined) {
    obj.call_enabled = config.callEnabled;
  }
  if (config.callDensity !== undefined) {
    obj.call_density = config.callDensity;
  }
  if (config.introChant !== undefined) {
    obj.intro_chant = config.introChant;
  }
  if (config.mixPattern !== undefined) {
    obj.mix_pattern = config.mixPattern;
  }
  if (config.callNotesEnabled !== undefined) {
    obj.call_notes_enabled = config.callNotesEnabled;
  }

  return JSON.stringify(obj);
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

  generateFromConfig(config: SongConfigOptions): number {
    const generateFn = this.module.cwrap('midisketch_generate_from_json', 'number', [
      'number',
      'string',
      'number',
    ]) as (h: number, json: string, len: number) => number;

    const json = serializeSongConfig(config);
    return generateFn(this.handle, json, json.length);
  }

  generateVocal(config: SongConfigOptions): number {
    const generateFn = this.module.cwrap('midisketch_generate_vocal_from_json', 'number', [
      'number',
      'string',
      'number',
    ]) as (h: number, json: string, len: number) => number;

    const json = serializeSongConfig(config);
    return generateFn(this.handle, json, json.length);
  }

  regenerateVocal(newSeed: number): number {
    const regenerateFn = this.module.cwrap('midisketch_regenerate_vocal_from_json', 'number', [
      'number',
      'string',
      'number',
    ]) as (h: number, json: string, len: number) => number;

    const json = JSON.stringify({ seed: newSeed });
    return regenerateFn(this.handle, json, json.length);
  }

  generateAccompaniment(config?: AccompanimentConfigOptions): number {
    if (config) {
      const generateFn = this.module.cwrap(
        'midisketch_generate_accompaniment_from_json',
        'number',
        ['number', 'string', 'number'],
      ) as (h: number, json: string, len: number) => number;

      const json = serializeAccompanimentConfig(config);
      return generateFn(this.handle, json, json.length);
    } else {
      const generateFn = this.module.cwrap('midisketch_generate_accompaniment', 'number', [
        'number',
      ]) as (h: number) => number;
      return generateFn(this.handle);
    }
  }

  regenerateAccompaniment(seedOrConfig?: number | AccompanimentConfigOptions): number {
    if (typeof seedOrConfig === 'object') {
      const regenerateFn = this.module.cwrap(
        'midisketch_regenerate_accompaniment_from_json',
        'number',
        ['number', 'string', 'number'],
      ) as (h: number, json: string, len: number) => number;

      const json = serializeAccompanimentConfig(seedOrConfig);
      return regenerateFn(this.handle, json, json.length);
    } else {
      const regenerateFn = this.module.cwrap('midisketch_regenerate_accompaniment', 'number', [
        'number',
        'number',
      ]) as (h: number, seed: number) => number;
      return regenerateFn(this.handle, seedOrConfig ?? 0);
    }
  }

  generateWithVocal(config: SongConfigOptions): number {
    const generateFn = this.module.cwrap('midisketch_generate_with_vocal_from_json', 'number', [
      'number',
      'string',
      'number',
    ]) as (h: number, json: string, len: number) => number;

    const json = serializeSongConfig(config);
    return generateFn(this.handle, json, json.length);
  }

  setVocalNotes(
    config: SongConfigOptions,
    notes: { startTick: number; duration: number; pitch: number; velocity: number }[],
  ): number {
    const setNotesFn = this.module.cwrap('midisketch_set_vocal_notes_from_json', 'number', [
      'number',
      'string',
      'number',
    ]) as (h: number, json: string, len: number) => number;

    // Build the combined JSON: {"config": {...SongConfig...}, "notes": [...]}
    const configObj = JSON.parse(serializeSongConfig(config));
    const notesArr = notes.map((note) => ({
      start_tick: note.startTick,
      duration: note.duration,
      pitch: note.pitch,
      velocity: note.velocity,
    }));

    const json = JSON.stringify({ config: configObj, notes: notesArr });
    return setNotesFn(this.handle, json, json.length);
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

      for (let idx = 0; idx < count; idx++) {
        const infoPtr = infoArrayPtr + idx * infoSize;
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
    for (let idx = 0; idx < 128; idx++) {
      safety.push(view.getUint8(ptr + 6 + idx));
    }

    const reason: number[] = [];
    for (let idx = 0; idx < 128; idx++) {
      reason.push(view.getUint16(ptr + 134 + idx * 2, true));
    }

    const collision: CollisionInfo[] = [];
    for (let idx = 0; idx < 128; idx++) {
      const offset = ptr + 390 + idx * 3;
      collision.push({
        trackRole: view.getUint8(offset),
        collidingPitch: view.getUint8(offset + 1),
        intervalSemitones: view.getUint8(offset + 2),
      });
    }

    const recommendedCount = view.getUint8(ptr + 782);
    const recommended: number[] = [];
    for (let idx = 0; idx < recommendedCount && idx < 8; idx++) {
      recommended.push(view.getUint8(ptr + 774 + idx));
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
