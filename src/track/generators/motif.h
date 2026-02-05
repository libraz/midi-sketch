/**
 * @file motif.h
 * @brief Motif track generator implementing ITrackBase.
 *
 * Background motif track generation with RhythmSync/RhythmLock coordination support.
 * Motif can act as "coordinate axis" in RhythmSync paradigm with Locked policy.
 */

#ifndef MIDISKETCH_TRACK_GENERATORS_MOTIF_H
#define MIDISKETCH_TRACK_GENERATORS_MOTIF_H

#include <random>
#include <vector>

#include "core/track_base.h"
#include "core/types.h"

namespace midisketch {

class Song;
struct MotifContext;
struct GeneratorParams;

/// @brief Motif track generator implementing ITrackBase interface.
///
/// Generates background motif patterns following chord progressions.
/// Supports RhythmSync paradigm where Motif acts as "coordinate axis".
class MotifGenerator : public TrackBase {
 public:
  MotifGenerator() = default;
  ~MotifGenerator() override = default;

  // =========================================================================
  // ITrackBase interface
  // =========================================================================

  TrackRole getRole() const override { return TrackRole::Motif; }

  TrackPriority getDefaultPriority() const override { return TrackPriority::Medium; }

  PhysicalModel getPhysicalModel() const override { return PhysicalModels::kMotifSynth; }

  void generateSection(MidiTrack& track, const Section& section, TrackContext& ctx) override;

  /// @brief Generate full motif track using FullTrackContext.
  void generateFullTrack(MidiTrack& track, const FullTrackContext& ctx) override;
};

// =============================================================================
// Standalone helper functions
// =============================================================================

/// @brief Generates a single motif pattern (one cycle).
/// @param params Generation parameters
/// @param rng Random number generator
/// @returns Vector of NoteEvents for one motif cycle
std::vector<NoteEvent> generateMotifPattern(const GeneratorParams& params, std::mt19937& rng);

// Forward declarations for motif_detail functions used by generator.cpp
enum class MotifRhythmTemplate : uint8_t;
enum class MotifRhythmDensity : uint8_t;

namespace motif_detail {

/// @brief Configuration for a single motif rhythm template.
struct MotifRhythmTemplateConfig {
  float beat_positions[16];
  float accent_weights[16];
  uint8_t note_count;
  MotifRhythmDensity effective_density;
};

/// @brief Select a rhythm template based on BPM.
MotifRhythmTemplate selectRhythmSyncTemplate(uint16_t bpm, std::mt19937& rng);

/// @brief Get the template config for a given template ID.
const MotifRhythmTemplateConfig& getTemplateConfig(MotifRhythmTemplate tmpl);

}  // namespace motif_detail

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_GENERATORS_MOTIF_H
