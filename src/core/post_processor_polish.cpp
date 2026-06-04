/**
 * @file post_processor_polish.cpp
 * @brief PostProcessor polish and finalization methods.
 *
 * Contains: fixMotifVocalClashes, fixTrackVocalClashes, fixInterTrackClashes,
 * synchronizeBassKick, applyTrackPanning, applyExpressionCurves,
 * smoothLargeLeaps, alignChordNoteDurations.
 */

#include <algorithm>
#include <cstdlib>
#include <unordered_map>
#include <vector>

#include "core/chord_utils.h"
#include "core/i_collision_detector.h"
#include "core/i_harmony_context.h"
#include "core/note_creator.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/post_processor.h"
#include "core/timing_constants.h"
#include "core/velocity.h"
#include "core/velocity_helper.h"

namespace midisketch {

// ============================================================================
// Motif-Vocal Clash Resolution Implementation
// ============================================================================

namespace {

// Helper to check if a pitch clashes with vocal at a given time range
bool clashesWithVocal(uint8_t pitch, Tick start, Tick end, const MidiTrack& vocal) {
  for (const auto& v_note : vocal.notes()) {
    Tick v_end = v_note.start_tick + v_note.duration;
    // Check overlap
    if (start < v_end && end > v_note.start_tick) {
      int interval = std::abs(static_cast<int>(pitch) - static_cast<int>(v_note.note));
      int interval_class = interval % 12;
      bool is_dissonant = (interval_class == 1) ||                 // minor 2nd / minor 9th
                          (interval_class == 11) ||                // major 7th
                          (interval_class == 2 && interval < 12);  // major 2nd (close only)
      if (is_dissonant) {
        return true;
      }
    }
  }
  return false;
}

// Find a safe chord tone pitch that doesn't clash with vocal or any registered tracks.
// Checks BOTH the vocal track directly AND harmony.isConsonantWithOtherTracks() for comprehensive
// checking. Tries different octaves and different chord tones.
// @param vocal_ceiling When > 0, candidates at or below this pitch are strongly
//        preferred over candidates above it, so dissonance resolution does not
//        escape ABOVE the vocal (which would create a register crossing). Among
//        equally-preferred candidates the one closest to the original pitch
//        wins. When 0 (e.g. stub unit tests with no vocal ceiling) the original
//        closest-distance behavior is used.
uint8_t findSafeChordTone(uint8_t original_pitch, int8_t degree, Tick start, Tick duration,
                          const MidiTrack& vocal, const ICollisionDetector& harmony,
                          uint8_t vocal_ceiling = 0) {
  ChordTones ct = getChordTones(degree);
  int base_octave = original_pitch / 12;
  Tick end = start + duration;

  // Strategy 1: Try each chord tone at different octaves (prefer close to original)
  struct Candidate {
    uint8_t pitch;
    int distance;
    bool above_ceiling;  // true if pitch crosses above the vocal ceiling
  };
  std::vector<Candidate> candidates;

  for (uint8_t i = 0; i < ct.count; ++i) {
    int ct_pc = ct.pitch_classes[i];
    if (ct_pc < 0) continue;

    // Try octaves from -2 to +2 relative to base octave
    for (int oct_offset = -2; oct_offset <= 2; ++oct_offset) {
      int candidate_pitch = (base_octave + oct_offset) * 12 + ct_pc;
      if (candidate_pitch < MOTIF_LOW || candidate_pitch > MOTIF_HIGH) continue;

      uint8_t clamped = static_cast<uint8_t>(candidate_pitch);
      // Check against vocal directly (for tests that don't register vocal)
      if (clashesWithVocal(clamped, start, end, vocal)) continue;
      // Also check against all registered tracks (chord, bass, etc.)
      if (!harmony.isConsonantWithOtherTracks(clamped, start, duration, TrackRole::Motif)) continue;

      int dist = std::abs(candidate_pitch - static_cast<int>(original_pitch));
      bool above = (vocal_ceiling > 0 && clamped > vocal_ceiling);
      candidates.push_back({clamped, dist, above});
    }
  }

  // Return best non-clashing chord tone: prefer at-or-below the vocal ceiling,
  // then closest to the original pitch.
  if (!candidates.empty()) {
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
      if (a.above_ceiling != b.above_ceiling) return !a.above_ceiling;  // below ceiling first
      return a.distance < b.distance;
    });
    return candidates[0].pitch;
  }

