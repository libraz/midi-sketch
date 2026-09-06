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
  Fingerpick,    ///< Nylon guitar (GM 25): arpeggiated chord tones
  Strum,         ///< Clean guitar (GM 27): rhythmic strumming
  PowerChord,    ///< Overdriven guitar (GM 29): root+5th downstrokes
  PedalTone,     ///< 16th note root pedal with octave variation
  RhythmChord,   ///< 16th note root+5th power chord pattern
  TremoloPick,   ///< 32nd note tremolo picking / scale run
  SweepArpeggio  ///< 32nd note sweep arpeggio across chord tones
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

  /// A strum spells the chord the same way the chord track does.
  static constexpr ChordBoundaryPolicy kChordBoundary = ChordBoundaryPolicy::ClipAtBoundary;
  ChordBoundaryPolicy getChordBoundaryPolicy() const override { return kChordBoundary; }

  /// @brief Generate full guitar track using FullTrackContext.
  void doGenerateFullTrack(MidiTrack& track, const FullTrackContext& ctx) override;
};

/// @brief Get guitar style from GM program number.
/// @param program GM program number (25=Nylon, 27=Clean, 29=Overdriven)
/// @return Guitar style for generation
GuitarStyle guitarStyleFromProgram(uint8_t program);

namespace guitar_detail {

/// @brief How long a strummed chord sounds inside the space it was given.
///
/// Every stroke sounding the same fixed length erases two different things.
///
/// The first is the figure. A pattern that strikes the first and third beats
/// and answers each with a pickup gives its downstrokes three eighths of space;
/// a duration fixed at a fraction of one eighth cuts them off early, and a bar
/// that should ring twice and answer twice arrives as four equal chops with
/// silence between them. A downstroke therefore takes half the space it was
/// given.
///
/// The second is the difference between the two hands. On a figure that strikes
/// every eighth the hand never stops moving, and the upstroke between the beats
/// is damped -- that is what makes the pattern read as rhythm rather than as a
/// held chord, and it is why such an upstroke is already struck more softly
/// than the beat around it. Where the figure leaves rests instead, the upstroke
/// is part of a two-stroke gesture and rings like the stroke it answers, so the
/// cut is asked for only on the continuous figure. The reference backings
/// separate on exactly this line: the ones that fill every eighth cut about
/// half their notes, the ones that leave space cut almost none.
///
/// The floor under a ringing stroke is the length a straight-eighth strum
/// already sounded; at that spacing there is nothing to open up.
///
/// @param upstroke Whether the hand moved up on this stroke
/// @param continuous_figure Whether the pattern strikes every eighth
/// @param gap Ticks from this stroke to the next one the pattern asks for
/// @return Sounding length in ticks, never longer than the gap
Tick strumNoteDuration(bool upstroke, bool continuous_figure, Tick gap);

}  // namespace guitar_detail

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_GENERATORS_GUITAR_H
