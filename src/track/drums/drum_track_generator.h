/**
 * @file drum_track_generator.h
 * @brief Unified drum track generation implementation.
 *
 * This module consolidates the common logic from generateDrumsTrack() and
 * generateDrumsTrackWithVocal() into a single implementation with optional
 * vocal synchronization via callback.
 */

#ifndef MIDISKETCH_TRACK_DRUMS_DRUM_TRACK_GENERATOR_H
#define MIDISKETCH_TRACK_DRUMS_DRUM_TRACK_GENERATOR_H

#include <functional>
#include <optional>
#include <random>

#include "core/midi_track.h"
#include "core/preset_data.h"
#include "core/production_blueprint.h"
#include "core/section_types.h"
#include "core/song.h"
#include "core/types.h"
#include "track/drums/hihat_control.h"
#include "track/vocal_analysis.h"

namespace midisketch {
namespace drums {

/// @brief Callback for vocal-synced kick generation.
/// @param track Target track
/// @param bar_start Start tick of bar
/// @param bar_end End tick of bar
/// @param section Current section
/// @param velocity Base velocity for kicks
/// @param rng Random number generator
/// @return true if kicks were added (no fallback needed), false for fallback pattern
using VocalSyncCallback = std::function<bool(
    MidiTrack& track, Tick bar_start, Tick bar_end, const Section& section,
    uint8_t velocity, std::mt19937& rng)>;

/// @brief Parameters for drum track generation.
struct DrumGenerationParams {
  Mood mood;
  uint16_t bpm;
  uint8_t blueprint_id;
  CompositionStyle composition_style;
  GenerationParadigm paradigm;
  MotifDrumParams motif_drum;
};

/// @brief Section-level context for drum generation.
struct DrumSectionContext {
  DrumStyle style = DrumStyle::Standard;
  DrumGrooveFeel groove = DrumGrooveFeel::Swing;
  float density_mult;
  bool add_crash_accent;
  bool use_ghost_notes;
  bool use_ride;
  bool motif_open_hh;
  int ohh_bar_interval = 0;
  bool use_foot_hh = false;
  HiHatLevel hh_level = HiHatLevel::Eighth;
  bool is_background_motif = false;
};

/// @brief Compute section-level drum generation context.
/// @param section Current section
/// @param params Generation parameters
/// @param style Base drum style
/// @param rng Random number generator
/// @return Section context for drum generation
DrumSectionContext computeSectionContext(const Section& section,
                                          const DrumGenerationParams& params,
                                          DrumStyle style,
                                          std::mt19937& rng);

/// @brief Unified drum track generation implementation.
///
/// This is the core implementation shared by generateDrumsTrack() and
/// generateDrumsTrackWithVocal(). The vocal_sync_callback is optional:
/// - nullptr: Normal kick pattern generation
/// - provided: Tries vocal-synced kicks, falls back to pattern if returns false
///
/// @param track Target MidiTrack
/// @param song Song with arrangement
/// @param params Generation parameters
/// @param rng Random number generator
/// @param vocal_sync_callback Optional callback for vocal-synced kicks
void generateDrumsTrackImpl(MidiTrack& track, const Song& song,
                            const DrumGenerationParams& params,
                            std::mt19937& rng,
                            VocalSyncCallback vocal_sync_callback = nullptr);

/// @brief Create vocal sync callback for kick drum synchronization.
/// @param vocal_analysis Vocal analysis data
/// @return Callback function for generateDrumsTrackImpl
VocalSyncCallback createVocalSyncCallback(const VocalAnalysis& vocal_analysis);

}  // namespace drums
}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_DRUMS_DRUM_TRACK_GENERATOR_H
