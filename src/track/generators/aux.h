/**
 * @file aux.h
 * @brief Aux track: sub-melodies and accent patterns.
 *
 * 8 functions: PulseLoop, TargetHint, GrooveAccent, PhraseTail,
 * EmotionalPad, Unison, MelodicHook, MotifCounter.
 */

#ifndef MIDISKETCH_TRACK_GENERATORS_AUX_H
#define MIDISKETCH_TRACK_GENERATORS_AUX_H

#include <optional>
#include <random>
#include <unordered_map>
#include <vector>

#include "core/midi_track.h"
#include "core/motif.h"
#include "core/pitch_utils.h"
#include "core/track_base.h"
#include "core/track_layer.h"
#include "core/types.h"
#include "track/vocal/vocal_analysis.h"

namespace midisketch {

class IHarmonyContext;
struct ChordProgression;

// ============================================================================
// Aux Enums and Types
// ============================================================================

/// Aux timing: Rhythmic=beat grid, Reactive=responds to melody, Sustained=long notes.
enum class AuxTimingRole : uint8_t {
  Rhythmic,  ///< Beat grid (PulseLoop, GrooveAccent)
  Reactive,  ///< Responds to melody (TargetHint, PhraseTail)
  Sustained  ///< Long notes (EmotionalPad)
};

/// Aux pitch strategy: ChordTone=safe, Target=anticipate, Following=echo, Accent=R/5,
/// Unison=double.
enum class AuxHarmonicRole : uint8_t {
  ChordTone,  ///< Chord tones only
  Target,     ///< Anticipate melody destination
  Following,  ///< Follow melody with delay
  Accent,     ///< Root/5th emphasis
  Unison      ///< Same pitch as melody
};

/// Harmony mode: UnisonOnly, ThirdAbove (Beatles style), ThirdBelow (R&B), Alternating.
enum class HarmonyMode : uint8_t {
  UnisonOnly,  ///< Same pitch as melody
  ThirdAbove,  ///< 3rd above melody
  ThirdBelow,  ///< 3rd below melody
  Alternating  ///< Alternate unison/harmony
};

/// How density_ratio works: EventProbability, SkipRatio, VoiceCount.
enum class AuxDensityBehavior : uint8_t {
  EventProbability,  ///< Probability of event
  SkipRatio,         ///< Skip rate
  VoiceCount         ///< Voice count multiplier
};

/// Meta information for AuxFunction.
struct AuxFunctionMeta {
  AuxTimingRole timing_role;            ///< When notes occur
  AuxHarmonicRole harmonic_role;        ///< How pitches are selected
  AuxDensityBehavior density_behavior;  ///< How density is interpreted
  float base_density;                   ///< Default density when ratio = 1.0
  float dissonance_tolerance;           ///< Higher = allow more dissonance (0.0-1.0)
};

/// Get meta information for AuxFunction.
const AuxFunctionMeta& getAuxFunctionMeta(AuxFunction func);

/// @brief Derivability score for melody-to-aux transformation.
///
/// Evaluates how suitable a melody is for deriving harmony parts,
/// unison doublings, or counter-melodies. Higher scores indicate
/// melodies that will produce better-sounding derived parts.
struct DerivabilityScore {
  float rhythm_stability;  ///< Consistent rhythm patterns (0.0-1.0)
  float contour_clarity;   ///< Clear melodic direction (0.0-1.0)
  float pitch_simplicity;  ///< Simple pitch relationships (0.0-1.0)

  /// @brief Check if melody is suitable for derivation.
  /// @returns true if all thresholds met
  bool canDerive() const {
    return rhythm_stability > 0.7f && contour_clarity > 0.6f && pitch_simplicity > 0.5f;
  }

  /// @brief Get overall derivability score.
  /// @returns Weighted average (0.0-1.0)
  float total() const {
    return rhythm_stability * 0.4f + contour_clarity * 0.35f + pitch_simplicity * 0.25f;
  }
};

/// @brief Analyze melody for derivability.
///
/// Examines rhythm regularity, melodic contour, and pitch complexity
/// to determine if the melody is suitable for generating harmony parts.
///
/// @param notes Melody notes to analyze
/// @returns DerivabilityScore with component scores
DerivabilityScore analyzeDerivability(const std::vector<NoteEvent>& notes);

/// Cache key for aux phrase reuse (repeated sections like Chorus1/Chorus2).
struct AuxCacheKey {
  AuxFunction function;      ///< Which aux function was used
  SectionType section_type;  ///< What section type (Verse, Chorus, etc.)
  uint8_t bars;              ///< Section length in bars

