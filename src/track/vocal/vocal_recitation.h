/**
 * @file vocal_recitation.h
 * @brief Recitation device: a rapid same-pitch run at a phrase head.
 *
 * The figure is the driving patter of up-tempo idol and synth-vocal pop: one
 * pitch rearticulated about five times as fast as the singer can get the
 * syllables out, after which the line moves on by step. It is placed, not
 * emergent, so it is decided once per generated section and replayed with the
 * section's cached phrase.
 *
 * The run replaces the notes inside its span and hands over to a note the line
 * already has. Where the line's rhythm is locked to the coordinate axis, every
 * onset inside the span must stay on the run's lattice, so the run fills in
 * between the axis onsets instead of moving them.
 *
 * Run notes carry is_syllabic_subdivision: they are intentional same-pitch
 * rearticulation, and every pass that bounds same-pitch runs exempts that flag.
 */

#ifndef MIDISKETCH_TRACK_VOCAL_VOCAL_RECITATION_H
#define MIDISKETCH_TRACK_VOCAL_VOCAL_RECITATION_H

#include <cstdint>
#include <random>
#include <vector>

#include "core/basic_types.h"
#include "core/melody_types.h"
#include "core/section_types.h"
#include "core/timing_constants.h"

namespace midisketch {

class IHarmonyContext;

/// Shortest inter-onset interval a human singer is asked to articulate (ms).
constexpr int kHumanRecitationFloorMs = 110;
/// Shortest inter-onset interval for synthesized vocal styles (ms).
constexpr int kSynthRecitationFloorMs = 80;
/// Fewest notes that make a recitation run.
constexpr int kMinRecitationNotes = 4;
/// Most notes a recitation run may hold before the line has to move. A longer
/// unbroken repeat reads as a stuck line rather than a figure.
constexpr int kMaxRecitationNotes = 8;

/// @brief Inter-onset floor of a recitation note for a vocal style, in ms.
int recitationFloorMs(VocalStylePreset style);

/// @brief Finest run step: the first of a sixteenth, an eighth triplet and an
/// eighth whose duration at @p bpm is not below the style's floor. A coarser
/// one of the three is used where the line's onsets do not fit the finer.
Tick recitationStepTicks(VocalStylePreset style, uint16_t bpm);

/// @brief How strongly a section invites the device, relative to the section
/// that hands over to the chorus (1.0).
float recitationSectionWeight(SectionType type, bool leads_into_chorus);

/// @brief Where and how often one section may carry the device.
struct RecitationSpec {
  SectionType section_type = SectionType::A;
  bool leads_into_chorus = false;  ///< The section is followed by a Chorus
  float style_rate = 0.0f;         ///< Per-style rate (VocalStylePresetData::recitation_rate)
  Tick step = TICK_SIXTEENTH;      ///< Finest inter-onset interval of the run
  uint16_t bpm = 120;              ///< Tempo the step was derived from
  /// The line's onsets are a coordinate axis: the run may add onsets between
  /// them but must keep every one inside its span.
  bool keep_onsets = false;
  uint8_t vocal_low = 0;  ///< Range the step continuation may use
  uint8_t vocal_high = 127;
};

/**
 * @brief Place at most one recitation run in a section's vocal line.
 *
 * The run starts on a bar downbeat (beat 3 as a fallback) at least one bar into
 * the section, or in the last two bars when the section leads into a chorus. It
 * holds the anchor note's pitch, which must be a chord tone clear of the other
 * tracks at every run onset: the run ends where a chord change makes the held
 * pitch dissonant or the line rests for more than a beat, and a run shorter
 * than kMinRecitationNotes is not placed. The run hands over to a note a legal
 * scale step away: the line's own note at that tick, moved if needed, or a new
 * one held until the line's next onset. A section whose line already has a
 * same-pitch run at patter speed gets none.
 *
 * @param notes Section notes, sorted by start tick (modified in place)
 * @param part_start First tick of the section
 * @param part_end Tick after the section
 * @param spec Rate, step and range for this section
 * @param harmony Chord timeline and the tracks already registered
 * @param rng Random source used only by this device
 * @return Number of notes in the placed run, 0 when none was placed
 */
int placeRecitation(std::vector<NoteEvent>& notes, Tick part_start, Tick part_end,
                    const RecitationSpec& spec, const IHarmonyContext& harmony, std::mt19937& rng);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_VOCAL_VOCAL_RECITATION_H