  // Fallback: no safe chord tone found, return original (clash may persist)
  return original_pitch;
}

// Remove notes from track that clash with a reference melodic line.
// @param opts Dissonance policy for the pair being checked.
void removeClashingNotesAgainstReference(MidiTrack& track, const MidiTrack& reference,
                                         const DissonanceCheckOptions& opts) {
  auto& notes = track.notes();
  const auto& reference_notes = reference.notes();
  if (notes.empty() || reference_notes.empty()) return;

  std::vector<size_t> notes_to_remove;
  for (size_t idx = 0; idx < notes.size(); ++idx) {
    const auto& note = notes[idx];
    Tick note_end = note.start_tick + note.duration;

    for (const auto& ref_note : reference_notes) {
      Tick ref_end = ref_note.start_tick + ref_note.duration;
      if (note.start_tick < ref_end && note_end > ref_note.start_tick) {
        int interval = std::abs(static_cast<int>(note.note) - static_cast<int>(ref_note.note));
        if (isDissonantSemitoneInterval(interval, opts)) {
          notes_to_remove.push_back(idx);
          break;
        }
      }
    }
  }

  for (auto iter = notes_to_remove.rbegin(); iter != notes_to_remove.rend(); ++iter) {
    notes.erase(notes.begin() + static_cast<std::ptrdiff_t>(*iter));
  }
}

// Find the lowest vocal pitch overlapping [start, end). Returns 0 if no vocal
// note overlaps (e.g. vocal rest). The crossing criterion (see
// scripts/check_pitch_crossing.py) flags a violation against ANY overlapping
// vocal note, so an accompaniment note must clear the LOWEST overlapping vocal.
uint8_t lowestOverlappingVocal(Tick start, Tick end, const MidiTrack& vocal) {
  uint8_t lowest = 0;
  for (const auto& v_note : vocal.notes()) {
    Tick v_end = v_note.start_tick + v_note.duration;
    if (start < v_end && end > v_note.start_tick) {
      if (lowest == 0 || v_note.note < lowest) {
        lowest = v_note.note;
      }
    }
  }
  return lowest;
}

