/**
 * Production Blueprint API
 */

import { getApi } from './internal';

// ============================================================================
// Blueprint Types
// ============================================================================

/**
 * Generation paradigm for blueprint
 */
export const GenerationParadigm = {
  /** Existing behavior */
  Traditional: 0,
  /** Orangestar style (rhythm-synced) */
  RhythmSync: 1,
  /** YOASOBI style (melody-driven) */
  MelodyDriven: 2,
} as const;

export type GenerationParadigmType = (typeof GenerationParadigm)[keyof typeof GenerationParadigm];

/**
 * Riff policy for blueprint
 */
export const RiffPolicy = {
  /** Free variation per section */
  Free: 0,
  /** Pitch contour fixed, expression variable (recommended) */
  LockedContour: 1,
  /** Pitch completely fixed, velocity variable */
  LockedPitch: 2,
  /** Completely fixed (monotonous, not recommended) */
  LockedAll: 3,
  /** Gradual evolution with variations */
  Evolving: 4,
  /** Alias for LockedContour (backward compatibility) */
  Locked: 1,
} as const;

export type RiffPolicyType = (typeof RiffPolicy)[keyof typeof RiffPolicy];

/**
 * Blueprint information
 */
export interface BlueprintInfo {
  /** Blueprint ID (0-3) */
  id: number;
  /** Blueprint name */
  name: string;
  /** Generation paradigm */
  paradigm: GenerationParadigmType;
  /** Riff policy */
  riffPolicy: RiffPolicyType;
  /** Selection weight (0-100) */
  weight: number;
}

// ============================================================================
// Blueprint API Functions
// ============================================================================

/**
 * Get number of available blueprints
 */
export function getBlueprintCount(): number {
  return getApi().blueprintCount();
}

/**
 * Get blueprint name by ID
 * @param id Blueprint ID (0-3)
 */
export function getBlueprintName(id: number): string {
  return getApi().blueprintName(id);
}

/**
 * Get blueprint paradigm by ID
 * @param id Blueprint ID (0-3)
 */
export function getBlueprintParadigm(id: number): GenerationParadigmType {
  return getApi().blueprintParadigm(id) as GenerationParadigmType;
}

/**
 * Get blueprint riff policy by ID
 * @param id Blueprint ID (0-3)
 */
export function getBlueprintRiffPolicy(id: number): RiffPolicyType {
  return getApi().blueprintRiffPolicy(id) as RiffPolicyType;
}

/**
 * Get blueprint weight by ID
 * @param id Blueprint ID (0-3)
 */
export function getBlueprintWeight(id: number): number {
  return getApi().blueprintWeight(id);
}

/**
 * Get all blueprints as an array
 */
export function getBlueprints(): BlueprintInfo[] {
  const a = getApi();
  const count = a.blueprintCount();
  const result: BlueprintInfo[] = [];
  for (let i = 0; i < count; i++) {
    result.push({
      id: i,
      name: a.blueprintName(i),
      paradigm: a.blueprintParadigm(i) as GenerationParadigmType,
      riffPolicy: a.blueprintRiffPolicy(i) as RiffPolicyType,
      weight: a.blueprintWeight(i),
    });
  }
  return result;
}
