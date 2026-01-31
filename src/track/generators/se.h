/**
 * @file se.h
 * @brief SE (Sound Effect) track generator implementing ITrackBase.
 *
 * SE track generates section markers, modulation events, and call-and-response patterns.
 * This track does not participate in pitch collision detection (TrackPriority::None).
 */

#ifndef MIDISKETCH_TRACK_GENERATORS_SE_H
#define MIDISKETCH_TRACK_GENERATORS_SE_H

#include <random>
#include <vector>

#include "core/track_base.h"
#include "core/types.h"

namespace midisketch {

class Song;
struct Section;

/// @brief SE track generator implementing ITrackBase interface.
///
/// Generates section markers, modulation events, and optional call-and-response patterns.
/// Note: SE doesn't participate in pitch collision detection (TrackPriority::None).
class SEGenerator : public TrackBase {
 public:
  SEGenerator() = default;
  ~SEGenerator() override = default;

  // =========================================================================
  // ITrackBase interface
  // =========================================================================

  TrackRole getRole() const override { return TrackRole::SE; }

  TrackPriority getDefaultPriority() const override { return TrackPriority::None; }

  PhysicalModel getPhysicalModel() const override {
    // SE has no pitch constraints (text events only in many cases)
    return PhysicalModel{0, 127, 1, 127, 30, false, 0};
  }

  void generateSection(MidiTrack& track, const Section& section,
                       TrackContext& ctx) override;

  /// @brief Generate full SE track using FullTrackContext.
  void generateFullTrack(MidiTrack& track, const FullTrackContext& ctx) override;

  /// @brief Generate SE track with call system.
  /// @param track Target track
  /// @param song Song containing arrangement
  /// @param call_enabled Enable call-and-response patterns
  /// @param call_notes_enabled Include pitched notes in calls
  /// @param intro_chant Intro chant style
  /// @param mix_pattern Mix breakdown pattern
  /// @param call_density Call frequency
  /// @param rng Random number generator
  void generateWithCalls(MidiTrack& track, const Song& song, bool call_enabled,
                         bool call_notes_enabled, IntroChant intro_chant, MixPattern mix_pattern,
                         CallDensity call_density, std::mt19937& rng);
};

// =============================================================================
// Standalone helper functions (for backward compatibility)
// =============================================================================

/// @brief Check if call feature should be enabled for a vocal style.
/// @param style VocalStylePreset to check
/// @returns true if calls should be enabled
bool isCallEnabled(VocalStylePreset style);

/// @brief Insert PPPH pattern at B→Chorus transitions.
/// @param track Target MidiTrack
/// @param sections Song sections
/// @param notes_enabled Whether to output as notes
void insertPPPHAtBtoChorus(MidiTrack& track, const std::vector<Section>& sections,
                           bool notes_enabled);

/// @brief Insert MIX pattern at Intro sections.
/// @param track Target MidiTrack
/// @param sections Song sections
/// @param notes_enabled Whether to output as notes
void insertMIXAtIntro(MidiTrack& track, const std::vector<Section>& sections, bool notes_enabled);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_GENERATORS_SE_H
