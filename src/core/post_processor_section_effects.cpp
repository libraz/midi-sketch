/**
 * @file post_processor_section_effects.cpp
 * @brief PostProcessor section transition and exit pattern methods.
 *
 * Contains: applyExitFadeout, applyExitFinalHit, applyExitCutOff,
 * applyExitSustain, applyExitPattern, applyAllExitPatterns,
 * applyChorusDrop,
 * applyRitardando, applyEnhancedFinalHit.
 */

#include "core/post_processor.h"

#include <algorithm>
#include <cstdlib>

#include "core/chord_utils.h"
#include "core/i_chord_lookup.h"
#include "core/i_collision_detector.h"
#include "core/note_creator.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/timing_constants.h"
#include "core/velocity_helper.h"

namespace midisketch {

// ============================================================================
// ExitPattern Helper Functions
// ============================================================================

void PostProcessor::applyExitFadeout(std::vector<NoteEvent>& notes, Tick section_end,
                                     uint8_t section_bars) {

  // Velocity gradually decreases in last 2 bars (1.0 -> 0.4)
  uint8_t fade_bars = std::min(section_bars, static_cast<uint8_t>(2));
  Tick fade_start = section_end - fade_bars * TICKS_PER_BAR;
  Tick fade_duration = section_end - fade_start;
  if (fade_duration == 0) return;

  constexpr float kFadeStartMult = 1.0f;
  constexpr float kFadeEndMult = 0.4f;

  for (auto& note : notes) {
    if (note.start_tick >= fade_start && note.start_tick < section_end) {
      float progress = static_cast<float>(note.start_tick - fade_start) /
                       static_cast<float>(fade_duration);
      float multiplier = kFadeStartMult + (kFadeEndMult - kFadeStartMult) * progress;
      int new_vel = static_cast<int>(note.velocity * multiplier);
      uint8_t clamped_vel = vel::clamp(new_vel);
#ifdef MIDISKETCH_NOTE_PROVENANCE
      if (clamped_vel != note.velocity) {
        note.addTransformStep(TransformStepType::PostProcessVelocity, note.velocity, clamped_vel,
                              0, 0);
      }
#endif
      note.velocity = clamped_vel;
    }
  }
}

void PostProcessor::applyExitFinalHit(std::vector<NoteEvent>& notes, Tick section_end) {
  // Strong accent on the last beat of the section
  constexpr uint8_t kFinalHitVelocity = 120;
  Tick last_beat_start = section_end - TICKS_PER_BEAT;

  for (auto& note : notes) {
    if (note.start_tick >= last_beat_start && note.start_tick < section_end) {
      uint8_t new_vel = std::min(std::max(note.velocity, kFinalHitVelocity), static_cast<uint8_t>(127));
#ifdef MIDISKETCH_NOTE_PROVENANCE
      if (new_vel != note.velocity) {
        note.addTransformStep(TransformStepType::PostProcessVelocity, note.velocity, new_vel,
                              1, 0);
      }
#endif
      note.velocity = new_vel;
    }
  }
}

void PostProcessor::applyExitCutOff(std::vector<NoteEvent>& notes, Tick section_start,
                                    Tick section_end) {
  // Truncate notes that extend beyond (section_end - TICKS_PER_BEAT)
  // and remove notes that start in the last beat
  Tick cutoff_point = section_end - TICKS_PER_BEAT;

  // Remove notes starting after cutoff within this section
  notes.erase(
      std::remove_if(notes.begin(), notes.end(),
                     [section_start, section_end, cutoff_point](const NoteEvent& note) {
                       return note.start_tick >= cutoff_point &&
                              note.start_tick < section_end &&
                              note.start_tick >= section_start;
                     }),
      notes.end());

  // Truncate notes that extend past the cutoff
  for (auto& note : notes) {
    if (note.start_tick >= section_start && note.start_tick < cutoff_point) {
      Tick note_end = note.start_tick + note.duration;
      if (note_end > cutoff_point) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
        note.addTransformStep(TransformStepType::PostProcessDuration, 0, 0, -1, 0);
#endif
        note.duration = cutoff_point - note.start_tick;
      }
    }
  }
}

