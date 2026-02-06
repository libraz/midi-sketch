/**
 * @file preset_data.h
 * @brief Preset data accessors for moods and styles.
 */

#ifndef MIDISKETCH_CORE_PRESET_DATA_H
#define MIDISKETCH_CORE_PRESET_DATA_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/types.h"

namespace midisketch {

// Number of available structure patterns
constexpr uint8_t STRUCTURE_COUNT = 18;

// Number of available mood presets
constexpr uint8_t MOOD_COUNT = 24;

// Number of available chord progressions (20 x 4-chord + 2 x 5-chord)
constexpr uint8_t CHORD_COUNT = 22;

// Number of available style presets
constexpr uint8_t STYLE_PRESET_COUNT = 17;

// Number of available vocal style presets (Auto + 13 styles)
constexpr uint8_t VOCAL_STYLE_PRESET_COUNT = 14;

// Drum pattern style categories
enum class DrumStyle : uint8_t {
  Sparse,       // Ballad, Sentimental, Chill - minimal, half-time feel
  Standard,     // Pop patterns - 8th note hi-hats, 2&4 snare
  FourOnFloor,  // Dance/EDM - kick on every beat
  Upbeat,       // Energetic - syncopated, driving
  Rock,         // Rock patterns - crash accents, ride cymbal
  Synth,        // Synth-oriented - tight 16th hi-hats, punchy kick
  Trap,         // Trap - half-time snare (beat 3), hi-hat rolls
  Latin         // Latin - dembow rhythm (characteristic kick-snare pattern)
};

// Drum groove feel for hi-hat timing
// Swing delays off-beat notes by ~33% of the subdivision (triplet feel)
// Shuffle delays by ~50% (heavy swing)
enum class DrumGrooveFeel : uint8_t {
  Straight,  // Default - even timing
  Swing,     // Triplet swing (~33% delay on off-beats) - ballad, jazz, city pop
  Shuffle    // Heavy shuffle (~50% delay) - blues, some rock
};

// ============================================================================
// Bass Genre System
// ============================================================================
//
// Genre classification for bass pattern selection.
// Each genre defines preferred patterns for each section type.
//
enum class BassGenre : uint8_t {
  Standard,    // Default pop patterns
  Ballad,      // Slow, sustained (WholeNote, RootFifth)
  Rock,        // Aggressive, power-driven (PowerDrive, Aggressive)
  Dance,       // High-energy (Aggressive, OctaveJump)
  Electronic,  // Sidechain pulse, modern EDM (SidechainPulse)
  Jazz,        // Walking bass, groove (Groove, Walking)
  Idol,        // Bright, energetic (Driving, OctaveJump)
  RnB,         // R&B/Neo-Soul (Groove, chromatic approach, rootless voicing)
  Latin,       // Latin (Tresillo 3+3+2 pattern)
  Trap808,     // Trap (long sustained 808 sub-bass)
  Lofi,        // Lo-fi (simple patterns, heavy swing, low velocity)
  COUNT
};

// Bass pattern types (forward declaration for BassGenrePatterns)
// Full enum defined in bass.cpp - values must match
enum class BassPatternId : uint8_t {
  WholeNote = 0,
  RootFifth = 1,
  Syncopated = 2,
  Driving = 3,
  RhythmicDrive = 4,
  Walking = 5,
  PowerDrive = 6,
  Aggressive = 7,
  SidechainPulse = 8,
  Groove = 9,
  OctaveJump = 10,
  PedalTone = 11,
  Tresillo = 12,     // Latin 3+3+2 pattern
  SubBass808 = 13    // Trap long sustained 808 sub-bass
};

// Section indices for bass pattern table
enum class BassSection : uint8_t {
  Intro = 0,  // Also used for Interlude
  A = 1,      // Verse
  B = 2,      // Pre-chorus
  Chorus = 3,
  Bridge = 4,
  Outro = 5,
  Mix = 6,  // MixBreak
  COUNT = 7
};

// Pattern selection weights for a section
// First pattern is primary (60%), second is secondary (30%), third is rare (10%)
struct BassPatternChoice {
  BassPatternId primary;
  BassPatternId secondary;
  BassPatternId tertiary;
};

// Bass patterns for all sections of a genre
struct BassGenrePatterns {
  BassPatternChoice sections[static_cast<int>(BassSection::COUNT)];
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

// Find a mood by its name (case-insensitive).
// @param name Mood name to search for
// @returns Mood enum value if found, or std::nullopt if not found
std::optional<Mood> findMoodByName(const std::string& name);

// Find a structure pattern by its name (case-insensitive).
// @param name Structure pattern name to search for
// @returns StructurePattern enum value if found, or std::nullopt if not found
std::optional<StructurePattern> findStructurePatternByName(const std::string& name);

// Find a chord progression by its name (case-insensitive).
// @param name Chord progression name to search for
// @returns Chord progression ID if found, or std::nullopt if not found
std::optional<uint8_t> findChordProgressionByName(const std::string& name);

// Returns the drum style for a given mood.
// @param mood Mood preset
// @returns DrumStyle category
DrumStyle getMoodDrumStyle(Mood mood);

// Returns the drum groove feel for a given mood.
// @param mood Mood preset
// @returns DrumGrooveFeel (Straight, Swing, or Shuffle)
DrumGrooveFeel getMoodDrumGrooveFeel(Mood mood);

// Returns the bass genre for a given mood.
// @param mood Mood preset
// @returns BassGenre category
BassGenre getMoodBassGenre(Mood mood);

// Returns the bass genre patterns for a given genre.
// @param genre BassGenre category
// @returns Reference to BassGenrePatterns struct
const BassGenrePatterns& getBassGenrePatterns(BassGenre genre);

// ============================================================================
// Mood Program Mapping
// ============================================================================

// MIDI program numbers for each track based on mood.
// Maps each Mood to appropriate instrument sounds for all melodic tracks.
struct MoodProgramSet {
  uint8_t vocal;
  uint8_t chord;
  uint8_t bass;
  uint8_t motif;
  uint8_t arpeggio;
  uint8_t aux;
  uint8_t guitar;  ///< 0xFF = Silent (guitar disabled for this mood)
};

// Returns the MIDI programs for a given mood.
// @param mood Mood preset
// @returns MoodProgramSet with program numbers for each track
const MoodProgramSet& getMoodPrograms(Mood mood);

// ============================================================================
// StylePreset API
// ============================================================================

// Returns the style preset for the given ID.
// @param style_id Style preset ID
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
// VocalStylePreset Data Table
// ============================================================================

// Vocal style preset parameter data for table-driven configuration.
// Used by ConfigConverter::applyVocalStylePreset() instead of switch-case.
struct VocalStylePresetData {
  VocalStylePreset id;