  bool operator==(const AuxCacheKey& other) const {
    return function == other.function && section_type == other.section_type && bars == other.bars;
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

// ============================================================================
// AuxGenerator Class (ITrackBase implementation)
// ============================================================================

/// Aux track generator. Functions: A=PulseLoop, B=TargetHint, C=GrooveAccent,
/// D=PhraseTail, E=EmotionalPad, F=Unison, G=MelodicHook, H=MotifCounter.
class AuxGenerator : public TrackBase {
 public:
  /// Context for aux generation.
  struct AuxContext {
    Tick section_start = 0;       ///< Absolute start tick of the section
    Tick section_end = 0;         ///< Absolute end tick of the section
    int8_t chord_degree = 0;      ///< Starting chord degree (0-based scale degree)
    int key_offset = 0;           ///< Key offset from C major (for transposition)
    uint8_t base_velocity = 100;  ///< Base MIDI velocity for notes
    TessituraRange main_tessitura = {60, 72, 66, 55, 77};  ///< Main melody's comfortable range
    const std::vector<NoteEvent>* main_melody = nullptr;   ///< Reference to main melody notes
    /// Phrase boundaries from vocal generation (for breath coordination)
    const std::vector<PhraseBoundary>* phrase_boundaries = nullptr;
    SectionType section_type =
        SectionType::A;  ///< Section type for cache key and pattern selection
    /// Vocal rest positions for call-and-response patterns (optional)
    const std::vector<Tick>* rest_positions = nullptr;
  };

  /// Full song context for complete aux track generation.
  struct SongContext {
    const std::vector<Section>* sections = nullptr;            ///< All sections in song
    const MidiTrack* vocal_track = nullptr;                    ///< Vocal track for analysis
    const ChordProgression* progression = nullptr;             ///< Chord progression
    VocalStylePreset vocal_style = VocalStylePreset::CityPop;  ///< For template selection
    uint8_t vocal_low = 60;                                    ///< Vocal range low
    uint8_t vocal_high = 72;                                   ///< Vocal range high
  };

  AuxGenerator() = default;
  ~AuxGenerator() override = default;

  // =========================================================================
  // ITrackBase interface
  // =========================================================================

  TrackRole getRole() const override { return TrackRole::Aux; }

  TrackPriority getDefaultPriority() const override { return TrackPriority::High; }

  PhysicalModel getPhysicalModel() const override { return PhysicalModels::kAuxVocal; }

  void generateSection(MidiTrack& track, const Section& section,
                       TrackContext& ctx) override;

  /// @brief Generate full aux track using FullTrackContext.
  void generateFullTrack(MidiTrack& track, const FullTrackContext& ctx) override;

  // =========================================================================
  // Aux Generation Methods
  // =========================================================================

  /// Generate complete aux track for entire song.
  /// Handles section iteration, motif caching, and post-processing.
  /// @param track Output track to populate
  /// @param song_ctx Song-level context
  /// @param harmony HarmonyContext for chord info and collision avoidance
  /// @param rng Random number generator
  void generateFromSongContext(MidiTrack& track, const SongContext& song_ctx,
                               IHarmonyContext& harmony, std::mt19937& rng);

  /// Generate aux track based on config (single section).
  MidiTrack generate(const AuxConfig& config, const AuxContext& ctx, IHarmonyContext& harmony,
                     std::mt19937& rng);

  /// A: Pulse Loop - hypnotic chord tone pattern (BLACKPINK "Ice Cream" style).
  std::vector<NoteEvent> generatePulseLoop(const AuxContext& ctx, const AuxConfig& config,
                                           const IHarmonyContext& harmony, std::mt19937& rng);

  /// B: Target Hint - anticipates melody destination (R&B style).
  std::vector<NoteEvent> generateTargetHint(const AuxContext& ctx, const AuxConfig& config,
                                            const IHarmonyContext& harmony, std::mt19937& rng);

  /// C: Groove Accent - root/5th emphasis on off-beats for groove.
  std::vector<NoteEvent> generateGrooveAccent(const AuxContext& ctx, const AuxConfig& config,
                                              const IHarmonyContext& harmony, std::mt19937& rng);

  /// D: Phrase Tail - fills gaps after vocal phrases (call-response).
  std::vector<NoteEvent> generatePhraseTail(const AuxContext& ctx, const AuxConfig& config,
                                            const IHarmonyContext& harmony, std::mt19937& rng);

  /// E: Emotional Pad - sustained chord tones for atmosphere.
  std::vector<NoteEvent> generateEmotionalPad(const AuxContext& ctx, const AuxConfig& config,
                                              const IHarmonyContext& harmony, std::mt19937& rng);

  /// F: Unison - doubles melody for power (use sparingly).
  std::vector<NoteEvent> generateUnison(const AuxContext& ctx, const AuxConfig& config,
                                        const IHarmonyContext& harmony, std::mt19937& rng);

  /// F+: Harmony - parallel 3rds/6ths above/below melody (Beatles style).
  std::vector<NoteEvent> generateHarmony(const AuxContext& ctx, const AuxConfig& config,
                                         const IHarmonyContext& harmony, HarmonyMode mode,
                                         std::mt19937& rng);

  /// G: Melodic Hook - iconic intro riff (AKB48 "Fortune Cookie" style).
  std::vector<NoteEvent> generateMelodicHook(const AuxContext& ctx, const AuxConfig& config,
                                             const IHarmonyContext& harmony, std::mt19937& rng);

  /// H: Motif Counter - counter-melody using contrary motion/rhythmic complement.
  std::vector<NoteEvent> generateMotifCounter(const AuxContext& ctx, const AuxConfig& config,
                                              const IHarmonyContext& harmony,
                                              const VocalAnalysis& vocal_analysis,
                                              std::mt19937& rng);

  /// I: Sustain Pad - whole-note chord tone pads for Ballad/Sentimental moods.
  ///
  /// Generates sustained whole-note pads using chord tones.
  /// Designed for emotional ballad sections where a warm sustained layer
  /// adds depth without being intrusive.
  ///
  /// @param ctx Section context
  /// @param config Aux configuration
  /// @param harmony HarmonyContext for chord information
  /// @param rng Random number generator
  /// @return Vector of sustained pad notes
  std::vector<NoteEvent> generateSustainPad(const AuxContext& ctx, const AuxConfig& config,
                                            const IHarmonyContext& harmony, std::mt19937& rng);

  void clearCache() { phrase_cache_.clear(); }

 private:
  void calculateAuxRange(const AuxConfig& config, const TessituraRange& main_tessitura,
                         uint8_t& out_low, uint8_t& out_high);
  bool isPitchSafe(uint8_t pitch, Tick start, Tick duration,
                   const std::vector<NoteEvent>* main_melody, const IHarmonyContext& harmony,
                   float dissonance_tolerance = 0.0f);
  uint8_t resolveAuxPitch(uint8_t desired, Tick start, Tick duration,
                          const std::vector<NoteEvent>* main_melody, const IHarmonyContext& harmony,
                          uint8_t low, uint8_t high, int8_t chord_degree,
                          float dissonance_tolerance = 0.0f);
  std::vector<Tick> findBreathPointsInRange(const std::vector<PhraseBoundary>* boundaries,
                                            Tick start, Tick end);

  /// Post-process notes: resolve chord crossings, fix bass clashes.
  void postProcessNotes(std::vector<NoteEvent>& notes, IHarmonyContext& harmony);

  /// First pass: resolve notes that sustain over chord changes.
  /// Handles anticipation, note splitting, or pitch adjustment at chord boundaries.
  void resolveNotesOverChordBoundary(std::vector<NoteEvent>& notes,
                                     std::vector<NoteEvent>& notes_to_add,
                                     IHarmonyContext& harmony);

  /// Second pass: fix remaining clashes with other harmonic tracks.
  /// Finds safe pitch alternatives for notes that clash with Bass, Chord, etc.
  void resolvePitchClashes(std::vector<NoteEvent>& notes, IHarmonyContext& harmony);

  std::unordered_map<AuxCacheKey, CachedAuxPhrase, AuxCacheKeyHash> phrase_cache_;
  std::optional<Motif> cached_chorus_motif_;  ///< Chorus motif for intro placement
};

// ============================================================================
// Standalone Generation Functions (backward compatibility)
// ============================================================================

/// Generate aux track using legacy interface.
/// @param track Output track to populate
/// @param song_ctx Song-level context
/// @param harmony HarmonyContext for chord info and collision avoidance
/// @param rng Random number generator
void generateAuxTrack(MidiTrack& track, const AuxGenerator::SongContext& song_ctx,
                      IHarmonyContext& harmony, std::mt19937& rng);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_GENERATORS_AUX_H
