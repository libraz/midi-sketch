/**
 * @file chord_note_writer.h
 * @brief Putting a chosen voicing on the track without losing the chord.
 *
 * A voicing is a set of pitches; what reaches the track is fewer of them. Every
 * voice goes out through the note_creator API, and that path may move a pitch
 * or refuse it outright, so the order voices are offered in decides which ones
 * survive a crowded bar. They are offered by how much of the chord's identity
 * each carries -- offering them low to high let the fifth take the last free
 * slot and left a chord with no quality.
 *
 * Below a floor the subtraction has to stop. A single surviving voice states no
 * harmony at all, so when too few have been placed at an onset the minimum-voice
 * fill puts tones back even at the cost of a doubling.
 *
 * The chord being written is the one the shared timeline holds, not one rebuilt
 * from a degree: rebuilding drops the quality the timeline stores, so a
 * registered secondary dominant would read back as its plain diatonic triad and
 * its leading tone would never sound.
 */

#ifndef MIDISKETCH_TRACK_CHORD_CHORD_NOTE_WRITER_H
#define MIDISKETCH_TRACK_CHORD_CHORD_NOTE_WRITER_H

#include <cstdint>

#include "core/basic_types.h"
#include "core/chord.h"
#include "track/chord/chord_rhythm.h"
#include "track/chord/voicing_generator.h"

namespace midisketch {

class MidiTrack;
class IHarmonyContext;

/// @brief State for tracking chord voicing note count per tick.
///
/// Used to ensure a minimum of 2 notes per chord voicing, even when
/// no safe unique pitches exist. When fewer than kMinRequired notes
/// have been added, we allow doubling or even collision to maintain
/// functional harmony.
struct ChordVoicingState {
  Tick current_tick = 0;          ///< Current tick being processed
  uint8_t safe_count = 0;         ///< Number of safe notes added at current_tick
  uint8_t added_pitches[8] = {};  ///< Pitches added at current_tick (max 8)
  uint8_t added_pitch_count = 0;  ///< Number of entries in added_pitches
  ChordTones chord_tones{};       ///< Tones of the chord sounding at current_tick

  static constexpr uint8_t kMinRequired = 3;  ///< Minimum notes for full chord voicing

  /// Reset state for a new tick.
  void reset(Tick tick) {
    current_tick = tick;
    safe_count = 0;
    added_pitch_count = 0;
    chord_tones = ChordTones{};
  }

  /// Number of distinct pitch classes sounding at this tick.
  uint8_t distinctPitchClassCount() const {
    uint16_t seen = 0;
    for (uint8_t i = 0; i < added_pitch_count; ++i) {
      seen |= static_cast<uint16_t>(1U << (added_pitches[i] % 12));
    }
    uint8_t count = 0;
    for (uint8_t pc = 0; pc < 12; ++pc) {
      if (seen & (1U << pc)) ++count;
    }
    return count;
  }

  /// Check if more notes are needed to meet minimum.
  ///
  /// The quota is on distinct tones, not on note count: a root doubled at the
  /// octave adds a voice and no harmony, so three notes spanning two pitch
  /// classes is a power chord wearing a triad's note count and has neither a
  /// major nor a minor identity.
  bool needsMore() const {
    return safe_count < kMinRequired || distinctPitchClassCount() < kMinRequired;
  }

  /// Check if pitch was already added at this tick.
  bool hasPitch(uint8_t pitch) const {
    for (uint8_t i = 0; i < added_pitch_count; ++i) {
      if (added_pitches[i] == pitch) return true;
    }
    return false;
  }

  /// Check if this pitch class is already sounding at this tick.
  ///
  /// The voice quota counts notes, but what makes a chord a chord is its
  /// distinct tones: a root doubled at the octave adds a note and no harmony.
  bool hasPitchClass(uint8_t pitch) const {
    for (uint8_t i = 0; i < added_pitch_count; ++i) {
      if (added_pitches[i] % 12 == pitch % 12) return true;
    }
    return false;
  }

  /// Check whether adding this pitch would put two voices a step apart.
  ///
  /// The collision detector only compares this track against the others, so a
  /// cluster built entirely out of this track's own voices passes it unseen.
  ///
  /// Whether the two voices are a cluster is decided by isVoicingCluster(), so
  /// this screen and the cleanup pass cannot answer the same question
  /// differently.
  bool wouldCluster(uint8_t pitch) const {
    for (uint8_t i = 0; i < added_pitch_count; ++i) {
      if (isVoicingCluster(pitch, added_pitches[i], chord_tones)) return true;
    }
    return false;
  }