void PostProcessor::applyExitSustain(std::vector<NoteEvent>& notes, Tick section_start,
                                     Tick section_end,
                                     const IChordLookup* chord_lookup,
                                     const ICollisionDetector* harmony,
                                     TrackRole track_role) {
  // Extend duration of notes in the last bar to reach section boundary,
  // but cap each note's extension at the start of the next chord to prevent overlaps
  Tick last_bar_start = section_end - TICKS_PER_BAR;

  // Collect notes in the last bar
  std::vector<NoteEvent*> last_bar_notes;
  for (auto& note : notes) {
    if (note.start_tick >= last_bar_start && note.start_tick < section_end &&
        note.start_tick >= section_start) {
      last_bar_notes.push_back(&note);
    }
  }

  if (last_bar_notes.empty()) return;

  // Sort by start_tick
  std::sort(last_bar_notes.begin(), last_bar_notes.end(),
            [](const NoteEvent* a, const NoteEvent* b) {
              return a->start_tick < b->start_tick;
            });

  // Collect unique start_ticks (sorted)
  std::vector<Tick> unique_starts;
  for (const auto* note : last_bar_notes) {
    if (unique_starts.empty() || unique_starts.back() != note->start_tick) {
      unique_starts.push_back(note->start_tick);
    }
  }

  // Extend each note, capping at the start of the next different start_tick
  for (auto* note : last_bar_notes) {
    Tick max_end = section_end;
    // Find the next different start_tick after this note's start
    for (Tick start : unique_starts) {
      if (start > note->start_tick) {
        max_end = start;
        break;
      }
    }
    if (max_end > note->start_tick) {
      Tick new_duration = max_end - note->start_tick;
      // Respect chord boundaries when extending
      if (chord_lookup != nullptr) {
        auto info = chord_lookup->analyzeChordBoundary(note->note, note->start_tick, new_duration);
        if (info.boundary_tick > 0 &&
            (info.safety == CrossBoundarySafety::NonChordTone ||
             info.safety == CrossBoundarySafety::AvoidNote) &&
            info.safe_duration >= TICK_SIXTEENTH) {
          new_duration = info.safe_duration;
        }
      }
      // Inter-track collision check: limit extension to avoid dissonance with other tracks
      if (harmony != nullptr) {
        Tick safe_end = harmony->getMaxSafeEnd(
            note->start_tick, note->note, track_role,
            note->start_tick + new_duration);
        if (safe_end > note->start_tick) {
          new_duration = safe_end - note->start_tick;
        }
      }
#ifdef MIDISKETCH_NOTE_PROVENANCE
      if (new_duration != note->duration) {
        note->addTransformStep(TransformStepType::PostProcessDuration, 0, 0, 1, 0);
      }
#endif
      note->duration = new_duration;
    }
  }
}

// ============================================================================
// ExitPattern Implementation
// ============================================================================

void PostProcessor::applyExitPattern(MidiTrack& track, const Section& section,
                                     ICollisionDetector* harmony,
                                     TrackRole track_role) {
  if (section.exit_pattern == ExitPattern::None) {
    return;
  }

  auto& notes = track.notes();
  if (notes.empty()) return;

  Tick section_start = section.start_tick;
  Tick section_end = section_start + section.bars * TICKS_PER_BAR;

  switch (section.exit_pattern) {
    case ExitPattern::Fadeout:
      applyExitFadeout(notes, section_end, section.bars);
      break;
    case ExitPattern::FinalHit:
      applyExitFinalHit(notes, section_end);
      break;
    case ExitPattern::CutOff:
      applyExitCutOff(notes, section_start, section_end);
      break;
    case ExitPattern::Sustain:
      applyExitSustain(notes, section_start, section_end, harmony, harmony, track_role);
      break;
    case ExitPattern::None:
      break;
  }
}

void PostProcessor::applyAllExitPatterns(std::vector<MidiTrack*>& tracks,
                                         const std::vector<Section>& sections,
                                         ICollisionDetector* harmony) {
  std::vector<TrackRole> empty_roles;
  applyAllExitPatterns(tracks, empty_roles, sections, harmony);
}

