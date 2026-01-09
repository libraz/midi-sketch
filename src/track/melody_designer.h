/**
 * @file melody_designer.h
 * @brief Template-driven melody generation with music theory constraints.
 */

#ifndef MIDISKETCH_TRACK_MELODY_DESIGNER_H
#define MIDISKETCH_TRACK_MELODY_DESIGNER_H

#include "core/chord_utils.h"
#include "core/melody_evaluator.h"
#include "core/melody_templates.h"
#include "core/motif.h"
#include "core/pitch_utils.h"
#include "core/types.h"
#include <optional>
#include <random>
#include <vector>

namespace midisketch {

class HarmonyContext;

/**
 * @brief Template-driven melody generator with music theory constraints.
 */
class MelodyDesigner {
 public:
  /// @brief Context for melody generation within a section.
  struct SectionContext {
    SectionType section_type;          ///< What kind of section (Verse, Chorus, etc.)
    Tick section_start;                ///< Absolute start tick
    Tick section_end;                  ///< Absolute end tick
    uint8_t section_bars;              ///< Length in bars
    int8_t chord_degree;               ///< Starting chord degree (0-6)
    int key_offset;                    ///< Key transposition from C
    TessituraRange tessitura;          ///< Comfortable singing range
    uint8_t vocal_low;                 ///< Absolute minimum pitch
    uint8_t vocal_high;                ///< Absolute maximum pitch
    Mood mood = Mood::StraightPop;     ///< Mood for harmonic rhythm
    float density_modifier = 1.0f;     ///< Section-specific note density (1.0 = default)
    float thirtysecond_ratio = 0.0f;   ///< Ratio of 32nd notes (0.0-1.0)
    float consecutive_same_note_prob = 1.0f;  ///< Probability of allowing repeated notes
    bool disable_vowel_constraints = false;   ///< Allow large intervals within syllables
    bool disable_breathing_gaps = false;      ///< Remove breathing rests between phrases
    const SectionTransition* transition_to_next = nullptr;  ///< Transition to next section
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
  std::vector<NoteEvent> generateSection(
      const MelodyTemplate& tmpl,
      const SectionContext& ctx,
      const HarmonyContext& harmony,
      std::mt19937& rng);

  /**
   * @brief Generate melody with evaluation and candidate selection.
   * @param tmpl Melody template defining style characteristics
   * @param ctx Section context with timing and range info
   * @param harmony Harmony context for chord-aware generation
   * @param rng Random number generator
   * @param vocal_style Style affects evaluation weights
   * @param candidate_count How many candidates to generate (default 3)
   * @return Best-scoring candidate's notes
   */
  std::vector<NoteEvent> generateSectionWithEvaluation(
      const MelodyTemplate& tmpl,
      const SectionContext& ctx,
      const HarmonyContext& harmony,
      std::mt19937& rng,
      VocalStylePreset vocal_style = VocalStylePreset::Standard,
      int candidate_count = 3);

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
  PhraseResult generateMelodyPhrase(
      const MelodyTemplate& tmpl,
      Tick phrase_start,
      uint8_t phrase_beats,
      const SectionContext& ctx,
      int prev_pitch,
      int direction_inertia,
      const HarmonyContext& harmony,
      std::mt19937& rng);

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
  PhraseResult generateHook(
      const MelodyTemplate& tmpl,
      Tick hook_start,
      const SectionContext& ctx,
      int prev_pitch,
      const HarmonyContext& harmony,
      std::mt19937& rng);

  /**
   * @brief Select pitch choice based on template and phrase position.
   * @param tmpl Melody template with movement probabilities
   * @param phrase_pos Position within phrase (0.0-1.0)
   * @param has_target Whether we're approaching a target pitch
   * @param rng Random number generator
   * @return Selected pitch choice
   */
  static PitchChoice selectPitchChoice(
      const MelodyTemplate& tmpl,
      float phrase_pos,
      bool has_target,
      std::mt19937& rng);

