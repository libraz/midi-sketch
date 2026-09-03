import { defineConfig } from 'vitest/config';

/**
 * The oracle tier drives the native CLI and reads its output back with an
 * independent theory engine, so it needs the C++ build rather than the WASM
 * bundle and it generates a whole pairwise corpus per file. It runs from its
 * own config to keep those costs out of `yarn test`.
 */
export default defineConfig({
  test: {
    globals: true,
    environment: 'node',
    include: ['tests/oracle/**/*.test.ts'],
    testTimeout: 120_000,
    hookTimeout: 600_000,
  },
});