  /// Record that a note was added.
  void added(uint8_t pitch = 0) {
    ++safe_count;
    if (added_pitch_count < 8) {
      added_pitches[added_pitch_count++] = pitch;
    }
  }
};

/// @brief A chord exactly as the shared harmony timeline defines it.
struct TimelineChord {
  int8_t degree = 0;                                ///< Degree the timeline reports
  ChordExtension extension = ChordExtension::None;  ///< Quality the timeline reports
  Chord chord{};                                    ///< Interval set for the voicer
  uint8_t root = 0;                                 ///< Root pitch in the chord register
};

/// @brief Resolve the chord for [start, end) and make the timeline agree with it.
///
/// This is the only place the chord track turns a harmonic position into a
/// Chord. When the timeline has no planned quality for the range, the caller's
/// colour is registered before any note is created -- colouring locally without
/// registering is worse than not colouring at all, because getChordTonesAt()
/// would still report the plain triad and the collision resolver would erase
/// the added tone.
///
/// @param harmony Shared harmony context (mutated when the range is unplanned)
/// @param start Start tick of the range being voiced
/// @param end End tick of the range being voiced
/// @param fallback Quality to claim when the timeline has none for this range
/// @return The chord every track will read back for this range
TimelineChord claimChord(IHarmonyContext& harmony, Tick start, Tick end, ChordExtension fallback);

/// @brief Write one chord voice, letting the note creator resolve collisions.
void addSafeChordNote(MidiTrack& track, IHarmonyContext& harmony, Tick start, Tick duration,
                      uint8_t pitch, uint8_t velocity, uint8_t vocal_ceiling = 0);

/// @brief Write one chord voice, recording it against the voices already at this tick.
///
/// Where no safe unique pitch exists this prefers doubling another track's
/// pitch, and below the minimum-voice floor it will accept a collision rather
/// than leave the chord unstated. Above the floor the voice is simply skipped.
void addChordNoteWithState(MidiTrack& track, IHarmonyContext& harmony, Tick start, Tick duration,
                           uint8_t pitch, uint8_t velocity, ChordVoicingState& state,
                           uint8_t vocal_ceiling = 0);

/// @brief Put voices back at a tick that ended up with too few to state the chord.
void ensureMinVoicesAtTick(MidiTrack& track, IHarmonyContext& harmony, Tick tick, Tick duration,
                           uint8_t velocity, ChordVoicingState& state, uint8_t vocal_ceiling,
                           uint8_t voicing_low);

/// @brief How an eighth-note chord pulse is thinned.
///
/// A full voicing on all eight eighths lands at 24+ notes per bar, far above the
/// 7.3-12.9 that reference piano comping sits at, so an eighth pulse always has
/// to give something up. What it gives up differs by paradigm, and that is a
/// musical difference rather than an inconsistency: under RhythmSync the chord
/// track is a rhythmic bed tracking the motif's grid, where a bare low note on
/// the weak eighths is the point; everywhere else the chord track is comping,
/// and a bare low note on six eighths out of eight states no harmony at all.
/// Both thinned shapes apply only to an eighth that restates the chord already
/// sounding. An entry's own first onset always carries the whole voicing,
/// wherever in the bar the harmony happens to change.
enum class EighthPulseShape : uint8_t {
  Full,     ///< Every eighth carries the whole voicing
  Comping,  ///< Chords on beats 1 and 3, a two-voice shell on the pushes, rests between
  Stub,     ///< Chords on beats 1 and 3, the voicing's lowest note on every other eighth
};

/// @brief Write one rhythmic segment of a chord entry.
void generateChordSegment(MidiTrack& track, Tick bar_start, Tick segment_start,
                          Tick segment_duration, const chord_voicing::VoicedChord& voicing,
                          chord_voicing::ChordRhythm rhythm, SectionType section, Mood mood,
                          IHarmonyContext& harmony, uint8_t root_pitch_class,
                          uint8_t vocal_ceiling = 0,
                          EighthPulseShape pulse_shape = EighthPulseShape::Full);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_CHORD_CHORD_NOTE_WRITER_H
