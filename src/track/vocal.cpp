/**
 * @file vocal.cpp
 * @brief Vocal melody track generation with phrase caching and variation.
 *
 * Phrase-based approach: each section generates/reuses cached phrases with
 * subtle variations for varied repetition (scale degrees, singability, cadences).
 */

#include "track/vocal.h"
#include "core/chord.h"
#include "core/chord_utils.h"
#include "core/harmony_context.h"
#include "core/melody_templates.h"
#include "core/pitch_utils.h"
#include "core/velocity.h"
#include "track/melody_designer.h"
#include <algorithm>
#include <unordered_map>

namespace midisketch {

namespace {

/// Cached phrase for section repetition (Chorus 1 & 2 share melody with variations).
struct CachedPhrase {
  std::vector<NoteEvent> notes;  ///< Notes with timing relative to section start
  uint8_t bars;                   ///< Section length when cached
  uint8_t vocal_low;              ///< Vocal range when cached
  uint8_t vocal_high;
  int reuse_count = 0;            ///< How many times this phrase has been reused
};

/// Phrase variation types: tail changes, timing shifts, ornaments, dynamics.
enum class PhraseVariation : uint8_t {
  Exact,              ///< No change - use original phrase
  LastNoteShift,      ///< Shift last note by scale degree (common ending variation)
  LastNoteLong,       ///< Extend last note duration (dramatic ending)
  TailSwap,           ///< Swap last two notes (melodic variation)
  SlightRush,         ///< Earlier timing on weak beats (adds energy)
  MicroRhythmChange,  ///< Subtle timing variation (human feel)
  BreathRestInsert,   ///< Short rest before phrase end (breathing room)
  SlurMerge,          ///< Merge short notes into longer (legato effect)
  RepeatNoteSimplify  ///< Reduce repeated notes (cleaner articulation)
};

/// Maximum number of variation types (excluding Exact)
constexpr int kVariationTypeCount = 8;

/// Maximum reuse count before variation is forced.
/// After this many exact repetitions, variation is mandatory to prevent monotony.
constexpr int kMaxExactReuse = 2;

/// @name Singing Effort Thresholds
/// Used to calculate vocal difficulty score for phrases.
/// Higher effort phrases are harder to sing and may need rest afterward.
/// @{

/// D5 (MIDI 74) and above requires significant vocal effort.
/// This is the "break point" for many singers (passaggio).
constexpr int kHighRegisterThreshold = 74;

/// Perfect 5th (7 semitones) and above is a significant vocal leap.
/// Larger intervals require more breath control and precision.
constexpr int kLargeIntervalThreshold = 7;

[[maybe_unused]] constexpr float kHighEffortScore = 1.0f;
constexpr float kMediumEffortScore = 0.5f;
/// @}

/**
 * @brief Extended cache key for phrase lookup.
 *
 * Phrases are cached not just by section type, but also by length and
 * starting chord. This ensures that a 4-bar chorus starting on I chord
 * is cached separately from an 8-bar chorus starting on IV chord.
 */
struct PhraseCacheKey {
  SectionType section_type;  ///< Section type (Verse, Chorus, etc.)
  uint8_t bars;              ///< Section length in bars
  int8_t chord_degree;       ///< Starting chord degree (affects melodic choices)

