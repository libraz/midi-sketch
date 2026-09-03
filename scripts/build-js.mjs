#!/usr/bin/env node
import * as esbuild from 'esbuild';
import { execSync } from 'child_process';
import { readFileSync, writeFileSync, unlinkSync, readdirSync, copyFileSync, mkdirSync, existsSync } from 'fs';
import { join } from 'path';

const distDir = './dist';

// Step 0: Copy WASM files to dist
console.log('Copying WASM files...');
if (!existsSync(distDir)) {
  mkdirSync(distDir, { recursive: true });
}
copyFileSync('./build-wasm/bin/midisketch.js', join(distDir, 'midisketch.js'));
copyFileSync('./build-wasm/bin/midisketch.wasm', join(distDir, 'midisketch.wasm'));

// Step 1: Generate .d.ts files with tsc
console.log('Generating type declarations...');
execSync('npx tsc --emitDeclarationOnly', { stdio: 'inherit' });

// Plugin to rewrite WASM module import path
const wasmPlugin = {
  name: 'wasm-path-rewrite',
  setup(build) {
    // Mark the WASM module import as external and rewrite path
    build.onResolve({ filter: /\.\.\/midisketch\.js$/ }, () => ({
      path: './midisketch.js',
      external: true,
    }));
  },
};

// Common esbuild options
const commonOptions = {
  entryPoints: ['js/src/index.ts'],
  bundle: true,
  sourcemap: true,
  target: 'es2020',
  plugins: [wasmPlugin],
};

// Step 2: Bundle JS with esbuild (ESM)
console.log('Bundling ESM...');
await esbuild.build({
  ...commonOptions,
  format: 'esm',
  outfile: 'dist/index.mjs',
});

// Step 3: Bundle JS with esbuild (CJS)
console.log('Bundling CJS...');
await esbuild.build({
  ...commonOptions,
  format: 'cjs',
  outfile: 'dist/index.cjs',
});

// Step 4: Create bundled .d.ts file
console.log('Bundling type declarations...');

// Read all .d.ts files and combine them
const dtsFiles = [
  'types.d.ts',
  'constants.d.ts',
  'blueprint.d.ts',
  'presets.d.ts',
  'internal.d.ts',
  'config.d.ts',
  'config-fields.d.ts',
  'builder.d.ts',
  'midi-sketch.d.ts',
  'utils.d.ts',
];

let combinedDts = `/**
 * midi-sketch - MIDI Auto-Generation Library
 * @packageDocumentation
 */

`;

// Read index.d.ts to get the exports
const indexDts = readFileSync(join(distDir, 'index.d.ts'), 'utf-8');

// Extract the re-exported names. These, and only these, are the identifiers the
// bundled declaration file may export: the runtime bundles are built from the
// same index.ts, so keeping the two derived from one list is what stops the
// declared surface and the runtime surface from drifting apart.
const reExports = [];
for (const line of indexDts.split('\n')) {
  const match = line.match(/^export (type )?\{(.+?),?\s*\} from ['"]\.\/([^'"]+)['"];/);
  if (!match) {
    continue;
  }
  const [, typeOnly, names, from] = match;
  for (const raw of names.split(',')) {
    const name = raw.trim();
    if (!name) {
      continue;
    }
    reExports.push({
      name: typeOnly && !name.startsWith('type ') ? `type ${name}` : name,
      from: `${from}.d.ts`,
    });
  }
}

// The default export is re-exported by name, so the declaration bundle has to
// resolve which local declaration it refers to.
function resolveDefaultExport(file) {
  const match = readFileSync(join(distDir, file), 'utf-8').match(/^export default (\w+);/m);
  if (!match) {
    throw new Error(`${file} is re-exported as default but declares no default export`);
  }
  return match[1];
}

// Read each source file and inline the declarations without their own export
// keywords, so that nothing leaks into the public surface implicitly.
for (const file of dtsFiles) {
  const filePath = join(distDir, file);
  if (!existsSync(filePath)) {
    throw new Error(`Expected declaration file ${file} was not emitted`);
  }
  let content = readFileSync(filePath, 'utf-8');
  // Remove import statements (they reference other local files)
  content = content.replace(/^import .+ from ['"]\.\/[^'"]+['"];?\n/gm, '');
  // Remove re-export and export-marker lines that reference other files
  content = content.replace(/^export (?:type )?\{[^}]*\} from ['"]\.\/[^'"]+['"];?\n/gm, '');
  content = content.replace(/^export \{\};?\n/gm, '');
  content = content.replace(/^export default \w+;?\n/gm, '');
  // Demote the remaining declarations to file-local ones
  content = content.replace(
    /^export (?=declare |type |interface |enum |abstract |class |function |const )/gm,
    '',
  );
  combinedDts += `// From ${file.replace('.d.ts', '.ts')}\n`;
  combinedDts += content + '\n';
}

const namedExports = [];
let defaultExport = null;
for (const { name, from } of reExports) {
  if (name === 'default') {
    defaultExport = resolveDefaultExport(from);
  } else {
    namedExports.push(name);
  }
}

combinedDts += `// Public surface, mirroring the re-exports of index.ts\n`;
combinedDts += `export {\n${namedExports.map((name) => `  ${name},`).join('\n')}\n};\n`;
if (defaultExport) {
  combinedDts += `export default ${defaultExport};\n`;
}

writeFileSync(join(distDir, 'index.d.ts'), combinedDts);

// Clean up individual .d.ts files (keep only index.d.ts)
for (const file of readdirSync(distDir)) {
  if (file.endsWith('.d.ts') && file !== 'index.d.ts') {
    unlinkSync(join(distDir, file));
  }
  if (file.endsWith('.d.ts.map')) {
    unlinkSync(join(distDir, file));
  }
  // Remove individual .js files (we now have bundled index.mjs/index.cjs)
  if (file.endsWith('.js') && file !== 'midisketch.js') {
    unlinkSync(join(distDir, file));
  }
  if (file.endsWith('.js.map') && file !== 'midisketch.js.map') {
    unlinkSync(join(distDir, file));
  }
}

console.log('Build complete!');
console.log('  dist/index.mjs  - ESM bundle');
console.log('  dist/index.cjs  - CJS bundle');
console.log('  dist/index.d.ts - Type declarations');
