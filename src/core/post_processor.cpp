/**
 * @file post_processor.cpp
 * @brief Implementation of track post-processing.
 */

#include "core/post_processor.h"

#include <algorithm>

#include "core/note_factory.h"  // for NoteSource enum

namespace midisketch {

bool PostProcessor::isStrongBeat(Tick tick) {
  Tick position_in_bar = tick % TICKS_PER_BAR;
  // Beats 1 and 3 are at 0 and TICKS_PER_BEAT*2
  return position_in_bar < TICKS_PER_BEAT / 4 ||
         (position_in_bar >= TICKS_PER_BEAT * 2 &&
          position_in_bar < TICKS_PER_BEAT * 2 + TICKS_PER_BEAT / 4);
}

void PostProcessor::applyHumanization(std::vector<MidiTrack*>& tracks, const HumanizeParams& params,
                                      std::mt19937& rng) {
  // ENHANCED: Stronger humanization for more natural feel.
  // Maximum timing offset in ticks (approximately 13ms at 120 BPM, was ~8ms)
  // This adds more "pocket" feel without being noticeable as timing errors.
  constexpr Tick MAX_TIMING_OFFSET = 25;  // Was 15
  // Maximum velocity variation - more expressive dynamics
  constexpr int MAX_VELOCITY_VARIATION = 12;  // Was 8

  // Scale factors from parameters
  float timing_scale = params.timing;
  float velocity_scale = params.velocity;

  // Create distributions
  std::normal_distribution<float> timing_dist(0.0f, 3.0f);
  std::uniform_int_distribution<int> velocity_dist(-MAX_VELOCITY_VARIATION, MAX_VELOCITY_VARIATION);

  for (MidiTrack* track : tracks) {
    auto& notes = track->notes();
    for (auto& note : notes) {
      // Timing humanization: only on weak beats
      if (!isStrongBeat(note.start_tick)) {
        float offset = timing_dist(rng) * timing_scale;
        int tick_offset = static_cast<int>(offset * MAX_TIMING_OFFSET / 3.0f);
        tick_offset = std::clamp(tick_offset, -static_cast<int>(MAX_TIMING_OFFSET),
                                 static_cast<int>(MAX_TIMING_OFFSET));
        // Ensure we don't go negative
        if (note.start_tick > static_cast<Tick>(-tick_offset)) {
          note.start_tick = static_cast<Tick>(static_cast<int>(note.start_tick) + tick_offset);
        }
      }

      // Velocity humanization: less variation on strong beats
      // Minimum velocity of 36 ensures non-ghost notes stay above ghost range (25-35)
      // after humanization. Actual ghost notes (25-35) are intentionally created
      // by addBassGhostNotes and should remain in that range.
      float vel_factor = isStrongBeat(note.start_tick) ? 0.5f : 1.0f;
      int vel_offset = static_cast<int>(velocity_dist(rng) * velocity_scale * vel_factor);
      int new_velocity = static_cast<int>(note.velocity) + vel_offset;
      // Preserve intentional ghost notes (25-35), but prevent non-ghost notes from
      // falling into ghost range. Notes originally above 35 should stay above 35.
      int min_velocity = (note.velocity <= 35) ? 1 : 36;
      note.velocity = static_cast<uint8_t>(std::clamp(new_velocity, min_velocity, 127));
    }
  }
}

void PostProcessor::fixVocalOverlaps(MidiTrack& vocal_track) {
  auto& vocal_notes = vocal_track.notes();
  if (vocal_notes.size() <= 1) {
    return;
  }

  // Sort by startTick to ensure proper order after humanization
  std::sort(vocal_notes.begin(), vocal_notes.end(),
            [](const NoteEvent& a, const NoteEvent& b) { return a.start_tick < b.start_tick; });

  for (size_t i = 0; i + 1 < vocal_notes.size(); ++i) {
    Tick end_tick = vocal_notes[i].start_tick + vocal_notes[i].duration;
    Tick next_start = vocal_notes[i + 1].start_tick;

    // Ensure no overlap: end of current note <= start of next note
    if (end_tick > next_start) {
      // Guard against underflow: if same startTick, use minimum duration
      Tick max_duration =
          (next_start > vocal_notes[i].start_tick) ? (next_start - vocal_notes[i].start_tick) : 1;
      vocal_notes[i].duration = max_duration;

      // If still overlapping (same startTick case), shift next note
      if (vocal_notes[i].start_tick + vocal_notes[i].duration > next_start) {
        vocal_notes[i + 1].start_tick = vocal_notes[i].start_tick + vocal_notes[i].duration;
      }
    }
  }
}

SectionType PostProcessor::getSectionTypeAtTick(Tick tick, const std::vector<Section>& sections) {
  for (const auto& section : sections) {
    Tick section_end = section.start_tick + section.bars * TICKS_PER_BAR;
    if (tick >= section.start_tick && tick < section_end) {
      return section.type;
    }
  }
  return SectionType::A;  // Default fallback
}

void PostProcessor::applySectionAwareVelocityHumanization(
    std::vector<MidiTrack*>& tracks, const std::vector<Section>& sections, std::mt19937& rng) {
  for (MidiTrack* track : tracks) {
    auto& notes = track->notes();
    for (auto& note : notes) {
      SectionType section_type = getSectionTypeAtTick(note.start_tick, sections);

      // Section-dependent velocity variation range
      // Chorus/MixBreak: tight (±6%) for consistent energy
      // Verse(A)/Bridge/Intro/Outro: relaxed (±12%) for natural feel
      // B(Pre-chorus): moderate (±8%)
      float variation_pct;
      switch (section_type) {
        case SectionType::Chorus:
        case SectionType::MixBreak:
          variation_pct = 0.06f;
          break;
        case SectionType::B:
        case SectionType::Chant:
          variation_pct = 0.08f;
          break;
        default:  // A, Intro, Bridge, Interlude, Outro
          variation_pct = 0.12f;
          break;
      }

      // Strong beats get half the variation
      float beat_factor = isStrongBeat(note.start_tick) ? 0.5f : 1.0f;
      float max_variation = note.velocity * variation_pct * beat_factor;

      std::uniform_real_distribution<float> dist(-max_variation, max_variation);
      int new_vel = static_cast<int>(note.velocity) + static_cast<int>(dist(rng));
      // Preserve intentional ghost notes (25-35), but prevent non-ghost notes from
      // falling into ghost range. Notes originally above 35 should stay above 35.
      int min_velocity = (note.velocity <= 35) ? 1 : 36;
      note.velocity = static_cast<uint8_t>(std::clamp(new_vel, min_velocity, 127));
    }
  }
}

void PostProcessor::applyMicroTimingOffsets(MidiTrack& vocal, MidiTrack& bass,
                                             MidiTrack& drum_track) {
  // Per-instrument timing offsets create the "pocket" feel.
  // Positive = push (ahead of beat), negative = lay back (behind beat).
  // Values in ticks (at 480 ticks/beat, 8 ticks ≈ 4ms at 120 BPM).

  // Helper to apply offset to all notes in a track
  auto applyOffset = [](MidiTrack& track, int offset) {
    if (offset == 0 || track.empty()) return;
    auto& notes = track.notes();
    for (auto& note : notes) {
      int new_tick = static_cast<int>(note.start_tick) + offset;
      if (new_tick > 0) {
        note.start_tick = static_cast<Tick>(new_tick);
      }
    }
  };

  // Drum instrument offsets (by MIDI note number)
  constexpr int HH_OFFSET = 8;    // Hi-hat slightly ahead (driving feel)
  constexpr int SD_OFFSET = -8;   // Snare slightly behind (relaxed pocket)

  // GM drum note numbers
  constexpr uint8_t BD_NOTE = 36;
  constexpr uint8_t SD_NOTE = 38;
  constexpr uint8_t HHC_NOTE = 42;
  constexpr uint8_t HHO_NOTE = 46;
  constexpr uint8_t HHF_NOTE = 44;

  // Apply per-instrument offsets to drum track
  auto& drum_notes = drum_track.notes();
  for (auto& note : drum_notes) {
    int offset = 0;
    if (note.note == HHC_NOTE || note.note == HHO_NOTE || note.note == HHF_NOTE) {
      offset = HH_OFFSET;
    } else if (note.note == SD_NOTE) {
      offset = SD_OFFSET;
    }
    // BD_NOTE stays on grid (anchor)
    (void)BD_NOTE;

    if (offset != 0) {
      int new_tick = static_cast<int>(note.start_tick) + offset;
      if (new_tick > 0) {
        note.start_tick = static_cast<Tick>(new_tick);
      }
    }
  }

  // Melodic track offsets
  applyOffset(vocal, 4);   // Vocal pushes slightly ahead
  applyOffset(bass, -4);   // Bass lays back slightly
}

// ============================================================================
// ExitPattern Implementation
// ============================================================================

void PostProcessor::applyExitPattern(MidiTrack& track, const Section& section) {
  if (section.exit_pattern == ExitPattern::None) {
    return;
  }

  auto& notes = track.notes();
  if (notes.empty()) return;

  Tick section_start = section.start_tick;
  Tick section_end = section_start + section.bars * TICKS_PER_BAR;

  switch (section.exit_pattern) {
    case ExitPattern::Fadeout: {
      // Velocity gradually decreases in last 2 bars (1.0 -> 0.4)
      uint8_t fade_bars = std::min(section.bars, static_cast<uint8_t>(2));
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
          note.velocity = static_cast<uint8_t>(std::clamp(new_vel, 1, 127));
        }
      }
      break;
    }

