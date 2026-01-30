/**
 * @file harmony_context.cpp
 * @brief Implementation of HarmonyContext facade.
 *
 * Delegates to ChordProgressionTracker, TrackCollisionDetector, and
 * SafePitchResolver for actual functionality.
 */

#include "core/harmony_context.h"

#include "core/arrangement.h"
#include "core/chord.h"
#include "core/midi_track.h"

namespace midisketch {

void HarmonyContext::initialize(const Arrangement& arrangement, const ChordProgression& progression,
                                Mood mood) {
  chord_tracker_.initialize(arrangement, progression, mood);
  collision_detector_.clearNotes();
}

int8_t HarmonyContext::getChordDegreeAt(Tick tick) const {
  return chord_tracker_.getChordDegreeAt(tick);
}

Tick HarmonyContext::getNextChordChangeTick(Tick after) const {
  return chord_tracker_.getNextChordChangeTick(after);
}

std::vector<int> HarmonyContext::getChordTonesAt(Tick tick) const {
  return chord_tracker_.getChordTonesAt(tick);
}

void HarmonyContext::registerNote(Tick start, Tick duration, uint8_t pitch, TrackRole track) {
  collision_detector_.registerNote(start, duration, pitch, track);
}

void HarmonyContext::registerTrack(const MidiTrack& track, TrackRole role) {
  collision_detector_.registerTrack(track, role);
}

bool HarmonyContext::isPitchSafe(uint8_t pitch, Tick start, Tick duration, TrackRole exclude,
                                 bool is_weak_beat) const {
  return collision_detector_.isPitchSafe(pitch, start, duration, exclude, &chord_tracker_,
                                         is_weak_beat);
}

uint8_t HarmonyContext::getBestAvailablePitch(uint8_t desired, Tick start, Tick duration,
                                              TrackRole track, uint8_t low, uint8_t high) const {
  return pitch_resolver_.getBestAvailablePitch(desired, start, duration, track, low, high,
                                               chord_tracker_, collision_detector_);
}

void HarmonyContext::clearNotes() { collision_detector_.clearNotes(); }

void HarmonyContext::clearNotesForTrack(TrackRole track) {
  collision_detector_.clearNotesForTrack(track);
}

bool HarmonyContext::hasBassCollision(uint8_t pitch, Tick start, Tick duration,
                                      int threshold) const {
  return collision_detector_.hasBassCollision(pitch, start, duration, threshold);
}

std::vector<int> HarmonyContext::getPitchClassesFromTrackAt(Tick tick, TrackRole role) const {
  return collision_detector_.getPitchClassesFromTrackAt(tick, role);
}

std::vector<int> HarmonyContext::getPitchClassesFromTrackInRange(Tick start, Tick end,
                                                                  TrackRole role) const {
  return collision_detector_.getPitchClassesFromTrackInRange(start, end, role);
}

void HarmonyContext::registerSecondaryDominant(Tick start, Tick end, int8_t degree) {
  chord_tracker_.registerSecondaryDominant(start, end, degree);
}

std::string HarmonyContext::dumpNotesAt(Tick tick, Tick range_ticks) const {
  return collision_detector_.dumpNotesAt(tick, range_ticks);
}

Tick HarmonyContext::getMaxSafeEnd(Tick note_start, uint8_t pitch, TrackRole exclude,
                                   Tick desired_end) const {
  return collision_detector_.getMaxSafeEnd(note_start, pitch, exclude, desired_end);
}

}  // namespace midisketch
