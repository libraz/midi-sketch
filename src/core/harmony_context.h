#ifndef MIDISKETCH_CORE_HARMONY_CONTEXT_H
#define MIDISKETCH_CORE_HARMONY_CONTEXT_H

#include "core/types.h"
#include <vector>

namespace midisketch {

class Arrangement;
class MidiTrack;
struct ChordProgression;

// HarmonyContext manages harmonic information for coordinated track generation.
// It knows the chord at each tick and tracks sounding notes from all tracks.
class HarmonyContext {
 public:
  HarmonyContext() = default;

  // Initialize with arrangement and chord progression.
  // This sets up chord information for every bar based on harmonic rhythm rules.
  void initialize(const Arrangement& arrangement,
                  const ChordProgression& progression,
                  Mood mood);

  // Get chord degree at a specific tick.
  // @param tick Position in ticks
  // @returns Scale degree (0=I, 1=ii, 2=iii, 3=IV, 4=V, 5=vi, 6=vii)
  int8_t getChordDegreeAt(Tick tick) const;

  // Get chord tones as pitch classes (0-11) at a specific tick.
  // @param tick Position in ticks
  // @returns Vector of pitch classes that are chord tones
  std::vector<int> getChordTonesAt(Tick tick) const;

  // Register a note from a track (for inter-track coordination).
  // Call this after adding a note to a track.
  // @param start Start tick of the note
  // @param duration Duration in ticks
  // @param pitch MIDI pitch
  // @param track Which track this note belongs to
  void registerNote(Tick start, Tick duration, uint8_t pitch, TrackRole track);

  // Register all notes from a track at once.
  // Call this after a track has been fully generated.
  // @param track The MidiTrack containing notes to register
  // @param role Which track role this represents
  void registerTrack(const MidiTrack& track, TrackRole role);

  // Check if a pitch is safe (doesn't create minor 2nd with registered notes).
  // @param pitch MIDI pitch to check
  // @param start Start tick
  // @param duration Duration in ticks
  // @param exclude Exclude notes from this track when checking
  // @returns true if pitch doesn't clash with other tracks
  bool isPitchSafe(uint8_t pitch, Tick start, Tick duration, TrackRole exclude) const;

  // Get a safe pitch that doesn't clash with other tracks.
  // Prefers chord tones, falls back to semitone adjustments.
  // @param desired Desired MIDI pitch
  // @param start Start tick
  // @param duration Duration in ticks
  // @param track Track that will play this note
  // @param low Minimum allowed pitch
  // @param high Maximum allowed pitch
  // @returns Safe pitch within range, or desired if no safe pitch found
  uint8_t getSafePitch(uint8_t desired, Tick start, Tick duration,
                       TrackRole track, uint8_t low, uint8_t high) const;

  // Clear all registered notes (for regeneration).
  void clearNotes();

  // Clear notes from a specific track.
  void clearNotesForTrack(TrackRole track);

 private:
  // Chord information for a tick range.
  struct ChordInfo {
    Tick start;
    Tick end;
    int8_t degree;
  };

  // Registered note from a track.
  struct RegisteredNote {
    Tick start;
    Tick end;
    uint8_t pitch;
    TrackRole track;
  };

  std::vector<ChordInfo> chords_;
  std::vector<RegisteredNote> notes_;

  // Helper: check if two pitch classes create a dissonant interval.
  static bool isDissonantInterval(int pc1, int pc2);

  // Helper: get pitch classes for chord tones of a degree.
  static std::vector<int> getChordTonePitchClasses(int8_t degree);
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_HARMONY_CONTEXT_H
