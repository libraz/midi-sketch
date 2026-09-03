/**
 * The TypeScript config surface must mirror what the core accepts and reports:
 * every error code the core can hand back needs a name, and every default the
 * core constructs needs to be reachable and reproducible from TypeScript.
 */
import fs from 'node:fs';
import path from 'node:path';
import { beforeAll, describe, expect, it } from 'vitest';
import { CONFIG_FIELDS } from '../../js/src/config-fields';
import type { ConfigErrorCode } from '../../js/src/constants';
import {
  ARPEGGIO_GATE_AUTO,
  ArpeggioPattern,
  ArpeggioSpeed,
  ConfigError,
  createDefaultConfig,
  deserializeConfig,
  getConfigErrorMessage,
  init,
  serializeConfig,
  validateConfig,
} from '../../js/src/index';
import type { SongConfig } from '../../js/src/types';

/** Message the core returns for a code it does not know. */
const UNKNOWN_ERROR_MESSAGE = 'Unknown config error';

/** Upper bound for the code scan; well past the highest code the core declares. */
const ERROR_CODE_SCAN_LIMIT = 128;

/** The C header that declares the error codes the JS table mirrors. */
const C_HEADER = path.resolve(__dirname, '../../src/midisketch_c.h');

/**
 * Reduce an error-code identifier to a form both languages can be compared in:
 * the C prefix and the word separators are dropped, and case is normalized.
 * `MIDISKETCH_CONFIG_INVALID_JSON` and `InvalidJson` both become `INVALIDJSON`.
 */
function normalizeErrorName(name: string): string {
  return name
    .replace(/^MIDISKETCH_CONFIG_/, '')
    .replace(/_/g, '')
    .toUpperCase();
}

/**
 * Extract the `MidiSketchConfigError` enum from C source text.
 *
 * Members without an explicit value take the previous value plus one, as C
 * does; a parser that silently dropped or mis-numbered members would turn the
 * comparison below into a test that can never fail.
 *
 * @param source C source containing the enum
 * @returns Normalized member name to numeric value
 */