// Resolve a motif note that crosses above the vocal by finding the highest
// chord tone at or below the vocal ceiling that does not clash with the vocal
// or other registered tracks. If no such chord tone exists (e.g. the ceiling is
// below the motif's register floor), try a plain octave-down shift that lands at
// or below the ceiling. If neither succeeds, return original_pitch unchanged so
// the dissonance pass's careful resolution is preserved. This runs in
// post-processing (after humanization) so the timing comparison is final and
// exactly mirrors the crossing check.
uint8_t resolveMotifAboveVocal(uint8_t original_pitch, uint8_t ceiling, int8_t degree, Tick start,
                               Tick duration, const MidiTrack& vocal,
                               const ICollisionDetector& harmony) {
  Tick end = start + duration;
  ChordTones ct = getChordTones(degree);

  // Strategy 1: highest chord tone <= ceiling that is consonant with the vocal
  // and other tracks.
  uint8_t best = 0;
  for (uint8_t i = 0; i < ct.count; ++i) {
    int ct_pc = ct.pitch_classes[i];
    if (ct_pc < 0) continue;
    for (int oct = MOTIF_LOW / 12; oct <= MOTIF_HIGH / 12; ++oct) {
      int candidate = oct * 12 + ct_pc;
      if (candidate < MOTIF_LOW || candidate > static_cast<int>(ceiling)) continue;
      uint8_t cand = static_cast<uint8_t>(candidate);
      if (clashesWithVocal(cand, start, end, vocal)) continue;
      if (!harmony.isConsonantWithOtherTracks(cand, start, duration, TrackRole::Motif)) continue;
      if (cand > best) best = cand;  // highest chord tone under the ceiling
    }
  }
  if (best != 0) return best;

  // Strategy 2: octave-down shift that lands at or below the ceiling and stays
  // in register, without clashing with the vocal.
  int shifted = static_cast<int>(original_pitch);
  while (shifted > static_cast<int>(ceiling) && shifted - 12 >= MOTIF_LOW) {
    shifted -= 12;
  }
  if (shifted >= MOTIF_LOW && shifted <= static_cast<int>(ceiling) && shifted != original_pitch &&
      !clashesWithVocal(static_cast<uint8_t>(shifted), start, end, vocal)) {
    return static_cast<uint8_t>(shifted);
  }

  // Strategy 3: no chord tone exists at or below the ceiling that is consonant
  // with everything (the vocal sits in/near the motif's register and the lower
  // chord tones are occupied by other tracks). Minimize the unavoidable
  // crossing: among all in-register chord tones that do not clash with the
  // vocal, pick the LOWEST. Prefer a tone consonant with the other tracks too,
  // but fall back to a vocal-only-consonant tone when that yields a
  // significantly smaller crossing (a small register crossing is preferable to
  // a +octave jump or an unresolved clash).
  {
    uint8_t best_full = 0;   // consonant with vocal AND other tracks
    uint8_t best_vocal = 0;  // consonant with vocal only
    for (uint8_t i = 0; i < ct.count; ++i) {
      int ct_pc = ct.pitch_classes[i];
      if (ct_pc < 0) continue;
      for (int oct = MOTIF_LOW / 12; oct <= MOTIF_HIGH / 12; ++oct) {
        int candidate = oct * 12 + ct_pc;
        if (candidate < MOTIF_LOW || candidate > MOTIF_HIGH) continue;
        uint8_t cand = static_cast<uint8_t>(candidate);
        if (clashesWithVocal(cand, start, end, vocal)) continue;
        if (best_vocal == 0 || cand < best_vocal) best_vocal = cand;
        if (harmony.isConsonantWithOtherTracks(cand, start, duration, TrackRole::Motif)) {
          if (best_full == 0 || cand < best_full) best_full = cand;
        }
      }
    }
    // Prefer the fully-consonant option unless it crosses much higher than the
    // vocal-only option (then take the smaller crossing).
    uint8_t pick = best_full;
    if (best_vocal != 0 && (best_full == 0 || best_vocal + 4 < best_full)) {
      pick = best_vocal;
    }
    if (pick != 0 && pick < original_pitch) return pick;
  }

  // No better option: leave the note unchanged. The caller treats an unchanged
  // result as "no fix".
  return original_pitch;
}

}  // namespace

