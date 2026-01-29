/**
 * @file melody_utils.cpp
 * @brief Implementation of melody utility functions.
 */

#include "track/melody/melody_utils.h"

#include <algorithm>
#include <cmath>

#include "core/chord_utils.h"
#include "core/pitch_utils.h"
#include "core/velocity.h"

namespace midisketch {
namespace melody {

float getMotifWeightForSection(SectionType section, int section_occurrence) {
  switch (section) {
    case SectionType::Chorus:
    case SectionType::Drop:
      return 0.35f;
    case SectionType::B:
    case SectionType::MixBreak:
      return 0.22f;
    case SectionType::A:
      return (section_occurrence == 1) ? 0.15f : 0.25f;
    case SectionType::Bridge:
      return 0.05f;
    case SectionType::Interlude:
      return 0.18f;
    case SectionType::Intro:
      return 0.08f;
    case SectionType::Outro:
      return 0.20f;
    case SectionType::Chant:
      return 0.05f;
  }
  return 0.12f;
}

int getEffectiveMaxInterval(SectionType section_type, uint8_t ctx_max_leap) {
  int section_max = getMaxMelodicIntervalForSection(section_type);
  return std::min(section_max, static_cast<int>(ctx_max_leap));
}

Tick getBaseBreathDuration(SectionType section, Mood mood) {
  if (mood == Mood::Ballad || mood == Mood::Sentimental) {
    return TICK_QUARTER;
  }
  if (section == SectionType::Chorus) {
    return TICK_SIXTEENTH;
  }
  return TICK_EIGHTH;
}

Tick getBreathDuration(SectionType section, Mood mood, float phrase_density,
                       uint8_t phrase_high_pitch, const BreathContext* ctx,
                       VocalStylePreset vocal_style) {
  VocalPhysicsParams physics = getVocalPhysicsParams(vocal_style);

  if (!physics.requires_breath) {
    return TICK_SIXTEENTH / 2;
  }

  Tick base = getBaseBreathDuration(section, mood);
  float mult = 1.0f;

  if (phrase_density > 1.0f) {
    mult *= 1.3f;
  } else if (phrase_density > 0.7f) {
    mult *= 1.15f;
  }

  if (phrase_high_pitch >= 72) {
    mult *= 1.2f;
  }

  if (ctx != nullptr) {
    if (ctx->phrase_load > 0.7f) {
      mult *= 1.2f;
    }
    if (ctx->next_section == SectionType::Chorus && ctx->is_section_boundary) {
      mult *= 1.25f;
    }
    if (ctx->prev_phrase_high >= 76) {
      mult *= 1.15f;
    }
  }

  mult *= physics.breath_scale;

  Tick result = static_cast<Tick>(base * mult);
  return std::min(result, TICK_HALF);
}

Tick getRhythmUnit(RhythmGrid grid, bool is_eighth) {
  switch (grid) {
    case RhythmGrid::Ternary:
      return is_eighth ? TICK_EIGHTH_TRIPLET : TICK_QUARTER_TRIPLET;
    case RhythmGrid::Hybrid:
    case RhythmGrid::Binary:
    default:
      return is_eighth ? TICK_EIGHTH : TICK_QUARTER;
  }
}

int getBassRootPitchClass(int8_t chord_degree) {
  constexpr int DEGREE_TO_ROOT[] = {0, 2, 4, 5, 7, 9, 11};
  int normalized = ((chord_degree % 7) + 7) % 7;
  return DEGREE_TO_ROOT[normalized];
}

bool isAvoidNoteWithChord(int pitch_pc, const std::vector<int>& chord_tones, int root_pc) {
  for (int ct : chord_tones) {
    int interval = std::abs(pitch_pc - ct);
    if (interval > 6) interval = 12 - interval;
    if (interval == 1) {
      return true;
    }
  }

  int root_interval = std::abs(pitch_pc - root_pc);
  if (root_interval > 6) root_interval = 12 - root_interval;
  if (root_interval == 6) {
    return true;
  }

  return false;
}

bool isAvoidNoteWithRoot(int pitch_pc, int root_pc) {
  int interval = std::abs(pitch_pc - root_pc);
  if (interval > 6) interval = 12 - interval;
  return interval == 1 || interval == 6;
}

int getNearestSafeChordTone(int current_pitch, int8_t chord_degree, int root_pc,
                            uint8_t vocal_low, uint8_t vocal_high) {
  std::vector<int> chord_tones = getChordTonePitchClasses(chord_degree);
  if (chord_tones.empty()) {
    return std::clamp(current_pitch, static_cast<int>(vocal_low), static_cast<int>(vocal_high));
  }

  int best_pitch = std::clamp(current_pitch, static_cast<int>(vocal_low), static_cast<int>(vocal_high));
  int best_distance = 100;

  for (int pc : chord_tones) {
    if (isAvoidNoteWithRoot(pc, root_pc)) continue;

    for (int oct = 3; oct <= 6; ++oct) {
      int candidate = oct * 12 + pc;
      if (candidate < static_cast<int>(vocal_low) || candidate > static_cast<int>(vocal_high)) {
        continue;
      }
      int dist = std::abs(candidate - current_pitch);
      if (dist < best_distance) {
        best_distance = dist;
        best_pitch = candidate;
      }
    }
  }

  return best_pitch;
}

int getAnchorTonePitch(int8_t chord_degree, int tessitura_center, uint8_t vocal_low,
                       uint8_t vocal_high) {
  constexpr int8_t ANCHOR_TONE_PCS[] = {0, 7, 9};
  int target_pc = ANCHOR_TONE_PCS[std::abs(chord_degree) % 3];
  int base = (tessitura_center / 12) * 12 + target_pc;
  if (base < static_cast<int>(vocal_low)) base += 12;
  if (base > static_cast<int>(vocal_high)) base -= 12;
  return std::clamp(base, static_cast<int>(vocal_low), static_cast<int>(vocal_high));
}

uint8_t calculatePhraseCount(uint8_t section_bars, uint8_t phrase_length_bars) {
  if (phrase_length_bars == 0) phrase_length_bars = 2;
  return (section_bars + phrase_length_bars - 1) / phrase_length_bars;
}

void applySequentialTransposition(std::vector<NoteEvent>& notes, uint8_t phrase_index,
                                  SectionType section_type, int key_offset, uint8_t vocal_low,
                                  uint8_t vocal_high) {
  if (section_type != SectionType::B || phrase_index == 0 || notes.empty()) {
    return;
  }

  constexpr int8_t kSequenceIntervals[] = {0, 2, 4, 5};
  int transpose = (phrase_index < 4) ? kSequenceIntervals[phrase_index] : 5;

  for (auto& note : notes) {
    int new_pitch = note.note + transpose;
    new_pitch = snapToNearestScaleTone(new_pitch, key_offset);
    new_pitch = std::clamp(new_pitch, static_cast<int>(vocal_low), static_cast<int>(vocal_high));
    note.note = static_cast<uint8_t>(new_pitch);
  }
}

}  // namespace melody
}  // namespace midisketch
