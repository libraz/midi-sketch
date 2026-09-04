/**
 * @file velocity.h
 * @brief Velocity (dynamics) calculation for musical expression.
 */

#ifndef MIDISKETCH_CORE_VELOCITY_H
#define MIDISKETCH_CORE_VELOCITY_H

#include <algorithm>
#include <vector>

#include "core/types.h"

namespace midisketch {

// Forward declaration
class MidiTrack;

/**
 * @brief Calculate velocity for a note based on musical context.
 * @param section Current section type (affects base energy)
 * @param beat Beat position within bar (0-3, affects emphasis)
 * @param mood Current mood preset (affects intensity)
 * @return Calculated velocity (0-127)
 */
uint8_t calculateVelocity(SectionType section, uint8_t beat, Mood mood);

/**
 * @brief Get velocity adjustment multiplier for a mood.
 * @param mood Mood preset
 * @return Multiplier (typically 0.9-1.1)
 */
float getMoodVelocityAdjustment(Mood mood);

/**
 * @brief Get energy level for a section type.
 *
 * Intro/Outro=1, Verse=2, Pre-chorus/Bridge=3, Chorus=4.
 *
 * @param section Section type
 * @return Energy level (1=lowest, 4=highest)
 */
int getSectionEnergy(SectionType section);

/**
 * @brief Get velocity multiplier for a section type.
 *
 * Centralized section-based velocity scaling to ensure consistent
 * dynamics across all tracks. Values range from 0.6 (very quiet)
 * to 1.1 (energetic).
 *
 * @param section Section type
 * @return Velocity multiplier (0.6-1.1)
 */
float getSectionVelocityMultiplier(SectionType section);

// ============================================================================
// SectionEnergy and PeakLevel Functions
// ============================================================================

/**
 * @brief Get effective section energy from Section struct.
 *
 * Prioritizes Blueprint's explicit energy setting. Falls back to
 * estimating from SectionType only when energy is Unset.
 *
 * @param section Section struct with energy field
 * @return SectionEnergy value
 */
SectionEnergy getEffectiveSectionEnergy(const Section& section);

/**
 * @brief Get velocity multiplier for PeakLevel.
 *
 * @param peak PeakLevel value
 * @return Velocity multiplier (1.0 for None, up to 1.1 for Max)
 */
float getPeakVelocityMultiplier(PeakLevel peak);

/**
 * @brief Get velocity multiplier for a SectionEnergy level.
 *
 * Expressed relative to velocity::kEnergyReferenceMultiplier, so a Peak section
 * returns 1.0 and the calmer levels step back from it in the proportions the
 * constants declare. Unset returns 1.0: an undeclared energy is not a quiet one.
 *
 * @param energy Section energy level
 * @return Velocity multiplier (about 0.71 for Low, 1.0 for Peak and Unset)
 */
float getSectionEnergyMultiplier(SectionEnergy energy);

/**
 * @brief Get the combined velocity scale a section imposes on its notes.
 *
 * This is the single place where the four section-level dynamics controls meet:
 * base velocity, section energy, peak level and section modifier are multiplied
 * into one factor. Every velocity that leaves generation is scaled by exactly
 * this value once, so changing any one of the four is guaranteed to change the
 * sounding dynamics.
 *
 * @param section Section whose controls to combine
 * @return Velocity scale (1.0 when every control is at its neutral value)
 */
float getSectionVelocityScale(const Section& section);

/// @brief Track-relative velocity multipliers for consistent mix balance.
struct VelocityBalance {
  static constexpr float VOCAL = 1.0f;      ///< Lead vocal - always on top
  static constexpr float CHORD = 0.75f;     ///< Chords - supportive harmony
  static constexpr float BASS = 0.85f;      ///< Bass - prominent foundation
  static constexpr float DRUMS = 0.90f;     ///< Drums - rhythmic backbone
  static constexpr float MOTIF = 0.70f;     ///< Motif - decorative element
  static constexpr float ARPEGGIO = 0.85f;  ///< Arpeggio - rhythmic texture
  static constexpr float AUX = 0.65f;       ///< Aux - subdued support