void PostProcessor::applyAllExitPatterns(std::vector<MidiTrack*>& tracks,
                                         const std::vector<TrackRole>& roles,
                                         const std::vector<Section>& sections,
                                         ICollisionDetector* harmony) {
  for (const auto& section : sections) {
    if (section.exit_pattern == ExitPattern::None) {
      continue;
    }

    for (size_t i = 0; i < tracks.size(); ++i) {
      if (tracks[i] != nullptr) {
        TrackRole role = (i < roles.size()) ? roles[i] : TrackRole::Vocal;
        applyExitPattern(*tracks[i], section, harmony, role);
      }
    }
  }
}


// ============================================================================
// Chorus Drop Implementation (Phase 2, Task 2-2)
// ============================================================================

void PostProcessor::applyChorusDrop(std::vector<MidiTrack*>& tracks,
                                     const std::vector<Section>& sections,
                                     MidiTrack* drum_track,
                                     ChorusDropStyle default_style) {
  constexpr uint8_t CRASH_NOTE = 49;     // Crash cymbal
  constexpr uint8_t CRASH_VEL = 110;     // Strong crash velocity

  // Find B sections followed by Chorus (or MixBreak/Drop sections that can also have drops)
  for (size_t idx = 0; idx + 1 < sections.size(); ++idx) {
    const Section& section = sections[idx];
    const Section& next_section = sections[idx + 1];

    // Only process sections before Chorus that could have a drop
    // (B sections, MixBreak, or any section with explicit drop_style)
    bool is_pre_chorus = (section.type == SectionType::B ||
                          section.type == SectionType::MixBreak ||
                          section.type == SectionType::Interlude) &&
                         next_section.type == SectionType::Chorus;

    // Use per-section drop_style if set, otherwise use default_style for B sections
    ChorusDropStyle style = section.drop_style;
    if (style == ChorusDropStyle::None) {
      // Fall back to default_style only for B sections before Chorus
      if (section.type == SectionType::B && next_section.type == SectionType::Chorus) {
        style = default_style;
      } else if (!is_pre_chorus) {
        continue;  // Skip sections without explicit drop_style that aren't B->Chorus
      }
    }

    // Skip if still None after fallback
    if (style == ChorusDropStyle::None) {
      continue;
    }

    // Calculate the drop zone (last 1 beat before next section)
    Tick section_end_tick = section.endTick();
    Tick drop_start_tick = section_end_tick - TICKS_PER_BEAT;
    Tick next_section_start_tick = next_section.start_tick;

    // Truncate melodic tracks in the drop zone
    for (MidiTrack* track : tracks) {
      if (track == nullptr) continue;

      auto& notes = track->notes();

      // Remove notes that start in the drop zone
      notes.erase(
          std::remove_if(notes.begin(), notes.end(),
                         [drop_start_tick, section_end_tick](const NoteEvent& note) {
                           return note.start_tick >= drop_start_tick &&
                                  note.start_tick < section_end_tick;
                         }),
          notes.end());

      // Truncate notes that extend into the drop zone
      for (auto& note : notes) {
        Tick note_end = note.start_tick + note.duration;
        if (note.start_tick < drop_start_tick && note_end > drop_start_tick) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
          note.addTransformStep(TransformStepType::PostProcessDuration, 0, 0, -1, 0);
#endif
          note.duration = drop_start_tick - note.start_tick;
        }
      }
    }

    // Dramatic/DrumHit: also truncate drum track (except fills)
    if (style == ChorusDropStyle::Dramatic || style == ChorusDropStyle::DrumHit) {
      if (drum_track != nullptr && !drum_track->empty()) {
        auto& drum_notes = drum_track->notes();
        // Remove drum notes in drop zone (fill should be added separately)
        drum_notes.erase(
            std::remove_if(drum_notes.begin(), drum_notes.end(),
                           [drop_start_tick, section_end_tick](const NoteEvent& note) {
                             return note.start_tick >= drop_start_tick &&
                                    note.start_tick < section_end_tick;
                           }),
            drum_notes.end());
      }
    }

    // DrumHit: add crash cymbal on next section entry
    if (style == ChorusDropStyle::DrumHit) {
      if (drum_track != nullptr) {
        auto& drum_notes = drum_track->notes();
        // Check if crash already exists at next section start
        bool has_crash = false;
        for (const auto& note : drum_notes) {
          if (note.start_tick == next_section_start_tick && note.note == CRASH_NOTE) {
            has_crash = true;
            break;
          }
        }
        // Add crash cymbal at next section entry
        if (!has_crash) {
          NoteEvent crash;
          crash.start_tick = next_section_start_tick;
          crash.duration = TICKS_PER_BEAT;
          crash.note = CRASH_NOTE;
          crash.velocity = CRASH_VEL;
#ifdef MIDISKETCH_NOTE_PROVENANCE
          crash.prov_chord_degree = -1;
          crash.prov_lookup_tick = next_section_start_tick;
          crash.prov_source = static_cast<uint8_t>(NoteSource::PostProcess);
          crash.prov_original_pitch = CRASH_NOTE;
#endif
          drum_notes.push_back(crash);
        }
      }
    }
  }
}

