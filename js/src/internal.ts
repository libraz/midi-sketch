/**
 * Internal WASM module bindings and initialization
 * @internal
 */

// ============================================================================
// Types for Emscripten Module
// ============================================================================

export interface EmscriptenModule {
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

export interface Api {
  create: () => number;
  destroy: (handle: number) => void;
  getMidi: (handle: number) => number;
  freeMidi: (ptr: number) => void;
  getEvents: (handle: number) => number;
  freeEvents: (ptr: number) => void;
  structureCount: () => number;
  moodCount: () => number;
  chordCount: () => number;
  structureName: (id: number) => string;
  moodName: (id: number) => string;
  chordName: (id: number) => string;
  chordDisplay: (id: number) => string;
  moodDefaultBpm: (id: number) => number;
  version: () => string;
  stylePresetCount: () => number;
  stylePresetName: (id: number) => string;
  stylePresetDisplayName: (id: number) => string;
  stylePresetDescription: (id: number) => string;
  stylePresetTempoDefault: (id: number) => number;
  stylePresetAllowedAttitudes: (id: number) => number;
  getProgressionsByStylePtr: (styleId: number) => number;
  getFormsByStylePtr: (styleId: number) => number;
  configErrorString: (error: number) => string;
  // Vocal-first generation APIs (no-config versions)
  generateAccompaniment: (handle: number) => number;
  regenerateAccompaniment: (handle: number, seed: number) => number;
  // Piano Roll Safety API
  getPianoRollSafety: (handle: number, startTick: number, endTick: number, step: number) => number;
  getPianoRollSafetyAt: (handle: number, tick: number) => number;
  getPianoRollSafetyWithContext: (handle: number, tick: number, prevPitch: number) => number;
  freePianoRollData: (ptr: number) => void;
  reasonToString: (reason: number) => string;
  collisionToString: (collisionPtr: number) => string;
  // JSON Config API
  generateFromJson: (handle: number, json: string, length: number) => number;
  createDefaultConfigJson: (styleId: number) => string;
  validateConfigJson: (json: string, length: number) => number;
  generateVocalFromJson: (handle: number, json: string, length: number) => number;
  generateWithVocalFromJson: (handle: number, json: string, length: number) => number;
  regenerateVocalFromJson: (handle: number, json: string, length: number) => number;
  generateAccompanimentFromJson: (handle: number, json: string, length: number) => number;
  regenerateAccompanimentFromJson: (handle: number, json: string, length: number) => number;
  setVocalNotesFromJson: (handle: number, json: string, length: number) => number;
  // Production Blueprint API
  blueprintCount: () => number;
  blueprintName: (id: number) => string;
  blueprintParadigm: (id: number) => number;
  blueprintRiffPolicy: (id: number) => number;
  blueprintWeight: (id: number) => number;
  getResolvedBlueprintId: (handle: number) => number;
}

// ============================================================================
// Module State
// ============================================================================

let moduleInstance: EmscriptenModule | null = null;
let api: Api | null = null;

/**
 * Get the WASM module instance
 * @throws Error if module not initialized
 * @internal
 */
export function getModule(): EmscriptenModule {
  if (!moduleInstance) {
    throw new Error('Module not initialized. Call init() first.');
  }
  return moduleInstance;
}

/**
 * Get the API bindings
 * @throws Error if module not initialized
 * @internal
 */
export function getApi(): Api {
  if (!api) {
    throw new Error('Module not initialized. Call init() first.');
  }
  return api;
}

/**
 * Check if module is initialized
 * @internal
 */
export function isInitialized(): boolean {
  return moduleInstance !== null && api !== null;
}

// ============================================================================
// Initialization
// ============================================================================

/**
 * Initialize the WASM module
 */
export async function init(options?: { wasmPath?: string }): Promise<void> {
  if (moduleInstance) {
    return;
  }

  const createModule = await import('../midisketch.js');
  moduleInstance = await createModule.default({
    locateFile: (path: string) => {
      if (path.endsWith('.wasm') && options?.wasmPath) {
        return options.wasmPath;
      }
      return path;
    },
  });

  const m = moduleInstance;

  // Setup cwrap bindings with proper types
  api = {
    create: m.cwrap('midisketch_create', 'number', []) as () => number,
    destroy: m.cwrap('midisketch_destroy', null, ['number']) as (handle: number) => void,
    getMidi: m.cwrap('midisketch_get_midi', 'number', ['number']) as (handle: number) => number,
    freeMidi: m.cwrap('midisketch_free_midi', null, ['number']) as (ptr: number) => void,
    getEvents: m.cwrap('midisketch_get_events', 'number', ['number']) as (handle: number) => number,
    freeEvents: m.cwrap('midisketch_free_events', null, ['number']) as (ptr: number) => void,
    structureCount: m.cwrap('midisketch_structure_count', 'number', []) as () => number,
    moodCount: m.cwrap('midisketch_mood_count', 'number', []) as () => number,
    chordCount: m.cwrap('midisketch_chord_count', 'number', []) as () => number,
    structureName: m.cwrap('midisketch_structure_name', 'string', ['number']) as (
      id: number,
    ) => string,
    moodName: m.cwrap('midisketch_mood_name', 'string', ['number']) as (id: number) => string,
    chordName: m.cwrap('midisketch_chord_name', 'string', ['number']) as (id: number) => string,
    chordDisplay: m.cwrap('midisketch_chord_display', 'string', ['number']) as (
      id: number,
    ) => string,
    moodDefaultBpm: m.cwrap('midisketch_mood_default_bpm', 'number', ['number']) as (
      id: number,
    ) => number,
    version: m.cwrap('midisketch_version', 'string', []) as () => string,
    stylePresetCount: m.cwrap('midisketch_style_preset_count', 'number', []) as () => number,
    stylePresetName: m.cwrap('midisketch_style_preset_name', 'string', ['number']) as (
      id: number,
    ) => string,
    stylePresetDisplayName: m.cwrap('midisketch_style_preset_display_name', 'string', [
      'number',
    ]) as (id: number) => string,
    stylePresetDescription: m.cwrap('midisketch_style_preset_description', 'string', [
      'number',
    ]) as (id: number) => string,
    stylePresetTempoDefault: m.cwrap('midisketch_style_preset_tempo_default', 'number', [
      'number',
    ]) as (id: number) => number,
    stylePresetAllowedAttitudes: m.cwrap('midisketch_style_preset_allowed_attitudes', 'number', [
      'number',
    ]) as (id: number) => number,
    getProgressionsByStylePtr: m.cwrap('midisketch_get_progressions_by_style_ptr', 'number', [
      'number',
    ]) as (styleId: number) => number,
    getFormsByStylePtr: m.cwrap('midisketch_get_forms_by_style_ptr', 'number', ['number']) as (
      styleId: number,
    ) => number,
    configErrorString: m.cwrap('midisketch_config_error_string', 'string', ['number']) as (
      error: number,
    ) => string,
    // Vocal-first generation APIs (no-config versions)
    generateAccompaniment: m.cwrap('midisketch_generate_accompaniment', 'number', ['number']) as (
      handle: number,
    ) => number,
    regenerateAccompaniment: m.cwrap('midisketch_regenerate_accompaniment', 'number', [
      'number',
      'number',
    ]) as (handle: number, seed: number) => number,
    // Piano Roll Safety API
    getPianoRollSafety: m.cwrap('midisketch_get_piano_roll_safety', 'number', [
      'number',
      'number',
      'number',
      'number',
    ]) as (handle: number, startTick: number, endTick: number, step: number) => number,
    getPianoRollSafetyAt: m.cwrap('midisketch_get_piano_roll_safety_at', 'number', [
      'number',
      'number',
    ]) as (handle: number, tick: number) => number,
    getPianoRollSafetyWithContext: m.cwrap(
      'midisketch_get_piano_roll_safety_with_context',
      'number',
      ['number', 'number', 'number'],
    ) as (handle: number, tick: number, prevPitch: number) => number,
    freePianoRollData: m.cwrap('midisketch_free_piano_roll_data', null, ['number']) as (
      ptr: number,
    ) => void,
    reasonToString: m.cwrap('midisketch_reason_to_string', 'string', ['number']) as (
      reason: number,
    ) => string,
    collisionToString: m.cwrap('midisketch_collision_to_string', 'string', ['number']) as (
      collisionPtr: number,
    ) => string,
    // JSON Config API
    generateFromJson: m.cwrap('midisketch_generate_from_json', 'number', [
      'number',
      'string',
      'number',
    ]) as (handle: number, json: string, length: number) => number,
    createDefaultConfigJson: m.cwrap('midisketch_create_default_config_json', 'string', [
      'number',
    ]) as (styleId: number) => string,
    validateConfigJson: m.cwrap('midisketch_validate_config_json', 'number', [
      'string',
      'number',
    ]) as (json: string, length: number) => number,
    generateVocalFromJson: m.cwrap('midisketch_generate_vocal_from_json', 'number', [
      'number',
      'string',
      'number',
    ]) as (handle: number, json: string, length: number) => number,
    generateWithVocalFromJson: m.cwrap('midisketch_generate_with_vocal_from_json', 'number', [
      'number',
      'string',
      'number',
    ]) as (handle: number, json: string, length: number) => number,
    regenerateVocalFromJson: m.cwrap('midisketch_regenerate_vocal_from_json', 'number', [
      'number',
      'string',
      'number',
    ]) as (handle: number, json: string, length: number) => number,
    generateAccompanimentFromJson: m.cwrap(
      'midisketch_generate_accompaniment_from_json',
      'number',
      ['number', 'string', 'number'],
    ) as (handle: number, json: string, length: number) => number,
    regenerateAccompanimentFromJson: m.cwrap(
      'midisketch_regenerate_accompaniment_from_json',
      'number',
      ['number', 'string', 'number'],
    ) as (handle: number, json: string, length: number) => number,
    setVocalNotesFromJson: m.cwrap('midisketch_set_vocal_notes_from_json', 'number', [
      'number',
      'string',
      'number',
    ]) as (handle: number, json: string, length: number) => number,
    // Production Blueprint API
    blueprintCount: m.cwrap('midisketch_blueprint_count', 'number', []) as () => number,
    blueprintName: m.cwrap('midisketch_blueprint_name', 'string', ['number']) as (
      id: number,
    ) => string,
    blueprintParadigm: m.cwrap('midisketch_blueprint_paradigm', 'number', ['number']) as (
      id: number,
    ) => number,
    blueprintRiffPolicy: m.cwrap('midisketch_blueprint_riff_policy', 'number', ['number']) as (
      id: number,
    ) => number,
    blueprintWeight: m.cwrap('midisketch_blueprint_weight', 'number', ['number']) as (
      id: number,
    ) => number,
    getResolvedBlueprintId: m.cwrap('midisketch_get_resolved_blueprint_id', 'number', [
      'number',
    ]) as (handle: number) => number,
  };
}
