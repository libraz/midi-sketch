/**
 * @file song.h
 * @brief Song container holding all tracks and arrangement.
 */

#ifndef MIDISKETCH_CORE_SONG_H
#define MIDISKETCH_CORE_SONG_H

#include "core/arrangement.h"
#include "core/midi_track.h"
#include "core/types.h"

namespace midisketch {

/// @brief Song container holding all tracks and arrangement.
class Song {
 public:
  Song() = default;

  /// @name Track Accessors
  /// @{
  MidiTrack& vocal() { return vocal_; }
  MidiTrack& chord() { return chord_; }
  MidiTrack& bass() { return bass_; }
  MidiTrack& drums() { return drums_; }
  MidiTrack& se() { return se_; }
  MidiTrack& motif() { return motif_; }
  MidiTrack& arpeggio() { return arpeggio_; }
  MidiTrack& aux() { return aux_; }
  const MidiTrack& vocal() const { return vocal_; }
  const MidiTrack& chord() const { return chord_; }
  const MidiTrack& bass() const { return bass_; }
  const MidiTrack& drums() const { return drums_; }
  const MidiTrack& se() const { return se_; }
  const MidiTrack& motif() const { return motif_; }
  const MidiTrack& arpeggio() const { return arpeggio_; }
  const MidiTrack& aux() const { return aux_; }
  /// @}

  /// @name Role-Based Access
  /// @{
  MidiTrack& track(TrackRole role);
  const MidiTrack& track(TrackRole role) const;
  /// @}

  /// @name Track Management
  /// @{
  void clearTrack(TrackRole role);
  void replaceTrack(TrackRole role, const MidiTrack& newTrack);
  void clearAll();
  /// @}

  /// @name Arrangement
  /// @{
  void setArrangement(const Arrangement& arrangement);
  const Arrangement& arrangement() const { return arrangement_; }
  /// @}

  /// @name Time Info
  /// @{
  Tick ticksPerBar() const { return TICKS_PER_BAR; }
  Tick ticksPerBeat() const { return TICKS_PER_BEAT; }
  uint8_t beatsPerBar() const { return BEATS_PER_BAR; }
  /// @}

  /// @name Metadata
  /// @{
  void setBpm(uint16_t bpm) { bpm_ = bpm; }
  uint16_t bpm() const { return bpm_; }
  void setModulation(Tick tick, int8_t amount) {
    modulationTick_ = tick;
    modulationAmount_ = amount;
  }
  Tick modulationTick() const { return modulationTick_; }
  int8_t modulationAmount() const { return modulationAmount_; }
  /// @}

  /// @name Seed Tracking
  /// @{
  void setMelodySeed(uint32_t seed) { melody_seed_ = seed; }
  uint32_t melodySeed() const { return melody_seed_; }
  void setMotifSeed(uint32_t seed) { motif_seed_ = seed; }
  uint32_t motifSeed() const { return motif_seed_; }
  void setArpeggioSeed(uint32_t seed) { arpeggio_seed_ = seed; }
  uint32_t arpeggioSeed() const { return arpeggio_seed_; }
  /// @}

  /// @name Motif Pattern
  /// @{
  void setMotifPattern(const std::vector<NoteEvent>& pattern) {
    motif_pattern_ = pattern;
  }
  const std::vector<NoteEvent>& motifPattern() const { return motif_pattern_; }
  /// @}

  /// @name Phrase Boundaries (inter-track coordination)
  /// @{
  void setPhraseBoundaries(const std::vector<PhraseBoundary>& boundaries) {
    phrase_boundaries_ = boundaries;
  }
  void addPhraseBoundary(const PhraseBoundary& boundary) {
    phrase_boundaries_.push_back(boundary);
  }
  const std::vector<PhraseBoundary>& phraseBoundaries() const {
    return phrase_boundaries_;
  }
  void clearPhraseBoundaries() { phrase_boundaries_.clear(); }
  /// @}

 private:
  MidiTrack vocal_;
  MidiTrack chord_;
  MidiTrack bass_;
  MidiTrack drums_;
  MidiTrack se_;
  MidiTrack motif_;
  MidiTrack arpeggio_;
  MidiTrack aux_;
  Arrangement arrangement_;
  uint16_t bpm_ = 120;
  Tick modulationTick_ = 0;
  int8_t modulationAmount_ = 0;
  uint32_t melody_seed_ = 0;
  uint32_t motif_seed_ = 0;
  uint32_t arpeggio_seed_ = 0;
  std::vector<NoteEvent> motif_pattern_;
  std::vector<PhraseBoundary> phrase_boundaries_;
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_SONG_H
