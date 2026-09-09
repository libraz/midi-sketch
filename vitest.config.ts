import path from 'node:path';
import { defineConfig } from 'vitest/config';

export default defineConfig({
  resolve: {
    alias: {
      '../midisketch.js': path.resolve(import.meta.dirname, 'dist/midisketch.js'),
    },
  },
  test: {
    globals: true,
    environment: 'node',
    include: ['tests/wasm/**/*.test.ts'],
    // Every test here generates whole songs through WASM, and the heaviest
    // generate a hundred of them in one case. Against the 5s default those
    // cases pass alone and time out when the files run side by side, which
    // reports how busy the machine is rather than whether the code is correct.
    // A timeout is here to catch a run that has stopped making progress, so it
    // is set far above the few seconds the slowest case takes.
    testTimeout: 60_000,
    coverage: {
      provider: 'v8',
      reporter: ['text', 'json', 'html'],
      include: ['js/src/**/*.ts'],
      exclude: ['js/**/*.test.ts', 'js/**/*.d.ts'],
    },
  },
});