  bool operator==(const PhraseCacheKey& other) const {
    return section_type == other.section_type &&
           bars == other.bars &&
           chord_degree == other.chord_degree;
  }
};

/// Hash function for PhraseCacheKey enabling use in unordered_map.
struct PhraseCacheKeyHash {
  size_t operator()(const PhraseCacheKey& key) const {
    return std::hash<uint8_t>()(static_cast<uint8_t>(key.section_type)) ^
           (std::hash<uint8_t>()(key.bars) << 4) ^
           (std::hash<int8_t>()(key.chord_degree) << 8);
  }
};

/// Select phrase variation: exact for first/early repeats, forced variation later.
PhraseVariation selectPhraseVariation(int reuse_count, std::mt19937& rng) {
  // First occurrence: establish the phrase exactly
  if (reuse_count == 0) return PhraseVariation::Exact;

  std::uniform_real_distribution<float> dist(0.0f, 1.0f);

  // Early repeats: 80% exact to reinforce, 20% variation for interest
  if (reuse_count <= kMaxExactReuse && dist(rng) < 0.8f) {
    return PhraseVariation::Exact;
  }

  // Later repeats: force variation to prevent monotony
  return static_cast<PhraseVariation>(1 + (rng() % kVariationTypeCount));
}

/**
 * @brief Apply phrase variation to notes (ending changes, timing shifts, slurs).
 * @param notes Notes to modify (in-place)
 * @param variation Type of variation to apply
 * @param rng Random number generator for variation parameters
 */
void applyPhraseVariation(std::vector<NoteEvent>& notes,
                          PhraseVariation variation,
                          [[maybe_unused]] std::mt19937& rng) {
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

    case PhraseVariation::TailSwap: {
      // Swap last two notes (pitches only)
      if (notes.size() >= 2) {
        size_t n = notes.size();
        std::swap(notes[n - 1].note, notes[n - 2].note);
      }
      break;
    }

    case PhraseVariation::SlightRush: {
      // Rush weak beat notes slightly (10-20 ticks earlier)
      for (auto& note : notes) {
        Tick pos_in_bar = note.start_tick % TICKS_PER_BAR;
        // Weak beats: beat 2 and 4 (around TICKS_PER_BEAT and 3*TICKS_PER_BEAT)
        bool is_weak = (pos_in_bar >= TICKS_PER_BEAT - 60 && pos_in_bar <= TICKS_PER_BEAT + 60) ||
                       (pos_in_bar >= 3 * TICKS_PER_BEAT - 60 && pos_in_bar <= 3 * TICKS_PER_BEAT + 60);
        if (is_weak && note.start_tick >= 15) {
          std::uniform_int_distribution<Tick> rush_dist(10, 20);
          note.start_tick -= rush_dist(rng);
        }
      }
      break;
    }

    // V1 additions: new subtle variations
    case PhraseVariation::MicroRhythmChange: {
      // Slight timing variation on random notes (±5-15 ticks)
      std::uniform_int_distribution<int> tick_dist(-15, 15);
      std::uniform_real_distribution<float> prob_dist(0.0f, 1.0f);
      for (auto& note : notes) {
        if (prob_dist(rng) < 0.3f) {  // 30% of notes
          int shift = tick_dist(rng);
          if (shift != 0) {
            int64_t new_tick = static_cast<int64_t>(note.start_tick) + shift;
            note.start_tick = static_cast<Tick>(std::max(static_cast<int64_t>(0), new_tick));
          }
        }
      }
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

    case PhraseVariation::SlurMerge: {
      // Merge two adjacent short notes into one longer note
      if (notes.size() >= 3) {
        // Find a pair of short notes to merge
        for (size_t i = 0; i + 1 < notes.size(); ++i) {
          if (notes[i].duration <= TICKS_PER_BEAT / 2 &&
              notes[i + 1].duration <= TICKS_PER_BEAT / 2) {
            // Merge: extend first note, remove second
            notes[i].duration = (notes[i + 1].start_tick + notes[i + 1].duration) - notes[i].start_tick;
            notes.erase(notes.begin() + static_cast<std::ptrdiff_t>(i + 1));
            break;  // Only merge one pair
          }
        }
      }
      break;
    }

    case PhraseVariation::RepeatNoteSimplify: {
      // Remove one repeated note (same pitch in sequence)
      if (notes.size() >= 3) {
        for (size_t i = 1; i < notes.size(); ++i) {
          if (notes[i].note == notes[i - 1].note) {
            // Extend previous note to cover the removed note
            notes[i - 1].duration = (notes[i].start_tick + notes[i].duration) - notes[i - 1].start_tick;
            notes.erase(notes.begin() + static_cast<std::ptrdiff_t>(i));
            break;  // Only simplify one instance
          }
        }
      }
      break;
    }

    case PhraseVariation::Exact:
      // No change
      break;
  }
}

/// Determine cadence: Strong (tonic tone+strong beat), Weak, Floating (tension),
/// or Deceptive (vi instead of I). Helps accompaniment support phrase endings.
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
  // 2nd(D), 4th(F), 6th(A), 7th(B) in C major
  bool is_tension = (pitch_class == 2 || pitch_class == 5 || pitch_class == 9 || pitch_class == 11);
  if (is_tension) {
    return CadenceType::Floating;
  }

