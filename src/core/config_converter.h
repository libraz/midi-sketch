#ifndef MIDISKETCH_CORE_CONFIG_CONVERTER_H
#define MIDISKETCH_CORE_CONFIG_CONVERTER_H

#include "core/types.h"

namespace midisketch {

// Converts SongConfig (new API) to GeneratorParams (internal representation).
// Handles style preset lookups, random form selection, and vocal style mapping.
class ConfigConverter {
 public:
  // Conversion result including call/modulation settings.
  struct ConversionResult {
    GeneratorParams params;

    // Call system settings
    bool se_enabled = true;
    bool call_enabled = false;
    bool call_notes_enabled = true;
    IntroChant intro_chant = IntroChant::None;
    MixPattern mix_pattern = MixPattern::None;
    CallDensity call_density = CallDensity::Standard;

    // Modulation settings
    ModulationTiming modulation_timing = ModulationTiming::None;
    int8_t modulation_semitones = 2;
  };

  // Converts SongConfig to GeneratorParams with all associated settings.
  // @param config Source configuration
  // @returns ConversionResult with params and settings
  static ConversionResult convert(const SongConfig& config);

  // Apply VocalStylePreset settings to melody parameters.
  // Public for use by Generator::regenerateMelody.
  static void applyVocalStylePreset(GeneratorParams& params,
                                     const SongConfig& config);

  // Apply MelodicComplexity settings to melody parameters.
  // Public for use by Generator::regenerateMelody.
  static void applyMelodicComplexity(GeneratorParams& params);
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_CONFIG_CONVERTER_H