    case ExitPattern::FinalHit: {
      // Strong accent on the last beat of the section
      constexpr uint8_t kFinalHitVelocity = 120;
      Tick last_beat_start = section_end - TICKS_PER_BEAT;

      for (auto& note : notes) {
        if (note.start_tick >= last_beat_start && note.start_tick < section_end) {
          note.velocity = std::max(note.velocity, kFinalHitVelocity);
          // Clamp to MIDI max
          if (note.velocity > 127) {
            note.velocity = 127;
          }
        }
      }
      break;
    }

    case ExitPattern::CutOff: {
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
            note.duration = cutoff_point - note.start_tick;
          }
        }
      }
      break;
    }

    case ExitPattern::Sustain: {
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

      if (last_bar_notes.empty()) break;

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
          note->duration = max_end - note->start_tick;
        }
      }
      break;
    }

    case ExitPattern::None:
      // No modification (already handled above, but kept for completeness)
      break;
  }
}

void PostProcessor::applyAllExitPatterns(std::vector<MidiTrack*>& tracks,
                                         const std::vector<Section>& sections) {
  for (const auto& section : sections) {
    if (section.exit_pattern == ExitPattern::None) {
      continue;
    }

    for (MidiTrack* track : tracks) {
      if (track != nullptr) {
        applyExitPattern(*track, section);
      }
    }
  }
}

