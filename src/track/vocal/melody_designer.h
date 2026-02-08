/**
 * @file melody_designer.h
 * @brief Template-driven melody generation with music theory constraints.
 */

#ifndef MIDISKETCH_TRACK_VOCAL_MELODY_DESIGNER_H
#define MIDISKETCH_TRACK_VOCAL_MELODY_DESIGNER_H

#include <array>
#include <optional>
#include <random>
#include <vector>

#include <unordered_map>

#include "core/chord_utils.h"
#include "core/melody_evaluator.h"
#include "core/melody_templates.h"
#include "core/melody_types.h"
#include "core/motif.h"
#include "core/motif_types.h"
#include "core/motif_transform.h"
#include "core/pitch_utils.h"
#include "core/section_types.h"
#include "core/types.h"

namespace midisketch {

class IHarmonyContext;

/// @brief Context information for breath calculation.
///
/// Provides additional context for calculating breath duration between phrases.
/// When provided, enables context-dependent adjustments such as deeper breaths
/// after high-load phrases or before chorus entries.
struct BreathContext {
  float phrase_load = 0.5f;            ///< Previous phrase load (0.0-1.0)
  uint8_t prev_phrase_high = 60;       ///< Highest note in previous phrase
  float prev_phrase_density = 0.5f;    ///< Note density of previous phrase
  SectionType next_section;            ///< Next section type
  bool is_section_boundary = false;    ///< Whether this is a section boundary
};

/**
 * @brief Template-driven melody generator with music theory constraints.
 */
class MelodyDesigner {
 public:
  /// @brief Context for melody generation within a section.
  struct SectionContext {
    SectionType section_type;                 ///< What kind of section (Verse, Chorus, etc.)
    Tick section_start;                       ///< Absolute start tick
    Tick section_end;                         ///< Absolute end tick
    uint8_t section_bars;                     ///< Length in bars
    int8_t chord_degree;                      ///< Starting chord degree (0-6)
    int key_offset;                           ///< Key transposition from C
    TessituraRange tessitura;                 ///< Comfortable singing range
    uint8_t vocal_low;                        ///< Absolute minimum pitch
    uint8_t vocal_high;                       ///< Absolute maximum pitch
    Mood mood = Mood::StraightPop;            ///< Mood for harmonic rhythm
    float density_modifier = 1.0f;            ///< Section-specific note density (1.0 = default)
    float thirtysecond_ratio = 0.0f;          ///< Ratio of 32nd notes (0.0-1.0)
    float consecutive_same_note_prob = 0.6f;  ///< Probability of allowing repeated notes
    bool disable_vowel_constraints = false;   ///< Allow large intervals within syllables
    bool disable_breathing_gaps = false;      ///< Remove breathing rests between phrases
    const SectionTransition* transition_to_next = nullptr;  ///< Transition to next section
    bool enable_embellishment = true;  ///< Enable melodic embellishment (NCT insertion)
    VocalAttitude vocal_attitude = VocalAttitude::Expressive;  ///< Vocal style attitude
    HookIntensity hook_intensity = HookIntensity::Normal;      ///< Hook pattern selection intensity
    // BPM for tempo-aware duration and breath calculations
    uint16_t bpm = 120;  ///< Beats per minute

    // RhythmSync support
    GenerationParadigm paradigm = GenerationParadigm::Traditional;  ///< Generation paradigm
    const DrumGrid* drum_grid = nullptr;  ///< Drum grid for RhythmSync quantization

    // Behavioral Loop support
    bool addictive_mode = false;  ///< Enable Behavioral Loop mode (fixed patterns)

    // Vocal groove feel for syncopation control
    VocalGrooveFeel vocal_groove = VocalGrooveFeel::Straight;  ///< Affects syncopation weight

    // Syncopation enable flag (must be true for syncopation effects)
    bool enable_syncopation = false;  ///< When false, syncopation weight is forced to 0

    // Drive feel for timing and syncopation modulation
    uint8_t drive_feel = 50;  ///< Drive feel (0=laid-back, 50=neutral, 100=aggressive)

    // Blueprint constraints for melodic leap and stepwise preference
    uint8_t max_leap_semitones = 12;  ///< Maximum melodic leap in semitones (default: octave)
    bool prefer_stepwise = false;      ///< Prefer stepwise motion over leaps

    // Guide tone priority for downbeat pitch selection
    uint8_t guide_tone_rate = 0;  ///< Guide tone (3rd/7th) priority rate (0=disabled, 1-100%)

