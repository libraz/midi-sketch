#ifndef MIDISKETCH_CORE_PRESET_DATA_H
#define MIDISKETCH_CORE_PRESET_DATA_H

#include "core/types.h"
#include <cstdint>
#include <vector>

namespace midisketch {

// Number of available structure patterns
constexpr uint8_t STRUCTURE_COUNT = 11;

// Number of available mood presets
constexpr uint8_t MOOD_COUNT = 20;

// Number of available chord progressions (20 x 4-chord + 2 x 5-chord)
constexpr uint8_t CHORD_COUNT = 22;

// Number of available style presets
constexpr uint8_t STYLE_PRESET_COUNT = 17;

// Drum pattern style categories
enum class DrumStyle : uint8_t {
  Sparse,       // Ballad, Sentimental, Chill - minimal, half-time feel
  Standard,     // Pop patterns - 8th note hi-hats, 2&4 snare
  FourOnFloor,  // Dance/EDM - kick on every beat
  Upbeat,       // Energetic - syncopated, driving
  Rock,         // Rock patterns - crash accents, ride cymbal
  Synth         // Synth-oriented - tight 16th hi-hats, punchy kick
};

// Returns the default BPM for a given mood.
// @param mood Mood preset
// @returns Default BPM (60-180)
uint16_t getMoodDefaultBpm(Mood mood);

// Returns the note density parameter for a mood.
// @param mood Mood preset
// @returns Density value (0.0 - 1.0)
float getMoodDensity(Mood mood);

// Returns the name of a structure pattern.
// @param pattern Structure pattern
// @returns Pattern name (e.g., "StandardPop")
const char* getStructureName(StructurePattern pattern);

// Returns the name of a mood preset.
// @param mood Mood preset
// @returns Mood name (e.g., "straight_pop")
const char* getMoodName(Mood mood);

// Returns the drum style for a given mood.
// @param mood Mood preset
// @returns DrumStyle category
DrumStyle getMoodDrumStyle(Mood mood);

// ============================================================================
// StylePreset API (Phase 1)
// ============================================================================

// Returns the style preset for the given ID.
// @param style_id Style preset ID (0-2 for Phase 1)
// @returns Reference to StylePreset struct
const StylePreset& getStylePreset(uint8_t style_id);

// Returns form candidates compatible with a style.
// @param style_id Style preset ID
// @returns Vector of compatible StructurePattern values
std::vector<StructurePattern> getFormsByStyle(uint8_t style_id);

// Creates a default SongConfig from a style preset.
// @param style_id Style preset ID
// @returns SongConfig with style defaults
SongConfig createDefaultSongConfig(uint8_t style_id);

// Validation error codes.
enum class SongConfigError : uint8_t {
  OK = 0,
  InvalidStylePreset,
  InvalidChordProgression,
  InvalidForm,
  InvalidVocalAttitude,
  InvalidVocalRange,
  InvalidBpm
};

// Validates a SongConfig.
// @param config SongConfig to validate
// @returns Error code (OK if valid)
SongConfigError validateSongConfig(const SongConfig& config);

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_PRESET_DATA_H
