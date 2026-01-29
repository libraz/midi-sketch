/**
 * Preset retrieval functions
 */

import { getApi, getModule } from './internal';
import type { PresetInfo, StylePresetInfo } from './types';

/**
 * Get structure presets
 */
export function getStructures(): PresetInfo[] {
  const a = getApi();
  const count = a.structureCount();
  const result: PresetInfo[] = [];
  for (let i = 0; i < count; i++) {
    result.push({ name: a.structureName(i) });
  }
  return result;
}

/**
 * Get mood presets
 */
export function getMoods(): PresetInfo[] {
  const a = getApi();
  const count = a.moodCount();
  const result: PresetInfo[] = [];
  for (let i = 0; i < count; i++) {
    result.push({
      name: a.moodName(i),
      defaultBpm: a.moodDefaultBpm(i),
    });
  }
  return result;
}

/**
 * Get chord progression presets
 */
export function getChords(): PresetInfo[] {
  const a = getApi();
  const count = a.chordCount();
  const result: PresetInfo[] = [];
  for (let i = 0; i < count; i++) {
    result.push({
      name: a.chordName(i),
      display: a.chordDisplay(i),
    });
  }
  return result;
}

/**
 * Get style presets
 */
export function getStylePresets(): StylePresetInfo[] {
  const a = getApi();
  const count = a.stylePresetCount();
  const result: StylePresetInfo[] = [];
  for (let i = 0; i < count; i++) {
    result.push({
      id: i,
      name: a.stylePresetName(i),
      displayName: a.stylePresetDisplayName(i),
      description: a.stylePresetDescription(i),
      tempoDefault: a.stylePresetTempoDefault(i),
      allowedAttitudes: a.stylePresetAllowedAttitudes(i),
    });
  }
  return result;
}

/**
 * Get chord progressions compatible with a style
 */
export function getProgressionsByStyle(styleId: number): number[] {
  const a = getApi();
  const m = getModule();
  const retPtr = a.getProgressionsByStylePtr(styleId);
  const view = new DataView(m.HEAPU8.buffer);
  const count = view.getUint8(retPtr);
  const result: number[] = [];
  for (let i = 0; i < count; i++) {
    result.push(view.getUint8(retPtr + 1 + i));
  }
  return result;
}

/**
 * Get forms compatible with a style
 */
export function getFormsByStyle(styleId: number): number[] {
  const a = getApi();
  const m = getModule();
  const retPtr = a.getFormsByStylePtr(styleId);
  const view = new DataView(m.HEAPU8.buffer);
  const count = view.getUint8(retPtr);
  const result: number[] = [];
  for (let i = 0; i < count; i++) {
    result.push(view.getUint8(retPtr + 1 + i));
  }
  return result;
}
