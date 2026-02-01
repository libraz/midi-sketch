/**
 * @file post_processor.cpp
 * @brief Implementation of track post-processing.
 */

#include "core/post_processor.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "core/chord_utils.h"         // for nearestChordTonePitch, ChordToneHelper, ChordTones
#include "core/i_harmony_context.h"   // for IHarmonyContext
#include "core/note_creator.h"        // for getSafePitchCandidates
#include "core/note_source.h"         // for NoteSource enum
#include "core/pitch_utils.h"         // for MOTIF_LOW, MOTIF_HIGH
#include "core/note_timeline_utils.h" // for NoteTimeline utilities
#include "core/timing_constants.h"    // for TICK_EIGHTH
#include "core/velocity.h"            // for DriveMapping

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

  // Use NoteTimeline utilities for overlap fixing
  NoteTimeline::sortByStartTick(vocal_notes);
  NoteTimeline::fixOverlaps(vocal_notes);
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

namespace {

/// @brief Get phrase position for a given tick within sections.
/// @param tick Note start tick
/// @param sections Song sections for phrase boundary detection
/// @return Phrase position (Start, Middle, or End)
PhrasePosition getPhrasePosition(Tick tick, const std::vector<Section>& sections) {
  constexpr int PHRASE_BARS = 4;

  // Find the section containing this tick
  for (const auto& section : sections) {
    Tick section_end = section.start_tick + section.bars * TICKS_PER_BAR;
    if (tick >= section.start_tick && tick < section_end) {
      // Calculate position within 4-bar phrase
      Tick relative = tick - section.start_tick;
      int bar_in_section = static_cast<int>(relative / TICKS_PER_BAR);
      int bar_in_phrase = bar_in_section % PHRASE_BARS;

      if (bar_in_phrase == 0) {
        return PhrasePosition::Start;  // First bar of phrase
      } else if (bar_in_phrase >= PHRASE_BARS - 1) {
        return PhrasePosition::End;    // Last bar of phrase
      } else {
        return PhrasePosition::Middle; // Middle bars
      }
    }
  }

  return PhrasePosition::Middle;  // Default fallback
}

/// @brief Calculate tessitura center from vocal notes.
/// @param notes Vector of vocal note events
/// @return Center pitch of the tessitura (average of min and max)
uint8_t calculateTessituraCenter(const std::vector<NoteEvent>& notes) {
  if (notes.empty()) {
    return 67;  // Default: G4 (typical vocal center)
  }

  uint8_t min_pitch = 127;
  uint8_t max_pitch = 0;
  for (const auto& note : notes) {
    if (note.note < min_pitch) min_pitch = note.note;
    if (note.note > max_pitch) max_pitch = note.note;
  }

  return static_cast<uint8_t>((min_pitch + max_pitch) / 2);
}

/// @brief Get vocal timing offset based on phrase position.
/// @param pos Phrase position
/// @param timing_mult Timing multiplier from drive_feel
/// @return Timing offset in ticks (positive = push ahead)
int getVocalTimingOffset(PhrasePosition pos, float timing_mult) {
  // Base offsets scaled by drive_feel:
  // - Low drive (0): offsets reduced by 0.5x for laid-back feel
  // - Neutral (50): 1.0x (default behavior)
  // - High drive (100): offsets increased by 1.5x for driving feel
  int base_start = 8;
  int base_middle = 4;
  int base_end = 0;

  switch (pos) {
    case PhrasePosition::Start:
      return static_cast<int>(base_start * timing_mult);
    case PhrasePosition::Middle:
      return static_cast<int>(base_middle * timing_mult);
    case PhrasePosition::End:
      return base_end;  // Phrase end always at 0 regardless of drive
  }
  return static_cast<int>(base_middle * timing_mult);  // Default
}

}  // namespace