// ============================================================================
// Ritardando Implementation (Phase 2, Task 2-3)
// ============================================================================

namespace {
// Helper: Check if extending note duration would create dissonance with other tracks
// Returns the maximum safe end tick (may be less than desired_end if clash found)
Tick getSafeEndForRitardando(const NoteEvent& note, Tick desired_end,
                              const std::vector<MidiTrack*>& all_tracks,
                              const MidiTrack* current_track) {
  Tick safe_end = desired_end;

  for (const MidiTrack* other_track : all_tracks) {
    if (other_track == nullptr || other_track == current_track) continue;

    for (const auto& other_note : other_track->notes()) {
      // Skip notes that end before or at note start
      Tick other_end = other_note.start_tick + other_note.duration;
      if (other_end <= note.start_tick) continue;

      // Skip notes that start at or after desired_end
      if (other_note.start_tick >= desired_end) continue;

      // Check if extension would create dissonance
      int actual_semitones = std::abs(static_cast<int>(note.note) -
                                       static_cast<int>(other_note.note));
      bool is_dissonant = isDissonantActualInterval(actual_semitones, 0);

      if (is_dissonant) {
        // If other note starts after our note, we can extend up to (but not including) it
        if (other_note.start_tick > note.start_tick && other_note.start_tick < safe_end) {
          safe_end = other_note.start_tick;
        }
      }
    }
  }

  return safe_end;
}
}  // namespace

