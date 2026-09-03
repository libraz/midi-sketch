/**
 * The shipped type declarations and the shipped runtime bundle must describe
 * the same package. A name that only one of them has is either an import that
 * type-checks and then resolves to undefined, or a working function that no
 * TypeScript consumer is allowed to call.
 */
import path from 'node:path';
import ts from 'typescript';
import { describe, expect, it } from 'vitest';

const DIST_DIR = path.resolve(__dirname, '../../dist');
const DECLARATION_FILE = path.join(DIST_DIR, 'index.d.ts');
const ESM_BUNDLE = path.join(DIST_DIR, 'index.mjs');

const COMPILER_OPTIONS: ts.CompilerOptions = {
  strict: true,
  noEmit: true,
  target: ts.ScriptTarget.ES2020,
  module: ts.ModuleKind.ESNext,
  moduleResolution: ts.ModuleResolutionKind.Bundler,
  lib: ['lib.es2020.d.ts', 'lib.dom.d.ts'],
};

const program = ts.createProgram([DECLARATION_FILE], COMPILER_OPTIONS);
const checker = program.getTypeChecker();

/** Split the declaration file's exports into value exports and type-only exports. */
function readDeclaredExports(): { values: string[]; types: string[] } {
  const sourceFile = program.getSourceFile(DECLARATION_FILE);
  if (!sourceFile) {
    throw new Error(`Could not load ${DECLARATION_FILE}`);
  }
  const moduleSymbol = checker.getSymbolAtLocation(sourceFile);
  if (!moduleSymbol) {
    throw new Error(`${DECLARATION_FILE} is not a module`);
  }

  const values: string[] = [];
  const types: string[] = [];
  for (const exported of checker.getExportsOfModule(moduleSymbol)) {
    const resolved =
      exported.flags & ts.SymbolFlags.Alias ? checker.getAliasedSymbol(exported) : exported;
    if (resolved.flags & ts.SymbolFlags.Value) {
      values.push(exported.name);
    } else {
      types.push(exported.name);
    }
  }
  return { values: values.sort(), types: types.sort() };
}

describe('shipped package surface', () => {
  it('has type declarations that compile on their own', () => {
    const diagnostics = [
      ...program.getSemanticDiagnostics(),
      ...program.getSyntacticDiagnostics(),
    ].map((d) => ts.flattenDiagnosticMessageText(d.messageText, ' '));
    expect(diagnostics).toEqual([]);
  });

  it('declares exactly the values the runtime bundle exports', async () => {
    const runtime = (await import(ESM_BUNDLE)) as Record<string, unknown>;
    const runtimeExports = Object.keys(runtime).sort();
    const { values } = readDeclaredExports();

    expect(runtimeExports.length).toBeGreaterThan(0);
    expect(values).toEqual(runtimeExports);
  });

  it('resolves every declared value export to a defined runtime binding', async () => {
    const runtime = (await import(ESM_BUNDLE)) as Record<string, unknown>;
    const { values } = readDeclaredExports();
    const undefinedExports = values.filter((name) => runtime[name] === undefined);
    expect(undefinedExports).toEqual([]);
  });

  it('does not declare the internal module bindings', () => {
    const { values, types } = readDeclaredExports();
    const declared = new Set([...values, ...types]);
    for (const internalName of ['getApi', 'getModule', 'Api', 'EmscriptenModule']) {
      expect(declared.has(internalName)).toBe(false);
    }
  });

  it('declares the config serializers the runtime bundle exports', () => {
    const { values } = readDeclaredExports();
    for (const name of [
      'serializeConfig',
      'deserializeConfig',
      'serializeVocalConfig',
      'serializeAccompanimentConfig',
    ]) {
      expect(values).toContain(name);
    }
  });
});
