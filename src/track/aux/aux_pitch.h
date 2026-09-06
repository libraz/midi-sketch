/**
 * @file aux_pitch.h
 * @brief Where an aux voice may sit, and what it may sound there.
 *
 * The aux track is a second voice against the lead, and the two rules that
 * distinguish it from a doubling are both about position: it keeps to a window
 * derived from the lead's tessitura rather than the instrument's range, and it
 * never crosses above the pitch the lead is sounding at that moment.
 *
 * The pitch resolver here deliberately does not delegate to the generic
 * note_creator candidate path. Strong-beat chord-tone preference, the melody's
 * own consonance tolerance and safe-versus-any chord-tone tracking have to run
 * in that order for a counter-melody; the generic path runs them in another,
 * and an aux line built through it drifts onto the lead.
 */

#ifndef MIDISKETCH_TRACK_AUX_AUX_PITCH_H
#define MIDISKETCH_TRACK_AUX_AUX_PITCH_H

#include <cstdint>
#include <vector>

#include "track/generators/aux.h"

namespace midisketch {

class IHarmonyContext;

/// @brief The pitch window this aux voice may use.
///
/// Derived from the lead's comfortable range rather than the instrument's, and
/// capped by the blueprint's ceiling when it sets one.
///
/// @param range_ceiling Blueprint ceiling relative to the lead's top, 0 to disable
void calculateAuxRange(const AuxConfig& config, const TessituraRange& main_tessitura,
                       uint8_t& out_low, uint8_t& out_high, int8_t range_ceiling = 0);

/// @brief Whether this pitch is consonant with both the lead and every registered track.
///
/// @param dissonance_tolerance How much of a clash against the lead is accepted
bool isConsonantWithMelodyAndTracks(uint8_t pitch, Tick start, Tick duration,
                                    const std::vector<NoteEvent>* main_melody,
                                    const IHarmonyContext& harmony,
                                    float dissonance_tolerance = 0.0f);

/// @brief Move a desired aux pitch onto one that can sound here.
///
/// The search ceiling is capped at the lowest concurrently sounding lead pitch,
/// so the counter-melody never crosses above the voice it answers. A safe chord
/// tone wins; failing that, any chord tone, which still states the harmony where
/// a non-chord tone would only clash.
uint8_t resolveAuxPitch(uint8_t desired, Tick start, Tick duration,
                        const std::vector<NoteEvent>* main_melody, const IHarmonyContext& harmony,
                        uint8_t low, uint8_t high, float dissonance_tolerance = 0.0f);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_AUX_AUX_PITCH_H
