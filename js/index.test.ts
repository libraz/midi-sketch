import path from 'node:path';
import { beforeAll, describe, expect, it } from 'vitest';
import { getVersion, init } from './index';

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
});