  // Weak: chord tone but not fully resolved
  return CadenceType::Weak;
}

/// Calculate singing effort: high register + large intervals + note density.
/// @return Effort score 0.0 (easy) to 1.0+ (demanding). Reserved for future use.
[[maybe_unused]] static float calculateSingingEffort(
    const std::vector<NoteEvent>& notes) {
  if (notes.empty()) return 0.0f;

  float effort = 0.0f;

  for (size_t i = 0; i < notes.size(); ++i) {
    // High register penalty
    if (notes[i].note >= kHighRegisterThreshold) {
      // Longer high notes = more effort
      effort += kMediumEffortScore * (notes[i].duration / static_cast<float>(TICKS_PER_BEAT));
    }

    // Large interval penalty
    if (i > 0) {
      int interval = std::abs(static_cast<int>(notes[i].note) - static_cast<int>(notes[i - 1].note));
      if (interval >= kLargeIntervalThreshold) {
        effort += kMediumEffortScore;
      }
    }
  }

  // Density penalty: many notes in short time
  if (notes.size() > 1) {
    Tick phrase_length = notes.back().start_tick + notes.back().duration - notes[0].start_tick;
    float notes_per_beat = notes.size() * TICKS_PER_BEAT / static_cast<float>(phrase_length);
    if (notes_per_beat > 2.0f) {  // More than 2 notes per beat = dense
      effort += (notes_per_beat - 2.0f) * kMediumEffortScore;
    }
  }

  // Normalize by phrase length (effort per bar)
  if (notes.size() > 0) {
    Tick phrase_length = notes.back().start_tick + notes.back().duration - notes[0].start_tick;
    float bars = phrase_length / static_cast<float>(TICKS_PER_BAR);
    if (bars > 0) {
      effort /= bars;
    }
  }

  return effort;
}

// Shift note timings by offset
std::vector<NoteEvent> shiftTiming(const std::vector<NoteEvent>& notes, Tick offset) {
  std::vector<NoteEvent> result;
  result.reserve(notes.size());
  for (const auto& note : notes) {
    NoteEvent shifted = note;
    shifted.start_tick += offset;
    result.push_back(shifted);
  }
  return result;
}

// Adjust pitches to new vocal range
std::vector<NoteEvent> adjustPitchRange(const std::vector<NoteEvent>& notes,
                                         uint8_t orig_low, uint8_t orig_high,
                                         uint8_t new_low, uint8_t new_high,
                                         int key_offset = 0) {
  if (orig_low == new_low && orig_high == new_high) {
    return notes;  // No adjustment needed
  }

  std::vector<NoteEvent> result;
  result.reserve(notes.size());

  // Calculate shift based on center points
  int orig_center = (orig_low + orig_high) / 2;
  int new_center = (new_low + new_high) / 2;
  int shift = new_center - orig_center;

  for (const auto& note : notes) {
    NoteEvent adjusted = note;
    int new_pitch = static_cast<int>(note.note) + shift;
    // Snap to scale to prevent chromatic notes
    new_pitch = snapToNearestScaleTone(new_pitch, key_offset);
    // Clamp to new range
    new_pitch = std::clamp(new_pitch, static_cast<int>(new_low), static_cast<int>(new_high));
    adjusted.note = static_cast<uint8_t>(new_pitch);
    result.push_back(adjusted);
  }
  return result;
}

// Convert notes to relative timing (subtract section start)
std::vector<NoteEvent> toRelativeTiming(const std::vector<NoteEvent>& notes, Tick section_start) {
  std::vector<NoteEvent> result;
  result.reserve(notes.size());
  for (const auto& note : notes) {
    NoteEvent relative = note;
    relative.start_tick -= section_start;
    result.push_back(relative);
  }
  return result;
}

// Get register shift for section type based on melody params
int8_t getRegisterShift(SectionType type, const StyleMelodyParams& params) {
  switch (type) {
    case SectionType::A:
      return params.verse_register_shift;
    case SectionType::B:
      return params.prechorus_register_shift;
    case SectionType::Chorus:
      return params.chorus_register_shift;
    case SectionType::Bridge:
      return params.bridge_register_shift;
    default:
      return 0;
  }
}

// Get density modifier for section type based on melody params
float getDensityModifier(SectionType type, const StyleMelodyParams& params) {
  switch (type) {
    case SectionType::A:
      return params.verse_density_modifier;
    case SectionType::B:
      return params.prechorus_density_modifier;
    case SectionType::Chorus:
      return params.chorus_density_modifier;
    case SectionType::Bridge:
      return params.bridge_density_modifier;
    default:
      return 1.0f;
  }
}

// Get 32nd note ratio for section type based on melody params
float getThirtysecondRatio(SectionType type, const StyleMelodyParams& params) {
  switch (type) {
    case SectionType::A:
      return params.verse_thirtysecond_ratio;
    case SectionType::B:
      return params.prechorus_thirtysecond_ratio;
    case SectionType::Chorus:
      return params.chorus_thirtysecond_ratio;
    case SectionType::Bridge:
      return params.bridge_thirtysecond_ratio;
    default:
      return params.thirtysecond_note_ratio;  // Fallback to base ratio
  }
}

// Check if section type should have vocals
bool sectionHasVocals(SectionType type) {
  switch (type) {
    case SectionType::Intro:
    case SectionType::Interlude:
    case SectionType::Outro:
    case SectionType::Chant:
    case SectionType::MixBreak:
      return false;
    default:
      return true;
  }
}

// Apply velocity balance for track role
void applyVelocityBalance(std::vector<NoteEvent>& notes, float scale) {
  for (auto& note : notes) {
    int vel = static_cast<int>(note.velocity * scale);
    note.velocity = static_cast<uint8_t>(std::clamp(vel, 1, 127));
  }
}

// Remove overlapping notes by adjusting duration
// Ensures end_tick <= next_start for all consecutive note pairs
void removeOverlaps(std::vector<NoteEvent>& notes) {
  if (notes.size() < 2) return;

  // Sort by start tick
  std::sort(notes.begin(), notes.end(),
            [](const NoteEvent& a, const NoteEvent& b) {
              return a.start_tick < b.start_tick;
            });

  // Adjust durations to prevent overlap
  for (size_t i = 0; i + 1 < notes.size(); ++i) {
    Tick end_tick = notes[i].start_tick + notes[i].duration;
    Tick next_start = notes[i + 1].start_tick;

    if (end_tick > next_start) {
      // Guard against underflow: if same startTick, use minimum duration
      Tick max_duration = (next_start > notes[i].start_tick)
                              ? (next_start - notes[i].start_tick)
                              : 1;
      notes[i].duration = max_duration;

      // If still overlapping (same startTick case), shift next note forward
      if (notes[i].start_tick + notes[i].duration > notes[i + 1].start_tick) {
        notes[i + 1].start_tick = notes[i].start_tick + notes[i].duration;
      }
    }
  }
}

/// Apply hook intensity (Light/Normal/Strong) to first 1-3 notes of section.
/// Emphasizes "money notes" at chorus/B-section starts.
void applyHookIntensity(std::vector<NoteEvent>& notes, SectionType section_type,
                        HookIntensity intensity, Tick section_start) {
  if (intensity == HookIntensity::Off || notes.empty()) {
    return;
  }

  // Hook points: Chorus start, B section climax
  bool is_hook_section = (section_type == SectionType::Chorus ||
                          section_type == SectionType::B);
  if (!is_hook_section && intensity != HookIntensity::Strong) {
    return;  // Only Strong applies to all sections
  }

  // Find notes at or near section start (first beat)
  Tick hook_window = TICKS_PER_BEAT * 2;  // First 2 beats
  std::vector<size_t> hook_note_indices;

  for (size_t i = 0; i < notes.size(); ++i) {
    if (notes[i].start_tick >= section_start &&
        notes[i].start_tick < section_start + hook_window) {
      hook_note_indices.push_back(i);
    }
  }

  if (hook_note_indices.empty()) return;

  // Apply effects based on intensity
  float duration_mult = 1.0f;
  float velocity_boost = 0.0f;

  switch (intensity) {
    case HookIntensity::Light:
      duration_mult = 1.3f;   // 30% longer
      velocity_boost = 5.0f;  // Slight velocity boost
      break;
    case HookIntensity::Normal:
      duration_mult = 1.5f;   // 50% longer
      velocity_boost = 10.0f;
      break;
    case HookIntensity::Strong:
      duration_mult = 2.0f;   // Double duration
      velocity_boost = 15.0f;
      break;
    default:
      break;
  }

  // Apply to first few notes (depending on intensity)
  size_t max_notes = (intensity == HookIntensity::Light) ? 1 :
                     (intensity == HookIntensity::Normal) ? 2 : 3;
  size_t apply_count = std::min(hook_note_indices.size(), max_notes);

  for (size_t i = 0; i < apply_count; ++i) {
    size_t idx = hook_note_indices[i];
    notes[idx].duration = static_cast<Tick>(notes[idx].duration * duration_mult);
    notes[idx].velocity = static_cast<uint8_t>(
        std::clamp(static_cast<int>(notes[idx].velocity + velocity_boost), 1, 127));
  }
}

/// Apply groove timing: OffBeat (laid-back), Swing (shuffle), Syncopated (funk),
/// Driving16th (energetic), Bouncy8th (playful). 10-60 tick adjustments.
void applyGrooveFeel(std::vector<NoteEvent>& notes, VocalGrooveFeel groove) {
  if (groove == VocalGrooveFeel::Straight || notes.empty()) {
    return;  // No adjustment for straight timing
  }

  constexpr Tick TICK_8TH = TICKS_PER_BEAT / 2;   // 240 ticks
  constexpr Tick TICK_16TH = TICKS_PER_BEAT / 4;  // 120 ticks

  for (auto& note : notes) {
    // Get position within beat
    Tick beat_pos = note.start_tick % TICKS_PER_BEAT;
    Tick shift = 0;

    switch (groove) {
      case VocalGrooveFeel::OffBeat:
        // Shift on-beat notes slightly late, emphasize off-beats
        if (beat_pos < TICK_16TH) {
          shift = TICK_16TH / 2;  // Push on-beats late
        }
        break;

      case VocalGrooveFeel::Swing:
        // Swing: delay second 8th note of each beat pair
        if (beat_pos >= TICK_8TH - TICK_16TH && beat_pos < TICK_8TH + TICK_16TH) {
          // Second 8th note: push later for swing feel
          shift = TICK_16TH / 2;
        }
        break;

      case VocalGrooveFeel::Syncopated:
        // Push notes on beats 2 and 4 earlier (anticipation)
        {
          Tick bar_pos = note.start_tick % TICKS_PER_BAR;
          // Beats 2 and 4 (at 480 and 1440 ticks)
          if ((bar_pos >= TICKS_PER_BEAT - TICK_16TH && bar_pos < TICKS_PER_BEAT + TICK_16TH) ||
              (bar_pos >= TICKS_PER_BEAT * 3 - TICK_16TH && bar_pos < TICKS_PER_BEAT * 3 + TICK_16TH)) {
            shift = -TICK_16TH / 2;  // Anticipate
          }
        }
        break;

      case VocalGrooveFeel::Driving16th:
        // Slight rush on all 16th notes (energetic feel)
        if (beat_pos % TICK_16TH < TICK_16TH / 4) {
          shift = -TICK_16TH / 4;  // Slight rush
        }
        break;

      case VocalGrooveFeel::Bouncy8th:
        // Bouncy: first 8th slightly short, second 8th delayed
        if (beat_pos < TICK_8TH) {
          // First 8th: no shift but make duration shorter
          if (note.duration > TICK_8TH) {
            note.duration = note.duration * 85 / 100;  // 85% duration
          }
        } else {
          // Second 8th: slight delay
          shift = TICK_16TH / 3;
        }
        break;

      default:
        break;
    }

    // Apply shift (ensure non-negative)
    if (shift != 0) {
      int64_t new_tick = static_cast<int64_t>(note.start_tick) + shift;
      note.start_tick = static_cast<Tick>(std::max(static_cast<int64_t>(0), new_tick));
    }
  }
}

/// Apply collision avoidance while maintaining singable intervals (≤major 6th).
/// Snaps to chord tones after avoiding clashes with bass/chord.
void applyCollisionAvoidanceWithIntervalConstraint(
    std::vector<NoteEvent>& notes,
    const HarmonyContext& harmony,
    uint8_t vocal_low,
    uint8_t vocal_high) {
  if (notes.empty()) return;

  // Major 6th (9 semitones) - the practical limit for singable leaps
  constexpr int MAX_VOCAL_INTERVAL = 9;

  for (size_t i = 0; i < notes.size(); ++i) {
    auto& note = notes[i];

    // Get chord degree at this note's position
    int8_t chord_degree = harmony.getChordDegreeAt(note.start_tick);

    // Apply collision avoidance
    uint8_t safe_pitch = harmony.getSafePitch(
        note.note, note.start_tick, note.duration, TrackRole::Vocal,
        vocal_low, vocal_high);
    // Snap to chord tone (to maintain harmonic stability)
    int snapped = nearestChordTonePitch(safe_pitch, chord_degree);
    note.note = static_cast<uint8_t>(std::clamp(snapped,
        static_cast<int>(vocal_low), static_cast<int>(vocal_high)));

    // Re-enforce interval constraint (getSafePitch may have expanded interval)
    if (i > 0) {
      int prev_pitch = notes[i - 1].note;
      int interval = std::abs(static_cast<int>(note.note) - prev_pitch);
      if (interval > MAX_VOCAL_INTERVAL) {
        // Use nearestChordToneWithinInterval to find chord tone within constraint
        int new_pitch = nearestChordToneWithinInterval(
            note.note, prev_pitch, chord_degree, MAX_VOCAL_INTERVAL,
            vocal_low, vocal_high, nullptr);
        note.note = static_cast<uint8_t>(new_pitch);
      }
    }
  }
}

}  // namespace