// ============================================================================
// Pre-chorus Lift Implementation
// ============================================================================

void PostProcessor::applyPreChorusLift(std::vector<MidiTrack*>& tracks,
                                        const std::vector<Section>& sections) {
  // Find B sections that are followed by Chorus
  for (size_t idx = 0; idx + 1 < sections.size(); ++idx) {
    const Section& section = sections[idx];
    const Section& next_section = sections[idx + 1];

    // Only B sections before Chorus
    if (section.type != SectionType::B || next_section.type != SectionType::Chorus) {
      continue;
    }

    // Need at least 3 bars for lift effect (last 2 bars = lift zone)
    if (section.bars < 3) {
      continue;
    }

    // Calculate lift zone (last 2 bars of B section)
    Tick section_end_tick = section.start_tick + section.bars * TICKS_PER_BAR;
    Tick lift_start_tick = section_end_tick - 2 * TICKS_PER_BAR;

    // Apply lift effect to each melodic track
    for (MidiTrack* track : tracks) {
      if (track != nullptr) {
        applyPreChorusLiftToTrack(*track, section, lift_start_tick, section_end_tick);
      }
    }
  }
}

void PostProcessor::applyPreChorusLiftToTrack(MidiTrack& track, const Section& section,
                                               Tick lift_start_tick, Tick section_end_tick) {
  (void)section;  // Used for potential future extension

  auto& notes = track.notes();
  if (notes.empty()) return;

  // Find notes that start in the lift zone
  std::vector<NoteEvent*> lift_notes;
  for (auto& note : notes) {
    if (note.start_tick >= lift_start_tick && note.start_tick < section_end_tick) {
      lift_notes.push_back(&note);
    }
  }

  if (lift_notes.empty()) return;

  // Strategy: Extend the last note in the lift zone to sustain until section end
  // This creates a "held breath" effect before Chorus
  NoteEvent* last_note = lift_notes.back();

  // Find the latest note that starts before the section end
  for (auto* note : lift_notes) {
    if (note->start_tick > last_note->start_tick) {
      last_note = note;
    }
  }

  // Extend the last note to fill until section boundary
  // (minus a small gap for natural release)
  constexpr Tick RELEASE_GAP = TICKS_PER_BEAT / 4;  // 16th note gap
  Tick target_end = section_end_tick - RELEASE_GAP;

  if (last_note->start_tick < target_end) {
    Tick new_duration = target_end - last_note->start_tick;
    // Only extend if it makes the note longer
    if (new_duration > last_note->duration) {
      last_note->duration = new_duration;
    }
  }

  // Additionally, remove notes that might overlap with the extended note
  // (after the last note) - but this should rarely happen in practice
}