    // Anticipation rest mode for phrase breathing
    AnticipationRestMode anticipation_rest = AnticipationRestMode::Off;  ///< Rest before phrases

    // Phrase contour template for explicit melodic shaping
    // When set, overrides default section-based direction bias in selectPitchChoice
    std::optional<ContourType> forced_contour = std::nullopt;  ///< Optional forced contour

    // Vocal style preset for style-specific physics
    VocalStylePreset vocal_style = VocalStylePreset::Standard;  ///< Affects breath and timing

    // Motif fragment enforcement for A/B sections
    // When true, inject fragments from GlobalMotif at phrase beginnings
    // Creates song-wide melodic unity by echoing chorus motif in verses
    bool enforce_motif_fragments = false;  ///< Enable motif fragment injection

    // Motif rhythm template for RhythmSync accent-linked velocity
    const MotifParams* motif_params = nullptr;  ///< Motif params for template accent weights

    // Occurrence count for occurrence-dependent embellishment density
    int section_occurrence = 1;  ///< Which occurrence of this section type (1-based)

    // Style melody params wired from StyleMelodyParams (zombie param connections)
    bool chorus_long_tones = false;   ///< Extend short notes to longer tones in chorus
    bool allow_bar_crossing = true;   ///< Allow notes to cross bar boundaries
    uint8_t min_note_division = 0;    ///< Minimum note division (4/8/16/32, 0=no filter)
    float tension_usage = 0.2f;       ///< Tension note probability (0.0-1.0)
    float syncopation_prob = 0.15f;   ///< Syncopation probability scaling (0.0-0.5)
    float long_note_ratio_override = -1.0f;  ///< Override MelodyTemplate long_note_ratio (-1=no override)
    uint8_t phrase_length_bars = 0;   ///< Override phrase length in bars (0=use template)

    // ========================================================================
    // Task 5-2: Internal 4-Stage Structure within Section
    // ========================================================================
    // For 8-bar sections, track internal 2-bar "sub-phrase index" (0-3):
    // - 0 (bars 1-2): Presentation - motif initial/recap, higher plateau_ratio
    // - 1 (bars 3-4): Development - transform motif, wider step sizes
    // - 2 (bars 5-6): Climax - highest note placement, tessitura shift up
    // - 3 (bars 7-8): Resolution - cadence, stronger phrase_end_resolution

    uint8_t sub_phrase_index = 0;  ///< Internal arc position (0-3 for 8-bar sections)

    /// @brief Check if current position is in the "climax" sub-phrase.
    bool isClimaxSubPhrase() const { return sub_phrase_index == 2; }

    /// @brief Check if current position is in the "resolution" sub-phrase.
    bool isResolutionSubPhrase() const { return sub_phrase_index == 3; }

    /// @brief Get tessitura adjustment for internal arc.
    /// Climax sub-phrase shifts tessitura up by 2 semitones.
    int getTessituraAdjustment() const {
      if (isClimaxSubPhrase()) return 2;       // Shift up for climax
      if (isResolutionSubPhrase()) return -1;  // Slight drop for resolution
      return 0;
    }

    /// @brief Get step size multiplier for internal arc.
    /// Development sub-phrase allows wider intervals.
    float getStepSizeMultiplier() const {
      if (sub_phrase_index == 1) return 1.3f;   // Development: wider steps
      if (isResolutionSubPhrase()) return 0.8f; // Resolution: smaller steps
      return 1.0f;
    }
  };

  /// @brief Result of generating a single phrase.
  struct PhraseResult {
    std::vector<NoteEvent> notes;  ///< Generated notes for this phrase
    int last_pitch;                ///< Final pitch (for next phrase continuity)
    int direction_inertia;         ///< Accumulated direction momentum (-N to +N)
  };

  MelodyDesigner() = default;

  /**
   * @brief Generate melody for an entire section.
   * @param tmpl Melody template defining style characteristics
   * @param ctx Section context with timing and range info
   * @param harmony Harmony context for chord-aware generation
   * @param rng Random number generator
   * @return Vector of note events for the section
   */
  std::vector<NoteEvent> generateSection(const MelodyTemplate& tmpl, const SectionContext& ctx,
                                         const IHarmonyContext& harmony, std::mt19937& rng);