  // Basic parameters
  uint8_t max_leap_interval;  // Max leap in semitones (5-14)
  float syncopation_prob;     // Syncopation probability (0.0-0.5)
  bool allow_bar_crossing;    // Allow notes to cross bar lines

  // Section density modifiers (multiplied with template density)
  float verse_density_modifier;      // Verse (A) density modifier
  float prechorus_density_modifier;  // Pre-chorus (B) density modifier
  float chorus_density_modifier;     // Chorus density modifier
  float bridge_density_modifier;     // Bridge density modifier

  // Section-specific 32nd note ratios (for UltraVocaloid style)
  float verse_thirtysecond_ratio;      // Verse 32nd note ratio
  float prechorus_thirtysecond_ratio;  // Pre-chorus 32nd note ratio
  float chorus_thirtysecond_ratio;     // Chorus 32nd note ratio
  float bridge_thirtysecond_ratio;     // Bridge 32nd note ratio

  // Additional parameters
  float consecutive_same_note_prob;  // Same-note repetition probability (0.0-1.0)
  bool disable_vowel_constraints;    // Disable vowel section limits
  bool hook_repetition;              // Enable hook repetition in chorus
  bool chorus_long_tones;            // Use long sustained tones in chorus
  int8_t chorus_register_shift;      // Chorus register shift (semitones)
  float tension_usage;               // Tension usage probability (0.0-1.0)
};

// Returns the vocal style preset data for the given style.
// @param style VocalStylePreset value
// @returns Reference to VocalStylePresetData struct
const VocalStylePresetData& getVocalStylePresetData(VocalStylePreset style);

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
