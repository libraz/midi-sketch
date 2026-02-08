/**
 * @file timing_offset_calculator.cpp
 * @brief Implementation of timing offset calculations for micro-timing.
 */

#include "core/timing_offset_calculator.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "core/midi_track.h"
#include "core/post_processor.h"
#include "core/timing_constants.h"
#include "core/velocity.h"

namespace midisketch {

namespace {

// Phrase length in bars for position detection
constexpr int kPhraseBars = 4;

// Drum timing profiles indexed by DrumStyle.
// Standard (Pop) profile uses the original hardcoded values.
constexpr DrumTimingProfile kDrumTimingProfiles[] = {
    // Sparse (Ballad): subtle offsets for a relaxed, airy feel
    {-1, -2, 1, -3, -5, -2, -2, 3, 5, 6},
    // Standard (Pop): original hardcoded values - natural pocket groove
    {-1, -3, 2, -6, -8, -4, -3, 8, 12, 15},
    // FourOnFloor (Dance/EDM): tight kick for metronomic pulse
    {0, -1, 1, -5, -6, -3, -2, 8, 12, 15},
    // Upbeat (Idol/Energetic): driving hi-hat push, snappy snare
    {-1, -2, 3, -5, -7, -3, -2, 10, 14, 18},
    // Rock: tighter than pop, less hi-hat push for heavier feel
    {-2, -4, 2, -4, -6, -3, -2, 5, 8, 10},
    // Synth: precision timing, near-zero kick, wide hi-hat push
    {0, 0, 1, -2, -4, -1, -1, 10, 15, 20},
    // Trap: laid-back snare, moderate hi-hat, tight kick
    {0, -1, 2, -6, -10, -4, -2, 5, 8, 10},
    // Latin: syncopated feel with moderate offsets
    {-1, -2, 3, -5, -7, -3, -2, 7, 11, 14},
};

// Verify profile count matches DrumStyle enum count at compile time.
static_assert(sizeof(kDrumTimingProfiles) / sizeof(kDrumTimingProfiles[0]) == 8,
              "DrumTimingProfile count must match DrumStyle enum count");

}  // namespace

const DrumTimingProfile& getDrumTimingProfile(DrumStyle style) {
  auto idx = static_cast<uint8_t>(style);
  if (idx >= sizeof(kDrumTimingProfiles) / sizeof(kDrumTimingProfiles[0])) {
    // Fallback to Standard if out of range
    return kDrumTimingProfiles[static_cast<uint8_t>(DrumStyle::Standard)];
  }
  return kDrumTimingProfiles[idx];
}

TimingOffsetCalculator::TimingOffsetCalculator(uint8_t drive_feel, VocalStylePreset vocal_style,
                                               DrumStyle drum_style, float humanize_timing,
                                               GenerationParadigm paradigm)
    : timing_mult_(DriveMapping::getTimingMultiplier(drive_feel)),
      humanize_timing_(std::clamp(humanize_timing, 0.0f, 1.0f)),
      physics_(getVocalPhysicsParams(vocal_style)),
      profile_(getDrumTimingProfile(drum_style)),
      paradigm_(paradigm) {}

// ============================================================================
// Drum Timing
// ============================================================================

int TimingOffsetCalculator::getDrumTimingOffset(uint8_t note_number, Tick tick) const {
  Tick pos_in_bar = positionInBar(tick);
  int beat_in_bar = static_cast<int>(beatInBar(tick));
  bool is_offbeat = (pos_in_bar % TICKS_PER_BEAT) >= (TICKS_PER_BEAT / 2);

  int base_offset = 0;

  if (note_number == kBassNote) {
    // Kick: tight on downbeats (beats 0,2), slightly ahead on others
    base_offset = (beat_in_bar == 0 || beat_in_bar == 2) ? profile_.kick_downbeat
                                                          : profile_.kick_other;
    if (is_offbeat) base_offset += profile_.kick_offbeat_push;
  } else if (note_number == kSnareNote) {
    // Snare: maximum layback on beat 4 for tension before downbeat
    // Moderate layback on beat 2, less on offbeats
    if (beat_in_bar == 3) {
      base_offset = profile_.snare_beat4;
    } else if (beat_in_bar == 1) {
      base_offset = profile_.snare_backbeat;
    } else {
      base_offset = profile_.snare_standard;
    }
    if (is_offbeat) base_offset = profile_.snare_offbeat;
  } else if (note_number == kHiHatClosed || note_number == kHiHatOpen ||
             note_number == kHiHatFoot) {
    // Hi-hat: push ahead for driving feel, stronger on backbeats
    if (is_offbeat) {
      // Stronger push on offbeats (beat 2 and 4 offbeats)
      base_offset = (beat_in_bar == 1 || beat_in_bar == 3) ? profile_.hh_backbeat_off
                                                            : profile_.hh_offbeat;
    } else {
      base_offset = profile_.hh_downbeat;
    }
  }

  return static_cast<int>(base_offset * timing_mult_ * humanize_timing_);
}

void TimingOffsetCalculator::applyDrumOffsets(MidiTrack& drum_track) const {
  auto& notes = drum_track.notes();
  for (auto& note : notes) {
    int offset = getDrumTimingOffset(note.note, note.start_tick);
    if (offset != 0) {
      int new_tick = static_cast<int>(note.start_tick) + offset;
      if (new_tick > 0) {
        note.start_tick = static_cast<Tick>(new_tick);
      }
    }
  }
}

// ============================================================================
// Bass Timing
// ============================================================================

int TimingOffsetCalculator::getBassTimingOffset() const {
  return static_cast<int>(kBassBaseOffset * timing_mult_ * humanize_timing_);
}

void TimingOffsetCalculator::applyBassOffset(MidiTrack& bass_track) const {
  int offset = getBassTimingOffset();
  applyUniformOffset(bass_track, offset);
}

// ============================================================================
// Vocal Timing
// ============================================================================

int TimingOffsetCalculator::getRhythmSyncBeatOffset(Tick tick) const {
  // Beat-strength-aware micro-timing for RhythmSync paradigm.
  // Stronger beats anchor tighter, weaker beats add groove feel.
  // Values are max shifts at humanize_timing=1.0; actual scaling applied in caller.
  // Negative bias (-60%/+40%) for Orangestar's forward-leaning feel.
  Tick pos_in_bar = positionInBar(tick);
  Tick beat_pos = pos_in_bar % TICKS_PER_BEAT;
  int beat_idx = static_cast<int>(beatInBar(tick));

  int max_shift = 0;
  if (beat_pos == 0) {
    // On-beat positions
    if (beat_idx == 0 || beat_idx == 2) {
      max_shift = 8;    // Strong beats: tight anchor
    } else {
      max_shift = 15;   // Weak beats: moderate groove
    }
  } else if (beat_pos == TICKS_PER_BEAT / 2) {
    // Offbeat (8th note) positions
    max_shift = 20;     // Maximum groove feel
  } else if (beat_pos == TICKS_PER_BEAT / 4 || beat_pos == 3 * TICKS_PER_BEAT / 4) {
    // 16th note positions
    max_shift = 10;     // Tight for clarity
  } else {
    max_shift = 12;     // Other positions: moderate
  }

  // Apply forward-lean bias: -60% / +40% (negative = ahead of grid)
  // Use a deterministic offset based on tick position for consistency
  // Strong beats lean slightly forward, offbeats lean more
  int biased_offset = -static_cast<int>(max_shift * 0.6f * timing_mult_);

  return biased_offset;
}

int TimingOffsetCalculator::getVocalTimingOffset(const NoteEvent& note, size_t note_idx,
                                                  const std::vector<NoteEvent>& vocal_notes,
                                                  const std::vector<Section>& sections,
                                                  uint8_t tessitura_center) const {
  // Base phrase position timing
  PhrasePosition pos = getPhrasePosition(note.start_tick, sections);
  int offset = getBaseVocalTimingOffset(pos, timing_mult_);

  // Human body timing model: context-dependent delays
  // All delays are scaled by physics.timing_scale (0=mechanical, 1=human)

  // High pitch delay: high notes need more preparation
  int high_pitch_delay = static_cast<int>(
      DriveMapping::getHighPitchDelay(note.note, tessitura_center) * physics_.timing_scale);
  offset += high_pitch_delay;

  // Leap landing delay: large intervals require stabilization
  if (note_idx > 0) {
    int interval = std::abs(static_cast<int>(note.note) -
                            static_cast<int>(vocal_notes[note_idx - 1].note));
    int leap_delay =
        static_cast<int>(DriveMapping::getLeapLandingDelay(interval) * physics_.timing_scale);
    offset += leap_delay;
  }

  // Post-breath delay: notes after breath gaps start slightly late
  if (isPostBreath(note_idx, vocal_notes)) {
    int breath_delay =
        static_cast<int>(DriveMapping::getPostBreathDelay(true) * physics_.timing_scale);
    offset += breath_delay;
  }

  // RhythmSync: vocal is rhythm-locked to motif coordinate axis.
  // Any offset breaks the lock, so skip beat-strength offset entirely.
  if (paradigm_ == GenerationParadigm::RhythmSync) {
    return 0;
  }

  // Scale all timing offsets by humanize_timing, then cap to sub-threshold range.
  // Vocal timing precision is paramount; groove comes from drums/bass layback,
  // not melody drift. ±2 ticks ≈ 1ms at 160BPM, below auditory perception threshold.
  constexpr int kMaxVocalOffset = 2;
  int raw_offset = static_cast<int>(offset * humanize_timing_);
  return std::clamp(raw_offset, -kMaxVocalOffset, kMaxVocalOffset);
}

void TimingOffsetCalculator::applyVocalOffsets(MidiTrack& vocal_track,
                                                const std::vector<Section>& sections) const {
  if (vocal_track.empty() || sections.empty()) {
    // Fallback: apply uniform offset (also scaled by humanize_timing)
    int vocal_offset = static_cast<int>(4 * timing_mult_ * humanize_timing_);
    applyUniformOffset(vocal_track, vocal_offset);
    return;
  }

  auto& vocal_notes = vocal_track.notes();
  uint8_t tessitura_center = calculateTessituraCenter(vocal_notes);

  // Two-pass approach: first calculate all offsets using ORIGINAL positions,
  // then apply them. This ensures breath gap detection uses unmodified timing.
  std::vector<int> offsets(vocal_notes.size(), 0);

  // Pass 1: Calculate all offsets
  for (size_t idx = 0; idx < vocal_notes.size(); ++idx) {
    offsets[idx] =
        getVocalTimingOffset(vocal_notes[idx], idx, vocal_notes, sections, tessitura_center);
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
}

// ============================================================================
// Utility
// ============================================================================

void TimingOffsetCalculator::applyUniformOffset(MidiTrack& track, int offset) {
  if (offset == 0 || track.empty()) return;
  auto& notes = track.notes();
  for (auto& note : notes) {
    int new_tick = static_cast<int>(note.start_tick) + offset;
    if (new_tick > 0) {
      note.start_tick = static_cast<Tick>(new_tick);
    }
  }
}

PhrasePosition TimingOffsetCalculator::getPhrasePosition(Tick tick,
                                                          const std::vector<Section>& sections) {
  for (const auto& section : sections) {
    Tick section_end = section.endTick();
    if (tick >= section.start_tick && tick < section_end) {
      Tick relative = tick - section.start_tick;
      int bar_in_section = static_cast<int>(tickToBar(relative));
      int bar_in_phrase = bar_in_section % kPhraseBars;

      if (bar_in_phrase == 0) {
        return PhrasePosition::Start;
      } else if (bar_in_phrase >= kPhraseBars - 1) {
        return PhrasePosition::End;
      } else {
        return PhrasePosition::Middle;
      }
    }
  }
  return PhrasePosition::Middle;
}

int TimingOffsetCalculator::getBaseVocalTimingOffset(PhrasePosition pos, float timing_mult) {
  // Base offsets scaled by drive_feel
  constexpr int kBaseStart = 8;
  constexpr int kBaseMiddle = 4;
  constexpr int kBaseEnd = 0;

  switch (pos) {
    case PhrasePosition::Start:
      return static_cast<int>(kBaseStart * timing_mult);
    case PhrasePosition::Middle:
      return static_cast<int>(kBaseMiddle * timing_mult);
    case PhrasePosition::End:
      return kBaseEnd;  // Always 0 at phrase end
  }
  return static_cast<int>(kBaseMiddle * timing_mult);
}

uint8_t TimingOffsetCalculator::calculateTessituraCenter(const std::vector<NoteEvent>& notes) {
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

bool TimingOffsetCalculator::isPostBreath(size_t note_idx,
                                           const std::vector<NoteEvent>& vocal_notes) const {
  // Only applies if vocal style requires breath
  if (!physics_.requires_breath) {
    return false;
  }

  if (note_idx == 0) {
    return true;  // First note is always post-breath
  }

  // Calculate gap using ORIGINAL positions
  Tick prev_end = vocal_notes[note_idx - 1].start_tick + vocal_notes[note_idx - 1].duration;
  int64_t gap =
      static_cast<int64_t>(vocal_notes[note_idx].start_tick) - static_cast<int64_t>(prev_end);

  // A positive gap larger than TICK_EIGHTH indicates a breath
  return gap > static_cast<int64_t>(TICK_EIGHTH);
}

}  // namespace midisketch