void generateVocalTrack(MidiTrack& track, Song& song,
                        const GeneratorParams& params, std::mt19937& rng,
                        const MidiTrack* motif_track,
                        const HarmonyContext& harmony,
                        bool skip_collision_avoidance) {

  // Determine effective vocal range
  uint8_t effective_vocal_low = params.vocal_low;
  uint8_t effective_vocal_high = params.vocal_high;

  // Adjust range for BackgroundMotif to avoid collision with motif
  if (params.composition_style == CompositionStyle::BackgroundMotif &&
      motif_track != nullptr && !motif_track->empty()) {
    auto [motif_low, motif_high] = motif_track->analyzeRange();

    if (motif_high > 72) {  // Motif in high register
      effective_vocal_high = std::min(effective_vocal_high, static_cast<uint8_t>(72));
      if (effective_vocal_high - effective_vocal_low < 12) {
        effective_vocal_low = std::max(static_cast<uint8_t>(48),
                                        static_cast<uint8_t>(effective_vocal_high - 12));
      }
    } else if (motif_low < 60) {  // Motif in low register
      effective_vocal_low = std::max(effective_vocal_low, static_cast<uint8_t>(65));
      if (effective_vocal_high - effective_vocal_low < 12) {
        effective_vocal_high = std::min(static_cast<uint8_t>(96),
                                         static_cast<uint8_t>(effective_vocal_low + 12));
      }
    }
  }

  // Get chord progression
  const auto& progression = getChordProgression(params.chord_id);

  // Velocity scale for composition style
  float velocity_scale = 1.0f;
  if (params.composition_style == CompositionStyle::BackgroundMotif) {
    velocity_scale = 0.7f;
  } else if (params.composition_style == CompositionStyle::SynthDriven) {
    velocity_scale = 0.75f;
  }

  // Create MelodyDesigner
  MelodyDesigner designer;

  // Collect all notes
  std::vector<NoteEvent> all_notes;

  // Phrase cache for section repetition (V2: extended key with bars + chord_degree)
  std::unordered_map<PhraseCacheKey, CachedPhrase, PhraseCacheKeyHash> phrase_cache;

  // Clear existing phrase boundaries for fresh generation
  song.clearPhraseBoundaries();

  // Process each section
  for (const auto& section : song.arrangement().sections()) {
    // Skip sections without vocals
    if (!sectionHasVocals(section.type)) {
      continue;
    }

    // Get template: use explicit template if specified, otherwise auto-select by style/section
    MelodyTemplateId section_template_id =
        (params.melody_template != MelodyTemplateId::Auto)
            ? params.melody_template
            : getDefaultTemplateForStyle(params.vocal_style, section.type);
    const MelodyTemplate& section_tmpl = getTemplate(section_template_id);

    // Calculate section boundaries
    Tick section_start = section.start_tick;
    Tick section_end = section.start_tick + section.bars * TICKS_PER_BAR;

    // Get chord for this section
    int chord_idx = section.start_bar % progression.length;
    int8_t chord_degree = progression.at(chord_idx);

    // Apply register shift for section (clamped to original range)
    int8_t register_shift = getRegisterShift(section.type, params.melody_params);
    // Register shift adjusts the preferred center but must not exceed original range
    uint8_t section_vocal_low = static_cast<uint8_t>(
        std::clamp(static_cast<int>(effective_vocal_low) + register_shift,
                   static_cast<int>(effective_vocal_low),
                   static_cast<int>(effective_vocal_high) - 6));  // At least 6 semitone range
    uint8_t section_vocal_high = static_cast<uint8_t>(
        std::clamp(static_cast<int>(effective_vocal_high) + register_shift,
                   static_cast<int>(effective_vocal_low) + 6,
                   static_cast<int>(effective_vocal_high)));  // Stay within original high

    // Recalculate tessitura for section
    TessituraRange section_tessitura = calculateTessitura(section_vocal_low, section_vocal_high);

    std::vector<NoteEvent> section_notes;

    // V2: Create extended cache key
    PhraseCacheKey cache_key{section.type, section.bars, chord_degree};

    // Check phrase cache for repeated sections (V2: extended key)
    auto cache_it = phrase_cache.find(cache_key);
    if (cache_it != phrase_cache.end()) {
      // Cache hit: reuse cached phrase with timing adjustment and optional variation
      CachedPhrase& cached = cache_it->second;

      // Select variation based on reuse count (80% Exact, 20% variation)
      PhraseVariation variation = selectPhraseVariation(cached.reuse_count, rng);
      cached.reuse_count++;

      // Shift timing to current section start
      section_notes = shiftTiming(cached.notes, section_start);

      // Apply subtle variation for interest while maintaining recognizability
      applyPhraseVariation(section_notes, variation, rng);

      // Adjust pitch range if different
      section_notes = adjustPitchRange(section_notes,
                                        cached.vocal_low, cached.vocal_high,
                                        section_vocal_low, section_vocal_high);

      // Re-apply collision avoidance (chord context may differ)
      if (!skip_collision_avoidance) {
        applyCollisionAvoidanceWithIntervalConstraint(
            section_notes, harmony, section_vocal_low, section_vocal_high);
      }
    } else {
      // Cache miss: generate new melody
      MelodyDesigner::SectionContext ctx;
      ctx.section_type = section.type;
      ctx.section_start = section_start;
      ctx.section_end = section_end;
      ctx.section_bars = section.bars;
      ctx.chord_degree = chord_degree;
      ctx.key_offset = 0;  // Always C major internally
      ctx.tessitura = section_tessitura;
      ctx.vocal_low = section_vocal_low;
      ctx.vocal_high = section_vocal_high;
      ctx.mood = params.mood;  // For harmonic rhythm alignment
      ctx.density_modifier = getDensityModifier(section.type, params.melody_params);
      ctx.thirtysecond_ratio = getThirtysecondRatio(section.type, params.melody_params);
      ctx.consecutive_same_note_prob = params.melody_params.consecutive_same_note_prob;
      ctx.disable_vowel_constraints = params.melody_params.disable_vowel_constraints;
      ctx.disable_breathing_gaps = params.melody_params.disable_breathing_gaps;

      // Set transition info for next section (if any)
      const auto& sections = song.arrangement().sections();
      for (size_t i = 0; i < sections.size(); ++i) {
        if (&sections[i] == &section && i + 1 < sections.size()) {
          ctx.transition_to_next = getTransition(section.type, sections[i + 1].type);
          break;
        }
      }

      // Generate melody with evaluation (selects best from 3 candidates)
      section_notes = designer.generateSectionWithEvaluation(
          section_tmpl, ctx, harmony, rng, params.vocal_style, 3);

      // Apply transition approach if transition info was set
      if (ctx.transition_to_next) {
        designer.applyTransitionApproach(section_notes, ctx, harmony);
      }

      // Apply HarmonyContext collision avoidance with interval constraint
      if (!skip_collision_avoidance) {
        applyCollisionAvoidanceWithIntervalConstraint(
            section_notes, harmony, section_vocal_low, section_vocal_high);
      }

      // Apply hook intensity effects at hook points (Chorus, B section)
      applyHookIntensity(section_notes, section.type, params.hook_intensity, section_start);

      // Cache the phrase (with relative timing)
      CachedPhrase cache_entry;
      cache_entry.notes = toRelativeTiming(section_notes, section_start);
      cache_entry.bars = section.bars;
      cache_entry.vocal_low = section_vocal_low;
      cache_entry.vocal_high = section_vocal_high;
      phrase_cache[cache_key] = std::move(cache_entry);
    }

    // V5: Generate phrase boundary at section end
    if (!section_notes.empty()) {
      CadenceType cadence = detectCadenceType(section_notes, chord_degree);
      bool is_section_end = true;
      bool is_breath = true;  // Breath at every section end

      PhraseBoundary boundary;
      boundary.tick = section_end;
      boundary.is_breath = is_breath;
      boundary.is_section_end = is_section_end;
      boundary.cadence = cadence;
      song.addPhraseBoundary(boundary);
    }

    // Add to collected notes
    // Check interval between last note of previous section and first note of this section
    if (!all_notes.empty() && !section_notes.empty()) {
      constexpr int MAX_INTERVAL = 9;
      int prev_note = all_notes.back().note;
      int first_note = section_notes.front().note;
      int interval = std::abs(first_note - prev_note);
      if (interval > MAX_INTERVAL) {
        // Get chord degree at first note's position
        int8_t first_note_chord_degree = harmony.getChordDegreeAt(section_notes.front().start_tick);
        // Use nearestChordToneWithinInterval to stay on chord tones
        int new_pitch = nearestChordToneWithinInterval(
            first_note, prev_note, first_note_chord_degree, MAX_INTERVAL,
            section_vocal_low, section_vocal_high, nullptr);
        section_notes.front().note = static_cast<uint8_t>(new_pitch);
      }
    }
    for (const auto& note : section_notes) {
      all_notes.push_back(note);
    }
  }

  // NOTE: Modulation is NOT applied internally.
  // MidiWriter applies modulation to all tracks when generating MIDI bytes.
  // This ensures consistent behavior and avoids double-modulation.

  // Apply groove feel timing adjustments
  applyGrooveFeel(all_notes, params.vocal_groove);

  // Remove overlapping notes
  removeOverlaps(all_notes);

  // Apply velocity scale
  applyVelocityBalance(all_notes, velocity_scale);

  // Add notes to track (preserving provenance)
  for (const auto& note : all_notes) {
    track.addNote(note);
  }
}

}  // namespace midisketch
