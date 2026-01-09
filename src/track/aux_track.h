/**
 * @file aux_track.h
 * @brief Aux track: sub-melodies and accent patterns.
 *
 * 8 functions: PulseLoop, TargetHint, GrooveAccent, PhraseTail,
 * EmotionalPad, Unison, MelodicHook, MotifCounter.
 */

#ifndef MIDISKETCH_TRACK_AUX_TRACK_H
#define MIDISKETCH_TRACK_AUX_TRACK_H

#include "core/midi_track.h"
#include "core/pitch_utils.h"
#include "core/track_layer.h"
#include "core/types.h"
#include "track/vocal_analysis.h"
#include <random>
#include <unordered_map>
#include <vector>

namespace midisketch {

class HarmonyContext;

/// Aux timing: Rhythmic=beat grid, Reactive=responds to melody, Sustained=long notes.
enum class AuxTimingRole : uint8_t {
  Rhythmic,    ///< Beat grid (PulseLoop, GrooveAccent)
  Reactive,    ///< Responds to melody (TargetHint, PhraseTail)
  Sustained    ///< Long notes (EmotionalPad)
};

/// Aux pitch strategy: ChordTone=safe, Target=anticipate, Following=echo, Accent=R/5, Unison=double.
enum class AuxHarmonicRole : uint8_t {
  ChordTone,   ///< Chord tones only
  Target,      ///< Anticipate melody destination
  Following,   ///< Follow melody with delay
  Accent,      ///< Root/5th emphasis
  Unison       ///< Same pitch as melody
};

/// Harmony mode: UnisonOnly, ThirdAbove (Beatles style), ThirdBelow (R&B), Alternating.
enum class HarmonyMode : uint8_t {
  UnisonOnly,      ///< Same pitch as melody
  ThirdAbove,      ///< 3rd above melody
  ThirdBelow,      ///< 3rd below melody
  Alternating      ///< Alternate unison/harmony
};

/// How density_ratio works: EventProbability, SkipRatio, VoiceCount.
enum class AuxDensityBehavior : uint8_t {
  EventProbability,  ///< Probability of event
  SkipRatio,         ///< Skip rate
  VoiceCount         ///< Voice count multiplier
};

/// Meta information for AuxFunction.
struct AuxFunctionMeta {
  AuxTimingRole timing_role;        ///< When notes occur
  AuxHarmonicRole harmonic_role;    ///< How pitches are selected
  AuxDensityBehavior density_behavior;  ///< How density is interpreted
  float base_density;               ///< Default density when ratio = 1.0
  float dissonance_tolerance;       ///< Higher = allow more dissonance (0.0-1.0)
};

/// Get meta information for AuxFunction.
const AuxFunctionMeta& getAuxFunctionMeta(AuxFunction func);

/// Cache key for aux phrase reuse (repeated sections like Chorus1/Chorus2).
struct AuxCacheKey {
  AuxFunction function;       ///< Which aux function was used
  SectionType section_type;   ///< What section type (Verse, Chorus, etc.)
  uint8_t bars;               ///< Section length in bars

  bool operator==(const AuxCacheKey& other) const {
    return function == other.function &&
           section_type == other.section_type &&
           bars == other.bars;
  }
};

struct AuxCacheKeyHash {
  size_t operator()(const AuxCacheKey& key) const {
    return std::hash<uint8_t>()(static_cast<uint8_t>(key.function)) ^
           (std::hash<uint8_t>()(static_cast<uint8_t>(key.section_type)) << 4) ^
           (std::hash<uint8_t>()(key.bars) << 8);
  }
};

/// Cached aux phrase with section-relative timing for reuse.
struct CachedAuxPhrase {
  std::vector<NoteEvent> notes;  ///< Notes with section-relative timing
  uint8_t bars;                  ///< Section length when cached
  int reuse_count = 0;           ///< How many times this phrase was reused
};

/// Aux track generator. Functions: A=PulseLoop, B=TargetHint, C=GrooveAccent,
/// D=PhraseTail, E=EmotionalPad, F=Unison, G=MelodicHook, H=MotifCounter.
class AuxTrackGenerator {
 public:
  /// Context for aux generation.
  struct AuxContext {
    Tick section_start;         ///< Absolute start tick of the section
    Tick section_end;           ///< Absolute end tick of the section
    int8_t chord_degree;        ///< Starting chord degree (0-based scale degree)
    int key_offset;             ///< Key offset from C major (for transposition)
    uint8_t base_velocity;      ///< Base MIDI velocity for notes
    TessituraRange main_tessitura;  ///< Main melody's comfortable range
    const std::vector<NoteEvent>* main_melody;  ///< Reference to main melody notes
    /// Phrase boundaries from vocal generation (for breath coordination)
    const std::vector<PhraseBoundary>* phrase_boundaries;
    SectionType section_type;   ///< Section type for cache key and pattern selection
  };

