/**
 * @file beat_processors.h
 * @brief Per-beat drum generation processors.
 *
 * These functions generate individual drum elements (kick, snare, ghost notes,
 * hi-hat) for a single beat. They are called from the main drum generation loop.
 */

#ifndef MIDISKETCH_TRACK_DRUMS_BEAT_PROCESSORS_H
#define MIDISKETCH_TRACK_DRUMS_BEAT_PROCESSORS_H

#include <random>

#include "core/midi_track.h"
#include "core/preset_data.h"
#include "core/section_types.h"
#include "core/types.h"
#include "track/drums/drum_track_generator.h"
#include "track/drums/hihat_control.h"
#include "track/drums/kick_patterns.h"

namespace midisketch {
namespace drums {

// ============================================================================
// Parameter Structs
// ============================================================================

/// @brief Common per-beat context shared across all beat processors.
///
/// Contains the beat position, velocity, section metadata, and RNG reference
/// that every beat processor needs. Constructed once per beat in the main
/// drum generation loop.
struct BeatContext {
  Tick beat_tick;              ///< Tick position of the beat
  uint8_t beat;               ///< Beat number within bar (0-3)
  uint8_t velocity;           ///< Base velocity for this beat
  SectionType section_type;   ///< Current section type
  Mood mood;                  ///< Current mood
  uint16_t bpm;               ///< Tempo in BPM
  uint8_t bar;                ///< Current bar number within section
  uint8_t section_bars;       ///< Total bars in section
  bool in_prechorus_lift;     ///< Whether in pre-chorus buildup zone
  std::mt19937& rng;          ///< Random number generator
};

/// @brief Kick drum-specific beat parameters.
struct KickBeatParams {
  Tick adjusted_beat_tick;     ///< Time-feel adjusted tick position
  const KickPattern& kick;    ///< Kick pattern flags
  float kick_prob;             ///< DrumRole-based kick probability
  float humanize_timing;      ///< Global humanization scaling (0.0-1.0)
};

/// @brief Snare drum-specific beat parameters.
struct SnareBeatParams {
  DrumStyle style;             ///< Drum style
  DrumRole role;               ///< Drum role
  float snare_prob;            ///< DrumRole-based snare probability
  bool use_groove_snare;       ///< Whether to use groove template snare pattern
  uint16_t groove_snare_pattern;  ///< Groove template snare bitmask
  bool is_intro_first;         ///< Whether this is first bar of intro
};

/// @brief Ghost note-specific beat parameters.
struct GhostBeatParams {
  BackingDensity backing_density;  ///< Backing density setting
  bool use_euclidean;              ///< Whether using Euclidean rhythms
  float groove_ghost_density;      ///< Ghost density from groove template
};

/// @brief Hi-hat-specific beat parameters.
struct HiHatBeatParams {
  DrumRole role;               ///< Drum role
  float density_mult;          ///< Density multiplier
  bool bar_has_open_hh;        ///< Whether this bar has open hi-hat accent
  uint8_t open_hh_beat;        ///< Beat for open hi-hat (if applicable)
  bool peak_open_hh_24;        ///< Whether peak level forces open HH on 2/4
  float swing_amount;          ///< Current swing amount
  DrumGrooveFeel groove;       ///< Groove feel
};

// ============================================================================
// Beat Processor Functions
// ============================================================================

/// @brief Generate kick drum for a single beat.
/// @param track Target MIDI track
/// @param beat_ctx Common beat context
/// @param params Kick-specific parameters
void generateKickForBeat(MidiTrack& track, const BeatContext& beat_ctx,
                         const KickBeatParams& params);

/// @brief Generate snare drum for a single beat.
/// @param track Target MIDI track
/// @param beat_ctx Common beat context
/// @param params Snare-specific parameters
void generateSnareForBeat(MidiTrack& track, const BeatContext& beat_ctx,
                          const SnareBeatParams& params);

/// @brief Generate ghost notes for a single beat.
/// @param track Target MIDI track
/// @param beat_ctx Common beat context
/// @param params Ghost note-specific parameters
void generateGhostNotesForBeat(MidiTrack& track, const BeatContext& beat_ctx,
                               const GhostBeatParams& params);

/// @brief Generate pre-chorus buildup pattern for a beat.
/// @param track Target MIDI track
/// @param beat_tick Tick position of the beat
/// @param beat Beat number (0-3)
/// @param velocity Base velocity
/// @param bar Current bar in section
/// @param section_bars Total bars in section
/// @param is_section_last_bar Whether this is the last bar
/// @return true if buildup was generated
bool generatePreChorusBuildup(MidiTrack& track, Tick beat_tick, uint8_t beat, uint8_t velocity,
                              uint8_t bar, uint8_t section_bars, bool is_section_last_bar);

/// @brief Generate hi-hat for a single beat.
/// @param track Target MIDI track
/// @param beat_ctx Common beat context
/// @param ctx Section context
/// @param params Hi-hat-specific parameters
void generateHiHatForBeat(MidiTrack& track, const BeatContext& beat_ctx,
                          const DrumSectionContext& ctx, const HiHatBeatParams& params);

/// @brief Get hi-hat swing factor based on mood.
/// @param mood Current mood
/// @return Swing factor (0.0-1.0)
float getHiHatSwingFactor(Mood mood);

/// @brief Apply time feel offset to tick position.
/// @param base_tick Original tick position
/// @param feel Time feel setting
/// @param bpm Tempo in BPM
/// @return Adjusted tick position
Tick applyTimeFeel(Tick base_tick, TimeFeel feel, uint16_t bpm);

/// @brief Get default time feel for a mood.
/// @param mood Current mood
/// @return Time feel setting
TimeFeel getMoodTimeFeel(Mood mood);

}  // namespace drums
}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_DRUMS_BEAT_PROCESSORS_H
