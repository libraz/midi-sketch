/**
 * @file post_processor_humanization.cpp
 * @brief PostProcessor humanization and timing offset methods.
 *
 * Contains: isStrongBeat, applyHumanization, fixVocalOverlaps,
 * getSectionTypeAtTick, applySectionAwareVelocityHumanization,
 * applyMicroTimingOffsets.
 */

#include "core/post_processor.h"

#include <algorithm>

#include "core/note_timeline_utils.h"
#include "core/timing_constants.h"
#include "core/timing_offset_calculator.h"
#include "core/velocity_helper.h"

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

}  // namespace midisketch
