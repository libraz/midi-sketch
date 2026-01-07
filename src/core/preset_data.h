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

// Selects a random form compatible with a style using weighted probability.
// @param style_id Style preset ID
// @param seed Random seed
// @returns Selected StructurePattern
StructurePattern selectRandomForm(uint8_t style_id, uint32_t seed);

// Selects a random vocal style compatible with a style using weighted probability.
// Only used when VocalStylePreset::Auto is specified.
// @param style_id Style preset ID
// @param seed Random seed
// @returns Selected VocalStylePreset (never Auto or UltraVocaloid)
VocalStylePreset selectRandomVocalStyle(uint8_t style_id, uint32_t seed);

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
  InvalidBpm,
  DurationTooShortForCall,   // Duration too short for call settings
  InvalidModulationAmount,   // Modulation semitones out of range
  InvalidKey,                // Key out of range (0-11)
  InvalidCompositionStyle,   // CompositionStyle out of range (0-2)
  InvalidArpeggioPattern,    // ArpeggioPattern out of range (0-3)
  InvalidArpeggioSpeed,      // ArpeggioSpeed out of range (0-2)
  InvalidVocalStyle,         // VocalStylePreset out of range (0-12)
  InvalidMelodyTemplate,     // MelodyTemplateId out of range (0-7)
  InvalidMelodicComplexity,  // MelodicComplexity out of range (0-2)
  InvalidHookIntensity,      // HookIntensity out of range (0-3)
  InvalidVocalGroove,        // VocalGrooveFeel out of range (0-5)
  InvalidCallDensity,        // CallDensity out of range (0-3)
  InvalidIntroChant,         // IntroChant out of range (0-2)
  InvalidMixPattern,         // MixPattern out of range (0-2)
  InvalidMotifRepeatScope,   // MotifRepeatScope out of range (0-1)
  InvalidArrangementGrowth,  // ArrangementGrowth out of range (0-1)
  InvalidModulationTiming    // ModulationTiming out of range (0-4)
};

// Validates a SongConfig.
// @param config SongConfig to validate
// @returns Error code (OK if valid)
SongConfigError validateSongConfig(const SongConfig& config);

// ============================================================================
// Call System Functions
// ============================================================================

// Calculates the number of bars for an intro chant pattern based on BPM.
// The result is dynamically computed to ensure enough time for the chant.
// @param chant IntroChant pattern
// @param bpm BPM of the song
// @returns Number of bars (2-16)
uint8_t calcIntroChantBars(IntroChant chant, uint16_t bpm);

// Calculates the number of bars for a MIX pattern based on BPM.
// @param mix MixPattern
// @param bpm BPM of the song
// @returns Number of bars (2-12)
uint8_t calcMixPatternBars(MixPattern mix, uint16_t bpm);

// Calculates the minimum number of bars required for call settings.
// @param intro_chant IntroChant pattern
// @param mix_pattern MixPattern
// @param bpm BPM of the song
// @returns Minimum bars required
uint16_t getMinimumBarsForCall(IntroChant intro_chant, MixPattern mix_pattern, uint16_t bpm);

// Calculates the minimum duration in seconds for call settings.
// @param intro_chant IntroChant pattern
// @param mix_pattern MixPattern
// @param bpm BPM of the song
// @returns Minimum seconds required
uint16_t getMinimumSecondsForCall(IntroChant intro_chant, MixPattern mix_pattern, uint16_t bpm);

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_PRESET_DATA_H
