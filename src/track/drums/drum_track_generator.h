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
#include <vector>

#include "core/midi_track.h"
#include "core/preset_data.h"
#include "core/production_blueprint.h"
#include "core/section_types.h"
#include "core/song.h"
#include "core/types.h"
#include "track/drums/hihat_control.h"
#include "track/drums/percussion_generator.h"
#include "track/vocal/vocal_analysis.h"

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
using VocalSyncCallback =
    std::function<bool(MidiTrack& track, Tick bar_start, Tick bar_end, const Section& section,
                       uint8_t velocity, std::mt19937& rng)>;

/// @brief Parameters for drum track generation.
struct DrumGenerationParams {
  Mood mood;
  uint16_t bpm;
  uint8_t blueprint_id;
  CompositionStyle composition_style;
  GenerationParadigm paradigm;
  MotifDrumParams motif_drum;
  uint8_t drum_style_hint = 0;   ///< 0=auto, otherwise DrumStyle enum + 1
  bool humanize = false;         ///< Master humanize switch
  float humanize_timing = 1.0f;  ///< Global humanization scaling (0.0-1.0)

  /// @brief The blueprint the caller is running.
  ///
  /// Every blueprint value the drum generator reads comes from this entity, so
  /// a caller that builds or overrides a blueprint sees its own values in the
  /// output. When it is null the generator falls back to the shipped table
  /// entry for @ref blueprint_id, once, at the single resolution point.
  const ProductionBlueprint* blueprint = nullptr;
};

/// @brief Section-level context for drum generation.
///
/// Every quantity that decides how many events a section plays, and where in
/// the bar they land, is resolved here once. Beat processors read this context
/// and never re-derive groove, feel or density on their own.
struct DrumSectionContext {
  DrumStyle style = DrumStyle::Standard;
  DrumGrooveFeel groove = DrumGrooveFeel::Swing;
  TimeFeel time_feel = TimeFeel::OnBeat;  ///< Feel shared by every voice in the section
  float density_mult;
  float density_scale = 1.0f;  ///< Note-density scale from the section density percent
  bool add_crash_accent;
  bool use_ghost_notes;
  bool use_ride;
  bool motif_open_hh;
  int ohh_bar_interval = 0;
  bool use_foot_hh = false;
  HiHatLevel hh_level = HiHatLevel::Eighth;
  bool is_background_motif = false;
  bool has_drums = false;         ///< Whether the section plays drums at all
  PercussionConfig percussion{};  ///< Auxiliary percussion layers for the section
};

/// @brief Compute section-level drum generation context.
/// @param section Current section
/// @param params Generation parameters
/// @param blueprint The blueprint the caller is running
/// @param style Base drum style
/// @param rng Random number generator
/// @return Section context for drum generation
DrumSectionContext computeSectionContext(const Section& section, const DrumGenerationParams& params,
                                         const ProductionBlueprint& blueprint, DrumStyle style,
                                         std::mt19937& rng);

/// @brief Resolve the context of every section before any note is written.
///
/// Sections are resolved together because the energy arc is a relation between
/// them: a B section may not put more events in a bar than the chorus it leads
/// into, and that comparison needs the chorus settings up front.
///
/// @param sections Song sections in arrangement order
/// @param params Generation parameters
/// @param blueprint The blueprint the caller is running
/// @param style Base drum style
/// @param rng Random number generator
/// @return One context per section, aligned with @p sections
std::vector<DrumSectionContext> resolveSectionContexts(const std::vector<Section>& sections,
                                                       const DrumGenerationParams& params,
                                                       const ProductionBlueprint& blueprint,
                                                       DrumStyle style, std::mt19937& rng);

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
void generateDrumsTrackImpl(MidiTrack& track, const Song& song, const DrumGenerationParams& params,
                            std::mt19937& rng, VocalSyncCallback vocal_sync_callback = nullptr);

/// @brief Create vocal sync callback for kick drum synchronization.
/// @param vocal_analysis Vocal analysis data
/// @param bpm Tempo in BPM (used to limit kick density at high tempos)
/// @return Callback function for generateDrumsTrackImpl
VocalSyncCallback createVocalSyncCallback(const VocalAnalysis& vocal_analysis, uint16_t bpm);

/// @brief Create MelodyDriven callback for phrase-aware drum generation.
///
/// MelodyDriven paradigm differs from RhythmSync in that drums adapt to
/// vocal phrases rather than syncing to individual onsets:
/// - Fill placement at phrase boundaries (not beat-locked)
/// - Kick density increases during dense vocal sections
/// - Drum breaks considered during vocal rests
///
/// @param vocal_analysis Pre-analyzed vocal track data
/// @return Callback function for generateDrumsTrackImpl
VocalSyncCallback createMelodyDrivenCallback(const VocalAnalysis& vocal_analysis);

}  // namespace drums
}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_DRUMS_DRUM_TRACK_GENERATOR_H
