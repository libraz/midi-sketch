/**
 * @file guitar.h
 * @brief Electric guitar track generator implementing ITrackBase.
 *
 * Generates rhythm/lead guitar patterns for pop music.
 */

#ifndef MIDISKETCH_TRACK_GENERATORS_GUITAR_H
#define MIDISKETCH_TRACK_GENERATORS_GUITAR_H

#include "core/track_base.h"

namespace midisketch {

/// @brief Guitar playing style determined by mood program.
enum class GuitarStyle : uint8_t {
  Fingerpick,   ///< Nylon guitar (GM 25): arpeggiated chord tones
  Strum,        ///< Clean guitar (GM 27): rhythmic strumming
  PowerChord,   ///< Overdriven guitar (GM 29): root+5th downstrokes
  PedalTone,    ///< 16th note root pedal with octave variation
  RhythmChord   ///< 16th note root+5th power chord pattern
};

/// @brief Electric guitar track generator implementing ITrackBase interface.
///
/// Generates guitar patterns following chord progressions.
/// Uses GuitarModel for realistic chord voicings and createNoteAndAdd
/// for collision-safe note creation.
class GuitarGenerator : public TrackBase {
 public:
  GuitarGenerator() = default;
  ~GuitarGenerator() override = default;

  // =========================================================================
  // ITrackBase interface
  // =========================================================================

  TrackRole getRole() const override { return TrackRole::Guitar; }

  TrackPriority getDefaultPriority() const override { return TrackPriority::Lower; }

  PhysicalModel getPhysicalModel() const override { return PhysicalModels::kElectricGuitar; }

  /// @brief Generate full guitar track using FullTrackContext.
  void generateFullTrack(MidiTrack& track, const FullTrackContext& ctx) override;
};

/// @brief Get guitar style from GM program number.
/// @param program GM program number (25=Nylon, 27=Clean, 29=Overdriven)
/// @return Guitar style for generation
GuitarStyle guitarStyleFromProgram(uint8_t program);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_GENERATORS_GUITAR_H