void PostProcessor::applyRitardando(std::vector<MidiTrack*>& tracks,
                                     const std::vector<Section>& sections,
                                     const std::vector<MidiTrack*>& collision_check_tracks) {
  // Build combined list for collision checking (tracks + collision_check_tracks)
  std::vector<MidiTrack*> all_tracks_for_collision;
  all_tracks_for_collision.reserve(tracks.size() + collision_check_tracks.size());
  for (MidiTrack* t : tracks) {
    all_tracks_for_collision.push_back(t);
  }
  for (MidiTrack* t : collision_check_tracks) {
    // Avoid duplicates
    bool found = false;
    for (MidiTrack* existing : all_tracks_for_collision) {
      if (existing == t) {
        found = true;
        break;
      }
    }
    if (!found) {
      all_tracks_for_collision.push_back(t);
    }
  }

  // Find Outro sections (usually the last section)
  for (size_t idx = 0; idx < sections.size(); ++idx) {
    const Section& section = sections[idx];

    if (section.type != SectionType::Outro) {
      continue;
    }

    // Need at least 4 bars for ritardando effect
    uint8_t rit_bars = std::min(section.bars, static_cast<uint8_t>(4));
    if (rit_bars < 2) continue;

    Tick section_end_tick = section.endTick();
    Tick rit_start_tick = section_end_tick - rit_bars * TICKS_PER_BAR;
    Tick rit_duration = section_end_tick - rit_start_tick;

    for (MidiTrack* track : tracks) {
      if (track == nullptr) continue;

      auto& notes = track->notes();
      NoteEvent* last_note_in_rit = nullptr;

      for (auto& note : notes) {
        if (note.start_tick >= rit_start_tick && note.start_tick < section_end_tick) {
          // Calculate progress through ritardando zone (0.0 to 1.0)
          float progress = static_cast<float>(note.start_tick - rit_start_tick) /
                           static_cast<float>(rit_duration);

          // Duration stretch: 1.0 -> 1.3 (30% longer at the end)
          float duration_mult = 1.0f + progress * 0.3f;
          Tick desired_duration = static_cast<Tick>(note.duration * duration_mult);
          Tick desired_end = note.start_tick + desired_duration;

          // Check for dissonance with other tracks and limit extension
          Tick safe_end = getSafeEndForRitardando(note, desired_end, all_tracks_for_collision, track);
#ifdef MIDISKETCH_NOTE_PROVENANCE
          if (safe_end - note.start_tick != note.duration) {
            note.addTransformStep(TransformStepType::PostProcessDuration, 0, 0, 1, 0);
          }
#endif
          note.duration = safe_end - note.start_tick;

          // Velocity decrescendo: 1.0 -> 0.75 (25% softer at the end)
          float velocity_mult = 1.0f - progress * 0.25f;
          int new_vel = static_cast<int>(note.velocity * velocity_mult);
          uint8_t clamped_vel = vel::clamp(new_vel, 30, 127);
#ifdef MIDISKETCH_NOTE_PROVENANCE
          if (clamped_vel != note.velocity) {
            note.addTransformStep(TransformStepType::PostProcessVelocity, note.velocity, clamped_vel,
                                  2, 0);
          }
#endif
          note.velocity = clamped_vel;

          // Track the last note for fermata
          if (last_note_in_rit == nullptr ||
              note.start_tick > last_note_in_rit->start_tick) {
            last_note_in_rit = &note;
          }
        }
      }

      // Fermata effect: extend final note duration to fill until section end
      // Also check for dissonance before extending
      if (last_note_in_rit != nullptr) {
        Tick target_end = section_end_tick - TICKS_PER_BEAT / 8;  // Small release gap
        if (last_note_in_rit->start_tick < target_end) {
          Tick safe_end = getSafeEndForRitardando(*last_note_in_rit, target_end,
                                                   all_tracks_for_collision, track);
          if (safe_end > last_note_in_rit->start_tick + last_note_in_rit->duration) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
            last_note_in_rit->addTransformStep(TransformStepType::PostProcessDuration, 0, 0, 1, 0);
#endif
            last_note_in_rit->duration = safe_end - last_note_in_rit->start_tick;
          }
        }
      }
    }
  }
}

// ============================================================================
// Enhanced FinalHit Implementation (Phase 2, Task 2-4)
// ============================================================================

// Helper: Check if extending a chord note would create dissonance with vocal
// Returns the maximum safe end tick (may be less than desired_end if clash found)
static Tick getMaxSafeEndTick(const NoteEvent& chord_note, Tick desired_end,
                               const MidiTrack* vocal_track) {
  if (vocal_track == nullptr) {
    return desired_end;  // No vocal to clash with
  }

  Tick safe_end = desired_end;

  for (const auto& vocal_note : vocal_track->notes()) {
    Tick vocal_start = vocal_note.start_tick;
    Tick vocal_end = vocal_start + vocal_note.duration;

    // Check if extended chord would overlap with this vocal note
    if (chord_note.start_tick < vocal_end && desired_end > vocal_start) {
      // Calculate interval
      int actual_semitones =
          std::abs(static_cast<int>(chord_note.note) - static_cast<int>(vocal_note.note));

      bool is_dissonant = isDissonantActualInterval(actual_semitones, 0);

      if (is_dissonant) {
        // Found a clash - limit extension to just before the vocal note starts
        // But only if vocal starts after chord's original end
        Tick original_end = chord_note.start_tick + chord_note.duration;
        if (vocal_start > original_end) {
          // Safe to extend up to (but not including) vocal start
          safe_end = std::min(safe_end, vocal_start);
        } else if (vocal_start <= chord_note.start_tick) {
          // Vocal already playing when chord starts - don't extend at all
          safe_end = std::min(safe_end, original_end);
        } else {
          // Vocal starts during chord's original duration - no extension possible
          safe_end = std::min(safe_end, original_end);
        }
      }
    }
  }

  return safe_end;
}

