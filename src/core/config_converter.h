/**
 * @file config_converter.h
 * @brief Converts SongConfig to GeneratorParams.
 */

#ifndef MIDISKETCH_CORE_CONFIG_CONVERTER_H
#define MIDISKETCH_CORE_CONFIG_CONVERTER_H

#include "core/types.h"

namespace midisketch {

/// @brief Converts SongConfig to GeneratorParams (internal representation).
class ConfigConverter {
 public:
  /**
   * @brief Convert SongConfig to GeneratorParams.
   *
   * All settings including call/SE and modulation are stored directly
   * in GeneratorParams for single source of truth.
   *
   * @param config Source configuration
   * @return Converted GeneratorParams
   */
  static GeneratorParams convert(const SongConfig& config);

  /**
   * @brief Apply VocalStylePreset to melody parameters.
   * @param params Parameters to modify
   * @param config Configuration with style settings
   */
  static void applyVocalStylePreset(GeneratorParams& params, const SongConfig& config);

  /**
   * @brief Apply MelodicComplexity to melody parameters.
   * @param params Parameters to modify
   */
  static void applyMelodicComplexity(GeneratorParams& params);
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_CONFIG_CONVERTER_H
