/**
 * @file arpeggio.h
 * @brief Arpeggio track generator implementing ITrackBase.
 *
 * Generates arpeggio patterns following chord progressions with genre-specific styles.
 */

#ifndef MIDISKETCH_TRACK_GENERATORS_ARPEGGIO_H
#define MIDISKETCH_TRACK_GENERATORS_ARPEGGIO_H

#include <random>

#include "core/track_base.h"
#include "core/types.h"

namespace midisketch {

class Song;

/// @brief Arpeggio track generator implementing ITrackBase interface.
///
/// Generates arpeggio patterns following chord progressions.
class ArpeggioGenerator : public TrackBase {
 public:
  ArpeggioGenerator() = default;
  ~ArpeggioGenerator() override = default;

  // =========================================================================
  // ITrackBase interface
  // =========================================================================

  TrackRole getRole() const override { return TrackRole::Arpeggio; }

  TrackPriority getDefaultPriority() const override { return TrackPriority::Lowest; }

  PhysicalModel getPhysicalModel() const override { return PhysicalModels::kArpeggioSynth; }

  /// A broken chord names the harmony one tone at a time, so a tone left
  /// sounding past the change names the wrong chord.
  static constexpr ChordBoundaryPolicy kChordBoundary = ChordBoundaryPolicy::ClipAtBoundary;
  ChordBoundaryPolicy getChordBoundaryPolicy() const override { return kChordBoundary; }

  /// A running figure thins rather than stops, so it gives up the last beat
  /// where the riff and the strum give up the last half.
  static constexpr Tick kPhraseTailSilence = TICKS_PER_BEAT * 3;
  Tick getPhraseTailSilenceOffset(bool phrase_tail_rest, uint8_t bar_index,
                                  uint8_t section_bars) const override {
    return phraseTailSilenceOffset(phrase_tail_rest, bar_index, section_bars, kPhraseTailSilence);
  }

  /// @brief Generate full arpeggio track using FullTrackContext.
  void doGenerateFullTrack(MidiTrack& track, const FullTrackContext& ctx) override;
};

// =============================================================================
// Standalone helper functions
// =============================================================================

/**
 * @brief Get genre-specific arpeggio style based on mood.
 *
 * Provides appropriate timbre, rhythm, and register for each genre.
 * This is the single source of truth for arpeggio GM program numbers.
 *
 * @param mood Mood preset
 * @return ArpeggioStyle with speed, octave_offset, swing, gm_program, gate
 */
ArpeggioStyle getArpeggioStyleForMood(Mood mood);

/**
 * @brief Octave span a section's arpeggio covers.
 *
 * A PeakLevel::Max section reaches one octave further, within the 1-3 the
 * option allows; a blueprint that prefers stepwise motion keeps one octave
 * there as everywhere else.
 *
 * @param requested ArpeggioParams::octave_range (1-3)
 * @param peak_level The section's peak level
 * @param prefer_stepwise The blueprint's stepwise constraint
 * @return Octave range, at most 3
 */
uint8_t resolveArpeggioOctaveRange(uint8_t requested, PeakLevel peak_level, bool prefer_stepwise);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_GENERATORS_ARPEGGIO_H