void PostProcessor::fixMotifVocalClashes(MidiTrack& motif, const MidiTrack& vocal,
                                         const ICollisionDetector& harmony) {
  auto& motif_notes = motif.notes();
  const auto& vocal_notes = vocal.notes();

  if (motif_notes.empty() || vocal_notes.empty()) {
    return;
  }

  for (auto& m_note : motif_notes) {
    Tick m_end = m_note.start_tick + m_note.duration;

    for (const auto& v_note : vocal_notes) {
      Tick v_end = v_note.start_tick + v_note.duration;

      // Check overlap: motif note and vocal note must be sounding simultaneously
      if (m_note.start_tick < v_end && m_end > v_note.start_tick) {
        int interval = std::abs(static_cast<int>(m_note.note) - static_cast<int>(v_note.note));

        // Use unified dissonance check: m2, M2 (close), tritone (always), M7
        bool is_dissonant =
            isDissonantSemitoneInterval(interval, DissonanceCheckOptions::fullWithTritone());

        if (is_dissonant) {
          int8_t degree = harmony.getChordDegreeAt(m_note.start_tick);
          uint8_t original_pitch = m_note.note;

          // Prefer a resolution at or below the vocal so dissonance avoidance
          // does not escape above the melody (register crossing).
          uint8_t vocal_ceiling = lowestOverlappingVocal(m_note.start_tick, m_end, vocal);

          // Find a chord tone that doesn't clash with vocal or any registered track
          uint8_t new_pitch = findSafeChordTone(original_pitch, degree, m_note.start_tick,
                                                m_note.duration, vocal, harmony, vocal_ceiling);

          // If still clashing with vocal, try using getSafePitchCandidates as last resort
          if (clashesWithVocal(new_pitch, m_note.start_tick, m_end, vocal)) {
            auto candidates =
                getSafePitchCandidates(harmony, original_pitch, m_note.start_tick, m_note.duration,
                                       TrackRole::Motif, MOTIF_LOW, MOTIF_HIGH);
            if (!candidates.empty()) {
              // Select best candidate with melodic continuity preference
              PitchSelectionHints hints;
              hints.prev_pitch = static_cast<int8_t>(original_pitch);
              hints.note_duration = m_note.duration;
              hints.tessitura_center = (MOTIF_LOW + MOTIF_HIGH) / 2;
              new_pitch = selectBestCandidate(candidates, original_pitch, hints);
            }
          }

#ifdef MIDISKETCH_NOTE_PROVENANCE
          if (new_pitch != original_pitch) {
            m_note.addTransformStep(TransformStepType::CollisionAvoid, original_pitch, new_pitch,
                                    static_cast<int8_t>(v_note.note), 0);
            m_note.prov_original_pitch = original_pitch;
            m_note.prov_source = static_cast<uint8_t>(NoteSource::CollisionAvoid);
          }
          m_note.prov_lookup_tick = m_note.start_tick;
          m_note.prov_chord_degree = degree;
#endif

          m_note.note = new_pitch;
          break;  // Fixed this motif note, move to next
        }
      }
    }
  }

  // Second pass: resolve motif notes that cross ABOVE the vocal. The vocal
  // should occupy the highest register in pop arrangement; a motif note above
  // the overlapping vocal note buries the melody even when the interval is
  // consonant (see scripts/check_pitch_crossing.py, which flags
  // motif_pitch - vocal_pitch > 0 for any temporal overlap). Run after the
  // dissonance pass since that pass may itself raise a pitch above the vocal.
  for (auto& m_note : motif_notes) {
    Tick m_end = m_note.start_tick + m_note.duration;
    uint8_t vocal_floor = lowestOverlappingVocal(m_note.start_tick, m_end, vocal);
    if (vocal_floor == 0) continue;            // vocal rest: nothing to clear
    if (m_note.note <= vocal_floor) continue;  // already at or below vocal

    int8_t degree = harmony.getChordDegreeAt(m_note.start_tick);
    uint8_t original_pitch = m_note.note;
    uint8_t new_pitch = resolveMotifAboveVocal(original_pitch, vocal_floor, degree,
                                               m_note.start_tick, m_note.duration, vocal, harmony);
    if (new_pitch == original_pitch) continue;

#ifdef MIDISKETCH_NOTE_PROVENANCE
    m_note.addTransformStep(TransformStepType::CollisionAvoid, original_pitch, new_pitch,
                            static_cast<int8_t>(vocal_floor), 0);
    m_note.prov_original_pitch = original_pitch;
    m_note.prov_source = static_cast<uint8_t>(NoteSource::CollisionAvoid);
    m_note.prov_lookup_tick = m_note.start_tick;
    m_note.prov_chord_degree = degree;
#endif
    m_note.note = new_pitch;
  }
}

