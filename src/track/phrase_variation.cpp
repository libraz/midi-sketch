/**
 * @file phrase_variation.cpp
 * @brief Implementation of phrase variation functions.
 */

#include "track/phrase_variation.h"
#include "core/pitch_utils.h"
#include <algorithm>

namespace midisketch {

PhraseVariation selectPhraseVariation(int reuse_count, std::mt19937& rng) {
  // First occurrence: establish the phrase exactly
  if (reuse_count == 0) return PhraseVariation::Exact;

  std::uniform_real_distribution<float> dist(0.0f, 1.0f);

  // Early repeats: 80% exact to reinforce, 20% variation for interest
  if (reuse_count <= kMaxExactReuse && dist(rng) < 0.8f) {
    return PhraseVariation::Exact;
  }

  // Later repeats: select from safe variations only.
  // Exclude: TailSwap (direction destruction), SlightRush (wrong beat emphasis),
  // MicroRhythmChange (too random), SlurMerge (articulation loss),
  // RepeatNoteSimplify (rhythm motif destruction).
  constexpr PhraseVariation kSafeVariations[] = {
      PhraseVariation::LastNoteShift,   // Subtle ending variation
      PhraseVariation::LastNoteLong,    // Dramatic ending extension
      PhraseVariation::BreathRestInsert // Natural breathing room
  };
  constexpr size_t kSafeCount = sizeof(kSafeVariations) / sizeof(kSafeVariations[0]);
  return kSafeVariations[rng() % kSafeCount];
}

void applyPhraseVariation(std::vector<NoteEvent>& notes,
                          PhraseVariation variation,
                          std::mt19937& rng) {
  if (notes.empty() || variation == PhraseVariation::Exact) {
    return;
  }

  switch (variation) {
    case PhraseVariation::LastNoteShift: {
      // Shift last note by ±1-2 scale degrees (not semitones)
      auto& last = notes.back();
      std::uniform_int_distribution<int> shift_dist(-2, 2);
      int shift = shift_dist(rng);
      if (shift == 0) shift = 1;
      // Shift by scale degrees: find current scale position and move
      int pc = last.note % 12;
      int octave = last.note / 12;
      // Find current scale index
      int scale_idx = 0;
      for (int i = 0; i < 7; ++i) {
        if (SCALE[i] == pc || (SCALE[i] < pc && (i == 6 || SCALE[i + 1] > pc))) {
          scale_idx = i;
          break;
        }
      }
      // Apply scale degree shift with octave wrapping
      int new_scale_idx = scale_idx + shift;
      while (new_scale_idx < 0) {
        new_scale_idx += 7;
        octave--;
      }
      while (new_scale_idx >= 7) {
        new_scale_idx -= 7;
        octave++;
      }
      int new_pitch = octave * 12 + SCALE[new_scale_idx];
      last.note = static_cast<uint8_t>(std::clamp(new_pitch, 0, 127));
      break;
    }

    case PhraseVariation::LastNoteLong: {
      // Extend last note by 50%
      auto& last = notes.back();
      last.duration = static_cast<Tick>(last.duration * 1.5f);
      break;
    }

    case PhraseVariation::BreathRestInsert: {
      // Insert short rest before phrase end by shortening last note
      if (notes.size() >= 2) {
        auto& last = notes.back();
        // Reduce duration by 60-120 ticks (1/8 to 1/4 beat of rest)
        std::uniform_int_distribution<Tick> rest_dist(60, 120);
        Tick rest_amount = rest_dist(rng);
        if (last.duration > rest_amount + 60) {  // Keep at least 60 ticks
          last.duration -= rest_amount;
        }
      }
      break;
    }

    // Deprecated variations: do nothing (kept for enum compatibility)
    case PhraseVariation::TailSwap:
    case PhraseVariation::SlightRush:
    case PhraseVariation::MicroRhythmChange:
    case PhraseVariation::SlurMerge:
    case PhraseVariation::RepeatNoteSimplify:
    case PhraseVariation::Exact:
      break;
  }
}

CadenceType detectCadenceType(const std::vector<NoteEvent>& notes, int8_t chord_degree) {
  if (notes.empty()) return CadenceType::None;

  const auto& last_note = notes.back();
  uint8_t pitch_class = last_note.note % 12;  // 0=C, 2=D, 4=E, 5=F, 7=G, 9=A, 11=B

  // Strong cadence: ends on chord tone of tonic (I chord)
  // In C major: C(0), E(4), G(7) - the "stable" tones
  bool is_tonic_tone = (pitch_class == 0 || pitch_class == 4 || pitch_class == 7);

  // Check if on strong beat (beats 1 or 3 in 4/4)
  Tick beat_pos = last_note.start_tick % TICKS_PER_BAR;
  bool is_strong_beat = (beat_pos < TICKS_PER_BEAT / 4) ||
                        (beat_pos >= TICKS_PER_BEAT * 2 - TICKS_PER_BEAT / 4 &&
                         beat_pos < TICKS_PER_BEAT * 2 + TICKS_PER_BEAT / 4);

  // Long note = more stable resolution (quarter note or longer)
  bool is_long = last_note.duration >= TICKS_PER_BEAT;

  // Deceptive: ends on vi chord tone (A in C major)
  // chord_degree == 5 means vi chord (0-indexed scale degrees)
  if (chord_degree == 5 && pitch_class == 9) {
    return CadenceType::Deceptive;
  }

  // Strong: tonic tone + strong beat + long duration = maximum closure
  if (is_tonic_tone && is_strong_beat && is_long) {
    return CadenceType::Strong;
  }

  // Floating: tension note creates suspense
  // Tensions: 2nd(D, pc=2), 4th(F, pc=5), 7th(B, pc=11) in C major
  // Note: 6th(A, pc=9) is NOT a tension - it's the root of vi (Am) and a stable diatonic note
  bool is_tension = (pitch_class == 2 || pitch_class == 5 || pitch_class == 11);
  if (is_tension) {
    return CadenceType::Floating;
  }

  // Weak: chord tone but not fully resolved
  return CadenceType::Weak;
}

}  // namespace midisketch
