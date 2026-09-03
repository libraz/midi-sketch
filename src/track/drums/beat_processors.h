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
#include "track/drums/groove_grid.h"
#include "track/drums/hihat_control.h"
#include "track/drums/kick_patterns.h"

namespace midisketch {
namespace drums {

/// @brief Resolve a section swing amount against the active groove feel.
float getEffectiveDrumSwing(DrumGrooveFeel groove, float swing_amount);

/// @brief Resolve the groove used by every track for a section.
DrumGrooveFeel resolveSectionDrumGroove(Mood mood, GenerationParadigm paradigm,
                                        float section_swing);

/// @brief Quantize a drum-grid tick using the shared effective swing.
Tick quantizeDrumSwing(Tick tick, DrumGrooveFeel groove, float swing_amount);

// ============================================================================
// Parameter Structs
// ============================================================================

/// @brief Common per-beat context shared across all beat processors.
///
/// Contains the beat position, velocity, section metadata, beat grid and RNG
/// reference that every beat processor needs. Constructed once per beat in the
/// main drum generation loop. The grid is the only source of timing offsets;
/// a processor that needs a tick asks the grid for it.
struct BeatContext {
  Tick beat_tick;            ///< Nominal tick position of the beat
  uint8_t beat;              ///< Beat number within bar (0-3)
  uint8_t velocity;          ///< Base velocity for this beat
  SectionType section_type;  ///< Current section type
  Mood mood;                 ///< Current mood
  uint16_t bpm;              ///< Tempo in BPM
  uint8_t bar;               ///< Current bar number within section
  uint8_t section_bars;      ///< Total bars in section
  bool in_prechorus_lift;    ///< Whether in pre-chorus buildup zone
  const GrooveGrid& grid;    ///< Beat grid shared by every voice in the bar
  std::mt19937& rng;         ///< Random number generator
};

/// @brief Kick drum-specific beat parameters.
struct KickBeatParams {
  const KickPattern& kick;  ///< Kick pattern flags
  float kick_prob;          ///< DrumRole-based kick probability
  float humanize_timing;    ///< Global humanization scaling (0.0-1.0)
};

/// @brief Snare drum-specific beat parameters.
struct SnareBeatParams {
  DrumStyle style;                     ///< Drum style
  DrumRole role;                       ///< Drum role
  float snare_prob;                    ///< DrumRole-based snare probability
  bool use_groove_snare;               ///< Whether to use groove template snare pattern
  uint16_t groove_snare_pattern;       ///< Groove template snare bitmask
  bool is_intro_first;                 ///< Whether this is first bar of intro
  bool bridge_crossstick_timekeeping;  ///< Whether sidestick already carries this Bridge beat
};

/// @brief Ghost note-specific beat parameters.
struct GhostBeatParams {
  BackingDensity backing_density;  ///< Backing density setting
  bool use_euclidean;              ///< Whether using Euclidean rhythms
  float groove_ghost_density;      ///< Ghost density from groove template
  float density_scale;             ///< Section note-density scale (0.0-1.0)
};

/// @brief Hi-hat-specific beat parameters.
struct HiHatBeatParams {
  DrumRole role;         ///< Drum role
  float density_mult;    ///< Density multiplier
  bool bar_has_open_hh;  ///< Whether this bar has open hi-hat accent
  uint8_t open_hh_beat;  ///< Beat for open hi-hat (if applicable)
  bool peak_open_hh_24;  ///< Whether peak level forces open HH on 2/4
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
/// @param grid Beat grid shared by every voice in the bar
/// @param beat_tick Nominal tick position of the beat
/// @param beat Beat number (0-3)
/// @param velocity Base velocity
/// @param bar Current bar in section
/// @param section_bars Total bars in section
/// @param is_section_last_bar Whether this is the last bar
/// @param style Drum style for genre-appropriate buildup
/// @param allow_snare Whether the section's drum role admits snare-family notes
/// @return true if buildup was generated
bool generatePreChorusBuildup(MidiTrack& track, const GrooveGrid& grid, Tick beat_tick,
                              uint8_t beat, uint8_t velocity, uint8_t bar, uint8_t section_bars,
                              bool is_section_last_bar, DrumStyle style, bool allow_snare);

/// @brief Generate hi-hat for a single beat.
/// @param track Target MIDI track
/// @param beat_ctx Common beat context
/// @param ctx Section context
/// @param params Hi-hat-specific parameters
void generateHiHatForBeat(MidiTrack& track, const BeatContext& beat_ctx,
                          const DrumSectionContext& ctx, const HiHatBeatParams& params);

}  // namespace drums
}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_DRUMS_BEAT_PROCESSORS_H
