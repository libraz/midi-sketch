#include "core/song.h"

namespace midisketch {

MidiTrack& Song::track(TrackRole role) {
  switch (role) {
    case TrackRole::Vocal: return vocal_;
    case TrackRole::Chord: return chord_;
    case TrackRole::Bass: return bass_;
    case TrackRole::Drums: return drums_;
    case TrackRole::SE: return se_;
    case TrackRole::Motif: return motif_;
  }
  return vocal_;  // fallback
}

const MidiTrack& Song::track(TrackRole role) const {
  switch (role) {
    case TrackRole::Vocal: return vocal_;
    case TrackRole::Chord: return chord_;
    case TrackRole::Bass: return bass_;
    case TrackRole::Drums: return drums_;
    case TrackRole::SE: return se_;
    case TrackRole::Motif: return motif_;
  }
  return vocal_;  // fallback
}

void Song::clearTrack(TrackRole role) {
  track(role).clear();
}

void Song::replaceTrack(TrackRole role, const MidiTrack& newTrack) {
  track(role) = newTrack;
}

void Song::clearAll() {
  vocal_.clear();
  chord_.clear();
  bass_.clear();
  drums_.clear();
  se_.clear();
  motif_.clear();
  motif_pattern_.clear();
}

void Song::setArrangement(const Arrangement& arrangement) {
  arrangement_ = arrangement;
}

}  // namespace midisketch
