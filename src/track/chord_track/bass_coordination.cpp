/**
 * @file bass_coordination.cpp
 * @brief Implementation of bass and track collision avoidance.
 */

#include "track/chord_track/bass_coordination.h"

#include <cmath>

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

bool clashesWithBass(int pitch_class, int bass_pitch_class) {
  int interval = std::abs(pitch_class - bass_pitch_class);
  if (interval > 6) interval = 12 - interval;
  // Minor 2nd (1) and Tritone (6) both clash with bass
  // Tritone creates harsh dissonance on strong beats (e.g., B vs F)
  return interval == 1 || interval == 6;
}

bool voicingClashesWithBass(const VoicedChord& v, int bass_root_pc) {
  if (bass_root_pc < 0) return false;
  for (uint8_t i = 0; i < v.count; ++i) {
    if (clashesWithBass(v.pitches[i] % 12, bass_root_pc)) {
      return true;
    }
  }
  return false;
}

VoicedChord removeClashingPitch(const VoicedChord& v, int bass_root_pc) {
  if (bass_root_pc < 0) return v;

  VoicedChord result{};
  result.type = v.type;
  result.open_subtype = v.open_subtype;
  result.count = 0;

  for (uint8_t i = 0; i < v.count; ++i) {
    if (!clashesWithBass(v.pitches[i] % 12, bass_root_pc)) {
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
    int vocal_pc, int aux_pc, int bass_root_pc,
    const std::vector<int>& motif_pcs) {
  std::vector<VoicedChord> filtered;

  for (const auto& v : candidates) {
    bool has_vocal_clash = false;
    bool has_aux_clash = false;
    bool has_bass_clash = false;
    bool has_motif_clash = false;

    for (uint8_t i = 0; i < v.count; ++i) {
      int pc = v.pitches[i] % 12;

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

      // Priority 2: Bass semitone clash
      if (bass_root_pc >= 0 && clashesWithBass(pc, bass_root_pc)) {
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

    if (!has_vocal_clash && !has_bass_clash && !has_aux_clash && !has_motif_clash) {
      // Perfect: no issues
      filtered.push_back(v);
    } else if (!has_vocal_clash) {
      // Has bass/aux/motif clash but no vocal clash - try removing clashing pitches
      VoicedChord modified = v;
      modified.count = 0;
      for (uint8_t i = 0; i < v.count; ++i) {
        int pc = v.pitches[i] % 12;
        bool skip = false;

        // Skip if clashes with bass
        if (bass_root_pc >= 0 && clashesWithBass(pc, bass_root_pc)) {
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
        if (bass_root_pc >= 0 && !skip && clashesWithBass(pc, bass_root_pc)) {
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