  AuxTrackGenerator() = default;

  /// Generate aux track based on config.
  MidiTrack generate(const AuxConfig& config,
                     const AuxContext& ctx,
                     const HarmonyContext& harmony,
                     std::mt19937& rng);

  /// A: Pulse Loop - hypnotic chord tone pattern (BLACKPINK "Ice Cream" style).
  std::vector<NoteEvent> generatePulseLoop(
      const AuxContext& ctx,
      const AuxConfig& config,
      const HarmonyContext& harmony,
      std::mt19937& rng);

  /// B: Target Hint - anticipates melody destination (R&B style).
  std::vector<NoteEvent> generateTargetHint(
      const AuxContext& ctx,
      const AuxConfig& config,
      const HarmonyContext& harmony,
      std::mt19937& rng);

  /// C: Groove Accent - root/5th emphasis on off-beats for groove.
  std::vector<NoteEvent> generateGrooveAccent(
      const AuxContext& ctx,
      const AuxConfig& config,
      const HarmonyContext& harmony,
      std::mt19937& rng);

  /// D: Phrase Tail - fills gaps after vocal phrases (call-response).
  std::vector<NoteEvent> generatePhraseTail(
      const AuxContext& ctx,
      const AuxConfig& config,
      const HarmonyContext& harmony,
      std::mt19937& rng);

  /// E: Emotional Pad - sustained chord tones for atmosphere.
  std::vector<NoteEvent> generateEmotionalPad(
      const AuxContext& ctx,
      const AuxConfig& config,
      const HarmonyContext& harmony,
      std::mt19937& rng);

  /// F: Unison - doubles melody for power (use sparingly).
  std::vector<NoteEvent> generateUnison(
      const AuxContext& ctx,
      const AuxConfig& config,
      const HarmonyContext& harmony,
      std::mt19937& rng);

  /// F+: Harmony - parallel 3rds/6ths above/below melody (Beatles style).
  std::vector<NoteEvent> generateHarmony(
      const AuxContext& ctx,
      const AuxConfig& config,
      const HarmonyContext& harmony,
      HarmonyMode mode,
      std::mt19937& rng);

  /// G: Melodic Hook - iconic intro riff (AKB48 "Fortune Cookie" style).
  std::vector<NoteEvent> generateMelodicHook(
      const AuxContext& ctx,
      const AuxConfig& config,
      const HarmonyContext& harmony,
      std::mt19937& rng);

  /// H: Motif Counter - counter-melody using contrary motion/rhythmic complement.
  std::vector<NoteEvent> generateMotifCounter(
      const AuxContext& ctx,
      const AuxConfig& config,
      const HarmonyContext& harmony,
      const VocalAnalysis& vocal_analysis,
      std::mt19937& rng);

  void clearCache() { phrase_cache_.clear(); }

 private:
  void calculateAuxRange(const AuxConfig& config, const TessituraRange& main_tessitura,
                         uint8_t& out_low, uint8_t& out_high);
  bool isPitchSafe(uint8_t pitch, Tick start, Tick duration,
                   const std::vector<NoteEvent>* main_melody, const HarmonyContext& harmony,
                   float dissonance_tolerance = 0.0f);
  uint8_t getSafePitch(uint8_t desired, Tick start, Tick duration,
                       const std::vector<NoteEvent>* main_melody, const HarmonyContext& harmony,
                       uint8_t low, uint8_t high, int8_t chord_degree, float dissonance_tolerance = 0.0f);
  std::vector<Tick> findBreathPointsInRange(const std::vector<PhraseBoundary>* boundaries, Tick start, Tick end);

  std::unordered_map<AuxCacheKey, CachedAuxPhrase, AuxCacheKeyHash> phrase_cache_;
};

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_AUX_TRACK_H
