import path from 'node:path';
import { beforeAll, describe, expect, it } from 'vitest';
import {
  createDefaultConfig,
  GenerationParadigm,
  getBlueprintCount,
  getBlueprintName,
  getBlueprintParadigm,
  getBlueprintRiffPolicy,
  getBlueprints,
  getBlueprintWeight,
  getVersion,
  init,
  MidiSketch,
  RiffPolicy,
} from './index';

describe('MidiSketch JS API', () => {
  beforeAll(async () => {
    const wasmPath = path.resolve(__dirname, '../dist/midisketch.wasm');
    await init({ wasmPath });
  });

  describe('getVersion', () => {
    it('should return a valid semver version string', () => {
      const version = getVersion();
      expect(version).toMatch(/^\d+\.\d+\.\d+$/);
    });

    it('should return consistent version across multiple calls', () => {
      const version1 = getVersion();
      const version2 = getVersion();
      expect(version1).toBe(version2);
    });
  });

  // ============================================================================
  // Production Blueprint API Tests
  // ============================================================================

  describe('ProductionBlueprint API', () => {
    describe('getBlueprintCount', () => {
      it('should return 4 blueprints', () => {
        expect(getBlueprintCount()).toBe(4);
      });
    });

    describe('getBlueprintName', () => {
      it('should return correct names for each blueprint', () => {
        expect(getBlueprintName(0)).toBe('Traditional');
        expect(getBlueprintName(1)).toBe('Orangestar');
        expect(getBlueprintName(2)).toBe('YOASOBI');
        expect(getBlueprintName(3)).toBe('Ballad');
      });

      it('should return "Unknown" for invalid ID', () => {
        expect(getBlueprintName(255)).toBe('Unknown');
      });
    });

    describe('getBlueprintParadigm', () => {
      it('should return correct paradigm for each blueprint', () => {
        expect(getBlueprintParadigm(0)).toBe(GenerationParadigm.Traditional);
        expect(getBlueprintParadigm(1)).toBe(GenerationParadigm.RhythmSync);
        expect(getBlueprintParadigm(2)).toBe(GenerationParadigm.MelodyDriven);
        expect(getBlueprintParadigm(3)).toBe(GenerationParadigm.MelodyDriven);
      });
    });

    describe('getBlueprintRiffPolicy', () => {
      it('should return correct riff policy for each blueprint', () => {
        expect(getBlueprintRiffPolicy(0)).toBe(RiffPolicy.Free);
        expect(getBlueprintRiffPolicy(1)).toBe(RiffPolicy.Locked);
        expect(getBlueprintRiffPolicy(2)).toBe(RiffPolicy.Evolving);
        expect(getBlueprintRiffPolicy(3)).toBe(RiffPolicy.Free);
      });
    });

    describe('getBlueprintWeight', () => {
      it('should return correct weights for each blueprint', () => {
        expect(getBlueprintWeight(0)).toBe(60); // Traditional: 60%
        expect(getBlueprintWeight(1)).toBe(20); // Orangestar: 20%
        expect(getBlueprintWeight(2)).toBe(15); // YOASOBI: 15%
        expect(getBlueprintWeight(3)).toBe(5); // Ballad: 5%
      });

      it('weights should sum to 100', () => {
        const total =
          getBlueprintWeight(0) +
          getBlueprintWeight(1) +
          getBlueprintWeight(2) +
          getBlueprintWeight(3);
        expect(total).toBe(100);
      });
    });

    describe('getBlueprints', () => {
      it('should return array of all blueprints', () => {
        const blueprints = getBlueprints();
        expect(blueprints).toHaveLength(4);

        // Verify first blueprint (Traditional)
        expect(blueprints[0]).toEqual({
          id: 0,
          name: 'Traditional',
          paradigm: GenerationParadigm.Traditional,
          riffPolicy: RiffPolicy.Free,
          weight: 60,
        });

        // Verify second blueprint (Orangestar)
        expect(blueprints[1]).toEqual({
          id: 1,
          name: 'Orangestar',
          paradigm: GenerationParadigm.RhythmSync,
          riffPolicy: RiffPolicy.Locked,
          weight: 20,
        });
      });
    });

    describe('createDefaultConfig with blueprintId', () => {
      it('should include blueprintId in default config', () => {
        const config = createDefaultConfig(0);
        expect(config.blueprintId).toBeDefined();
        expect(typeof config.blueprintId).toBe('number');
      });
    });

    describe('MidiSketch.getResolvedBlueprintId', () => {
      it('should return resolved blueprint ID after generation', () => {
        const sketch = new MidiSketch();
        try {
          const config = createDefaultConfig(0);
          config.seed = 12345;
          config.blueprintId = 1; // Orangestar
          sketch.generateFromConfig(config);

          const resolvedId = sketch.getResolvedBlueprintId();
          expect(resolvedId).toBe(1);
        } finally {
          sketch.destroy();
        }
      });

      it('should return selected blueprint when blueprintId is 255 (random)', () => {
        const sketch = new MidiSketch();
        try {
          const config = createDefaultConfig(0);
          config.seed = 12345;
          config.blueprintId = 255; // Random selection
          sketch.generateFromConfig(config);

          const resolvedId = sketch.getResolvedBlueprintId();
          expect(resolvedId).toBeGreaterThanOrEqual(0);
          expect(resolvedId).toBeLessThanOrEqual(3);
        } finally {
          sketch.destroy();
        }
      });

      it('should return 0 (default) before generation', () => {
        const sketch = new MidiSketch();
        try {
          const resolvedId = sketch.getResolvedBlueprintId();
          expect(resolvedId).toBe(0); // Default is Traditional (0)
        } finally {
          sketch.destroy();
        }
      });
    });
  });
});