  /**
   * @brief Apply direction inertia to pitch movement.
   * @param choice Current pitch choice
   * @param inertia Accumulated direction (-N to +N)
   * @param tmpl Melody template
   * @param rng Random number generator
   * @return Modified pitch choice (may change direction)
   */
  static PitchChoice applyDirectionInertia(
      PitchChoice choice,
      int inertia,
      const MelodyTemplate& tmpl,
      std::mt19937& rng);

  /**
   * @brief Get effective plateau ratio considering register.
   * @param tmpl Melody template with base plateau ratio
   * @param current_pitch Current pitch
   * @param tessitura Tessitura range for register calculation
   * @return Effective plateau ratio (may be boosted for high notes)
   */
  static float getEffectivePlateauRatio(
      const MelodyTemplate& tmpl,
      int current_pitch,
      const TessituraRange& tessitura);

  /**
   * @brief Check if a leap should occur based on trigger conditions.
   * @param trigger Leap trigger type from template
   * @param phrase_pos Position within phrase (0.0-1.0)
   * @param section_pos Position within section (0.0-1.0)
   * @return true if conditions are right for a leap
   */
  static bool shouldLeap(
      LeapTrigger trigger,
      float phrase_pos,
      float section_pos);

  /**
   * @brief Get stabilization step after a leap (leap compensation).
   * @param leap_direction Direction of the leap (+1 up, -1 down)
   * @param max_step Maximum step size in semitones
   * @return Stabilization step (opposite direction, small magnitude)
   */
  static int getStabilizeStep(int leap_direction, int max_step);

  /**
   * @brief Check if two positions are in the same vowel section.
   * @param pos1 First position in beats
   * @param pos2 Second position in beats
   * @param phrase_length Phrase length in beats
   * @return true if positions are likely within same syllable
   */
  static bool isInSameVowelSection(float pos1, float pos2, uint8_t phrase_length);

  /**
   * @brief Get maximum step size within a vowel section.
   * @param in_same_vowel Whether positions are in same vowel section
   * @return Maximum step in semitones (smaller if in same vowel)
   */
  static int8_t getMaxStepInVowelSection(bool in_same_vowel);

  /**
   * @brief Apply transition approach processing to section end.
   * @param notes Notes to modify (in place)
   * @param ctx Section context with transition info
   * @param harmony Harmony context for chord-aware adjustments
   */
  void applyTransitionApproach(
      std::vector<NoteEvent>& notes,
      const SectionContext& ctx,
      const HarmonyContext& harmony);

 private:
  // Insert a leading tone at section boundary for smooth transition.
  // @param notes Notes to modify (in place)
  // @param ctx Section context
  // @param harmony Harmony context
  void insertLeadingTone(
      std::vector<NoteEvent>& notes,
      const SectionContext& ctx,
      const HarmonyContext& harmony);
  // Apply pitch choice to get new pitch.
  int applyPitchChoice(
      PitchChoice choice,
      int current_pitch,
      int target_pitch,
      int8_t chord_degree,
      int key_offset,
      uint8_t vocal_low,
      uint8_t vocal_high);

  // Calculate target pitch for phrase based on template.
  int calculateTargetPitch(
      const MelodyTemplate& tmpl,
      const SectionContext& ctx,
      int current_pitch,
      const HarmonyContext& harmony,
      std::mt19937& rng);

  // Generate rhythm for a phrase.
  // @param density_modifier Section-specific density multiplier (1.0 = default)
  // @param thirtysecond_ratio Ratio of 32nd notes (0.0-1.0)
  std::vector<RhythmNote> generatePhraseRhythm(
      const MelodyTemplate& tmpl,
      uint8_t phrase_beats,
      float density_modifier,
      float thirtysecond_ratio,
      std::mt19937& rng);

  // Cached chorus hook for Song-level fixation.
  // Once generated, the same hook is reused throughout the song.
  std::optional<Motif> cached_chorus_hook_;
};

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_MELODY_DESIGNER_H
