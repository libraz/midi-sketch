/**
 * @file drums.h
 * @brief Drum track generation for rhythmic foundation.
 */

#ifndef MIDISKETCH_TRACK_DRUMS_H
#define MIDISKETCH_TRACK_DRUMS_H

#include <random>

#include "core/midi_track.h"
#include "core/preset_data.h"
#include "core/song.h"
#include "core/types.h"
#include "track/vocal_analysis.h"

namespace midisketch {

/**
 * @brief Generate drum track with style-appropriate patterns.
 * @param track Target MidiTrack to populate with drum events
 * @param song Song containing arrangement and section information
 * @param params Generation parameters (mood affects pattern style)
 * @param rng Random number generator for fill variation
 * @note Drums use MIDI channel 9 (GM standard).
 */
void generateDrumsTrack(MidiTrack& track, const Song& song, const GeneratorParams& params,
                        std::mt19937& rng);

/**
 * @brief Generate drum track with vocal synchronization.
 *
 * When drums_sync_vocal is enabled, kick drums are placed to align with
 * vocal onset positions, creating a "rhythm lock" effect where the groove
 * follows the melody.
 *
 * @param track Target MidiTrack to populate with drum events
 * @param song Song containing arrangement and section information
 * @param params Generation parameters (mood affects pattern style)
 * @param rng Random number generator for fill variation
 * @param vocal_analysis Pre-analyzed vocal track data
 * @note Drums use MIDI channel 9 (GM standard).
 */
void generateDrumsTrackWithVocal(MidiTrack& track, const Song& song, const GeneratorParams& params,
                                 std::mt19937& rng, const VocalAnalysis& vocal_analysis);

// ============================================================================
// Swing Control API (for testing)
// ============================================================================

/**
 * @brief Calculate continuous swing amount based on section context.
 *
 * Returns a swing factor (0.0-0.7) that varies by section type and
 * progress within the section. This creates natural groove variation.
 *
 * @param section Section type (A, B, Chorus, etc.)
 * @param bar_in_section Current bar index within section (0-based)
 * @param total_bars Total bars in section
 * @param swing_override Optional explicit swing amount (-1 = use section default)
 * @return Swing amount (0.0 = straight, 0.5 = moderate swing, 0.7 = heavy)
 */
float calculateSwingAmount(SectionType section, int bar_in_section, int total_bars,
                          float swing_override = -1.0f);

/**
 * @brief Calculate swing offset with continuous control.
 *
 * Returns the tick offset to apply to off-beat notes. The offset varies
 * based on groove feel, section type, and progress within the section.
 *
 * @param groove Base groove feel (Straight, Swing, Shuffle)
 * @param subdivision Note subdivision (EIGHTH=240 or SIXTEENTH=120)
 * @param section Section type
 * @param bar_in_section Current bar index within section
 * @param total_bars Total bars in section
 * @param swing_override Optional explicit swing amount (-1 = use section default)
 * @return Tick offset to add to off-beat timing (0 for Straight groove)
 */
Tick getSwingOffsetContinuous(DrumGrooveFeel groove, Tick subdivision, SectionType section,
                               int bar_in_section, int total_bars,
                               float swing_override = -1.0f);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_DRUMS_H
