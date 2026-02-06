/**
 * Configuration utilities for SongConfig
 */

import { deserializeConfig, serializeConfig } from './config-fields';
import type { ConfigErrorCode } from './constants';
import { getApi } from './internal';
import type { SongConfig } from './types';

/**
 * Create a default song config for a style (JSON API)
 */
export function createDefaultConfig(styleId: number): SongConfig {
  const a = getApi();
  const json = a.createDefaultConfigJson(styleId);
  return deserializeConfig(json);
}

/**
 * Validate a song config before generation (JSON API).
 * Returns the error code (0 = OK, non-zero = error).
 * Use getConfigErrorMessage() to get human-readable error message.
 */
export function validateConfig(config: SongConfig): ConfigErrorCode {
  const a = getApi();
  const json = serializeConfig(config);
  return a.validateConfigJson(json, json.length) as ConfigErrorCode;
}

/**
 * Get human-readable error message for a config error code.
 */
export function getConfigErrorMessage(errorCode: ConfigErrorCode): string {
  const a = getApi();
  return a.configErrorString(errorCode);
}