void PostProcessor::fixMotifRepeatedPitches(MidiTrack& motif, const MidiTrack& vocal,
                                            const ICollisionDetector& harmony,
                                            int max_consecutive) {
  auto& motif_notes = motif.notes();
  if (motif_notes.empty() || max_consecutive < 1) {
    return;
  }

  std::sort(motif_notes.begin(), motif_notes.end(), [](const NoteEvent& a, const NoteEvent& b) {
    if (a.start_tick != b.start_tick) return a.start_tick < b.start_tick;
    return a.note < b.note;
  });

  uint8_t last_pitch = motif_notes.front().note;
  int consecutive = 0;

  for (auto& note : motif_notes) {
    if (note.note == last_pitch) {
      ++consecutive;
    } else {
      last_pitch = note.note;
      consecutive = 1;
    }

    if (consecutive <= max_consecutive) {
      continue;
    }

    uint8_t original_pitch = note.note;
    int8_t degree = harmony.getChordDegreeAt(note.start_tick);
    uint8_t new_pitch =
        findSafeChordTone(original_pitch, degree, note.start_tick, note.duration, vocal, harmony);
    if (new_pitch == original_pitch) {
      new_pitch = findSafeChordTone(
          static_cast<uint8_t>(std::clamp(static_cast<int>(original_pitch) + 4, 0, 127)), degree,
          note.start_tick, note.duration, vocal, harmony);
    }

    if (new_pitch != original_pitch) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
      note.addTransformStep(TransformStepType::CollisionAvoid, original_pitch, new_pitch, 0, 0);
      note.prov_original_pitch = original_pitch;
      note.prov_source = static_cast<uint8_t>(NoteSource::CollisionAvoid);
      note.prov_lookup_tick = note.start_tick;
      note.prov_chord_degree = degree;
#endif
      note.note = new_pitch;
      last_pitch = new_pitch;
      consecutive = 1;
    }
  }
}

void PostProcessor::fixTrackVocalClashes(MidiTrack& track, const MidiTrack& vocal, TrackRole role) {
  // Bass tracks skip close major 2nd detection because octave separation
  // makes the interval acceptable.
  auto opts = (role == TrackRole::Bass) ? DissonanceCheckOptions::minimalClash()
                                        : DissonanceCheckOptions::fullWithTritone();
  removeClashingNotesAgainstReference(track, vocal, opts);
}

void PostProcessor::fixTrackReferenceClashes(MidiTrack& track, const MidiTrack& reference,
                                             TrackRole role) {
  if (role == TrackRole::Motif) {
    return;
  }

  auto opts = DissonanceCheckOptions::closeVoicing();
  removeClashingNotesAgainstReference(track, reference, opts);
}

void PostProcessor::fixInterTrackClashes(MidiTrack& chord, const MidiTrack& bass,
                                         const MidiTrack& motif) {
  auto& notes = chord.notes();
  if (notes.empty()) return;

  // Close voicing check: m2, M7, and close-range M2 (no tritone)
  auto close_opts = DissonanceCheckOptions::closeVoicing();

  std::vector<size_t> notes_to_remove;

  for (size_t idx = 0; idx < notes.size(); ++idx) {
    const auto& note = notes[idx];
    Tick note_end = note.start_tick + note.duration;
    bool should_remove = false;

    // Check against bass
    for (const auto& b_note : bass.notes()) {
      Tick b_end = b_note.start_tick + b_note.duration;
      if (note.start_tick < b_end && note_end > b_note.start_tick) {
        int interval = std::abs(static_cast<int>(note.note) - static_cast<int>(b_note.note));
        if (isDissonantSemitoneInterval(interval, close_opts)) {
          should_remove = true;
          break;
        }
      }
    }

    // Check against motif (only if not already marked for removal)
    if (!should_remove) {
      for (const auto& m_note : motif.notes()) {
        Tick m_end = m_note.start_tick + m_note.duration;
        if (note.start_tick < m_end && note_end > m_note.start_tick) {
          int interval = std::abs(static_cast<int>(note.note) - static_cast<int>(m_note.note));
          if (isDissonantSemitoneInterval(interval, close_opts)) {
            should_remove = true;
            break;
          }
        }
      }
    }

    if (should_remove) {
      notes_to_remove.push_back(idx);
    }
  }

  for (auto iter = notes_to_remove.rbegin(); iter != notes_to_remove.rend(); ++iter) {
    notes.erase(notes.begin() + static_cast<std::ptrdiff_t>(*iter));
  }
}