function parseConfigErrorEnum(source: string): Map<string, number> {
  const block = source.match(/typedef enum \{([\s\S]*?)\} MidiSketchConfigError;/);
  if (!block) {
    throw new Error('Could not locate the MidiSketchConfigError enum');
  }

  const members = new Map<string, number>();
  let nextImplicitValue = 0;
  for (const rawLine of block[1].split('\n')) {
    // Drop trailing /// and /* */ documentation before looking for a member.
    const line = rawLine
      .replace(/\/\/.*$/, '')
      .replace(/\/\*[\s\S]*?\*\//g, '')
      .trim();
    const member = line.match(/^(MIDISKETCH_CONFIG_\w+)\s*(?:=\s*(-?\d+))?\s*,?$/);
    if (!member) {
      continue;
    }
    const value = member[2] === undefined ? nextImplicitValue : Number(member[2]);
    members.set(normalizeErrorName(member[1]), value);
    nextImplicitValue = value + 1;
  }

  if (members.size === 0) {
    throw new Error('Parsed no members from the MidiSketchConfigError enum');
  }
  return members;
}

/**
 * Read the enum from the C header that declares it.
 *
 * The header is the declared contract for every JSON entry point, so deriving
 * the expected set from it means a code added in C cannot stay unnamed in
 * TypeScript without a test failing.
 */
function readConfigErrorEnumFromHeader(): Map<string, number> {
  return parseConfigErrorEnum(fs.readFileSync(C_HEADER, 'utf-8'));
}

describe('config surface', () => {
  beforeAll(async () => {
    await init({ wasmPath: path.resolve(__dirname, '../../dist/midisketch.wasm') });
  });

  describe('ConfigError', () => {
    it('names every error code the core can report', () => {
      const namedCodes = new Set<number>(Object.values(ConfigError));
      const unnamed: Array<{ code: number; message: string }> = [];

      for (let code = 0; code < ERROR_CODE_SCAN_LIMIT; code++) {
        const message = getConfigErrorMessage(code as ConfigErrorCode);
        if (message !== UNKNOWN_ERROR_MESSAGE && !namedCodes.has(code)) {
          unnamed.push({ code, message });
        }
      }

      expect(unnamed).toEqual([]);
    });

    it('names no code the core does not report', () => {
      const unreported = Object.entries(ConfigError).filter(
        ([, code]) => getConfigErrorMessage(code) === UNKNOWN_ERROR_MESSAGE,
      );

      expect(unreported).toEqual([]);
    });

    it('reads the C enum the way a C compiler would', () => {
      const parsed = parseConfigErrorEnum(`
/// @brief Config validation error codes.
typedef enum {
  MIDISKETCH_CONFIG_OK = 0,
  MIDISKETCH_CONFIG_INVALID_STYLE = 1,
  MIDISKETCH_CONFIG_INVALID_CHORD,  ///< implicit, follows the previous value
  MIDISKETCH_CONFIG_INVALID_JSON = 33,
} MidiSketchConfigError;
`);

      expect([...parsed]).toEqual([
        ['OK', 0],
        ['INVALIDSTYLE', 1],
        ['INVALIDCHORD', 2],
        ['INVALIDJSON', 33],
      ]);
    });

    it('rejects source that does not contain the enum', () => {
      expect(() => parseConfigErrorEnum('int main(void) { return 0; }')).toThrow();
      expect(() => parseConfigErrorEnum('typedef enum {\n} MidiSketchConfigError;')).toThrow();
    });

    it('matches the C header member for member', () => {
      const fromHeader = readConfigErrorEnumFromHeader();
      const fromTypeScript = new Map(
        Object.entries(ConfigError).map(([name, value]) => [normalizeErrorName(name), value]),
      );

      // Sorted arrays rather than the maps themselves so a mismatch reports the
      // offending member instead of "two Maps differ".
      const asSortedEntries = (members: Map<string, number>) =>
        [...members].sort(([a], [b]) => a.localeCompare(b));

      expect(asSortedEntries(fromTypeScript)).toEqual(asSortedEntries(fromHeader));
    });

    it('exposes the malformed-input and mood codes by name', () => {
      expect(ConfigError.InvalidJson).toBe(33);
      expect(ConfigError.InvalidMood).toBe(34);
      expect(getConfigErrorMessage(ConfigError.InvalidJson)).not.toBe(UNKNOWN_ERROR_MESSAGE);
      expect(getConfigErrorMessage(ConfigError.InvalidMood)).not.toBe(UNKNOWN_ERROR_MESSAGE);
    });
  });

  describe('arpeggio defaults', () => {
    it('exposes the style-decides sentinels by name', () => {
      expect(ArpeggioPattern.Auto).toBe(255);
      expect(ArpeggioSpeed.Auto).toBe(255);
      expect(ARPEGGIO_GATE_AUTO).toBe(-1);
    });

    it('defaults an empty config to the same arpeggio settings as the core', () => {
      const fromCore = createDefaultConfig(0);
      const fromEmptyJson = deserializeConfig('{}');

      expect(fromEmptyJson.arpeggioPattern).toBe(fromCore.arpeggioPattern);
      expect(fromEmptyJson.arpeggioSpeed).toBe(fromCore.arpeggioSpeed);
      expect(fromEmptyJson.arpeggioGate).toBe(fromCore.arpeggioGate);
      expect(fromCore.arpeggioPattern).toBe(ArpeggioPattern.Auto);
      expect(fromCore.arpeggioSpeed).toBe(ArpeggioSpeed.Auto);
      expect(fromCore.arpeggioGate).toBe(ARPEGGIO_GATE_AUTO);
    });

    it('accepts the sentinels as a valid config', () => {
      const config: SongConfig = {
        ...createDefaultConfig(0),
        arpeggioEnabled: true,
        arpeggioPattern: ArpeggioPattern.Auto,
        arpeggioSpeed: ArpeggioSpeed.Auto,
        arpeggioGate: ARPEGGIO_GATE_AUTO,
      };

      expect(validateConfig(config)).toBe(ConfigError.OK);
    });
  });

  describe('deserializeConfig', () => {
    it('defines every non-optional SongConfig property for an empty document', () => {
      const config = deserializeConfig('{}') as unknown as Record<string, unknown>;
      const undefinedFields = CONFIG_FIELDS.map(({ js }) => js).filter(
        (name) => config[name] === undefined,
      );

      expect(undefinedFields).toEqual([]);
      for (const name of ['arpeggioPattern', 'arpeggioSpeed', 'arpeggioGate'] as const) {
        expect(typeof config[name], `${name} must be a number`).toBe('number');
      }
    });

    it('round-trips the core default config without losing or changing a field', () => {
      for (const styleId of [0, 5, 14]) {
        const roundTripped = deserializeConfig(serializeConfig(createDefaultConfig(styleId)));
        expect(roundTripped, `style ${styleId}`).toEqual(createDefaultConfig(styleId));
      }
    });
  });
});