// ============================================================================
// Chorus Drop Implementation (Phase 2, Task 2-2)
// ============================================================================

void PostProcessor::applyChorusDrop(std::vector<MidiTrack*>& tracks,
                                     const std::vector<Section>& sections,
                                     MidiTrack* drum_track,
                                     ChorusDropStyle style) {
  // None style: no drop effect
  if (style == ChorusDropStyle::None) {
    return;
  }

  constexpr uint8_t CRASH_NOTE = 49;     // Crash cymbal
  constexpr uint8_t CRASH_VEL = 110;     // Strong crash velocity

  // Find B sections followed by Chorus
  for (size_t idx = 0; idx + 1 < sections.size(); ++idx) {
    const Section& section = sections[idx];
    const Section& next_section = sections[idx + 1];

    // Only B sections before Chorus
    if (section.type != SectionType::B || next_section.type != SectionType::Chorus) {
      continue;
    }

    // Calculate the drop zone (last 1 beat before Chorus)
    Tick section_end_tick = section.start_tick + section.bars * TICKS_PER_BAR;
    Tick drop_start_tick = section_end_tick - TICKS_PER_BEAT;
    Tick chorus_start_tick = next_section.start_tick;

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

    // DrumHit: add crash cymbal on chorus entry
    if (style == ChorusDropStyle::DrumHit) {
      if (drum_track != nullptr) {
        auto& drum_notes = drum_track->notes();
        // Check if crash already exists at chorus start
        bool has_crash = false;
        for (const auto& note : drum_notes) {
          if (note.start_tick == chorus_start_tick && note.note == CRASH_NOTE) {
            has_crash = true;
            break;
          }
        }
        // Add crash cymbal at chorus entry
        if (!has_crash) {
          NoteEvent crash;
          crash.start_tick = chorus_start_tick;
          crash.duration = TICKS_PER_BEAT;
          crash.note = CRASH_NOTE;
          crash.velocity = CRASH_VEL;
          // Provenance tracking
          crash.prov_chord_degree = -1;
          crash.prov_lookup_tick = chorus_start_tick;
          crash.prov_source = static_cast<uint8_t>(NoteSource::PostProcess);
          crash.prov_original_pitch = CRASH_NOTE;
          drum_notes.push_back(crash);
        }
      }
    }
  }
}

// ============================================================================
// Ritardando Implementation (Phase 2, Task 2-3)
// ============================================================================

