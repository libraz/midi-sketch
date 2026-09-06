/**
 * @file motif_rhythm.h
 * @brief The motif's rhythmic vocabulary: templates, spans and note lengths.
 *
 * Under RhythmSync the motif is the coordinate axis, so its rhythm is not a
 * private detail of the motif generator -- the vocal takes its onsets from it
 * and the phrase cache stores it. Keeping it here means a track that needs a
 * template does not have to include the motif generator to reach one.
 *
 * A template states beat positions and accent weights for one statement of a
 * figure. What it does not state is how long that statement lasts: a template
 * can span two bars while the configured length says one, and anything laying
 * statements end to end has to advance by what the pattern covers rather than
 * by what was asked for. That measurement lives here too, next to the tables
 * that make it necessary.
 */

#ifndef MIDISKETCH_TRACK_MOTIF_MOTIF_RHYTHM_H
#define MIDISKETCH_TRACK_MOTIF_MOTIF_RHYTHM_H

#include <cstdint>
#include <random>
#include <vector>

#include "core/basic_types.h"
#include "core/motif_types.h"

namespace midisketch {

struct NoteEvent;

namespace motif_detail {

/// @brief Configuration for a single motif rhythm template.
struct MotifRhythmTemplateConfig {
  float beat_positions[16];
  float accent_weights[16];
  uint8_t note_count;
  MotifRhythmDensity effective_density;
};

/// @brief Get the template config for a given template ID.
const MotifRhythmTemplateConfig& getTemplateConfig(MotifRhythmTemplate tmpl);

/// @brief Select a rhythm template based on BPM.
MotifRhythmTemplate selectRhythmSyncTemplate(uint16_t bpm, std::mt19937& rng,
                                             bool prefer_straight_sixteenth = false,
                                             bool prefer_idol_chant = false);

/// @brief Generate rhythm positions from a template.
/// @return Vector of tick positions for one bar of the template.
std::vector<Tick> generateRhythmPositionsFromTemplate(MotifRhythmTemplate tmpl);

/// @brief Generate rhythm positions from a density setting rather than a template.
std::vector<Tick> generateRhythmPositions(MotifRhythmDensity density, MotifLength length,
                                          uint8_t note_count, std::mt19937& rng);

/// @brief The span one statement of a motif occupies, in ticks.
Tick motifCycleLength(Tick last_onset, MotifLength configured_bars);

/// @brief The span one statement of a pattern occupies, in ticks.
///
/// Anything that lays a motif pattern end to end has to advance by the span the
/// pattern actually covers. `MotifParams::length` is a request, not a
/// measurement: a rhythm template supplies its own onsets and one of them spans
/// two bars, so a caller that strides by the configured bar count restarts that
/// pattern before it has finished and sounds each statement over the previous
/// one. Measuring the pattern keeps the two in agreement whatever chose the
/// rhythm.
Tick motifCycleLengthOf(const std::vector<NoteEvent>& pattern, MotifLength configured_bars);

/// @brief How long a riff note sounds inside the space it was given.
///
/// Half the space, and never shorter than a sixteenth. Articulation is a
/// proportion, not a fixed margin: subtracting a constant number of ticks
/// separates the notes of a slow figure by a hair and leaves the fastest ones
/// touching, because the constant is a smaller share of a wide gap than of a
/// narrow one and eventually exceeds it. A riff whose notes fill the space
/// between their onsets stops being a riff and becomes a pad.
///
/// The floor is what keeps the rule from turning a slow figure into a row of
/// clicks: at sixteenth spacing the two clauses meet and the note fills its
/// space, which is how a fast run is played, while an eighth-spaced figure
/// lands on exactly one sixteenth and a quarter-spaced one on an eighth.
///
/// @param gap Ticks from this onset to the next
/// @return Sounding length in ticks
Tick riffNoteDuration(Tick gap);

}  // namespace motif_detail

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_MOTIF_MOTIF_RHYTHM_H
