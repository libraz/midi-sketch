#ifndef MIDISKETCH_CORE_SONG_H
#define MIDISKETCH_CORE_SONG_H

#include "core/arrangement.h"
#include "core/midi_track.h"
#include "core/types.h"

namespace midisketch {

// Overall song management class.
// Holds all tracks and provides regeneration control.
class Song {
 public:
  Song() = default;

  // Track accessors
  MidiTrack& vocal() { return vocal_; }
  MidiTrack& chord() { return chord_; }
  MidiTrack& bass() { return bass_; }
  MidiTrack& drums() { return drums_; }
  MidiTrack& se() { return se_; }
  MidiTrack& motif() { return motif_; }

  const MidiTrack& vocal() const { return vocal_; }
  const MidiTrack& chord() const { return chord_; }
  const MidiTrack& bass() const { return bass_; }
  const MidiTrack& drums() const { return drums_; }
  const MidiTrack& se() const { return se_; }
  const MidiTrack& motif() const { return motif_; }

  // Role-based access
  MidiTrack& track(TrackRole role);
  const MidiTrack& track(TrackRole role) const;

  // Track management
  void clearTrack(TrackRole role);
  void replaceTrack(TrackRole role, const MidiTrack& newTrack);
  void clearAll();

  // Arrangement
  void setArrangement(const Arrangement& arrangement);
  const Arrangement& arrangement() const { return arrangement_; }

  // Time info
  Tick ticksPerBar() const { return TICKS_PER_BAR; }
  Tick ticksPerBeat() const { return TICKS_PER_BEAT; }
  uint8_t beatsPerBar() const { return BEATS_PER_BAR; }

  // Metadata
  void setBpm(uint16_t bpm) { bpm_ = bpm; }
  uint16_t bpm() const { return bpm_; }

  void setModulation(Tick tick, int8_t amount) {
    modulationTick_ = tick;
    modulationAmount_ = amount;
  }
  Tick modulationTick() const { return modulationTick_; }
  int8_t modulationAmount() const { return modulationAmount_; }

  // Melody seed tracking (for getMelody/setMelody)
  void setMelodySeed(uint32_t seed) { melody_seed_ = seed; }
  uint32_t melodySeed() const { return melody_seed_; }

  // Motif seed tracking (for getMotif/setMotif)
  void setMotifSeed(uint32_t seed) { motif_seed_ = seed; }
  uint32_t motifSeed() const { return motif_seed_; }

  // Motif pattern storage (base pattern for repetition)
  void setMotifPattern(const std::vector<NoteEvent>& pattern) {
    motif_pattern_ = pattern;
  }
  const std::vector<NoteEvent>& motifPattern() const { return motif_pattern_; }

 private:
  MidiTrack vocal_;
  MidiTrack chord_;
  MidiTrack bass_;
  MidiTrack drums_;
  MidiTrack se_;
  MidiTrack motif_;
  Arrangement arrangement_;
  uint16_t bpm_ = 120;
  Tick modulationTick_ = 0;
  int8_t modulationAmount_ = 0;
  uint32_t melody_seed_ = 0;
  uint32_t motif_seed_ = 0;
  std::vector<NoteEvent> motif_pattern_;
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_SONG_H