  /**
   * @brief Get velocity multiplier for a track role.
   * @param role Track role to look up
   * @return Velocity multiplier for mix balance
   */
  static float getMultiplier(TrackRole role);
};

/**
 * @brief Get velocity multiplier for bar position within a section.
 *
 * Implements 4-bar phrase dynamics (build→hit pattern) using continuous
 * cosine interpolation for smooth transitions. Section-level crescendo
 * is applied for Chorus sections.
 *
 * The 4-bar phrase pattern (continuous curve from 0.75 to 1.00):
 * - Uses cosine interpolation for natural S-curve acceleration
 * - No discrete steps - velocity changes smoothly throughout phrase
 *
 * @param bar_in_section Bar number within the section (0-indexed)
 * @param total_bars Total number of bars in the section
 * @param section_type Type of section (affects curve shape)
 * @return Velocity multiplier (typically 0.75-1.12)
 */
float getBarVelocityMultiplier(int bar_in_section, int total_bars, SectionType section_type);

/// @brief Named velocity ratio constants for consistent dynamics.
namespace VelocityRatio {
constexpr float ACCENT = 0.95f;      ///< Accented notes (emphasized)
constexpr float NORMAL = 0.9f;       ///< Standard velocity
constexpr float WEAK_BEAT = 0.85f;   ///< Off-beat or weak beat notes
constexpr float SOFT = 0.8f;         ///< Softer notes (intro/outro)
constexpr float TENSION = 0.7f;      ///< Tension notes, doublings
constexpr float BACKGROUND = 0.65f;  ///< Background elements
constexpr float VERY_SOFT = 0.6f;    ///< Very subdued notes
constexpr float GHOST = 0.5f;        ///< Ghost notes (nearly silent)
}  // namespace VelocityRatio

/**
 * @brief Apply crescendo/decrescendo dynamics at section transitions.
 * @param track Track to modify (in-place)
 * @param section_start Start tick of the current section
 * @param section_end End tick of the current section
 * @param from Section type being exited
 * @param to Section type being entered
 */
void applyTransitionDynamics(MidiTrack& track, Tick section_start, Tick section_end,
                             SectionType from, SectionType to);

/**
 * @brief Apply transition dynamics to all melodic tracks.
 * @param tracks Vector of tracks to modify (in-place)
 * @param sections Arrangement sections for transition points
 */
void applyAllTransitionDynamics(std::vector<MidiTrack*>& tracks,
                                const std::vector<Section>& sections);

/**
 * @brief Apply entry pattern dynamics to notes at section start.
 *
 * Implements GradualBuild and DropIn entry effects.
 * - GradualBuild: Start at 60% velocity, ramp to 100% over 2 bars
 * - DropIn: Slight velocity boost at section start
 * - Immediate/Stagger: No velocity adjustment
 *
 * @param track Track to modify (in-place)
 * @param section_start Start tick of the section
 * @param bars Number of bars in the section
 * @param pattern Entry pattern type
 */
void applyEntryPatternDynamics(MidiTrack& track, Tick section_start, uint8_t bars,
                               EntryPattern pattern);

/**
 * @brief Apply entry pattern dynamics to all tracks for all sections.
 *
 * Processes each section's entry_pattern setting and applies appropriate
 * velocity modifications to tracks.
 *
 * A GradualBuild ramp starts the section at 60% velocity, which contradicts the
 * entry protection applyBarVelocityCurve() gives a section that lifts energy.
 * The ramp is therefore skipped where the section's effective energy is at least
 * the previous section's, so a chorus or climax never enters below its setup.
 *
 * @param tracks Vector of tracks to modify (in-place)
 * @param sections Arrangement sections with entry_pattern settings
 */
void applyAllEntryPatternDynamics(std::vector<MidiTrack*>& tracks,
                                  const std::vector<Section>& sections);

/**
 * @brief Scale a velocity for its position in a staggered-entry fade-in.
 *
 * A track that enters mid-intro fades from velocity::kStaggerFadeStart up to
 * full level across its fade span. A faded note is still a note, so the result
 * never reaches 0, which MIDI reads as a note-off rather than a quiet attack.
 *
 * @param velocity Velocity before the fade
 * @param progress Position within the fade span (0.0 at entry, 1.0 at full level)
 * @return Faded velocity, at least 1
 */
uint8_t getEntryFadeVelocity(uint8_t velocity, float progress);

/**
 * @brief Apply bar-level velocity curves to a track within a section.
 *
 * Applies the 4-bar phrase dynamics (build→hit pattern) and section-level
 * crescendo for Chorus sections. This adds subtle dynamics that prevent
 * sections from sounding flat.
 *
 * @param track Track to modify (in-place)
 * @param section Section containing the notes to modify
 */
void applyBarVelocityCurve(MidiTrack& track, const Section& section);

/**
 * @brief Apply bar-level velocity curves with transition context.
 *
 * When the current section's effective energy is at least the previous
 * section's, the first bar is protected from phrase-start dips so an entry into
 * a chorus or climax does not become quieter than the setup section.
 *
 * @param track Track to modify (in-place)
 * @param section Section containing the notes to modify
 * @param prev_section Previous section, or nullptr if none
 */
void applyBarVelocityCurve(MidiTrack& track, const Section& section, const Section* prev_section);

/**
 * @brief Apply bar-level velocity curves to all tracks for all sections.
 *
 * Processes each section and applies bar-level velocity curves to create
 * natural phrase dynamics within each section.
 *
 * @param tracks Vector of tracks to modify (in-place)
 * @param sections Arrangement sections
 */
void applyAllBarVelocityCurves(std::vector<MidiTrack*>& tracks,
                               const std::vector<Section>& sections);

/**
 * @brief Scale note velocities by each section's combined velocity scale.
 *
 * Runs getSectionVelocityScale() over every note of every supplied track in one
 * multiplication, before the remaining phrase/beat dynamics. Passing the drum
 * and SE tracks alongside the melodic ones is what makes the section controls
 * audible across the whole arrangement rather than in the pitched tracks only.
 *
 * @param tracks Tracks to modify (in-place)
 * @param sections Arrangement sections carrying the dynamics controls
 */
void applySectionDynamics(std::vector<MidiTrack*>& tracks, const std::vector<Section>& sections);

/**
 * @brief Keep an arrival section from entering weaker than its setup.
 *
 * applyBarVelocityCurve() protects the entry bar of an energy lift, but later
 * velocity passes can undo that protection. This closes the pipeline by
 * restoring the guarantee: an arrival section (chorus, pre-chorus, MixBreak,
 * Drop, or a climax modifier) whose effective energy is at least the previous
 * section's has a first bar that sounds no weaker on average than the bar it
 * follows. Sections that step down in energy, and sections the arrangement does
 * not build toward, keep whatever entry the earlier passes gave them.
 *
 * Runs after any declared velocity ceiling so the lift observes it rather than
 * being cut back by it. Passes that run later still and change which notes a bar
 * contains can move the average slightly afterwards.
 *
 * @param tracks Tracks to modify (in-place)
 * @param sections Arrangement sections
 * @param max_velocity Velocity ceiling the lift must stay under
 */
void enforceSectionEntryLift(std::vector<MidiTrack*>& tracks, const std::vector<Section>& sections,
                             uint8_t max_velocity = 127);

// ============================================================================
// Melody Contour Velocity
// ============================================================================

/**
 * @brief Apply melody-contour-following velocity to vocal notes.
 *
 * Boosts velocity for phrase-high notes and creates gradual velocity changes
 * for ascending/descending passages. This makes the melody feel more naturally
 * performed by following the melodic contour with dynamics.
 *
 * Rules:
 * - Phrase-local highest note: +15 velocity boost
 * - Ascending passages: gradual velocity increase
 * - Descending passages: gradual velocity decrease
 * - Phrase boundary resets contour tracking (every 4 bars)
 *
 * @param track Vocal track to modify (in-place)
 * @param sections Song sections for phrase boundary detection
 */
void applyMelodyContourVelocity(MidiTrack& track, const std::vector<Section>& sections);

// ============================================================================
// Musical Accent Patterns
// ============================================================================

/**
 * @brief Apply musical accent patterns to a track.
 *
 * Three accent types are applied:
 * - Phrase-head accent: +8 velocity on first note of each 2-bar phrase
 * - Contour accent: +10 on the highest note within each 2-bar phrase
 * - Agogic accent: +5 on notes longer than a quarter note (held notes naturally
 *   receive more emphasis from performers)
 *
 * @param track Track to modify (in-place)
 * @param sections Song sections for phrase boundary detection
 */
void applyAccentPatterns(MidiTrack& track, const std::vector<Section>& sections);

/**
 * @brief Clamp all note velocities to a maximum value.
 *
 * Used to enforce blueprint constraints (e.g., IdolKawaii max_velocity=80).
 * Should be called as the final velocity processing step.
 *
 * @param track Track to modify (in-place)
 * @param max_velocity Maximum allowed velocity (1-127)
 */
void clampTrackVelocity(MidiTrack& track, uint8_t max_velocity);

/**
 * @brief Bring a percussion track under a velocity ceiling without flattening it.
 *
 * A blueprint that declares a maximum velocity is describing how hard the
 * production hits, and that has to bind the drum kit as well as the pitched
 * tracks. Clipping the kit is the wrong way to get there: on a drum track the
 * velocity selects the sound — ghost, normal, accent, rimshot — so cutting
 * everything above the ceiling to the ceiling collapses the loud half of the
 * kit's vocabulary onto one value and the groove reads as flat.
 *
 * Instead the range above velocity::kCeilingKneeRatio of the ceiling is folded
 * into the headroom that is left. Hits below the knee keep their exact value,
 * the order of every pair of hits is preserved, and nothing ends up louder than
 * the ceiling.
 *
 * @param track Percussion track to modify (in-place)
 * @param max_velocity Declared ceiling; 127 or above leaves the track untouched
 */
void applyPercussionVelocityCeiling(MidiTrack& track, uint8_t max_velocity);

// ============================================================================
// Micro-Dynamics (Beat-Level Velocity Curves)
// ============================================================================

/**
 * @brief Get beat-level velocity curve multiplier.
 *
 * Implements subtle dynamics within each bar for natural breathing:
 * - Beat 1: 1.08 (strongest - downbeat emphasis)
 * - Beat 2: 0.95 (weak - natural dip)
 * - Beat 3: 1.03 (medium-strong - secondary accent)
 * - Beat 4: 0.92 (weakest - anticipates next downbeat)
 *
 * Based on pop music feel where beats 1 and 3 are naturally accented.
 *
 * @param beat_position Position within bar (0.0-4.0)
 * @returns Velocity multiplier (0.92-1.08)
 */
float getBeatMicroCurve(float beat_position);

/**
 * @brief Apply phrase-end decay and duration stretch to notes.
 *
 * Reduces velocity and extends duration in the last beat of 4-bar phrases
 * for natural breathing. This creates a subtle exhale feeling at phrase
 * boundaries, making the melody sound more human and less machine-generated.
 *
 * Duration stretch is section-dependent:
 * - Bridge/Outro: 1.08x (stronger expression for emotional sections)
 * - Other sections: 1.05x (subtle stretch)
 *
 * @param track Track to modify (in-place)
 * @param sections Sections for phrase boundary detection
 * @param drive_feel Drive feel value (0-100), affects phrase-end duration stretch
 */
void applyPhraseEndDecay(MidiTrack& track, const std::vector<Section>& sections,
                         uint8_t drive_feel = 50);

/**
 * @brief Apply beat-level micro-dynamics to a track.
 *
 * Applies the subtle beat-level velocity curve to all notes in the track.
 * This adds natural breathing and groove without being obtrusive.
 *
 * @param track Track to modify (in-place)
 */
void applyBeatMicroDynamics(MidiTrack& track);

// ============================================================================
// Syncopation Weight
// ============================================================================

/**
 * @brief Get syncopation weight based on vocal groove feel and section type.
 *
 * Combines VocalGrooveFeel with section-aware adjustments for natural
 * rhythmic variation across the song.
 *
 * @param feel Vocal groove feel setting
 * @param section Section type (B sections suppress syncopation for buildup)
 * @param drive_feel Drive feel value (0-100), boosts syncopation at higher values
 * @return Syncopation weight (0.0-0.35)
 */
float getSyncopationWeight(VocalGrooveFeel feel, SectionType section, uint8_t drive_feel = 50);

/**
 * @brief Get context-aware syncopation weight for natural drive feel.
 *
 * Adjusts syncopation weight based on phrase position and beat position
 * within the bar. Creates more natural rhythmic variation:
 * - Phrase latter half (>0.5): Up to 1.3x boost for building momentum
 * - Beats 2/4 backbeat positions: 1.15x boost for groove emphasis
 *
 * @param base_weight Base syncopation weight from getSyncopationWeight()
 * @param phrase_progress Phrase progress (0.0-1.0)
 * @param beat_in_bar Beat position within bar (0-3)
 * @param section Section type for context
 * @return Adjusted syncopation weight (clamped to 0.40 max)
 */
float getContextualSyncopationWeight(float base_weight, float phrase_progress, int beat_in_bar,
                                     SectionType section);

// ============================================================================
// Drive Feel Mapping
// ============================================================================

/**
 * @brief Utility functions to map drive_feel (0-100) to various parameters.
 *
 * Drive controls the "forward motion" of the music:
 * - 0 = laid-back (relaxed, behind the beat)
 * - 50 = neutral (default)
 * - 100 = aggressive (driving, ahead of the beat)
 */
namespace DriveMapping {

/**
 * @brief Get timing offset multiplier based on drive feel.
 * @param drive Drive feel value (0-100)
 * @return Multiplier for timing offsets (0.5-1.5)
 *         - drive=0: 0.5 (half the push, more laid-back)
 *         - drive=50: 1.0 (neutral)
 *         - drive=100: 1.5 (stronger push, more driving)
 */
inline float getTimingMultiplier(uint8_t drive) {
  return 0.5f + drive * 0.01f;  // 0.5 to 1.5
}

/**
 * @brief How much of the style's groove pocket an explicit drive setting asks for.
 *
 * getTimingMultiplier() only scales a pocket that something else decided to
 * apply, so it cannot answer "did the caller ask for micro-timing at all" -- it
 * never returns 0. This does: drive is documented as 50 = neutral, and neutral
 * asks for no push and no lay-back, so a song that sets no timing options is
 * left exactly on the grid. Moving the control toward either end asks for the
 * pocket in proportion to the distance travelled.
 *
 * @param drive Drive feel value (0-100)
 * @return Amount of groove timing requested (0.0 at neutral, 1.0 at either end)
 */
inline float getGrooveTimingAmount(uint8_t drive) {
  int distance = drive > 50 ? drive - 50 : 50 - drive;
  return std::min(1.0f, static_cast<float>(distance) / 50.0f);
}

/**
 * @brief Get velocity attack boost based on drive feel.
 * @param drive Drive feel value (0-100)
 * @return Multiplier for attack velocity (0.9-1.1)
 *         - drive=0: 0.9 (softer attacks)
 *         - drive=50: 1.0 (neutral)
 *         - drive=100: 1.1 (harder attacks)
 */
inline float getVelocityAttack(uint8_t drive) {
  return 0.9f + drive * 0.002f;  // 0.9 to 1.1
}

/**
 * @brief Get syncopation boost based on drive feel.
 * @param drive Drive feel value (0-100)
 * @return Multiplier for syncopation weight (0.8-1.2)
 *         - drive=0: 0.8 (less syncopation, more on-beat)
 *         - drive=50: 1.0 (neutral)
 *         - drive=100: 1.2 (more syncopation, more groove)
 */
inline float getSyncopationBoost(uint8_t drive) {
  return 0.8f + drive * 0.004f;  // 0.8 to 1.2
}

/**
 * @brief Get phrase-end duration stretch based on drive feel.
 * @param drive Drive feel value (0-100)
 * @return Duration stretch multiplier (1.08-1.02)
 *         - drive=0: 1.08 (longer phrase endings, more breath)
 *         - drive=50: 1.05 (neutral)
 *         - drive=100: 1.02 (shorter phrase endings, more urgent)
 */
inline float getPhraseEndStretch(uint8_t drive) {
  return 1.08f - drive * 0.0006f;  // 1.08 to 1.02
}

/// @brief High pitch delay (high notes need preparation -> slightly late).
///
/// Singers naturally take slightly longer to hit high notes because they
/// require more breath control and preparation. This adds realism to
/// vocal melody generation.
///
/// @param pitch MIDI note number
/// @param tessitura_center Tessitura center note (typical vocal range center)
/// @return Delay in ticks (0-12)
inline int getHighPitchDelay(uint8_t pitch, uint8_t tessitura_center) {
  if (pitch <= tessitura_center) return 0;
  int diff = pitch - tessitura_center;
  return std::min(diff, 12);  // Max 12 ticks (~6ms @120BPM)
}

/// @brief Leap landing delay (after large leap, slight delay for stability).
///
/// Large melodic intervals require more time to execute accurately.
/// Singers instinctively take a moment to stabilize pitch after
/// jumping a significant interval.
///
/// @param interval_semitones Absolute interval from previous note
/// @return Delay in ticks (0-8)
inline int getLeapLandingDelay(int interval_semitones) {
  if (interval_semitones < 5) return 0;  // Under perfect 4th: ignore
  if (interval_semitones < 7) return 4;  // 5-6 semitones: 4 ticks
  return 8;                              // 7+ semitones: 8 ticks
}

/// @brief Post-breath soft start (slight delay after breathing).
///
/// After taking a breath, singers need a moment to begin phonation.
/// This applies a subtle delay to notes that follow a breath gap.
///
/// @param is_post_breath Whether there was a breath before this note
/// @return Delay in ticks (0-6)
inline int getPostBreathDelay(bool is_post_breath) { return is_post_breath ? 6 : 0; }

}  // namespace DriveMapping

// ============================================================================
// Vocal Physics Parameters
// ============================================================================

/// @brief Forward declaration for VocalStylePreset.
enum class VocalStylePreset : uint8_t;

/// @brief Vocal physics scaling parameters.
///
/// Controls how much human singing physics are applied to vocal generation.
/// Human singers have natural timing delays for high notes, breath pauses,
/// and pitch instability. Vocaloid and UltraVocaloid styles reduce these
/// effects for a more mechanical, precise sound.
struct VocalPhysicsParams {
  float timing_scale = 1.0f;      ///< Timing delay scale (0=mechanical, 1=human)
  float breath_scale = 1.0f;      ///< Breath duration scale
  float pitch_bend_scale = 1.0f;  ///< Pitch bend depth scale
  bool requires_breath = true;    ///< Whether breath gaps are meaningful
  uint8_t max_phrase_bars = 8;    ///< Maximum bars before forced breath
};

/// @brief Get vocal physics parameters for a given vocal style.
///
/// Returns appropriate physics scaling for the vocal style:
/// - UltraVocaloid: No human physics (completely mechanical)
/// - Vocaloid: 50% human physics (imitation level)
/// - Ballad: Enhanced human physics (more expressive)
/// - Idol: Slightly reduced physics (more agile)
/// - Rock: Standard physics with stronger attack
/// - Others (Standard, Auto, CityPop, Anime): Full human physics
///
/// @param style Vocal style preset
/// @return VocalPhysicsParams with appropriate scales
inline VocalPhysicsParams getVocalPhysicsParams(VocalStylePreset style) {
  // Use underlying value to avoid circular dependency with melody_types.h
  // VocalStylePreset values: Auto=0, Standard=1, Vocaloid=2, UltraVocaloid=3,
  //                          Idol=4, Ballad=5, Rock=6, CityPop=7, Anime=8
  uint8_t style_val = static_cast<uint8_t>(style);

  switch (style_val) {
    case 3:  // UltraVocaloid - completely mechanical, no human physics
      return {0.0f, 0.0f, 0.0f, false, 255};
    case 2:  // Vocaloid - 50% human physics imitation
      return {0.5f, 0.3f, 0.5f, true, 12};
    case 5:  // Ballad - enhanced human expression
      return {1.2f, 1.3f, 1.1f, true, 4};
    case 4:  // Idol - slightly more agile
      return {0.8f, 0.8f, 0.7f, true, 6};
    case 6:  // Rock - standard with stronger dynamics
      return {1.0f, 0.9f, 1.2f, true, 8};
    case 13:  // KPop - agile, moderate expression
      return {0.9f, 0.7f, 0.8f, true, 6};
    default:  // Standard, Auto, CityPop, Anime, extended styles
      return {1.0f, 1.0f, 1.0f, true, 8};
  }
}

// ============================================================================
// EmotionCurve-based Velocity Calculations
// ============================================================================

/**
 * @brief Calculate velocity ceiling based on EmotionCurve tension.
 *
 * Higher tension allows higher velocity ceiling, while low tension
 * limits the maximum velocity to maintain dynamic range.
 *
 * @param base_velocity Base velocity value (0-127)
 * @param tension Tension level from EmotionCurve (0.0-1.0)
 * @return Adjusted velocity ceiling (0-127)
 */
uint8_t calculateVelocityCeiling(uint8_t base_velocity, float tension);

// ============================================================================
// Phrase Note Velocity Curve
// ============================================================================

// Forward declaration
enum class ContourType : uint8_t;

/**
 * @brief Get phrase-internal velocity curve multiplier.
 *
 * Creates natural crescendo/decrescendo within a phrase based on note position.
 * The curve shape is adapted based on the phrase contour type:
 * - Peak contour: climax at 60% through the phrase
 * - Other contours: climax at 75% through the phrase
 *
 * Velocity curve:
 * - Before climax: gradual crescendo (0.88 -> 1.08)
 * - After climax: gradual decrescendo (1.08 -> 0.92)
 *
 * @param note_index Current note index in phrase (0-based)
 * @param total_notes Total number of notes in the phrase
 * @param contour Optional phrase contour type affecting climax position
 * @return Velocity multiplier (0.85-1.10)
 */
float getPhraseNoteVelocityCurve(int note_index, int total_notes, ContourType contour);

/**
 * @brief Get chord tone preference based on EmotionCurve resolution_need.
 *
 * Higher resolution_need means melody should favor chord tones over
 * non-chord tones for more stable, resolved sound.
 *
 * The curve is planned after the arrangement is fixed, which is after the
 * melody has chosen its pitches, so nothing in the generation path calls this.
 * It states the mapping the curve's resolution_need was designed for; wiring it
 * up means moving the decision to a point where notes are still being chosen.
 *
 * @param resolution_need Resolution need from EmotionCurve (0.0-1.0)
 * @return Chord tone probability boost (0.0-0.3)
 */
float getChordTonePreferenceBoost(float resolution_need);

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_VELOCITY_H