void PostProcessor::synchronizeBassKick(MidiTrack& bass, const MidiTrack& drums,
                                        DrumStyle drum_style) {
  const auto& drum_notes = drums.notes();
  if (drum_notes.empty()) return;

  // Extract kick (note 36) onset ticks
  std::vector<Tick> kick_ticks;
  kick_ticks.reserve(256);
  for (const auto& note : drum_notes) {
    if (note.note == 36) {
      kick_ticks.push_back(note.start_tick);
    }
  }
  if (kick_ticks.empty()) return;

  // Sort kick ticks for binary search
  std::sort(kick_ticks.begin(), kick_ticks.end());

  // Tolerance based on DrumStyle
  Tick tolerance;
  switch (drum_style) {
    case DrumStyle::Sparse:
      tolerance = 72;  // Ballad: loose sync
      break;
    case DrumStyle::FourOnFloor:
    case DrumStyle::Synth:
    case DrumStyle::Trap:
      tolerance = 24;  // Dance/electronic: tight sync
      break;
    default:
      tolerance = 48;  // Standard: moderate sync
      break;
  }

  // Snap bass notes to nearest kick within tolerance
  for (auto& note : bass.notes()) {
    // Binary search for nearest kick
    auto iter = std::lower_bound(kick_ticks.begin(), kick_ticks.end(), note.start_tick);

    Tick best_kick = 0;
    Tick best_diff = tolerance + 1;  // Start beyond tolerance

    // Check the kick at or after the bass note
    if (iter != kick_ticks.end()) {
      Tick diff = *iter - note.start_tick;
      if (diff < best_diff) {
        best_diff = diff;
        best_kick = *iter;
      }
    }
    // Check the kick before the bass note
    if (iter != kick_ticks.begin()) {
      --iter;
      // Safe unsigned subtraction: note.start_tick >= *iter because lower_bound
      // returned an element after iter, so *iter <= note.start_tick
      Tick diff = note.start_tick - *iter;
      if (diff < best_diff) {
        best_diff = diff;
        best_kick = *iter;
      }
    }

    // Snap if within tolerance and not already aligned
    if (best_diff <= tolerance && best_diff > 0) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
      {
        int sync_offset = static_cast<int>(best_kick) - static_cast<int>(note.start_tick);
        note.addTransformStep(TransformStepType::PostProcessTiming, 0, 0,
                              static_cast<int8_t>(std::clamp(sync_offset, -128, 127)), 5);
      }
#endif
      note.start_tick = best_kick;
    }
  }
}

void PostProcessor::applyTrackPanning(MidiTrack& vocal, MidiTrack& chord, MidiTrack& bass,
                                      MidiTrack& motif, MidiTrack& arpeggio, MidiTrack& aux,
                                      MidiTrack& guitar) {
  // Pan values: 0=hard left, 64=center, 127=hard right
  // Only apply panning to tracks that contain notes to avoid marking empty tracks as non-empty.
  struct PanEntry {
    MidiTrack* track;
    uint8_t pan_value;
  };
  PanEntry entries[] = {
      {&vocal, 64},     // Center
      {&bass, 64},      // Center
      {&chord, 52},     // Slight left
      {&arpeggio, 76},  // Slight right
      {&motif, 44},     // Left
      {&aux, 84},       // Right
      {&guitar, 38},    // Slight left (symmetric with Chord)
  };
  for (const auto& entry : entries) {
    if (!entry.track->notes().empty()) {
      entry.track->addCC(0, MidiCC::kPan, entry.pan_value);
    }
  }
}

