#ifndef MIDISKETCH_TRACK_AUX_TRACK_H
#define MIDISKETCH_TRACK_AUX_TRACK_H

#include "core/midi_track.h"
#include "core/pitch_utils.h"
#include "core/track_layer.h"
#include "core/types.h"
#include <random>
#include <unordered_map>
#include <vector>

namespace midisketch {

class HarmonyContext;

// ============================================================================
// Phase 2: AuxFunction Meta Information (A1)
// ============================================================================

// Timing role describes when aux notes typically occur.
enum class AuxTimingRole : uint8_t {
  Rhythmic,    // PulseLoop, GrooveAccent: tied to beat grid
  Reactive,    // TargetHint, PhraseTail: responds to main melody
  Sustained    // EmotionalPad: long held notes
};

// Harmonic role describes pitch selection strategy.
enum class AuxHarmonicRole : uint8_t {
  ChordTone,   // PulseLoop, EmotionalPad: chord tones only
  Target,      // TargetHint: anticipate melody targets
  Following,   // PhraseTail: follow melody pitch
  Accent,      // GrooveAccent: root/fifth emphasis
  Unison       // Unison: exact same pitch as main melody
};

// Harmony mode for unison/harmony switching.
enum class HarmonyMode : uint8_t {
  UnisonOnly,      // Always unison (same pitch)
  ThirdAbove,      // Harmony 3rd above
  ThirdBelow,      // Harmony 3rd below
  Alternating      // Alternate between unison and harmony
};

// Density behavior describes how density_ratio is interpreted.
enum class AuxDensityBehavior : uint8_t {
  EventProbability,  // Each potential event has density_ratio chance
  SkipRatio,         // Skip notes at density_ratio rate
  VoiceCount         // Multiply voice count by density_ratio
};

// Meta information for each AuxFunction.
struct AuxFunctionMeta {
  AuxTimingRole timing_role;
  AuxHarmonicRole harmonic_role;
  AuxDensityBehavior density_behavior;
  float base_density;           // Default density when density_ratio = 1.0
  float dissonance_tolerance;   // A7: Higher = allow more dissonance (0.0-1.0)
};

// Get meta information for an AuxFunction.
const AuxFunctionMeta& getAuxFunctionMeta(AuxFunction func);

// ============================================================================
// Phase 2: Aux Cache Key (A3)
// ============================================================================

// Cache key for aux phrase reuse (similar to Vocal's PhraseCacheKey).
struct AuxCacheKey {
  AuxFunction function;
  SectionType section_type;
  uint8_t bars;

  bool operator==(const AuxCacheKey& other) const {
    return function == other.function &&
           section_type == other.section_type &&
           bars == other.bars;
  }
};

// Hash function for AuxCacheKey.
struct AuxCacheKeyHash {
  size_t operator()(const AuxCacheKey& key) const {
    return std::hash<uint8_t>()(static_cast<uint8_t>(key.function)) ^
           (std::hash<uint8_t>()(static_cast<uint8_t>(key.section_type)) << 4) ^
           (std::hash<uint8_t>()(key.bars) << 8);
  }
};

// Cached aux phrase (relative timing).
struct CachedAuxPhrase {
  std::vector<NoteEvent> notes;  // Relative timing from section start
  uint8_t bars;
  int reuse_count = 0;
};

// AuxTrackGenerator generates auxiliary sub-melody tracks.
// Provides 5 different functions to complement the main melody.
class AuxTrackGenerator {
 public:
  // Context for aux track generation.
  struct AuxContext {
    Tick section_start;
    Tick section_end;
    int8_t chord_degree;
    int key_offset;
    uint8_t base_velocity;
    TessituraRange main_tessitura;  // Main melody's tessitura
    const std::vector<NoteEvent>* main_melody;  // Reference to main melody
    // A4: Vocal breath coordination
    const std::vector<PhraseBoundary>* phrase_boundaries;  // From vocal generation
    SectionType section_type;  // For cache key
  };

  AuxTrackGenerator() = default;

  // Generate complete aux track based on configuration.
  // @param config Aux track configuration
  // @param ctx Aux context
  // @param harmony Harmony context for collision avoidance
  // @param rng Random number generator
  // @returns MidiTrack with aux notes
  MidiTrack generate(const AuxConfig& config,
                     const AuxContext& ctx,
                     const HarmonyContext& harmony,
                     std::mt19937& rng);

  // A: Pulse Loop - Addictive repetition pattern (Ice Cream style)
  // Creates short repeating pattern that complements the rhythm.
  // @param ctx Aux context
  // @param config Configuration
  // @param harmony Harmony context
  // @param rng Random number generator
  // @returns Vector of note events
  std::vector<NoteEvent> generatePulseLoop(
      const AuxContext& ctx,
      const AuxConfig& config,
      const HarmonyContext& harmony,
      std::mt19937& rng);