void PostProcessor::applyRitardando(std::vector<MidiTrack*>& tracks,
                                     const std::vector<Section>& sections) {
  // Find Outro sections (usually the last section)
  for (size_t idx = 0; idx < sections.size(); ++idx) {
    const Section& section = sections[idx];

    if (section.type != SectionType::Outro) {
      continue;
    }

    // Need at least 4 bars for ritardando effect
    uint8_t rit_bars = std::min(section.bars, static_cast<uint8_t>(4));
    if (rit_bars < 2) continue;

    Tick section_end_tick = section.start_tick + section.bars * TICKS_PER_BAR;
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
          note.duration = static_cast<Tick>(note.duration * duration_mult);

          // Velocity decrescendo: 1.0 -> 0.75 (25% softer at the end)
          float velocity_mult = 1.0f - progress * 0.25f;
          int new_vel = static_cast<int>(note.velocity * velocity_mult);
          note.velocity = static_cast<uint8_t>(std::clamp(new_vel, 30, 127));

          // Track the last note for fermata
          if (last_note_in_rit == nullptr ||
              note.start_tick > last_note_in_rit->start_tick) {
            last_note_in_rit = &note;
          }
        }
      }

      // Fermata effect: extend final note duration to fill until section end
      if (last_note_in_rit != nullptr) {
        Tick target_end = section_end_tick - TICKS_PER_BEAT / 8;  // Small release gap
        if (last_note_in_rit->start_tick < target_end) {
          Tick new_duration = target_end - last_note_in_rit->start_tick;
          if (new_duration > last_note_in_rit->duration) {
            last_note_in_rit->duration = new_duration;
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

      // Check for dissonant intervals: minor 2nd (1), major 2nd (2), major 7th (11), minor 9th (13)
      // Also check compound intervals within 3 octaves
      int pc_interval = actual_semitones % 12;
      bool is_dissonant = (pc_interval == 1) ||                     // minor 2nd
                          (actual_semitones == 2) ||                // major 2nd (close range only)
                          (pc_interval == 11 && actual_semitones < 36);  // major 7th

      if (is_dissonant && actual_semitones < 36) {
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
                                           const Section& section) {
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
        note.velocity = std::max(note.velocity, FINAL_HIT_VEL);
        has_final_bass = true;
      }
    }

    // If no bass note exists on final beat, add one (root note at bass range)
    if (!has_final_bass) {
      NoteEvent final_bass;
      final_bass.start_tick = final_beat_start;
      final_bass.duration = TICKS_PER_BEAT;
      final_bass.note = 36;  // C2 - typical bass root
      final_bass.velocity = FINAL_HIT_VEL;
      // Provenance tracking
      final_bass.prov_chord_degree = -1;
      final_bass.prov_lookup_tick = final_beat_start;
      final_bass.prov_source = static_cast<uint8_t>(NoteSource::PostProcess);
      final_bass.prov_original_pitch = 36;
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
      // Provenance tracking
      kick.prov_chord_degree = -1;
      kick.prov_lookup_tick = final_beat_start;
      kick.prov_source = static_cast<uint8_t>(NoteSource::PostProcess);
      kick.prov_original_pitch = BD_NOTE;
      drum_notes.push_back(kick);
    }

    // Add crash if missing
    if (!has_final_crash) {
      NoteEvent crash;
      crash.start_tick = final_beat_start;
      crash.duration = TICKS_PER_BEAT;
      crash.note = CRASH_NOTE;
      crash.velocity = FINAL_HIT_VEL;
      // Provenance tracking
      crash.prov_chord_degree = -1;
      crash.prov_lookup_tick = final_beat_start;
      crash.prov_source = static_cast<uint8_t>(NoteSource::PostProcess);
      crash.prov_original_pitch = CRASH_NOTE;
      drum_notes.push_back(crash);
    }
  }

  // Chord track: sustain final chord as whole note with strong velocity
  // Check against vocal track to avoid creating dissonance from extension
  if (chord_track != nullptr) {
    auto& chord_notes = chord_track->notes();

    for (auto& note : chord_notes) {
      if (note.start_tick >= final_beat_start && note.start_tick < section_end) {
        // Extend duration, but check for vocal clashes first
        Tick safe_end = getMaxSafeEndTick(note, section_end, vocal_track);
        if (safe_end > note.start_tick) {
          note.duration = safe_end - note.start_tick;
        }
        note.velocity = std::max(note.velocity, FINAL_HIT_VEL);
      }
    }

    // Also extend chord notes from the last bar that could sustain through
    Tick last_bar_start = section_end - TICKS_PER_BAR;
    for (auto& note : chord_notes) {
      if (note.start_tick >= last_bar_start && note.start_tick < final_beat_start) {
        // Extend to section end, but check for vocal clashes first
        Tick safe_end = getMaxSafeEndTick(note, section_end, vocal_track);
        if (safe_end > note.start_tick + note.duration) {
          note.duration = safe_end - note.start_tick;
        }
      }
    }
  }
}

}  // namespace midisketch