void PostProcessor::applyExpressionCurves(MidiTrack& vocal, MidiTrack& chord, MidiTrack& aux,
                                          const std::vector<Section>& sections) {
  constexpr uint8_t kExprDefault = 100;
  constexpr Tick kResolution = TICKS_PER_BEAT / 2;  // 8th note resolution

  // Vocal: crescendo-diminuendo on long notes (>= 2 beats)
  constexpr Tick kLongNoteThreshold = TICKS_PER_BEAT * 2;
  for (const auto& note : vocal.notes()) {
    if (note.duration >= kLongNoteThreshold) {
      Tick start = note.start_tick;
      Tick end = start + note.duration;
      Tick mid = start + note.duration / 2;

      // Crescendo: 80 -> 110
      for (Tick tick = start; tick < mid; tick += kResolution) {
        float progress = static_cast<float>(tick - start) / static_cast<float>(mid - start);
        uint8_t val = static_cast<uint8_t>(80 + 30 * progress);
        vocal.addCC(tick, MidiCC::kExpression, val);
      }
      // Diminuendo: 110 -> 90
      for (Tick tick = mid; tick < end; tick += kResolution) {
        float progress = static_cast<float>(tick - mid) / static_cast<float>(end - mid);
        uint8_t val = static_cast<uint8_t>(110 - 20 * progress);
        vocal.addCC(tick, MidiCC::kExpression, val);
      }
      // Reset after note
      vocal.addCC(end, MidiCC::kExpression, kExprDefault);
    }
  }

  // Chord/Aux: section-level expression curve (80 -> 100 -> 90)
  // Only apply to tracks that contain notes to avoid marking empty tracks as non-empty.
  MidiTrack* section_tracks[] = {&chord, &aux};
  for (MidiTrack* track : section_tracks) {
    if (track->notes().empty()) continue;
    for (const auto& section : sections) {
      Tick sec_start = section.start_tick;
      Tick sec_end = section.endTick();
      Tick sec_mid = sec_start + (sec_end - sec_start) / 2;
      Tick sec_duration = sec_end - sec_start;
      if (sec_duration == 0) continue;

      // First half: 80 -> 100
      for (Tick tick = sec_start; tick < sec_mid; tick += kResolution) {
        float progress =
            static_cast<float>(tick - sec_start) / static_cast<float>(sec_mid - sec_start);
        uint8_t val = static_cast<uint8_t>(80 + 20 * progress);
        track->addCC(tick, MidiCC::kExpression, val);
      }
      // Second half: 100 -> 90
      for (Tick tick = sec_mid; tick < sec_end; tick += kResolution) {
        float progress = static_cast<float>(tick - sec_mid) / static_cast<float>(sec_end - sec_mid);
        uint8_t val = static_cast<uint8_t>(100 - 10 * progress);
        track->addCC(tick, MidiCC::kExpression, val);
      }
    }
  }
}

void PostProcessor::smoothLargeLeaps(MidiTrack& track, int max_semitones) {
  auto& notes = track.notes();
  if (notes.size() < 2) return;

  // Sort by start tick to ensure correct adjacency
  std::sort(notes.begin(), notes.end(), [](const NoteEvent& lhs, const NoteEvent& rhs) {
    return lhs.start_tick < rhs.start_tick;
  });

  // Iterative removal: each pass removes at most one note per large leap,
  // then re-checks.  This avoids over-removal when the note AFTER a
  // removed note would have been fine (e.g. A->B->C where B is removed
  // and A->C turns out to be small).
  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t idx = 1; idx < notes.size(); ++idx) {
      int leap =
          std::abs(static_cast<int>(notes[idx].note) - static_cast<int>(notes[idx - 1].note));
      if (leap > max_semitones) {
        notes.erase(notes.begin() + static_cast<std::ptrdiff_t>(idx));
        changed = true;
        break;  // Restart scan after each removal
      }
    }
  }
}

void PostProcessor::alignChordNoteDurations(MidiTrack& track) {
  auto& notes = track.notes();
  if (notes.size() < 2) return;

  // First pass: find minimum duration per start_tick
  std::unordered_map<Tick, Tick> min_dur;
  std::unordered_map<Tick, int> count;
  for (const auto& n : notes) {
    auto iter = min_dur.find(n.start_tick);
    if (iter == min_dur.end()) {
      min_dur[n.start_tick] = n.duration;
      count[n.start_tick] = 1;
    } else {
      if (n.duration < iter->second) {
        iter->second = n.duration;
      }
      count[n.start_tick]++;
    }
  }

  // Second pass: apply minimum duration to all notes at ticks with 2+ notes
  for (auto& n : notes) {
    if (count[n.start_tick] > 1) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
      if (n.duration != min_dur[n.start_tick]) {
        n.addTransformStep(TransformStepType::PostProcessDuration, 0, 0, -1, 0);
      }
#endif
      n.duration = min_dur[n.start_tick];
    }
  }
}

}  // namespace midisketch
