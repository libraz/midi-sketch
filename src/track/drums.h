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

/**
 * @brief Get hi-hat swing factor based on mood.
 *
 * Returns a mood-dependent swing factor for 16th note hi-hats.
 * Jazz/CityPop get stronger swing (0.7), IdolPop/Yoasobi get lighter swing (0.3),
 * and most other moods use standard swing (0.5).
 *
 * @param mood Current mood
 * @return Swing factor (0.0-1.0) to multiply with base swing offset
 */
float getHiHatSwingFactor(Mood mood);

/**
 * @brief Apply time feel offset to a tick position.
 *
 * Applies micro-timing adjustments based on the time feel setting.
 * LaidBack pushes notes slightly behind the beat (relaxed feel),
 * while Pushed pulls notes slightly ahead (driving feel).
 *
 * @param base_tick Original tick position
 * @param feel Time feel setting
 * @param bpm Song tempo (affects the timing offset in ticks)
 * @return Adjusted tick position
 */
Tick applyTimeFeel(Tick base_tick, TimeFeel feel, uint16_t bpm);

/**
 * @brief Get the default time feel for a mood.
 *
 * Maps moods to appropriate time feels:
 * - Ballad, Chill -> LaidBack (relaxed)
 * - EnergeticDance, Yoasobi -> Pushed (driving)
 * - Most others -> OnBeat (standard)
 *
 * @param mood Mood preset
 * @return TimeFeel appropriate for the mood
 */
TimeFeel getMoodTimeFeel(Mood mood);

// ============================================================================
// Kick Pattern Pre-computation (for Bass-Kick sync)
// ============================================================================

/**
 * @brief Compute predicted kick positions for a section.
 *
 * Returns a KickPatternCache containing the tick positions where kicks would
 * likely be placed based on section type, mood, and style. This can be used
 * by bass generation to align notes with kick hits for tighter groove.
 *
 * Note: This is a simplified prediction. Actual kick positions may differ
 * due to fills, humanization, and other variations.
 *
 * @param sections Song sections
 * @param mood Current mood (affects drum style)
 * @param bpm Tempo (for density calculation)
 * @return KickPatternCache with predicted kick positions
 */
KickPatternCache computeKickPattern(const std::vector<Section>& sections, Mood mood, uint16_t bpm);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_DRUMS_H
