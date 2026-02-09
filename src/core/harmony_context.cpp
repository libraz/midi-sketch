/**
 * @file harmony_context.cpp
 * @brief Implementation of HarmonyContext facade.
 *
 * Delegates to ChordProgressionTracker and TrackCollisionDetector
 * for actual functionality.
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

ChordBoundaryInfo HarmonyContext::analyzeChordBoundary(uint8_t pitch, Tick start,
                                                        Tick duration) const {
  return chord_tracker_.analyzeChordBoundary(pitch, start, duration);
}

void HarmonyContext::registerNote(Tick start, Tick duration, uint8_t pitch, TrackRole track) {
  collision_detector_.registerNote(start, duration, pitch, track);
}

void HarmonyContext::registerTrack(const MidiTrack& track, TrackRole role) {
  collision_detector_.registerTrack(track, role);
}

bool HarmonyContext::isConsonantWithOtherTracks(uint8_t pitch, Tick start, Tick duration, TrackRole exclude,
                                 bool is_weak_beat) const {
  return collision_detector_.isConsonantWithOtherTracks(pitch, start, duration, exclude, &chord_tracker_,
                                         is_weak_beat);
}

CollisionInfo HarmonyContext::getCollisionInfo(uint8_t pitch, Tick start, Tick duration,
                                               TrackRole exclude) const {
  return collision_detector_.getCollisionInfo(pitch, start, duration, exclude, &chord_tracker_);
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

bool HarmonyContext::isSecondaryDominantAt(Tick tick) const {
  return chord_tracker_.isSecondaryDominantAt(tick);
}

std::string HarmonyContext::dumpNotesAt(Tick tick, Tick range_ticks) const {
  return collision_detector_.dumpNotesAt(tick, range_ticks);
}

CollisionSnapshot HarmonyContext::getCollisionSnapshot(Tick tick, Tick range_ticks) const {
  return collision_detector_.getCollisionSnapshot(tick, range_ticks);
}

Tick HarmonyContext::getMaxSafeEnd(Tick note_start, uint8_t pitch, TrackRole exclude,
                                   Tick desired_end) const {
  return collision_detector_.getMaxSafeEnd(note_start, pitch, exclude, desired_end);
}

std::vector<int> HarmonyContext::getSoundingPitchClasses(Tick start, Tick end,
                                                           TrackRole exclude) const {
  return collision_detector_.getSoundingPitchClasses(start, end, exclude);
}

std::vector<uint8_t> HarmonyContext::getSoundingPitches(Tick start, Tick end,
                                                          TrackRole exclude) const {
  return collision_detector_.getSoundingPitches(start, end, exclude);
}

uint8_t HarmonyContext::getHighestPitchForTrackInRange(Tick start, Tick end,
                                                        TrackRole role) const {
  return collision_detector_.getHighestPitchForTrackInRange(start, end, role);
}

uint8_t HarmonyContext::getLowestPitchForTrackInRange(Tick start, Tick end,
                                                        TrackRole role) const {
  return collision_detector_.getLowestPitchForTrackInRange(start, end, role);
}

void HarmonyContext::registerPhantomNote(Tick start, Tick duration, uint8_t pitch,
                                          TrackRole track) {
  collision_detector_.registerPhantomNote(start, duration, pitch, track);
}

void HarmonyContext::clearPhantomNotes() {
  collision_detector_.clearPhantomNotes();
}

}  // namespace midisketch