void PostProcessor::applyMicroTimingOffsets(MidiTrack& vocal, MidiTrack& bass,
                                             MidiTrack& drum_track,
                                             const std::vector<Section>* sections,
                                             uint8_t drive_feel,
                                             VocalStylePreset vocal_style) {
  // Get vocal physics parameters for the style
  VocalPhysicsParams physics = getVocalPhysicsParams(vocal_style);
  // Per-instrument timing offsets create the "pocket" feel.
  // Positive = push (ahead of beat), negative = lay back (behind beat).
  // Values in ticks (at 480 ticks/beat, 8 ticks ≈ 4ms at 120 BPM).
  //
  // drive_feel scales all timing offsets:
  // - Low drive (0): 0.5x offsets for laid-back feel
  // - Neutral (50): 1.0x (default behavior)
  // - High drive (100): 1.5x offsets for driving feel
  float timing_mult = DriveMapping::getTimingMultiplier(drive_feel);

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

  // GM drum note numbers
  constexpr uint8_t BD_NOTE = 36;
  constexpr uint8_t SD_NOTE = 38;
  constexpr uint8_t HHC_NOTE = 42;
  constexpr uint8_t HHO_NOTE = 46;
  constexpr uint8_t HHF_NOTE = 44;
  constexpr int BASS_BASE_OFFSET = -4; // Bass lays back slightly

  // Beat-position-aware drum timing for enhanced "pocket" feel
  // Creates a more nuanced groove by varying timing based on beat position:
  // - Kick: -5~+3, tighter on downbeats, slightly ahead on offbeats
  // - Snare: -8~0, maximum layback on beat 4 for anticipation
  // - Hi-hat: +8~+15, stronger push on offbeats for drive
  auto getDrumTimingOffset = [](uint8_t note_number, Tick tick, float timing_mult) -> int {
    Tick pos_in_bar = tick % TICKS_PER_BAR;
    int beat_in_bar = static_cast<int>(pos_in_bar / TICKS_PER_BEAT);
    bool is_offbeat = (pos_in_bar % TICKS_PER_BEAT) >= (TICKS_PER_BEAT / 2);

    int base_offset = 0;
    if (note_number == BD_NOTE) {
      // Kick: tight on downbeats (beats 0,2), slightly ahead on others
      base_offset = (beat_in_bar == 0 || beat_in_bar == 2) ? -1 : -3;
      if (is_offbeat) base_offset += 2;  // Push offbeat kicks slightly forward
    } else if (note_number == SD_NOTE) {
      // Snare: maximum layback on beat 4 for tension before downbeat
      // Moderate layback on beat 2, less on offbeats
      if (beat_in_bar == 3) {
        base_offset = -8;  // Maximum layback on beat 4
      } else if (beat_in_bar == 1) {
        base_offset = -6;  // Backbeat layback
      } else {
        base_offset = -4;  // Standard layback
      }
      if (is_offbeat) base_offset = -3;  // Less layback on offbeat fills
    } else if (note_number == HHC_NOTE || note_number == HHO_NOTE || note_number == HHF_NOTE) {
      // Hi-hat: push ahead for driving feel, stronger on backbeats
      if (is_offbeat) {
        // Stronger push on offbeats (beat 2 and 4 offbeats)
        base_offset = (beat_in_bar == 1 || beat_in_bar == 3) ? 15 : 12;
      } else {
        base_offset = 8;  // Standard push on downbeats
      }
    }
    return static_cast<int>(base_offset * timing_mult);
  };

  int bass_offset = static_cast<int>(BASS_BASE_OFFSET * timing_mult);

  // Apply beat-position-aware offsets to drum track
  auto& drum_notes = drum_track.notes();
  for (auto& note : drum_notes) {
    int offset = getDrumTimingOffset(note.note, note.start_tick, timing_mult);

    if (offset != 0) {
      int new_tick = static_cast<int>(note.start_tick) + offset;
      if (new_tick > 0) {
        note.start_tick = static_cast<Tick>(new_tick);
      }
    }
  }

  // Bass: always lays back slightly (scaled by drive_feel)
  applyOffset(bass, bass_offset);

  // Vocal: phrase-position-aware timing with human body model
  // When sections are provided, vary timing based on position within 4-bar phrases:
  // - Start of phrase: push ahead (+8) for energy/drive
  // - Middle of phrase: neutral (+4, original behavior)
  // - End of phrase: lay back (0) for breath/relaxation
  // All offsets are scaled by drive_feel
  //
  // Human body timing model adds context-dependent delays:
  // - High pitch delay: notes above tessitura center need preparation
  // - Leap landing delay: large intervals require stabilization time
  // - Post-breath delay: notes after breath gaps start slightly late
  if (sections != nullptr && !sections->empty() && !vocal.empty()) {
    auto& vocal_notes = vocal.notes();

    // Calculate tessitura center for high pitch delay calculation
    uint8_t tessitura_center = calculateTessituraCenter(vocal_notes);

    // Two-pass approach: first calculate all offsets using ORIGINAL positions,
    // then apply them. This ensures breath gap detection uses unmodified timing.
    std::vector<int> offsets(vocal_notes.size(), 0);

    // Pass 1: Calculate all offsets based on original note positions
    for (size_t idx = 0; idx < vocal_notes.size(); ++idx) {
      const auto& note = vocal_notes[idx];

      // Base phrase position timing
      PhrasePosition pos = getPhrasePosition(note.start_tick, *sections);
      int offset = getVocalTimingOffset(pos, timing_mult);

      // Human body timing model: context-dependent delays
      // All delays are scaled by physics.timing_scale (0=mechanical, 1=human)
      // High pitch delay: high notes need more preparation
      int high_pitch_delay = static_cast<int>(
          DriveMapping::getHighPitchDelay(note.note, tessitura_center) * physics.timing_scale);
      offset += high_pitch_delay;

      // Leap landing delay: large intervals require stabilization
      if (idx > 0) {
        int interval = std::abs(static_cast<int>(note.note) -
                               static_cast<int>(vocal_notes[idx - 1].note));
        int leap_delay = static_cast<int>(
            DriveMapping::getLeapLandingDelay(interval) * physics.timing_scale);
        offset += leap_delay;
      }

      // Post-breath delay: notes after breath gaps start slightly late
      // A breath gap is when there's more than an eighth note of rest
      // Only applies if vocal style requires breath (physics.requires_breath)
      bool is_post_breath = false;
      if (physics.requires_breath) {
        if (idx == 0) {
          is_post_breath = true;
        } else {
          // Calculate gap using ORIGINAL positions (notes haven't been modified yet)
          Tick prev_end = vocal_notes[idx - 1].start_tick + vocal_notes[idx - 1].duration;
          // Use signed arithmetic to handle edge cases
          int64_t gap = static_cast<int64_t>(note.start_tick) - static_cast<int64_t>(prev_end);
          // A positive gap larger than TICK_EIGHTH indicates a breath
          is_post_breath = (gap > static_cast<int64_t>(TICK_EIGHTH));
        }
      }
      int breath_delay = static_cast<int>(
          DriveMapping::getPostBreathDelay(is_post_breath) * physics.timing_scale);
      offset += breath_delay;

      offsets[idx] = offset;
    }

    // Pass 2: Apply all offsets
    for (size_t idx = 0; idx < vocal_notes.size(); ++idx) {
      if (offsets[idx] != 0) {
        int new_tick = static_cast<int>(vocal_notes[idx].start_tick) + offsets[idx];
        if (new_tick > 0) {
          vocal_notes[idx].start_tick = static_cast<Tick>(new_tick);
        }
      }
    }
  } else {
    // Fallback: apply uniform offset (original behavior, scaled by drive_feel)
    int vocal_offset = static_cast<int>(4 * timing_mult);
    applyOffset(vocal, vocal_offset);
  }
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
    Tick section_end_tick = section.start_tick + section.bars * TICKS_PER_BAR;
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
      int pc_interval = actual_semitones % 12;

      // Dissonant intervals: minor 2nd (1), major 2nd (2 in close range), major 7th (11)
      bool is_dissonant = (pc_interval == 1) ||
                          (actual_semitones == 2) ||
                          (pc_interval == 11 && actual_semitones < 36);

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
          Tick desired_duration = static_cast<Tick>(note.duration * duration_mult);
          Tick desired_end = note.start_tick + desired_duration;

          // Check for dissonance with other tracks and limit extension
          Tick safe_end = getSafeEndForRitardando(note, desired_end, all_tracks_for_collision, track);
          note.duration = safe_end - note.start_tick;

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
      // Also check for dissonance before extending
      if (last_note_in_rit != nullptr) {
        Tick target_end = section_end_tick - TICKS_PER_BEAT / 8;  // Small release gap
        if (last_note_in_rit->start_tick < target_end) {
          Tick safe_end = getSafeEndForRitardando(*last_note_in_rit, target_end,
                                                   all_tracks_for_collision, track);
          if (safe_end > last_note_in_rit->start_tick + last_note_in_rit->duration) {
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
                                           const Section& section,
                                           const IHarmonyContext* harmony) {
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
#ifdef MIDISKETCH_NOTE_PROVENANCE
      final_bass.prov_chord_degree = -1;
      final_bass.prov_lookup_tick = final_beat_start;
      final_bass.prov_source = static_cast<uint8_t>(NoteSource::PostProcess);
      final_bass.prov_original_pitch = 36;
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
// Checks BOTH the vocal track directly AND harmony.isPitchSafe() for comprehensive checking.
// Tries different octaves and different chord tones.
uint8_t findSafeChordTone(uint8_t original_pitch, int8_t degree, Tick start, Tick duration,
                          const MidiTrack& vocal, const IHarmonyContext& harmony) {
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
      if (!harmony.isPitchSafe(clamped, start, duration, TrackRole::Motif)) continue;

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
                                          const IHarmonyContext& harmony) {
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
        int interval_class = interval % 12;

        // Dissonant intervals:
        // - Minor 2nd (1): always dissonant
        // - Major 2nd (2): dissonant in close voicing (actual interval < 12)
        // - Tritone (6): dissonant between harmonic tracks
        // - Major 7th (11): always dissonant
        // - Minor 9th (13 -> 1 in interval_class): handled by minor 2nd
        bool is_dissonant = (interval_class == 1) ||            // minor 2nd / minor 9th
                            (interval_class == 6) ||            // tritone
                            (interval_class == 11) ||           // major 7th
                            (interval_class == 2 && interval < 12);  // major 2nd (close only)

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
              hints.prefer_chord_tones = true;
              hints.prefer_small_intervals = true;
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

  auto isDissonant = [include_close_major_2nd](int interval) {
    int interval_class = interval % 12;
    if (interval_class == 1 || interval_class == 11) return true;  // m2, M7
    if (include_close_major_2nd && interval_class == 2 && interval < 12) return true;
    return false;
  };

  std::vector<size_t> notes_to_remove;
  for (size_t idx = 0; idx < notes.size(); ++idx) {
    const auto& note = notes[idx];
    Tick note_end = note.start_tick + note.duration;

    for (const auto& v_note : vocal_notes) {
      Tick v_end = v_note.start_tick + v_note.duration;
      if (note.start_tick < v_end && note_end > v_note.start_tick) {
        int interval = std::abs(static_cast<int>(note.note) - static_cast<int>(v_note.note));
        if (isDissonant(interval)) {
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

void PostProcessor::fixChordVocalClashes(MidiTrack& chord, const MidiTrack& vocal) {
  removeVocalClashingNotes(chord, vocal, /*include_close_major_2nd=*/true);
}

void PostProcessor::fixAuxVocalClashes(MidiTrack& aux, const MidiTrack& vocal) {
  removeVocalClashingNotes(aux, vocal, /*include_close_major_2nd=*/true);
}

void PostProcessor::fixBassVocalClashes(MidiTrack& bass, const MidiTrack& vocal) {
  removeVocalClashingNotes(bass, vocal, /*include_close_major_2nd=*/false);
}

}  // namespace midisketch