  /**
   * @brief Generate melody with evaluation and candidate selection.
   * @param tmpl Melody template defining style characteristics
   * @param ctx Section context with timing and range info
   * @param harmony Harmony context for chord-aware generation
   * @param rng Random number generator
   * @param vocal_style Style affects evaluation weights
   * @param melodic_complexity Complexity affects StyleBias adjustments
   * @param candidate_count How many candidates to generate (default 100)
   * @return Best-scoring candidate's notes
   */
  std::vector<NoteEvent> generateSectionWithEvaluation(
      const MelodyTemplate& tmpl, const SectionContext& ctx, const IHarmonyContext& harmony,
      std::mt19937& rng, VocalStylePreset vocal_style = VocalStylePreset::Standard,
      MelodicComplexity melodic_complexity = MelodicComplexity::Standard,
      int candidate_count = 100);

  /**
   * @brief Get recommended candidate count for section type.
   *
   * Higher candidate counts for important sections (Chorus),
   * lower counts for stable sections (Verse) to optimize generation time.
   *
   * @param type Section type
   * @returns Recommended candidate count
   */
  static int getCandidateCountForSection(SectionType type) {
    switch (type) {
      case SectionType::Chorus:
        return 100;  // Most important: maximum candidates
      case SectionType::B:
        return 50;  // Pre-chorus: moderate
      case SectionType::Bridge:
      case SectionType::Chant:
        return 30;  // Variety elements: fewer OK
      default:      // A (Verse), Intro, Outro, Interlude, MixBreak
        return 20;  // Stability focused: fewer sufficient
    }
  }

  /**
   * @brief Generate a single melodic phrase.
   * @param tmpl Melody template with style parameters
   * @param phrase_start Start tick of phrase
   * @param phrase_beats Length of phrase in beats
   * @param ctx Section context
   * @param prev_pitch Previous pitch for continuity (-1 if none)
   * @param direction_inertia Current direction momentum
   * @param harmony Harmony context for chord-aware generation
   * @param rng Random number generator
   * @return Phrase result with notes and updated state
   */
  PhraseResult generateMelodyPhrase(const MelodyTemplate& tmpl, Tick phrase_start,
                                    uint8_t phrase_beats, const SectionContext& ctx, int prev_pitch,
                                    int direction_inertia, const IHarmonyContext& harmony,
                                    std::mt19937& rng);

  /**
   * @brief Get cached GlobalMotif (if any).
   * @return Optional GlobalMotif, empty if not yet extracted
   */
  const std::optional<GlobalMotif>& getCachedGlobalMotif() const { return cached_global_motif_; }

  /**
   * @brief Set GlobalMotif for song-wide reference.
   *
   * Also prepares section-specific variants using appropriate transformations:
   * - Chorus: original motif (strongest recognition)
   * - A section: diminished rhythm (slightly faster feel)
   * - B section: sequenced (building tension)
   * - Bridge: inverted (contrast)
   * - Outro: fragmented (winding down)
   *
   * @param motif GlobalMotif to cache
   */
  void setGlobalMotif(const GlobalMotif& motif) {
    cached_global_motif_ = motif;
    prepareMotifVariants(motif);
  }

  /**
   * @brief Get the motif variant for a specific section type.
   * @param section_type Section type
   * @return Appropriate motif variant (or original if no variant prepared)
   */
  const GlobalMotif& getMotifForSection(SectionType section_type) const;

  /**
   * @brief Generate a hook pattern for chorus sections.
   * @param tmpl Melody template
   * @param hook_start Start tick of hook
   * @param ctx Section context
   * @param prev_pitch Previous pitch for continuity
   * @param harmony Harmony context
   * @param rng Random number generator
   * @return Phrase result with hook notes
   */
  PhraseResult generateHook(const MelodyTemplate& tmpl, Tick hook_start, const SectionContext& ctx,
                            int prev_pitch, const IHarmonyContext& harmony, std::mt19937& rng);

  // selectPitchChoice, applyDirectionInertia, getEffectivePlateauRatio, shouldLeap,
  // getStabilizeStep, isInSameVowelSection, getMaxStepInVowelSection:
  //   -> melody::selectPitchChoice etc. in contour_direction.h
  //
  // generatePhraseRhythm:
  //   -> melody::generatePhraseRhythm in rhythm_generator.h
  //
  // extractGlobalMotif, evaluateWithGlobalMotif:
  //   -> melody::extractGlobalMotif etc. in motif_support.h

  /**
   * @brief Apply transition approach processing to section end.
   * @param notes Notes to modify (in place)
   * @param ctx Section context with transition info
   * @param harmony Harmony context for chord-aware adjustments
   */
  void applyTransitionApproach(std::vector<NoteEvent>& notes, const SectionContext& ctx,
                               const IHarmonyContext& harmony);

