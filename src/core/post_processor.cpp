/**
 * @file post_processor.cpp
 * @brief Implementation of track post-processing.
 */

#include "core/post_processor.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <unordered_map>
#include <vector>

#include "core/chord_utils.h"             // for nearestChordTonePitch, ChordToneHelper, ChordTones
#include "core/i_chord_lookup.h"          // for IChordLookup
#include "core/i_collision_detector.h"    // for ICollisionDetector
#include "core/i_harmony_context.h"       // for IHarmonyContext
#include "core/note_creator.h"            // for getSafePitchCandidates
#include "core/note_source.h"             // for NoteSource enum
#include "core/pitch_utils.h"             // for MOTIF_LOW, MOTIF_HIGH
#include "core/note_timeline_utils.h"     // for NoteTimeline utilities
#include "core/timing_constants.h"        // for TICK_EIGHTH
#include "core/timing_offset_calculator.h"  // for TimingOffsetCalculator
#include "core/velocity.h"                // for DriveMapping
#include "core/velocity_helper.h"         // for vel::clamp

namespace midisketch {

bool PostProcessor::isStrongBeat(Tick tick) {
  Tick position_in_bar = positionInBar(tick);
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
      note.velocity = vel::clamp(new_velocity, min_velocity, 127);
    }
  }
}

void PostProcessor::fixVocalOverlaps(MidiTrack& vocal_track) {
  auto& vocal_notes = vocal_track.notes();
  if (vocal_notes.size() <= 1) {
    return;
  }

  // Use NoteTimeline utilities for overlap fixing
  NoteTimeline::sortByStartTick(vocal_notes);
  NoteTimeline::fixOverlaps(vocal_notes);
}

SectionType PostProcessor::getSectionTypeAtTick(Tick tick, const std::vector<Section>& sections) {
  for (const auto& section : sections) {
    Tick section_end = section.endTick();
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
      note.velocity = vel::clamp(new_vel, min_velocity, 127);
    }
  }
}

// Helper functions for micro-timing have been extracted to TimingOffsetCalculator.
// See src/core/timing_offset_calculator.h for the refactored implementation.

void PostProcessor::applyMicroTimingOffsets(MidiTrack& vocal, MidiTrack& bass,
                                             MidiTrack& drum_track,
                                             const std::vector<Section>* sections,
                                             uint8_t drive_feel,
                                             VocalStylePreset vocal_style,
                                             DrumStyle drum_style,
                                             float humanize_timing,
                                             GenerationParadigm paradigm) {
  // Use TimingOffsetCalculator for clearer code structure and traceability.
  // The calculator encapsulates all timing logic previously in inline lambdas.
  // humanize_timing scales all timing offsets (0.0 = no timing variation, 1.0 = full variation)
  TimingOffsetCalculator calculator(drive_feel, vocal_style, drum_style, humanize_timing, paradigm);

  // Apply drum timing (beat-position-aware offsets)
  calculator.applyDrumOffsets(drum_track);

  // Apply bass timing (consistent layback)
  calculator.applyBassOffset(bass);

  // Apply vocal timing (phrase-position-aware with human body model)
  if (sections != nullptr && !sections->empty()) {
    calculator.applyVocalOffsets(vocal, *sections);
  } else {
    // Fallback: uniform offset
    int vocal_offset = static_cast<int>(4 * calculator.getTimingMultiplier());
    TimingOffsetCalculator::applyUniformOffset(vocal, vocal_offset);
  }
}

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
    Tick section_end_tick = section.endTick();
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
#ifdef MIDISKETCH_NOTE_PROVENANCE
      last_note->addTransformStep(TransformStepType::PostProcessDuration, 0, 0, 1, 0);
#endif
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
          note.duration = safe_end - note.start_tick;
        }
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
          note.duration = safe_end - note.start_tick;
        }
      }
    }
  }
}

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

namespace {

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

  for (auto it = notes_to_remove.rbegin(); it != notes_to_remove.rend(); ++it) {
    notes.erase(notes.begin() + static_cast<std::ptrdiff_t>(*it));
  }
}

}  // namespace

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

  for (auto it = notes_to_remove.rbegin(); it != notes_to_remove.rend(); ++it) {
    notes.erase(notes.begin() + static_cast<std::ptrdiff_t>(*it));
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
    auto it = min_dur.find(n.start_tick);
    if (it == min_dur.end()) {
      min_dur[n.start_tick] = n.duration;
      count[n.start_tick] = 1;
    } else {
      if (n.duration < it->second) {
        it->second = n.duration;
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
