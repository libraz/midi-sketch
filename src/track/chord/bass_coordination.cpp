/**
 * @file bass_coordination.cpp
 * @brief Implementation of bass and track collision avoidance.
 */

#include "track/chord/bass_coordination.h"

#include <cmath>

namespace midisketch {
namespace chord_voicing {

namespace {

bool chordContainsPitchClass(uint8_t root, const Chord& chord, int pitch_class) {
  int root_pc = root % 12;
  int pc = ((pitch_class % 12) + 12) % 12;
  for (uint8_t i = 0; i < chord.note_count && i < chord.intervals.size(); ++i) {
    if (chord.intervals[i] < 0) continue;
    if ((root_pc + chord.intervals[i]) % 12 == pc) {
      return true;
    }
  }
  return false;
}

bool chordContainsTritonePair(const Chord& chord) {
  for (uint8_t i = 0; i < chord.note_count && i < chord.intervals.size(); ++i) {
    if (chord.intervals[i] < 0) continue;
    for (uint8_t j = i + 1; j < chord.note_count && j < chord.intervals.size(); ++j) {
      if (chord.intervals[j] < 0) continue;
      int interval = std::abs(chord.intervals[i] - chord.intervals[j]) % 12;
      if (interval == 6) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

uint16_t buildBassPitchMask(const MidiTrack* bass_track, Tick bar_start, Tick bar_end) {
  if (bass_track == nullptr) return 0;

  uint16_t mask = 0;
  const std::array<Tick, 2> strong_beats = {bar_start, bar_start + 2 * TICKS_PER_BEAT};
  for (const auto& note : bass_track->notes()) {
    Tick note_end = note.start_tick + note.duration;
    for (Tick strong_beat : strong_beats) {
      if (strong_beat >= bar_end) {
        continue;
      }
      if (note.start_tick <= strong_beat && note_end > strong_beat) {
        mask |= (1 << (note.note % 12));
        break;
      }
    }
  }
  return mask;
}

bool clashesWithBass(int pitch_class, int bass_pitch_class) {
  int interval = std::abs(pitch_class - bass_pitch_class);
  if (interval > 6) interval = 12 - interval;
  // Minor 2nd (1) and Tritone (6) both clash with bass
  // Tritone creates harsh dissonance on strong beats (e.g., B vs F)
  return interval == 1 || interval == 6;
}

bool clashesWithBass(int pitch_class, int bass_pitch_class, uint8_t root, const Chord& chord) {
  int interval = std::abs(pitch_class - bass_pitch_class);
  if (interval > 6) interval = 12 - interval;
  if (interval == 1) {
    return true;
  }
  if (interval != 6) {
    return false;
  }

  return !(chordContainsTritonePair(chord) && chordContainsPitchClass(root, chord, pitch_class) &&
           chordContainsPitchClass(root, chord, bass_pitch_class));
}

bool clashesWithBassMask(int pitch_class, uint16_t bass_pitch_mask) {
  if (bass_pitch_mask == 0) return false;

  // Check against each bass pitch class in the mask
  for (int bass_pc = 0; bass_pc < 12; ++bass_pc) {
    if ((bass_pitch_mask & (1 << bass_pc)) != 0) {
      if (clashesWithBass(pitch_class, bass_pc)) {
        return true;
      }
    }
  }
  return false;
}

bool clashesWithBassMask(int pitch_class, uint16_t bass_pitch_mask, uint8_t root,
                         const Chord& chord) {
  if (bass_pitch_mask == 0) return false;

  for (int bass_pc = 0; bass_pc < 12; ++bass_pc) {
    if ((bass_pitch_mask & (1 << bass_pc)) != 0) {
      if (clashesWithBass(pitch_class, bass_pc, root, chord)) {
        return true;
      }
    }
  }
  return false;
}

bool voicingClashesWithBass(const VoicedChord& v, uint16_t bass_pitch_mask) {
  if (bass_pitch_mask == 0) return false;
  for (uint8_t i = 0; i < v.count; ++i) {
    if (clashesWithBassMask(v.pitches[i] % 12, bass_pitch_mask)) {
      return true;
    }
  }
  return false;
}

bool voicingClashesWithBass(const VoicedChord& v, uint16_t bass_pitch_mask, uint8_t root,
                            const Chord& chord) {
  if (bass_pitch_mask == 0) return false;
  for (uint8_t i = 0; i < v.count; ++i) {
    if (clashesWithBassMask(v.pitches[i] % 12, bass_pitch_mask, root, chord)) {
      return true;
    }
  }
  return false;
}

VoicedChord removeClashingPitch(const VoicedChord& v, uint16_t bass_pitch_mask) {
  if (bass_pitch_mask == 0) return v;

  VoicedChord result{};
  result.type = v.type;
  result.open_subtype = v.open_subtype;
  result.count = 0;

  for (uint8_t i = 0; i < v.count; ++i) {
    if (!clashesWithBassMask(v.pitches[i] % 12, bass_pitch_mask)) {
      result.pitches[result.count] = v.pitches[i];
      result.count++;
    }
  }

  return result;
}

VoicedChord removeClashingPitch(const VoicedChord& v, uint16_t bass_pitch_mask, uint8_t root,
                                const Chord& chord) {
  if (bass_pitch_mask == 0) return v;

  VoicedChord result{};
  result.type = v.type;
  result.open_subtype = v.open_subtype;
  result.count = 0;

  for (uint8_t i = 0; i < v.count; ++i) {
    if (!clashesWithBassMask(v.pitches[i] % 12, bass_pitch_mask, root, chord)) {
      result.pitches[result.count] = v.pitches[i];
      result.count++;
    }
  }

  return result;
}

// NOTE: clashesWithPitchClasses() and filterVoicingsForContext() have been removed.
// Chord voicing collision avoidance is now handled by
// IHarmonyContext::isConsonantWithOtherTracks(), which checks ALL registered tracks at tick-level
// granularity. See wouldClashWithRegisteredTracks() in chord.cpp.

}  // namespace chord_voicing
}  // namespace midisketch
