/**
 * @file voice_leading.h
 * @brief Voice leading optimization for chord voicing selection.
 *
 * Provides functions to select optimal voicings based on voice leading
 * principles: common tones, minimal movement, and avoiding parallel 5ths/octaves.
 */

#ifndef MIDISKETCH_TRACK_CHORD_TRACK_VOICE_LEADING_H
#define MIDISKETCH_TRACK_CHORD_TRACK_VOICE_LEADING_H

#include <random>

#include "core/mood_utils.h"
#include "core/section_properties.h"
#include "track/chord_track.h"
#include "track/chord_track/voicing_generator.h"

namespace midisketch {
namespace chord_voicing {

/// @name Voicing Type Selection
/// @{

/// Select voicing type based on section, mood, and bass pattern.
/// Design: Express section contrast through voicing spread, not rhythm density.
/// - A section: Close (stable foundation)
/// - B section: Close-dominant (reduce "darkness", build anticipation)
/// - Chorus: Open-dominant (spacious release, room for vocals)
/// - Bridge: Mixed (introspective flexibility)
/// @param section Current section type
/// @param mood Current mood
/// @param bass_has_root True if bass is playing the root note
/// @param rng Random number generator for probabilistic selection (optional)
/// @return Recommended voicing type
VoicingType selectVoicingType(SectionType section, Mood mood, bool bass_has_root,
                              std::mt19937* rng = nullptr);

/// Select open voicing subtype based on section and chord context.
/// @param section Current section type
/// @param mood Current mood
/// @param chord Current chord
/// @param rng Random number generator
/// @return Recommended open voicing subtype
OpenVoicingType selectOpenVoicingSubtype(SectionType section, Mood mood,
                                         const Chord& chord, std::mt19937& rng);

/// @}

/// @name Parallel Motion Penalty
/// @{

/// Get mood-dependent parallel motion penalty.
/// Classical/sophisticated moods enforce strict voice leading rules.
/// Pop/energetic moods allow parallel motion for power and energy.
/// @param mood Current mood
/// @return Penalty value (negative, applied to score)
int getParallelPenalty(Mood mood);

/// @}

/// @name Voicing Selection
/// @{

/// Select best voicing considering voice leading from previous chord.
/// @param root Root note (MIDI pitch)
/// @param chord Chord structure
/// @param prev_voicing Previous chord voicing
/// @param has_prev True if there is a previous voicing
/// @param preferred_type Preferred voicing type
/// @param bass_root_pc Bass note pitch class (0-11) for collision avoidance, or -1 if unknown
/// @param rng Random number generator
/// @param open_subtype Which open voicing variant to prefer
/// @param mood Current mood (affects parallel motion penalty)
/// @return Selected voicing
VoicedChord selectVoicing(uint8_t root, const Chord& chord, const VoicedChord& prev_voicing,
                          bool has_prev, VoicingType preferred_type, int bass_root_pc,
                          std::mt19937& rng, OpenVoicingType open_subtype = OpenVoicingType::Drop2,
                          Mood mood = Mood::StraightPop);

/// @}

/// @name Harmonic Functions
/// @{

/// Check if a chord degree is the dominant (V).
/// @param degree Chord degree (0-6)
/// @return True if degree is V (4)
bool isDominant(int8_t degree);

/// Check if the next section is a Chorus (for cadence preparation).
/// @param current Current section type
/// @param next Next section type
/// @param current_degree Current chord degree
/// @param mood Current mood
/// @return True if dominant preparation should be added
bool shouldAddDominantPreparation(SectionType current, SectionType next,
                                  int8_t current_degree, Mood mood);

/// Check if section ending needs a cadence fix for irregular progression lengths.
/// Returns true if the progression ends mid-cycle at section end.
/// @param section_bars Number of bars in section
/// @param progression_length Length of chord progression
/// @param section Current section type
/// @param next_section Next section type
/// @return True if cadence fix is needed
bool needsCadenceFix(uint8_t section_bars, uint8_t progression_length,
                     SectionType section, SectionType next_section);

/// Check if section type allows anticipation.
/// @param section Section type
/// @return True if anticipation is allowed
bool allowsAnticipation(SectionType section);

/// @}

}  // namespace chord_voicing
}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_CHORD_TRACK_VOICE_LEADING_H