  // B: Target Hint - Hints at main melody destination
  // Plays chord tones that anticipate where the melody is heading.
  // @param ctx Aux context
  // @param config Configuration
  // @param harmony Harmony context
  // @param rng Random number generator
  // @returns Vector of note events
  std::vector<NoteEvent> generateTargetHint(
      const AuxContext& ctx,
      const AuxConfig& config,
      const HarmonyContext& harmony,
      std::mt19937& rng);

  // C: Groove Accent - Physical groove accent
  // Adds rhythmic accents that emphasize the groove.
  // @param ctx Aux context
  // @param config Configuration
  // @param harmony Harmony context
  // @param rng Random number generator
  // @returns Vector of note events
  std::vector<NoteEvent> generateGrooveAccent(
      const AuxContext& ctx,
      const AuxConfig& config,
      const HarmonyContext& harmony,
      std::mt19937& rng);

  // D: Phrase Tail - Phrase ending, breathing
  // Adds notes at phrase endings for smoothness.
  // @param ctx Aux context
  // @param config Configuration
  // @param harmony Harmony context
  // @param rng Random number generator
  // @returns Vector of note events
  std::vector<NoteEvent> generatePhraseTail(
      const AuxContext& ctx,
      const AuxConfig& config,
      const HarmonyContext& harmony,
      std::mt19937& rng);

  // E: Emotional Pad - Emotional floor/pad
  // Creates sustained tones that provide emotional foundation.
  // @param ctx Aux context
  // @param config Configuration
  // @param harmony Harmony context
  // @param rng Random number generator
  // @returns Vector of note events
  std::vector<NoteEvent> generateEmotionalPad(
      const AuxContext& ctx,
      const AuxConfig& config,
      const HarmonyContext& harmony,
      std::mt19937& rng);

  // F: Unison - Doubles the main melody
  // Creates a slightly offset copy of the main melody for doubling effect.
  // @param ctx Aux context
  // @param config Configuration
  // @param harmony Harmony context
  // @param rng Random number generator
  // @returns Vector of note events
  std::vector<NoteEvent> generateUnison(
      const AuxContext& ctx,
      const AuxConfig& config,
      const HarmonyContext& harmony,
      std::mt19937& rng);

  // F+: Harmony - Creates harmony line based on main melody
  // Generates harmony (3rd above/below) based on the main melody.
  // @param ctx Aux context
  // @param config Configuration
  // @param harmony Harmony context
  // @param mode Harmony mode (third above/below/alternating)
  // @param rng Random number generator
  // @returns Vector of note events
  std::vector<NoteEvent> generateHarmony(
      const AuxContext& ctx,
      const AuxConfig& config,
      const HarmonyContext& harmony,
      HarmonyMode mode,
      std::mt19937& rng);

  // G: Melodic Hook - Creates memorable hook phrase
  // Generates a repeating melodic hook (Fortune Cookie intro style).
  // @param ctx Aux context
  // @param config Configuration
  // @param harmony Harmony context
  // @param rng Random number generator
  // @returns Vector of note events
  std::vector<NoteEvent> generateMelodicHook(
      const AuxContext& ctx,
      const AuxConfig& config,
      const HarmonyContext& harmony,
      std::mt19937& rng);

  // Clear phrase cache (call at start of song generation).
  void clearCache() { phrase_cache_.clear(); }

 private:
  // Calculate aux range based on config offset and main tessitura.
  void calculateAuxRange(const AuxConfig& config,
                         const TessituraRange& main_tessitura,
                         uint8_t& out_low, uint8_t& out_high);

  // Check if pitch is safe (doesn't clash with main melody).
  // A7: Uses function-specific dissonance tolerance.
  bool isPitchSafe(uint8_t pitch, Tick start, Tick duration,
                   const std::vector<NoteEvent>* main_melody,
                   const HarmonyContext& harmony,
                   float dissonance_tolerance = 0.0f);

  // Get safe pitch that doesn't clash.
  uint8_t getSafePitch(uint8_t desired, Tick start, Tick duration,
                       const std::vector<NoteEvent>* main_melody,
                       const HarmonyContext& harmony,
                       uint8_t low, uint8_t high,
                       int8_t chord_degree,
                       float dissonance_tolerance = 0.0f);

  // A4: Find phrase boundaries within a time range.
  std::vector<Tick> findBreathPointsInRange(
      const std::vector<PhraseBoundary>* boundaries,
      Tick start, Tick end);

  // A3: Phrase cache for section repetition.
  std::unordered_map<AuxCacheKey, CachedAuxPhrase, AuxCacheKeyHash> phrase_cache_;
};

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_AUX_TRACK_H