void PostProcessor::applyEnhancedFinalHit(MidiTrack* bass_track, MidiTrack* drum_track,
                                           MidiTrack* chord_track, const MidiTrack* vocal_track,
                                           const Section& section,
                                           const ICollisionDetector* harmony) {
  if (section.exit_pattern != ExitPattern::FinalHit) {
    return;
  }

  Tick section_start = section.start_tick;
  Tick section_end = section_start + section.bars * TICKS_PER_BAR;
  Tick final_beat_start = section_end - TICKS_PER_BEAT;

  constexpr uint8_t FINAL_HIT_VEL = 110;
  constexpr uint8_t BD_NOTE = 36;     // Bass drum
  constexpr uint8_t CRASH_NOTE = 49;  // Crash cymbal

  // Bass track: ensure strong hit on final beat with velocity 110+
  if (bass_track != nullptr) {
    auto& bass_notes = bass_track->notes();
    bool has_final_bass = false;

    for (auto& note : bass_notes) {
      if (note.start_tick >= final_beat_start && note.start_tick < section_end) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
        if (note.velocity < FINAL_HIT_VEL) {
          note.addTransformStep(TransformStepType::PostProcessVelocity, note.velocity, FINAL_HIT_VEL, 5, 0);
        }
#endif
        note.velocity = std::max(note.velocity, FINAL_HIT_VEL);
        has_final_bass = true;
      }
    }

    // If no bass note exists on final beat, add one (root note at bass range)
    if (!has_final_bass) {
      constexpr uint8_t DEFAULT_BASS_ROOT = 36;  // C2
      uint8_t bass_pitch = DEFAULT_BASS_ROOT;

      // Verify pitch is safe; find alternative if collision detected
      if (harmony != nullptr &&
          !harmony->isConsonantWithOtherTracks(bass_pitch, final_beat_start, TICKS_PER_BEAT,
                                                TrackRole::Bass)) {
        auto candidates = getSafePitchCandidates(*harmony, bass_pitch, final_beat_start,
                                                  TICKS_PER_BEAT, TrackRole::Bass,
                                                  BASS_LOW, BASS_HIGH,
                                                  PitchPreference::PreferRootFifth);
        if (!candidates.empty()) {
          bass_pitch = candidates[0].pitch;
        }
      }

      NoteEvent final_bass;
      final_bass.start_tick = final_beat_start;
      final_bass.duration = TICKS_PER_BEAT;
      final_bass.note = bass_pitch;
      final_bass.velocity = FINAL_HIT_VEL;
#ifdef MIDISKETCH_NOTE_PROVENANCE
      final_bass.prov_chord_degree = -1;
      final_bass.prov_lookup_tick = final_beat_start;
      final_bass.prov_source = static_cast<uint8_t>(NoteSource::PostProcess);
      final_bass.prov_original_pitch = DEFAULT_BASS_ROOT;
#endif
      bass_notes.push_back(final_bass);
    }
  }

  // Drum track: add kick + crash on final beat with velocity 110+
  // Only process if drum track has notes (drums are enabled)
  if (drum_track != nullptr && !drum_track->empty()) {
    auto& drum_notes = drum_track->notes();
    bool has_final_kick = false;
    bool has_final_crash = false;

    // Only boost core kit elements (kick, snare, crash) - not auxiliary percussion
    constexpr uint8_t SD_NOTE = 38;  // Snare drum
    for (auto& note : drum_notes) {
      if (note.start_tick >= final_beat_start && note.start_tick < section_end) {
        // Only boost kick, snare, and crash velocity
        if (note.note == BD_NOTE || note.note == SD_NOTE || note.note == CRASH_NOTE) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
          if (note.velocity < FINAL_HIT_VEL) {
            note.addTransformStep(TransformStepType::PostProcessVelocity, note.velocity, FINAL_HIT_VEL, 5, 0);
          }
#endif
          note.velocity = std::max(note.velocity, FINAL_HIT_VEL);
        }
        if (note.note == BD_NOTE) has_final_kick = true;
        if (note.note == CRASH_NOTE) has_final_crash = true;
      }
    }

    // Add kick if missing
    if (!has_final_kick) {
      NoteEvent kick;
      kick.start_tick = final_beat_start;
      kick.duration = TICKS_PER_BEAT / 2;
      kick.note = BD_NOTE;
      kick.velocity = FINAL_HIT_VEL;
#ifdef MIDISKETCH_NOTE_PROVENANCE
      kick.prov_chord_degree = -1;
      kick.prov_lookup_tick = final_beat_start;
      kick.prov_source = static_cast<uint8_t>(NoteSource::PostProcess);
      kick.prov_original_pitch = BD_NOTE;
#endif
      drum_notes.push_back(kick);
    }

    // Add crash if missing
    if (!has_final_crash) {
      NoteEvent crash;
      crash.start_tick = final_beat_start;
      crash.duration = TICKS_PER_BEAT;
      crash.note = CRASH_NOTE;
      crash.velocity = FINAL_HIT_VEL;
#ifdef MIDISKETCH_NOTE_PROVENANCE
      crash.prov_chord_degree = -1;
      crash.prov_lookup_tick = final_beat_start;
      crash.prov_source = static_cast<uint8_t>(NoteSource::PostProcess);
      crash.prov_original_pitch = CRASH_NOTE;
#endif
      drum_notes.push_back(crash);
    }
  }

  // Chord track: sustain final chord as whole note with strong velocity
  // Check against all tracks (via harmony) or vocal only (fallback) to avoid dissonance
  if (chord_track != nullptr) {
    auto& chord_notes = chord_track->notes();

    for (auto& note : chord_notes) {
      if (note.start_tick >= final_beat_start && note.start_tick < section_end) {
        // Extend duration, but check for clashes first
        Tick safe_end;
        if (harmony != nullptr) {
          // Use comprehensive clash detection against all registered tracks
          safe_end = harmony->getMaxSafeEnd(note.start_tick, note.note, TrackRole::Chord, section_end);
        } else {
          // Fallback: check against vocal only
          safe_end = getMaxSafeEndTick(note, section_end, vocal_track);
        }
        if (safe_end > note.start_tick) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
          if (safe_end - note.start_tick != note.duration) {
            note.addTransformStep(TransformStepType::PostProcessDuration, 0, 0, 1, 0);
          }
#endif
          note.duration = safe_end - note.start_tick;
        }
#ifdef MIDISKETCH_NOTE_PROVENANCE
        if (note.velocity < FINAL_HIT_VEL) {
          note.addTransformStep(TransformStepType::PostProcessVelocity, note.velocity, FINAL_HIT_VEL, 5, 0);
        }
#endif
        note.velocity = std::max(note.velocity, FINAL_HIT_VEL);
      }
    }

    // Also extend chord notes from the last bar that could sustain through
    Tick last_bar_start = section_end - TICKS_PER_BAR;
    for (auto& note : chord_notes) {
      if (note.start_tick >= last_bar_start && note.start_tick < final_beat_start) {
        // Extend to section end, but check for clashes first
        Tick safe_end;
        if (harmony != nullptr) {
          safe_end = harmony->getMaxSafeEnd(note.start_tick, note.note, TrackRole::Chord, section_end);
        } else {
          safe_end = getMaxSafeEndTick(note, section_end, vocal_track);
        }
        if (safe_end > note.start_tick + note.duration) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
          if (safe_end - note.start_tick != note.duration) {
            note.addTransformStep(TransformStepType::PostProcessDuration, 0, 0, 1, 0);
          }
#endif
          note.duration = safe_end - note.start_tick;
        }
      }
    }
  }
}

}  // namespace midisketch
