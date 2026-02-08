/**
 * @file post_processor_polish.cpp
 * @brief PostProcessor polish and finalization methods.
 *
 * Contains: fixMotifVocalClashes, fixTrackVocalClashes, fixInterTrackClashes,
 * synchronizeBassKick, applyTrackPanning, applyExpressionCurves,
 * applyArrangementHoles, smoothLargeLeaps, alignChordNoteDurations.
 */

#include "core/post_processor.h"

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
      bool is_dissonant = (interval_class == 1) ||             // minor 2nd / minor 9th
                          (interval_class == 11) ||            // major 7th
                          (interval_class == 2 && interval < 12);  // major 2nd (close only)
      if (is_dissonant) {
        return true;
      }
    }
  }
  return false;
}

// Find a safe chord tone pitch that doesn't clash with vocal or any registered tracks.
// Checks BOTH the vocal track directly AND harmony.isConsonantWithOtherTracks() for comprehensive checking.
// Tries different octaves and different chord tones.
uint8_t findSafeChordTone(uint8_t original_pitch, int8_t degree, Tick start, Tick duration,
                          const MidiTrack& vocal, const ICollisionDetector& harmony) {
  ChordTones ct = getChordTones(degree);
  int base_octave = original_pitch / 12;
  Tick end = start + duration;

  // Strategy 1: Try each chord tone at different octaves (prefer close to original)
  struct Candidate {
    uint8_t pitch;
    int distance;
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
      candidates.push_back({clamped, dist});
    }
  }

  // Return closest non-clashing chord tone
  if (!candidates.empty()) {
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.distance < b.distance; });
    return candidates[0].pitch;
  }

  // Fallback: no safe chord tone found, return original (clash may persist)
  return original_pitch;
}

// Remove notes from track that clash with vocal.
// @param include_close_major_2nd If true, treat close major 2nd (interval < 12) as dissonant.
//        Bass uses false (octave separation makes M2 acceptable), Chord/Aux use true.
void removeVocalClashingNotes(MidiTrack& track, const MidiTrack& vocal,
                               bool include_close_major_2nd) {
  auto& notes = track.notes();
  const auto& vocal_notes = vocal.notes();
  if (notes.empty() || vocal_notes.empty()) return;

  // Bass: skip M2 check (octave separation makes it acceptable) = minimalClash
  // Chord/Aux: include close M2 check = closeVoicing
  auto dissonance_opts = include_close_major_2nd
      ? DissonanceCheckOptions::closeVoicing()
      : DissonanceCheckOptions::minimalClash();

  std::vector<size_t> notes_to_remove;
  for (size_t idx = 0; idx < notes.size(); ++idx) {
    const auto& note = notes[idx];
    Tick note_end = note.start_tick + note.duration;

    for (const auto& v_note : vocal_notes) {
      Tick v_end = v_note.start_tick + v_note.duration;
      if (note.start_tick < v_end && note_end > v_note.start_tick) {
        int interval = std::abs(static_cast<int>(note.note) - static_cast<int>(v_note.note));
        if (isDissonantSemitoneInterval(interval, dissonance_opts)) {
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
        bool is_dissonant = isDissonantSemitoneInterval(
            interval, DissonanceCheckOptions::fullWithTritone());

        if (is_dissonant) {
          int8_t degree = harmony.getChordDegreeAt(m_note.start_tick);
          uint8_t original_pitch = m_note.note;

          // Find a chord tone that doesn't clash with vocal or any registered track
          uint8_t new_pitch = findSafeChordTone(original_pitch, degree, m_note.start_tick,
                                                 m_note.duration, vocal, harmony);

          // If still clashing with vocal, try using getSafePitchCandidates as last resort
          if (clashesWithVocal(new_pitch, m_note.start_tick, m_end, vocal)) {
            auto candidates = getSafePitchCandidates(harmony, original_pitch, m_note.start_tick,
                                                      m_note.duration, TrackRole::Motif,
                                                      MOTIF_LOW, MOTIF_HIGH);
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
}

void PostProcessor::fixTrackVocalClashes(MidiTrack& track, const MidiTrack& vocal, TrackRole role) {
  // Bass tracks skip close major 2nd detection because octave separation
  // makes the interval acceptable.
  bool include_close_major_2nd = (role != TrackRole::Bass);
  removeVocalClashingNotes(track, vocal, include_close_major_2nd);
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
        float progress = static_cast<float>(tick - sec_start) / static_cast<float>(sec_mid - sec_start);
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

void PostProcessor::applyArrangementHoles(MidiTrack& motif, MidiTrack& arpeggio, MidiTrack& aux,
                                          MidiTrack& chord, MidiTrack& bass, MidiTrack& guitar,
                                          const std::vector<Section>& sections) {
  // Helper: remove notes that overlap with [hole_start, hole_end)
  auto muteRange = [](MidiTrack& track, Tick hole_start, Tick hole_end) {
    auto& notes = track.notes();
    notes.erase(std::remove_if(notes.begin(), notes.end(),
                               [hole_start, hole_end](const NoteEvent& n) {
                                 Tick note_end = n.start_tick + n.duration;
                                 // Remove if note overlaps with hole range
                                 return n.start_tick < hole_end && note_end > hole_start;
                               }),
                notes.end());
  };

  constexpr Tick kTwoBeats = TICKS_PER_BEAT * 2;

  for (size_t i = 0; i < sections.size(); ++i) {
    const auto& section = sections[i];

    // Chorus final 2 beats: mute background tracks for buildup effect
    // Only apply to Max peak level sections
    if (section.type == SectionType::Chorus && section.peak_level == PeakLevel::Max) {
      Tick hole_start = section.endTick() - kTwoBeats;
      Tick hole_end = section.endTick();
      if (hole_start >= section.start_tick) {
        muteRange(motif, hole_start, hole_end);
        muteRange(arpeggio, hole_start, hole_end);
        muteRange(aux, hole_start, hole_end);
        muteRange(guitar, hole_start, hole_end);
      }
    }

    // Bridge first 2 beats: mute non-vocal/non-drum tracks for contrast
    if (section.type == SectionType::Bridge) {
      Tick hole_start = section.start_tick;
      Tick hole_end = section.start_tick + kTwoBeats;
      if (hole_end <= section.endTick()) {
        muteRange(motif, hole_start, hole_end);
        muteRange(arpeggio, hole_start, hole_end);
        muteRange(aux, hole_start, hole_end);
        muteRange(chord, hole_start, hole_end);
        muteRange(bass, hole_start, hole_end);
        muteRange(guitar, hole_start, hole_end);
      }
    }
  }
}

void PostProcessor::smoothLargeLeaps(MidiTrack& track, int max_semitones) {
  auto& notes = track.notes();
  if (notes.size() < 2) return;

  // Sort by start tick to ensure correct adjacency
  std::sort(notes.begin(), notes.end(),
            [](const NoteEvent& lhs, const NoteEvent& rhs) {
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
      int leap = std::abs(static_cast<int>(notes[idx].note) -
                          static_cast<int>(notes[idx - 1].note));
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
      n.duration = min_dur[n.start_tick];
    }
  }
}

}  // namespace midisketch
