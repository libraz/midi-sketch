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
  /// @brief Conversion result including call/modulation settings.
  struct ConversionResult {
    GeneratorParams params;                                    ///< Converted params
    bool se_enabled = true;                                    ///< SE track enabled
    bool call_enabled = false;                                 ///< Call enabled
    bool call_notes_enabled = true;                            ///< Call as notes
    IntroChant intro_chant = IntroChant::None;                 ///< Intro chant
    MixPattern mix_pattern = MixPattern::None;                 ///< MIX pattern
    CallDensity call_density = CallDensity::Standard;          ///< Call density
    ModulationTiming modulation_timing = ModulationTiming::None;  ///< Modulation timing
    int8_t modulation_semitones = 2;                           ///< Modulation amount
  };

  /**
   * @brief Convert SongConfig to GeneratorParams.
   * @param config Source configuration
   * @return ConversionResult with params and settings
   */
  static ConversionResult convert(const SongConfig& config);

  /**
   * @brief Apply VocalStylePreset to melody parameters.
   * @param params Parameters to modify
   * @param config Configuration with style settings
   */
  static void applyVocalStylePreset(GeneratorParams& params,
                                     const SongConfig& config);

  /**
   * @brief Apply MelodicComplexity to melody parameters.
   * @param params Parameters to modify
   */
  static void applyMelodicComplexity(GeneratorParams& params);
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_CONFIG_CONVERTER_H
