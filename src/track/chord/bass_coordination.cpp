/**
 * @file bass_coordination.cpp
 * @brief Implementation of bass and track collision avoidance.
 */

#include "track/chord/bass_coordination.h"

#include <cmath>

#include "core/timing_constants.h"

namespace midisketch {
namespace chord_voicing {

int getAuxPitchClassAt(const MidiTrack* aux_track, Tick tick) {
  if (aux_track == nullptr) return -1;

  for (const auto& note : aux_track->notes()) {
    Tick note_end = note.start_tick + note.duration;
    if (note.start_tick <= tick && tick < note_end) {
      return note.note % 12;
    }
  }
  return -1;
}

uint16_t buildBassPitchMask(const MidiTrack* bass_track, Tick bar_start, Tick bar_end) {
  if (bass_track == nullptr) return 0;

  uint16_t mask = 0;
  for (const auto& note : bass_track->notes()) {
    // Include notes that start within the bar or are still sounding at bar start
    Tick note_end = note.start_tick + note.duration;
    if (note.start_tick < bar_end && note_end > bar_start) {
      mask |= (1 << (note.note % 12));
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

bool voicingClashesWithBass(const VoicedChord& v, uint16_t bass_pitch_mask) {
  if (bass_pitch_mask == 0) return false;
  for (uint8_t i = 0; i < v.count; ++i) {
    if (clashesWithBassMask(v.pitches[i] % 12, bass_pitch_mask)) {
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

bool clashesWithPitchClasses(int pc, const std::vector<int>& pitch_classes) {
  for (int other_pc : pitch_classes) {
    int interval = std::abs(pc - other_pc);
    if (interval > 6) interval = 12 - interval;
    if (interval == 1 || interval == 2) {  // Minor 2nd or Major 2nd clash
      return true;
    }
  }
  return false;
}

std::vector<VoicedChord> filterVoicingsForContext(
    const std::vector<VoicedChord>& candidates,
    int vocal_pc, int aux_pc, uint16_t bass_pitch_mask,
    const std::vector<int>& motif_pcs,
    uint8_t vocal_high) {
  std::vector<VoicedChord> filtered;

  // Chord should not exceed vocal's highest pitch by more than a small margin
  // This ensures melody remains the highest voice (musical convention)
  constexpr int kVocalHighMargin = 2;  // Allow up to 2 semitones above vocal

  for (const auto& v : candidates) {
    bool has_vocal_clash = false;
    bool has_aux_clash = false;
    bool has_bass_clash = false;
    bool has_motif_clash = false;
    bool exceeds_vocal_ceiling = false;

    for (uint8_t i = 0; i < v.count; ++i) {
      int pc = v.pitches[i] % 12;

      // Priority 0: Check if voicing exceeds vocal ceiling
      if (vocal_high > 0 && v.pitches[i] > vocal_high + kVocalHighMargin) {
        exceeds_vocal_ceiling = true;
      }

      // Priority 1: Vocal close interval clash (absolute prohibition)
      // Unison (0), minor 2nd (1), major 2nd (2) all cause harsh dissonance
      // Major 2nd sounds particularly harsh when chord and vocal overlap
      if (vocal_pc >= 0) {
        int interval = std::abs(pc - vocal_pc);
        if (interval > 6) interval = 12 - interval;
        if (interval <= 2) {
          has_vocal_clash = true;
        }
      }

      // Priority 2: Bass semitone clash (check all bass pitches in the bar)
      if (bass_pitch_mask != 0 && clashesWithBassMask(pc, bass_pitch_mask)) {
        has_bass_clash = true;
      }

      // Priority 3: Aux semitone clash
      if (aux_pc >= 0) {
        int interval = std::abs(pc - aux_pc);
        if (interval > 6) interval = 12 - interval;
        if (interval == 1) {  // Minor 2nd clash
          has_aux_clash = true;
        }
      }

      // Priority 4: Motif semitone clash (critical for BGM mode)
      if (!motif_pcs.empty() && clashesWithPitchClasses(pc, motif_pcs)) {
        has_motif_clash = true;
      }
    }

    if (!has_vocal_clash && !has_bass_clash && !has_aux_clash && !has_motif_clash &&
        !exceeds_vocal_ceiling) {
      // Perfect: no issues
      filtered.push_back(v);
    } else if (!has_vocal_clash && !exceeds_vocal_ceiling) {
      // Has bass/aux/motif clash but no vocal clash - try removing clashing pitches
      VoicedChord modified = v;
      modified.count = 0;
      for (uint8_t i = 0; i < v.count; ++i) {
        int pc = v.pitches[i] % 12;
        bool skip = false;

        // Skip if clashes with bass
        if (bass_pitch_mask != 0 && clashesWithBassMask(pc, bass_pitch_mask)) {
          skip = true;
        }

        // Skip if clashes with aux
        if (aux_pc >= 0 && !skip) {
          int interval = std::abs(pc - aux_pc);
          if (interval > 6) interval = 12 - interval;
          if (interval == 1) skip = true;
        }

        // Skip if clashes with motif
        if (!motif_pcs.empty() && !skip) {
          if (clashesWithPitchClasses(pc, motif_pcs)) {
            skip = true;
          }
        }

        if (!skip) {
          modified.pitches[modified.count] = v.pitches[i];
          modified.count++;
        }
      }

      if (modified.count >= 2) {
        filtered.push_back(modified);
      }
    } else if (exceeds_vocal_ceiling && !has_vocal_clash) {
      // Voicing exceeds vocal ceiling - try to remove only the highest pitches
      VoicedChord modified = v;
      modified.count = 0;
      for (uint8_t i = 0; i < v.count; ++i) {
        // Skip pitches that exceed vocal ceiling
        if (vocal_high > 0 && v.pitches[i] > vocal_high + kVocalHighMargin) {
          continue;
        }
        int pc = v.pitches[i] % 12;
        bool skip = false;
        if (bass_pitch_mask != 0 && clashesWithBassMask(pc, bass_pitch_mask)) skip = true;
        if (aux_pc >= 0 && !skip) {
          int interval = std::abs(pc - aux_pc);
          if (interval > 6) interval = 12 - interval;
          if (interval == 1) skip = true;
        }
        if (!motif_pcs.empty() && !skip && clashesWithPitchClasses(pc, motif_pcs)) skip = true;
        if (!skip) {
          modified.pitches[modified.count] = v.pitches[i];
          modified.count++;
        }
      }
      if (modified.count >= 2) {
        filtered.push_back(modified);
      }
    } else {
      // Has vocal clash - try removing clashing pitches first
      VoicedChord modified = v;
      modified.count = 0;
      for (uint8_t i = 0; i < v.count; ++i) {
        int pc = v.pitches[i] % 12;
        bool skip = false;

        // Skip if clashes with vocal (close interval)
        if (vocal_pc >= 0) {
          int interval = std::abs(pc - vocal_pc);
          if (interval > 6) interval = 12 - interval;
          if (interval <= 2) skip = true;
        }

        // Also skip other clashes
        if (bass_pitch_mask != 0 && !skip && clashesWithBassMask(pc, bass_pitch_mask)) {
          skip = true;
        }
        if (aux_pc >= 0 && !skip) {
          int interval = std::abs(pc - aux_pc);
          if (interval > 6) interval = 12 - interval;
          if (interval == 1) skip = true;
        }
        if (!motif_pcs.empty() && !skip && clashesWithPitchClasses(pc, motif_pcs)) {
          skip = true;
        }

        if (!skip) {
          modified.pitches[modified.count] = v.pitches[i];
          modified.count++;
        }
      }

      if (modified.count >= 2) {
        filtered.push_back(modified);
      }
      // If modified voicing has < 2 notes, skip it entirely
    }
  }

  // Fallback: if all filtered out, try to create voicings that minimize motif clashes
  if (filtered.empty()) {
    // Try again with relaxed motif clash filtering - keep at least 2 notes even if they clash
    for (const auto& v : candidates) {
      VoicedChord modified = v;
      modified.count = 0;

      // First pass: collect non-clashing notes
      for (uint8_t i = 0; i < v.count; ++i) {
        int pc = v.pitches[i] % 12;
        bool clashes = false;
        if (!motif_pcs.empty() && clashesWithPitchClasses(pc, motif_pcs)) {
          clashes = true;
        }
        if (!clashes) {
          modified.pitches[modified.count] = v.pitches[i];
          modified.count++;
        }
      }

      // If we have at least 2 non-clashing notes, use those
      if (modified.count >= 2) {
        filtered.push_back(modified);
      } else {
        // Not enough non-clashing notes - add back some notes to get at least 2
        // Prefer notes that don't clash, but add clashing ones if necessary
        modified.count = 0;
        for (uint8_t i = 0; i < v.count && modified.count < 2; ++i) {
          int pc = v.pitches[i] % 12;
          // Skip if clashes with vocal (highest priority)
          if (vocal_pc >= 0) {
            int interval = std::abs(pc - vocal_pc);
            if (interval > 6) interval = 12 - interval;
            if (interval <= 2) continue;
          }
          modified.pitches[modified.count] = v.pitches[i];
          modified.count++;
        }
        if (modified.count >= 2) {
          filtered.push_back(modified);
        }
      }
    }

    // If still empty, return original candidates as last resort
    if (filtered.empty()) {
      return candidates;
    }
  }

  return filtered;
}

}  // namespace chord_voicing
}  // namespace midisketch
