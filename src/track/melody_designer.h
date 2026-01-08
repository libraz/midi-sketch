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

// MelodyDesigner generates template-driven melodies.
// Uses MelodyTemplate parameters to constrain pitch selection and phrasing.
class MelodyDesigner {
 public:
  // Context for melody generation within a section.
  struct SectionContext {
    SectionType section_type;
    Tick section_start;
    Tick section_end;
    uint8_t section_bars;
    int8_t chord_degree;        // Current chord degree
    int key_offset;             // Key transposition
    TessituraRange tessitura;   // Comfortable singing range
    uint8_t vocal_low;          // Absolute minimum pitch
    uint8_t vocal_high;         // Absolute maximum pitch
    float density_modifier = 1.0f;     // Section-specific density multiplier (1.0 = default)
    float thirtysecond_ratio = 0.0f;   // Section-specific 32nd note ratio (0.0-1.0)
    float consecutive_same_note_prob = 1.0f;  // Probability of allowing same consecutive note (0.0-1.0)
    bool disable_vowel_constraints = false;   // Disable vowel section step limits
    bool disable_breathing_gaps = false;      // Disable breathing rests between phrases

    // Transition processing
    const SectionTransition* transition_to_next = nullptr;  // Transition to next section
  };

  // Phrase generation result.
  struct PhraseResult {
    std::vector<NoteEvent> notes;
    int last_pitch;             // Last pitch for continuity
    int direction_inertia;      // Accumulated direction (-1, 0, +1)
  };

  MelodyDesigner() = default;

  // Generate melody for an entire section.
  // @param tmpl Melody template to use
  // @param ctx Section context
  // @param harmony Harmony context for chord information
  // @param rng Random number generator
  // @returns Vector of note events
  std::vector<NoteEvent> generateSection(
      const MelodyTemplate& tmpl,
      const SectionContext& ctx,
      const HarmonyContext& harmony,
      std::mt19937& rng);

  // Generate melody with evaluation and candidate selection.
  // Generates multiple candidates and selects the best based on evaluation.
  // @param tmpl Melody template to use
  // @param ctx Section context
  // @param harmony Harmony context for chord information
  // @param rng Random number generator
  // @param vocal_style Vocal style for evaluation weights
  // @param candidate_count Number of candidates to generate (default 3)
  // @returns Vector of note events (best candidate)
  std::vector<NoteEvent> generateSectionWithEvaluation(
      const MelodyTemplate& tmpl,
      const SectionContext& ctx,
      const HarmonyContext& harmony,
      std::mt19937& rng,
      VocalStylePreset vocal_style = VocalStylePreset::Standard,
      int candidate_count = 3);

  // Generate a single melodic phrase.
  // @param tmpl Melody template
  // @param phrase_start Start tick of phrase
  // @param phrase_beats Length of phrase in beats
  // @param ctx Section context
  // @param prev_pitch Previous pitch for continuity (-1 if none)
  // @param direction_inertia Current direction momentum
  // @param harmony Harmony context
  // @param rng Random number generator
  // @returns Phrase result with notes and state
  PhraseResult generateMelodyPhrase(
      const MelodyTemplate& tmpl,
      Tick phrase_start,
      uint8_t phrase_beats,
      const SectionContext& ctx,
      int prev_pitch,
      int direction_inertia,
      const HarmonyContext& harmony,
      std::mt19937& rng);

  // Generate a hook pattern for chorus sections.
  // @param tmpl Melody template
  // @param hook_start Start tick of hook
  // @param ctx Section context
  // @param prev_pitch Previous pitch
  // @param harmony Harmony context
  // @param rng Random number generator
  // @returns Phrase result with hook notes
  PhraseResult generateHook(
      const MelodyTemplate& tmpl,
      Tick hook_start,
      const SectionContext& ctx,
      int prev_pitch,
      const HarmonyContext& harmony,
      std::mt19937& rng);

  // Select pitch choice based on template and phrase position.
  // @param tmpl Melody template
  // @param phrase_pos Position within phrase (0.0-1.0)
  // @param has_target Whether we have a target pitch
  // @param rng Random number generator
  // @returns Pitch choice
  static PitchChoice selectPitchChoice(
      const MelodyTemplate& tmpl,
      float phrase_pos,
      bool has_target,
      std::mt19937& rng);

  // Apply direction inertia to pitch movement.
  // @param choice Current pitch choice
  // @param inertia Direction inertia (-N to +N)
  // @param tmpl Melody template
  // @param rng Random number generator
  // @returns Modified pitch choice
  static PitchChoice applyDirectionInertia(
      PitchChoice choice,
      int inertia,
      const MelodyTemplate& tmpl,
      std::mt19937& rng);

  // Get effective plateau ratio considering register.
  // High register gets boosted plateau ratio for stability.
  // @param tmpl Melody template
  // @param current_pitch Current pitch
  // @param tessitura Tessitura range
  // @returns Effective plateau ratio
  static float getEffectivePlateauRatio(
      const MelodyTemplate& tmpl,
      int current_pitch,
      const TessituraRange& tessitura);

  // Check if a leap should occur based on trigger conditions.
  // @param trigger Leap trigger type
  // @param phrase_pos Position within phrase
  // @param section_pos Position within section
  // @returns true if leap should occur
  static bool shouldLeap(
      LeapTrigger trigger,
      float phrase_pos,
      float section_pos);

  // Get stabilization step after a leap.
  // Returns a small step in the opposite direction to stabilize.
  // @param leap_direction Direction of the leap (+1 or -1)
  // @param max_step Maximum step size
  // @returns Stabilization step (opposite direction, smaller magnitude)
  static int getStabilizeStep(int leap_direction, int max_step);

  // Check if two positions are in the same vowel section.
  // Used to constrain movement within syllable boundaries.
  // @param pos1 First position in beats
  // @param pos2 Second position in beats
  // @param phrase_length Phrase length in beats
  // @returns true if in same vowel section
  static bool isInSameVowelSection(float pos1, float pos2, uint8_t phrase_length);

  // Get maximum step size within a vowel section.
  // @param in_same_vowel Whether in same vowel section
  // @returns Maximum step in semitones
  static int8_t getMaxStepInVowelSection(bool in_same_vowel);

  // Apply transition approach processing to notes near section end.
  // Modifies pitch and velocity to create natural flow into next section.
  // @param notes Notes to modify (in place)
  // @param ctx Section context with transition info
  // @param harmony Harmony context for chord info
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