  /**
   * @brief Enhanced pitch selection for locked rhythm with melodic quality.
   *
   * Addresses melodic quality issues in RhythmSync paradigm:
   * 1. Direction bias based on phrase position (ascending start, resolving end)
   * 2. Direction inertia to maintain melodic momentum
   * 3. GlobalMotif interval pattern reference for song-wide unity
   * 4. Progressive penalty for consecutive same-pitch notes
   *
   * @param prev_pitch Previous pitch for melodic continuity
   * @param chord_degree Current chord degree (0-6)
   * @param vocal_low Minimum allowed pitch
   * @param vocal_high Maximum allowed pitch
   * @param phrase_position Position within phrase (0.0-1.0)
   * @param direction_inertia Current direction momentum (-3 to +3)
   * @param note_index Current note index within phrase
   * @param rng Random number generator
   * @param section_type Section type for context-aware thresholds
   * @param vocal_attitude Vocal attitude for tension allowance
   * @param same_pitch_streak Consecutive same pitch counter (0 = first note)
   * @return Selected pitch (MIDI note number)
   */
  uint8_t selectPitchForLockedRhythmEnhanced(uint8_t prev_pitch, int8_t chord_degree,
                                              uint8_t vocal_low, uint8_t vocal_high,
                                              float phrase_position, int direction_inertia,
                                              size_t note_index, std::mt19937& rng,
                                              SectionType section_type = SectionType::A,
                                              VocalAttitude vocal_attitude = VocalAttitude::Clean,
                                              int same_pitch_streak = 0);

 private:
  // Insert a leading tone at section boundary for smooth transition.
  // @param notes Notes to modify (in place)
  // @param ctx Section context
  // @param harmony Harmony context
  void insertLeadingTone(std::vector<NoteEvent>& notes, const SectionContext& ctx,
                         const IHarmonyContext& harmony);

  // Calculate target pitch for phrase based on template.
  // Adapts SectionContext fields to melody::calculateTargetPitch parameters.
  int calculateTargetPitch(const MelodyTemplate& tmpl, const SectionContext& ctx, int current_pitch,
                           const IHarmonyContext& harmony, std::mt19937& rng);

  /// @brief Cache for hook-related state across song generation.
  ///
  /// Consolidates hook caching for Song-level fixation, where the same hook
  /// patterns are reused throughout the song for melodic consistency.
  struct HookCache {
    /// Cached chorus hook (Motif rhythm) for Song-level fixation.
    std::optional<Motif> chorus_hook;

    /// Cached HookSkeleton for Hybrid approach (contour hint).
    /// skeleton: Used for first half of sections (stronger intensity)
    /// skeleton_later: Used for second half (base intensity, more variety)
    std::optional<HookSkeleton> skeleton;
    std::optional<HookSkeleton> skeleton_later;

    /// Cached rhythm pattern index (SIZE_MAX = not yet selected).
    size_t rhythm_pattern_idx = SIZE_MAX;

    /// Hook repetition counter for betrayal strategy (golden ratio: 3 then change).
    uint8_t repetition_count = 0;

    /// Cached sabi (chorus) head pitches for consistency (first 8 notes).
    std::array<uint8_t, 8> sabi_pitches{};
    bool pitches_cached = false;

    /// Cached sabi head rhythm (first 8 durations and velocities).
    std::array<Tick, 8> sabi_durations{};
    std::array<uint8_t, 8> sabi_velocities{};
    /// Cached tick advances (pre-gate durations) for grid-aligned timing.
    std::array<Tick, 8> sabi_tick_advances{};
    bool rhythm_cached = false;

    /// @brief Reset all cached state.
    void reset() {
      chorus_hook.reset();
      skeleton.reset();
      skeleton_later.reset();
      rhythm_pattern_idx = SIZE_MAX;
      repetition_count = 0;
      pitches_cached = false;
      rhythm_cached = false;
    }
  };

  HookCache hook_cache_;

  // Cached GlobalMotif for song-wide melodic unity.
  // Extracted from first chorus hook, used as evaluation reference.
  std::optional<GlobalMotif> cached_global_motif_;

  // Section-specific motif variants for development/transformation.
  // Prepared when GlobalMotif is set, used during evaluation.
  std::unordered_map<SectionType, GlobalMotif> motif_variants_;

  // Prepare section-specific motif variants from source motif.
  void prepareMotifVariants(const GlobalMotif& source);
};

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_VOCAL_MELODY_DESIGNER_H
