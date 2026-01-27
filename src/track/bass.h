/**
 * @file bass.h
 * @brief Bass track generation with vocal-first adaptation.
 *
 * Patterns: Whole, Root-Fifth, Syncopated, Driving, Walking.
 */

#ifndef MIDISKETCH_TRACK_BASS_H
#define MIDISKETCH_TRACK_BASS_H

#include <random>
#include <vector>

#include "core/midi_track.h"
#include "core/song.h"
#include "core/types.h"
#include "track/vocal_analysis.h"

namespace midisketch {

class IHarmonyContext;

/// Bass pattern analysis for chord voicing coordination (avoid doubling).
struct BassAnalysis {
  bool has_root_on_beat1 = true;   ///< Root note sounds on beat 1 (strong)
  bool has_root_on_beat3 = false;  ///< Root note sounds on beat 3 (secondary strong)
  bool has_fifth = false;          ///< Pattern includes 5th above root
  bool uses_octave_jump = false;   ///< Pattern includes octave leaps
  uint8_t root_note = 0;           ///< MIDI pitch of the root being played
  std::vector<Tick> accent_ticks;  ///< Tick positions of accented notes (vel >= 90)

  /// Analyze bar for root positions, 5th usage, and accents.
  static BassAnalysis analyzeBar(const MidiTrack& track, Tick bar_start, uint8_t expected_root);
};

/// Generate bass track with pattern selection based on section type.
/// @param kick_cache Optional pre-computed kick positions for Bass-Kick sync (can be nullptr)
void generateBassTrack(MidiTrack& track, const Song& song, const GeneratorParams& params,
                       std::mt19937& rng, const IHarmonyContext& harmony,
                       const KickPatternCache* kick_cache = nullptr);

/// Generate bass adapted to vocal (motion type, density reciprocity, doubling avoidance).
void generateBassTrackWithVocal(MidiTrack& track, const Song& song, const GeneratorParams& params,
                                std::mt19937& rng, const VocalAnalysis& vocal_analysis,
                                const IHarmonyContext& harmony);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_BASS_H
