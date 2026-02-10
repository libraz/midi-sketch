/**
 * @file vocal.cpp
 * @brief Vocal melody track generation with phrase caching and variation.
 *
 * Phrase-based approach: each section generates/reuses cached phrases with
 * subtle variations for varied repetition (scale degrees, singability, cadences).
 */

#include "track/generators/vocal.h"

#include <algorithm>
#include <set>
#include <unordered_map>

#include "core/chord.h"
#include "core/chord_utils.h"
#include "core/rng_util.h"
#include "core/i_harmony_context.h"
#include "core/melody_embellishment.h"
#include "core/melody_evaluator.h"
#include "core/melody_templates.h"
#include "core/mood_utils.h"
#include "core/note_creator.h"
#include "core/note_timeline_utils.h"
#include "core/note_source.h"
#include "core/pitch_bend_curves.h"
#include "core/pitch_utils.h"
#include "core/production_blueprint.h"
#include "core/song.h"
#include "core/velocity.h"
#include "track/melody/motif_support.h"
#include "track/vocal/melody_designer.h"
#include "track/vocal/phrase_cache.h"
#include "track/vocal/phrase_planner.h"
#include "track/vocal/phrase_variation.h"
#include "track/generators/motif.h"
#include "track/melody/melody_utils.h"
#include "track/vocal/vocal_helpers.h"

namespace midisketch {

// ============================================================================
// Motif Collision Avoidance Constants
// ============================================================================
// When BackgroundMotif mode, avoid vocal range collision with motif track.
// These MIDI note numbers define register boundaries for range separation.
constexpr uint8_t kMotifHighRegisterThreshold = 72;  // C5 - motif considered "high" if above this
constexpr uint8_t kMotifLowRegisterThreshold = 60;   // C4 - motif considered "low" if below this
constexpr uint8_t kVocalAvoidHighLimit = 72;         // Limit vocal high when motif is high
constexpr uint8_t kVocalAvoidLowLimit = 65;          // Limit vocal low when motif is low
constexpr uint8_t kMinVocalOctaveRange = 12;         // Minimum 1 octave range required
constexpr uint8_t kVocalRangeFloor = 48;             // C3 - absolute minimum for vocal
constexpr uint8_t kVocalRangeCeiling = 96;           // C7 - absolute maximum for vocal

// ============================================================================
// VocalRangeResult: Calculated effective vocal range
// ============================================================================

/// @brief Result of vocal range calculation.
struct VocalRangeResult {
  uint8_t effective_low;   ///< Effective lower bound of vocal range
  uint8_t effective_high;  ///< Effective upper bound of vocal range
  float velocity_scale;    ///< Velocity scaling factor for composition style
};

/// @brief Calculate effective vocal range considering constraints.
/// @param params Generation parameters
/// @param song Song with modulation info
/// @param motif_track Optional motif track for range separation
/// @return Calculated vocal range
VocalRangeResult calculateEffectiveVocalRange(const GeneratorParams& params, const Song& song,
                                               const MidiTrack* motif_track) {
  VocalRangeResult result;
  result.effective_low = params.vocal_low;
  result.effective_high = params.vocal_high;
  result.velocity_scale = 1.0f;

  // Apply blueprint max_pitch constraint (e.g., IdolKawaii limits to G5=79)
  if (params.blueprint_ref != nullptr) {
    const auto& constraints = params.blueprint_ref->constraints;
    if (constraints.max_pitch < result.effective_high) {
      result.effective_high = constraints.max_pitch;
    }
  }

  // Adjust vocal_high to account for modulation
  int8_t mod_amount = song.modulationAmount();
  if (mod_amount > 0) {
    int adjusted_high = static_cast<int>(result.effective_high) - mod_amount;
    int min_high = static_cast<int>(result.effective_low) + 12;  // At least 1 octave
    result.effective_high = static_cast<uint8_t>(std::max(min_high, adjusted_high));
  }

  // Adjust range for BackgroundMotif to avoid collision with motif
  if (params.composition_style == CompositionStyle::BackgroundMotif && motif_track != nullptr &&
      !motif_track->empty()) {
    auto [motif_low, motif_high] = motif_track->analyzeRange();

    if (motif_high > kMotifHighRegisterThreshold) {  // Motif in high register
      result.effective_high = std::min(result.effective_high, kVocalAvoidHighLimit);
      if (result.effective_high - result.effective_low < kMinVocalOctaveRange) {
        result.effective_low = std::max(
            kVocalRangeFloor, static_cast<uint8_t>(result.effective_high - kMinVocalOctaveRange));
      }
    } else if (motif_low < kMotifLowRegisterThreshold) {  // Motif in low register
      result.effective_low = std::max(result.effective_low, kVocalAvoidLowLimit);
      if (result.effective_high - result.effective_low < kMinVocalOctaveRange) {
        result.effective_high = std::min(
            kVocalRangeCeiling, static_cast<uint8_t>(result.effective_low + kMinVocalOctaveRange));
      }
    }
  }

  // Calculate velocity scale for composition style
  if (params.composition_style == CompositionStyle::BackgroundMotif) {
    result.velocity_scale = (params.motif_vocal.prominence == VocalProminence::Foreground)
                                ? 0.85f
                                : 0.65f;
  } else if (params.composition_style == CompositionStyle::SynthDriven) {
    result.velocity_scale = 0.75f;
  }

  return result;
}

// ============================================================================
// VocalPostProcessor: Post-processing helpers
// ============================================================================

/// @brief Apply pitch enforcement and interval fixes to vocal notes.
/// @param all_notes All generated notes
/// @param params Generation parameters
/// @param harmony Harmony context for chord lookups
void enforceVocalPitchConstraints(std::vector<NoteEvent>& all_notes, const GeneratorParams& params,
                                   IHarmonyContext& harmony) {
  // FINAL INTERVAL ENFORCEMENT: Ensure no consecutive notes exceed kMaxMelodicInterval
  for (size_t i = 1; i < all_notes.size(); ++i) {
    int prev_pitch = all_notes[i - 1].note;
    int curr_pitch = all_notes[i].note;
    int interval = std::abs(curr_pitch - prev_pitch);
    if (interval > kMaxMelodicInterval) {
      int8_t chord_degree = harmony.getChordDegreeAt(all_notes[i].start_tick);
#ifdef MIDISKETCH_NOTE_PROVENANCE
      uint8_t old_pitch = all_notes[i].note;
#endif
      int fixed_pitch =
          nearestChordToneWithinInterval(curr_pitch, prev_pitch, chord_degree, kMaxMelodicInterval,
                                         params.vocal_low, params.vocal_high, nullptr);
      // Re-verify collision safety after interval fix
      if (!harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(fixed_pitch),
                                               all_notes[i].start_tick, all_notes[i].duration,
                                               TrackRole::Vocal)) {
        fixed_pitch = curr_pitch;  // Keep original if fix introduces collision
      }
      all_notes[i].note = static_cast<uint8_t>(fixed_pitch);
#ifdef MIDISKETCH_NOTE_PROVENANCE
      if (old_pitch != all_notes[i].note) {
        all_notes[i].prov_original_pitch = old_pitch;
        all_notes[i].addTransformStep(TransformStepType::IntervalFix, old_pitch, all_notes[i].note,
                                       0, 0);
      }
#endif
    }
  }

  // FINAL SCALE ENFORCEMENT: Ensure all notes are diatonic
  for (auto& note : all_notes) {
    int snapped = snapToNearestScaleTone(note.note, 0);  // Always C major internally
    if (snapped != note.note) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
      uint8_t old_pitch = note.note;
#endif
      uint8_t snapped_clamped = static_cast<uint8_t>(std::clamp(snapped, static_cast<int>(params.vocal_low),
                                                   static_cast<int>(params.vocal_high)));
      // Re-verify collision safety after scale snap
      if (!harmony.isConsonantWithOtherTracks(snapped_clamped, note.start_tick, note.duration,
                                               TrackRole::Vocal)) {
        continue;  // Keep original pitch if snap introduces collision
      }
      note.note = snapped_clamped;
#ifdef MIDISKETCH_NOTE_PROVENANCE
      if (old_pitch != note.note) {
        note.prov_original_pitch = old_pitch;
        note.addTransformStep(TransformStepType::ScaleSnap, old_pitch, note.note, 0, 0);
      }
#endif
    }
  }
}

/// @brief Break up excessive consecutive same-pitch notes.
/// @param all_notes Notes to process (modified in place)
/// @param harmony Harmony context for finding safe alternative pitches
/// @param vocal_low Minimum vocal pitch
/// @param vocal_high Maximum vocal pitch
/// @param max_consecutive Maximum allowed consecutive same pitch (default: 4)
///
/// When more than max_consecutive notes have the same pitch, this function
/// alternates some notes to nearby chord tones to create melodic interest.
/// This is especially important for RhythmSync where collision avoidance
/// can cause long runs of the same pitch.
void breakConsecutiveSamePitch(std::vector<NoteEvent>& all_notes, const IHarmonyContext& harmony,
                                uint8_t vocal_low, uint8_t vocal_high, int max_consecutive = 4) {
  if (all_notes.size() < static_cast<size_t>(max_consecutive + 1)) return;

  // Sort by time first
  NoteTimeline::sortByStartTick(all_notes);

  size_t streak_start = 0;
  int streak_count = 1;
  uint8_t streak_pitch = all_notes[0].note;

  for (size_t i = 1; i <= all_notes.size(); ++i) {
    bool streak_continues = (i < all_notes.size() && all_notes[i].note == streak_pitch);

    if (streak_continues) {
      streak_count++;
    }

    // Process streak when it ends or at the last note
    if (!streak_continues || i == all_notes.size()) {
      if (streak_count > max_consecutive) {
        // Break up the streak: modify every other note starting from position max_consecutive
        for (size_t j = streak_start + static_cast<size_t>(max_consecutive); j < i; j += 2) {
          Tick tick = all_notes[j].start_tick;
          Tick duration = all_notes[j].duration;

          // Find nearby chord tones as alternatives
          auto chord_tones = harmony.getChordTonesAt(tick);
          if (chord_tones.empty()) continue;

          // Try to find a chord tone ±3 or ±4 semitones from streak_pitch
          int best_alt = -1;
          int best_dist = 100;
          for (int interval : {3, -3, 4, -4, 5, -5, 7, -7}) {
            int candidate = static_cast<int>(streak_pitch) + interval;
            if (candidate < static_cast<int>(vocal_low) || candidate > static_cast<int>(vocal_high)) continue;

            // Check if it's a chord tone or at least in scale
            int pc = candidate % 12;
            bool is_chord_tone = std::find(chord_tones.begin(), chord_tones.end(), pc) != chord_tones.end();
            bool is_scale = isScaleTone(pc);

            if (is_chord_tone) {
              // Verify no harsh collision
              if (harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(candidate), tick, duration, TrackRole::Vocal)) {
                int dist = std::abs(interval);
                if (dist < best_dist) {
                  best_dist = dist;
                  best_alt = candidate;
                }
              }
            } else if (is_scale && best_alt < 0) {
              // Fallback to scale tone if no safe chord tone found
              if (harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(candidate), tick, duration, TrackRole::Vocal)) {
                best_alt = candidate;
              }
            }
          }

          if (best_alt >= 0) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
            uint8_t old_pitch = all_notes[j].note;
#endif
            all_notes[j].note = static_cast<uint8_t>(best_alt);
#ifdef MIDISKETCH_NOTE_PROVENANCE
            all_notes[j].prov_original_pitch = old_pitch;
            all_notes[j].addTransformStep(TransformStepType::CollisionAvoid, old_pitch,
                                          all_notes[j].note, streak_pitch, 0);
#endif
          }
        }
      }

      // Reset for next potential streak
      if (i < all_notes.size()) {
        streak_start = i;
        streak_count = 1;
        streak_pitch = all_notes[i].note;
      }
    }
  }
}

/// @brief Apply pitch bend expressions to vocal track.
/// @param track Track to add pitch bends to
/// @param all_notes All notes for pitch bend application
/// @param params Generation parameters
/// @param rng Random number generator
/// @param sections Song sections for section-type aware vibrato (nullptr to skip)
void applyVocalPitchBendExpressions(MidiTrack& track, const std::vector<NoteEvent>& all_notes,
                                     const GeneratorParams& params, std::mt19937& rng,
                                     const std::vector<Section>* sections = nullptr) {
  VocalPhysicsParams physics = getVocalPhysicsParams(params.vocal_style);

  // Skip pitch bend entirely if scale is 0 (UltraVocaloid)
  if (params.vocal_attitude < VocalAttitude::Expressive || physics.pitch_bend_scale <= 0.0f) {
    return;
  }

  constexpr Tick kPhraseGapThreshold = TICKS_PER_BEAT;

  for (size_t note_idx = 0; note_idx < all_notes.size(); ++note_idx) {
    const auto& note = all_notes[note_idx];

    // Determine if this is a phrase start
    bool is_phrase_start = (note_idx == 0);
    if (note_idx > 0) {
      Tick prev_note_end = all_notes[note_idx - 1].start_tick + all_notes[note_idx - 1].duration;
      if (note.start_tick - prev_note_end >= kPhraseGapThreshold) {
        is_phrase_start = true;
      }
    }

    // Determine if this is a phrase end
    bool is_phrase_end = (note_idx == all_notes.size() - 1);
    if (note_idx + 1 < all_notes.size()) {
      Tick next_note_start = all_notes[note_idx + 1].start_tick;
      Tick this_note_end = note.start_tick + note.duration;
      if (next_note_start - this_note_end >= kPhraseGapThreshold) {
        is_phrase_end = true;
      }
    }

    // Scoop and fall probability based on attitude
    float scoop_prob = (params.vocal_attitude == VocalAttitude::Raw) ? 0.8f : 0.5f;
    float fall_prob = (params.vocal_attitude == VocalAttitude::Raw) ? 0.7f : 0.4f;
    scoop_prob *= physics.pitch_bend_scale;
    fall_prob *= physics.pitch_bend_scale;

    // Apply attack bend (scoop-up) at phrase starts
    if (is_phrase_start && note.duration >= TICK_EIGHTH && rng_util::rollProbability(rng, scoop_prob)) {
      int base_depth = (params.vocal_attitude == VocalAttitude::Raw) ? -40 : -25;
      int depth = static_cast<int>(base_depth * physics.pitch_bend_scale);
      if (depth != 0) {
        auto bends = PitchBendCurves::generateAttackBend(note.start_tick, depth, TICK_SIXTEENTH);
        for (const auto& bend : bends) {
          track.addPitchBend(bend.tick, bend.value);
        }
      }
    }

    // Apply fall-off at phrase ends
    if (is_phrase_end && note.duration >= TICK_HALF && rng_util::rollProbability(rng, fall_prob)) {
      int base_depth = (params.vocal_attitude == VocalAttitude::Raw) ? -100 : -60;
      int depth = static_cast<int>(base_depth * physics.pitch_bend_scale);
      if (depth != 0) {
        Tick note_end = note.start_tick + note.duration;
        auto bends = PitchBendCurves::generateFallOff(note_end, depth, TICK_EIGHTH);
        for (const auto& bend : bends) {
          track.addPitchBend(bend.tick, bend.value);
        }
        track.addPitchBend(note_end + TICK_SIXTEENTH, PitchBend::kCenter);
      }
    }

    // Apply vibrato to sustained notes
    constexpr Tick kVibratoMinDuration = TICKS_PER_BEAT / 2;
    constexpr Tick kVibratoDelay = TICKS_PER_BEAT / 4;
    if (note.duration >= kVibratoMinDuration && !is_phrase_end) {
      float vibrato_prob = (params.vocal_attitude == VocalAttitude::Raw) ? 0.7f : 0.5f;
      vibrato_prob *= physics.pitch_bend_scale;

      if (rng_util::rollProbability(rng, vibrato_prob)) {
        int base_vibrato_depth = (params.vocal_attitude == VocalAttitude::Raw) ? 25 : 15;
        int vibrato_depth = static_cast<int>(base_vibrato_depth * physics.pitch_bend_scale);
        float vibrato_rate = (params.vocal_attitude == VocalAttitude::Raw) ? 5.0f : 5.5f;

        // Section-type vibrato depth scaling: Chorus and Bridge get wider vibrato
        if (sections != nullptr) {
          for (const auto& sec : *sections) {
            if (note.start_tick >= sec.start_tick && note.start_tick < sec.endTick()) {
              if (sec.type == SectionType::Chorus) {
                vibrato_depth = static_cast<int>(vibrato_depth * 1.5f);
              } else if (sec.type == SectionType::Bridge) {
                vibrato_depth = static_cast<int>(vibrato_depth * 1.3f);
              }
              // Verse and other sections keep 1.0x depth
              break;
            }
          }
        }

        if (vibrato_depth > 0) {
          Tick vibrato_start = note.start_tick + kVibratoDelay;
          Tick vibrato_duration = note.duration - kVibratoDelay;

          if (vibrato_duration >= TICKS_PER_BEAT / 4) {
            auto vibrato_bends = PitchBendCurves::generateVibrato(
                vibrato_start, vibrato_duration, vibrato_depth, vibrato_rate, params.bpm);
            for (const auto& bend : vibrato_bends) {
              track.addPitchBend(bend.tick, bend.value);
            }
          }
        }
      }
    }

    // Portamento: pitch glide between consecutive close notes in same phrase
    if (note_idx + 1 < all_notes.size()) {
      const auto& next_note = all_notes[note_idx + 1];
      Tick this_end = note.start_tick + note.duration;
      Tick gap = (next_note.start_tick > this_end) ? (next_note.start_tick - this_end) : 0;

      int pitch_diff = static_cast<int>(next_note.note) - static_cast<int>(note.note);
      int abs_diff = std::abs(pitch_diff);

      // Conditions: interval 1-5 semitones, gap < eighth note, not a phrase boundary
      if (abs_diff > 0 && abs_diff <= 5 && gap < TICK_EIGHTH) {
        float portamento_prob = (params.vocal_attitude == VocalAttitude::Raw) ? 0.5f : 0.3f;
        portamento_prob *= physics.pitch_bend_scale;

        if (rng_util::rollProbability(rng, portamento_prob)) {
          // Glide from current pitch toward next pitch over last 16th of current note
          Tick glide_start = note.start_tick + note.duration - TICK_SIXTEENTH;
          if (glide_start > note.start_tick) {
            // Target bend value: pitch_diff semitones worth of pitch bend
            int16_t target_bend = static_cast<int16_t>(pitch_diff * PitchBend::kSemitone);
            // Clamp to valid pitch bend range
            target_bend = std::clamp(target_bend, PitchBend::kMin, PitchBend::kMax);

            // Generate smooth glide (4 steps over TICK_SIXTEENTH)
            constexpr int kGlideSteps = 4;
            Tick step_size = TICK_SIXTEENTH / kGlideSteps;
            for (int step = 0; step <= kGlideSteps; ++step) {
              float ratio = static_cast<float>(step) / static_cast<float>(kGlideSteps);
              int16_t bend_val = static_cast<int16_t>(target_bend * ratio);
              track.addPitchBend(glide_start + step * step_size, bend_val);
            }
            // Reset pitch bend at next note start
            track.addPitchBend(next_note.start_tick, PitchBend::kCenter);
          }
        }
      }
    }
  }
}

// ============================================================================
// Rhythm Lock Support
// ============================================================================

bool shouldLockVocalRhythm(const GeneratorParams& params) {
  // Rhythm lock is used for Orangestar style:
  // - RhythmSync paradigm (vocal syncs to drum grid)
  // - Locked riff policy (same rhythm throughout)
  if (params.paradigm != GenerationParadigm::RhythmSync) {
    return false;
  }
  // RiffPolicy::Locked is an alias for LockedContour, so check the underlying values
  uint8_t policy_value = static_cast<uint8_t>(params.riff_policy);
  // LockedContour=1, LockedPitch=2, LockedAll=3
  return policy_value >= 1 && policy_value <= 3;
}

// Check if rhythm lock should be per-section-type (for UltraVocaloid)
// UltraVocaloid needs different rhythms per section type (ballad verse + machine-gun chorus)
// but still wants consistency within the same section type
static bool shouldUsePerSectionTypeRhythmLock(const GeneratorParams& params) {
  return params.vocal_style == VocalStylePreset::UltraVocaloid;
}

// ============================================================================
// Inline Harmony-Aware Onset Skip Helpers
// ============================================================================

/// @brief Describes the desire for a long note at a given onset position.
struct LongNoteDesire {
  int max_skip;       ///< Maximum onsets to skip (0=normal 8th, 1-3=long note)
  float probability;  ///< Probability of attempting the skip (0.0-1.0)
};

/// @brief Check if any onset in the skip range falls on a bar head (beat 0).
///
/// When a long note skip consumes a bar-head onset (beat_in_bar ≈ 0.0), no note
/// is created at the downbeat and the melody appears to start late. Returns the
/// cap such that the bar-head onset becomes next_active (processed, not skipped).
static int barHeadSkipCap(size_t i, const std::vector<float>& onsets,
                          int max_skip) {
  constexpr float kEps = 0.01f;
  for (int s = 1; s <= max_skip; ++s) {
    size_t j = i + static_cast<size_t>(s);
    if (j >= onsets.size()) break;
    float beat_in_bar = std::fmod(onsets[j], 4.0f);
    if (beat_in_bar < kEps || beat_in_bar > 4.0f - kEps) {
      // Onset j is a bar head; cap so next_active = j (skip = j - i - 1)
      return static_cast<int>(j - i) - 1;
    }
  }
  return max_skip;  // No bar head in skip range
}

/// @brief Evaluate how much we want a long note at the current onset position.
///
/// Considers section type, phrase/section boundaries, bar alignment, and cooldown.
/// This replaces the pre-computed skip_indices approach, enabling pitch-aware decisions.
static LongNoteDesire evaluateLongNoteDesire(size_t i, const std::vector<float>& onsets,
                                              const Section& section,
                                              const std::set<float>& boundary_set,
                                              int onsets_since_long,
                                              uint16_t bpm = 120,
                                              const std::set<float>& phrase_start_beats = {}) {
  LongNoteDesire desire{0, 0.0f};
  size_t remaining = onsets.size() - i;

  // Cooldown: prevent consecutive long notes from destroying rhythmic feel.
  // Chorus/Drop allow shorter cooldown since they benefit from more sustained singing.
  // At fast tempos (>=150), reduce cooldown to allow more frequent long notes.
  int cooldown_threshold = (section.type == SectionType::Chorus ||
                            section.type == SectionType::Drop ||
                            section.type == SectionType::Bridge) ? 1 : 2;
  if (bpm >= 150) {
    // Chorus/Drop: keep minimum cooldown of 1 for rhythmic articulation
    if (section.type == SectionType::Chorus || section.type == SectionType::Drop) {
      cooldown_threshold = std::max(1, cooldown_threshold - 1);
    } else {
      cooldown_threshold = std::max(0, cooldown_threshold - 1);
    }
  }
  if (onsets_since_long < cooldown_threshold) {
    return desire;
  }

  // Short sections (< 4 onsets): only allow section-end skip
  if (onsets.size() < 4 && remaining > 1) {
    return desire;
  }

  // Section-dependent base parameters
  float base_prob = 0.0f;
  int base_max_skip = 0;
  int bar_interval = 4;

  switch (section.type) {
    case SectionType::Chorus:
    case SectionType::Drop:
      base_prob = 0.40f;
      base_max_skip = 2;
      bar_interval = 2;
      break;
    case SectionType::Bridge:
      base_prob = 0.50f;
      base_max_skip = 3;
      bar_interval = 2;
      break;
    case SectionType::B:
      base_prob = 0.35f;
      base_max_skip = 2;
      bar_interval = 2;
      break;
    case SectionType::A:
    default:
      base_prob = 0.25f;
      base_max_skip = 2;
      bar_interval = 3;
      break;
  }

  // BPM boost: at fast tempos, each onset is physically shorter so we need
  // more long notes to maintain natural vocal phrasing
  // Density-aware: dense onsets (< 0.8 beat spacing) already produce adequate
  // note lengths with base max_skip; skip boost only for sparse patterns
  float avg_onset_spacing = 1.0f;
  if (onsets.size() > 1) {
    avg_onset_spacing = (onsets.back() - onsets.front()) /
                        static_cast<float>(onsets.size() - 1);
  }

  float bpm_boost = 0.0f;
  int bpm_skip_boost = 0;
  if (bpm >= 150) {
    bpm_boost = 0.10f;
    if (avg_onset_spacing >= 1.0f) {
      bpm_skip_boost = 2;
    } else if (avg_onset_spacing >= 0.8f) {
      bpm_skip_boost = 1;
    }
  } else if (bpm >= 120) {
    bpm_boost = 0.05f;
    bpm_skip_boost = (avg_onset_spacing >= 0.8f) ? 1 : 0;
  }

  desire.max_skip = base_max_skip + bpm_skip_boost;
  desire.probability = base_prob + bpm_boost;

  // Cap max_skip to not consume all remaining onsets (keep at least 1 after)
  if (desire.max_skip >= static_cast<int>(remaining)) {
    desire.max_skip = static_cast<int>(remaining) - 1;
  }

  float beat = onsets[i];

  // Position-dependent overrides (highest priority first)

  // (1) Section-end: last 3 onsets get high skip desire
  if (remaining <= 3) {
    int section_end_skip = (section.type == SectionType::Chorus ||
                            section.type == SectionType::Drop) ? 3 : 2;
    desire.max_skip = std::max(desire.max_skip, section_end_skip);
    desire.probability = 0.95f;
    // Cap again
    if (desire.max_skip >= static_cast<int>(remaining)) {
      desire.max_skip = static_cast<int>(remaining) - 1;
    }
    return desire;
  }

  // (1.5) Phrase start: anchor note MUST be long for melodic grounding
  if (!phrase_start_beats.empty()) {
    constexpr float kTolerance = 0.1f;
    for (float ps : phrase_start_beats) {
      if (std::abs(beat - ps) < kTolerance) {
        desire.max_skip = std::max(desire.max_skip, 1);
        desire.probability = 1.0f;
        // Cap
        if (desire.max_skip >= static_cast<int>(remaining))
          desire.max_skip = static_cast<int>(remaining) - 1;
        return desire;
      }
    }
  }

  // (2) Near phrase boundary: 1 or 2 onsets before a boundary → always sustain.
  // Probability is 1.0 because phrase-end notes MUST be longer to avoid
  // "short note at phrase end" artifacts. If harmony rejects the skip, the note
  // stays short as a last resort, but we always attempt.
  bool near_boundary = false;
  {
    constexpr float kEps = 0.01f;
    float look_end = (i + 2 < onsets.size()) ? onsets[i + 2]
                   : (i + 1 < onsets.size()) ? onsets[i + 1]
                   : onsets[i] + 4.0f;
    for (float boundary : boundary_set) {
      if (boundary > onsets[i] + kEps && boundary <= look_end + kEps) {
        near_boundary = true;
        break;
      }
    }
  }
  if (near_boundary) {
    desire.max_skip = std::max(desire.max_skip, 2);
    desire.probability = 1.0f;  // Always attempt at phrase boundaries
    if (desire.max_skip >= static_cast<int>(remaining)) {
      desire.max_skip = static_cast<int>(remaining) - 1;
    }
    return desire;
  }

  // (3) Bar-aligned long tones: near beat 3.0-3.5 at bar_interval spacing
  float beat_in_bar = std::fmod(beat, 4.0f);
  int bar_index = static_cast<int>(beat / 4.0f);
  if (beat_in_bar >= 2.5f && beat_in_bar <= 3.6f &&
      (bar_index % bar_interval == (bar_interval - 1) || bar_index % 2 == 1)) {
    desire.max_skip = std::max(desire.max_skip, 2);
    desire.probability = std::min(base_prob * 1.5f, 0.85f);
    return desire;
  }

  // (4) Before natural rhythm gap: if a natural gap (>= 1 beat) exists in the
  // onset pattern within the next 4 onsets, create a long note to sustain into
  // the gap. This addresses phrase-end resolution regardless of boundary alignment.
  // Evaluated before strong-beat/spacing conditions since gap proximity is the
  // strongest indicator of where a long note is needed.
  for (size_t j = i + 1; j < std::min(i + 5, onsets.size()); ++j) {
    float gap = onsets[j] - onsets[j - 1];
    if (gap >= 1.0f) {  // Natural gap >= 1 beat in onset pattern
      desire.max_skip = std::max(desire.max_skip, static_cast<int>(j - i - 1) + 1);
      desire.probability = 0.95f;
      if (desire.max_skip >= static_cast<int>(remaining)) {
        desire.max_skip = static_cast<int>(remaining) - 1;
      }
      return desire;
    }
  }

  // (5) Strong beat positions (beat 0 or 2) with interval check
  if ((beat_in_bar < 0.1f || std::abs(beat_in_bar - 2.0f) < 0.1f) &&
      bar_index % bar_interval == 0 && onsets_since_long >= 3) {
    desire.max_skip = std::max(desire.max_skip, 1);
    desire.probability = base_prob;
    return desire;
  }

  // (6) Spacing-based fallback: if too many consecutive short notes,
  // force a long note attempt. At fast tempos, trigger earlier to prevent
  // long runs of uniform short notes.
  int spacing_threshold = (bpm >= 150) ? 3 : 4;
  if (onsets_since_long >= spacing_threshold) {
    desire.max_skip = std::max(desire.max_skip, 1);
    desire.probability = 0.85f;
    return desire;
  }

  return desire;
}

/// @brief Compute the maximum safe skip count given a chosen pitch.
///
/// Checks both chord boundary safety AND inter-track collision safety.
/// Brief passing dissonance from base_duration notes is acceptable, but
/// note extension must not create sustained dissonance with other tracks.
static int computeSafeSkipCount(uint8_t pitch, Tick tick, const std::vector<float>& onsets,
                                size_t i, int max_desired, const Section& section,
                                const IHarmonyContext& harmony) {
  Tick section_end = section.endTick();

  // Don't skip over bar-head onsets (downbeats) — ensures every bar starts
  // with a note, preventing melody from appearing to start late.
  max_desired = barHeadSkipCap(i, onsets, max_desired);

  for (int skip = max_desired; skip >= 1; --skip) {
    size_t next_active = i + 1 + static_cast<size_t>(skip);
    Tick extended_end;
    if (next_active < onsets.size()) {
      extended_end = section.start_tick +
                     static_cast<Tick>(onsets[next_active] * TICKS_PER_BEAT);
    } else {
      extended_end = section_end;
    }
    if (extended_end <= tick) continue;
    Tick extended_dur = extended_end - tick;

    // Chord boundary safety: reject if pitch is non-chord-tone or avoid-note
    // in the next chord AND the safe duration doesn't cover enough of the skip.
    auto info = harmony.analyzeChordBoundary(pitch, tick, extended_dur);
    if (info.safety == CrossBoundarySafety::NonChordTone ||
        info.safety == CrossBoundarySafety::AvoidNote) {
      Tick min_useful = (i + static_cast<size_t>(skip) < onsets.size())
          ? section.start_tick +
            static_cast<Tick>(onsets[i + static_cast<size_t>(skip)] * TICKS_PER_BEAT) - tick
          : extended_dur;
      if (info.safe_duration < min_useful) {
        continue;  // This skip count crosses into unsafe chord territory
      }
    }

    // Inter-track collision check: prevent extension from creating sustained
    // dissonance with other tracks (e.g., Vocal D5 extended over Motif C5).
    // Brief passing dissonance from base_duration notes is unaffected.
    if (!harmony.isConsonantWithOtherTracks(pitch, tick, extended_dur, TrackRole::Vocal)) {
      continue;
    }

    return skip;
  }

  return 0;  // No safe extension possible
}

/// @brief Onset contour info for phrase-aware pitch selection.
struct OnsetContourInfo {
  ContourType contour = ContourType::Ascending;
  float phrase_position = 0.0f;  // 0.0-1.0
};

/// @brief Apply phrase contour direction to pitch selection hints.
static void applyContourToHints(const OnsetContourInfo& ci, PitchSelectionHints& hints) {
  hints.phrase_position = ci.phrase_position;
  switch (ci.contour) {
    case ContourType::Ascending:  hints.contour_direction = 1;  break;
    case ContourType::Descending: hints.contour_direction = -1; break;
    case ContourType::Peak:
      hints.contour_direction = (ci.phrase_position < 0.6f) ? 1 : -1;
      break;
    case ContourType::Valley:
      hints.contour_direction = (ci.phrase_position < 0.4f) ? -1 : 1;
      break;
    default: break;  // Plateau: keep direction_inertia based value
  }
}

/**
 * @brief Generate a single pitch sequence candidate for locked rhythm evaluation.
 * @param rhythm Locked rhythm pattern to use
 * @param section Current section
 * @param designer Melody designer for pitch selection
 * @param harmony Harmony context
 * @param ctx Section context
 * @param rng Random number generator
 * @param phrase_plan Optional pre-planned phrase boundaries (nullptr = use detection fallback)
 * @return Generated notes with locked rhythm and new pitches
 */
static std::vector<NoteEvent> generateLockedRhythmCandidate(
    const CachedRhythmPattern& rhythm, const Section& section, MelodyDesigner& /*designer*/,
    const IHarmonyContext& harmony, const MelodyDesigner::SectionContext& ctx, std::mt19937& rng,
    const PhrasePlan* phrase_plan = nullptr) {
  std::vector<NoteEvent> notes;
  uint8_t section_beats = section.bars * 4;

  // Get scaled onsets and durations for this section's length
  auto onsets = rhythm.getScaledOnsets(section_beats);
  auto durations = rhythm.getScaledDurations(section_beats);

  if (onsets.empty()) {
    return notes;
  }

  // Ensure durations matches onsets size
  while (durations.size() < onsets.size()) {
    durations.push_back(0.5f);  // Default half-beat duration
  }

  // Use PhrasePlan boundaries if available, otherwise fall back to detection
  std::set<float> boundary_set;
  if (phrase_plan != nullptr && !phrase_plan->phrases.empty()) {
    // Convert planned phrase start ticks to beat positions relative to section
    for (const auto& planned : phrase_plan->phrases) {
      if (planned.phrase_index > 0) {  // Skip first phrase (no boundary before it)
        float beat = static_cast<float>(planned.start_tick - section.start_tick) / TICKS_PER_BEAT;
        boundary_set.insert(beat);
      }
    }
  } else {
    auto boundaries = detectPhraseBoundariesFromRhythm(rhythm, section.type);
    boundary_set.insert(boundaries.begin(), boundaries.end());
  }

  // Build phrase start beat positions for long-note anchoring
  std::set<float> phrase_start_beats;
  if (phrase_plan != nullptr) {
    for (const auto& planned : phrase_plan->phrases) {
      float beat = static_cast<float>(planned.start_tick - section.start_tick) / TICKS_PER_BEAT;
      phrase_start_beats.insert(beat);
    }
  }

  // Determine breath duration based on section type and mood
  bool is_ballad = MoodClassification::isBallad(ctx.mood);
  Tick breath_duration = getBreathDuration(section.type, is_ballad, false, ctx.bpm);

  // Gate ratio by section type for legato control
  float gate_ratio;
  switch (section.type) {
    case SectionType::Chorus:
    case SectionType::Drop:
      gate_ratio = 0.96f;
      break;
    case SectionType::B:
      gate_ratio = 0.94f;
      break;
    case SectionType::Bridge:
      gate_ratio = 0.96f;
      break;
    case SectionType::A:
    default:
      gate_ratio = 0.90f;
      break;
  }

  // Phrase-end minimum duration by section type.
  // At fast tempos, cadential notes need more ticks to feel "sustained" (~500ms).
  constexpr float kMinPhraseEndSeconds = 0.5f;
  Tick bpm_phrase_end_min = static_cast<Tick>(
      kMinPhraseEndSeconds * ctx.bpm * TICKS_PER_BEAT / 60.0f);

  Tick phrase_end_min;
  switch (section.type) {
    case SectionType::Chorus:
    case SectionType::Drop:
      phrase_end_min = std::max(TICK_HALF, bpm_phrase_end_min);
      break;
    case SectionType::B:
    case SectionType::Bridge:
      phrase_end_min = std::max(TICK_QUARTER + TICK_EIGHTH, bpm_phrase_end_min);
      break;
    default:
      phrase_end_min = std::max(TICK_QUARTER, bpm_phrase_end_min);
      break;
  }

  // Build onset→contour mapping from PhrasePlan
  std::vector<OnsetContourInfo> onset_contours(onsets.size());
  if (phrase_plan != nullptr && !phrase_plan->phrases.empty()) {
    for (size_t oi = 0; oi < onsets.size(); ++oi) {
      Tick onset_tick = section.start_tick + static_cast<Tick>(onsets[oi] * TICKS_PER_BEAT);
      for (const auto& ph : phrase_plan->phrases) {
        if (onset_tick >= ph.start_tick && onset_tick < ph.end_tick) {
          onset_contours[oi].contour = ph.contour;
          Tick dur = ph.end_tick - ph.start_tick;
          if (dur > 0)
            onset_contours[oi].phrase_position =
                static_cast<float>(onset_tick - ph.start_tick) / static_cast<float>(dur);
          break;
        }
      }
    }
  }

  uint8_t prev_pitch = (ctx.vocal_low + ctx.vocal_high) / 2;  // Start at center
  int direction_inertia = 0;  // Track melodic direction momentum
  int same_pitch_streak = 0;  // Track consecutive same pitch for progressive penalty
  int onsets_since_long = 100;  // Start high so first onset can be long if desired

  // Whether run-based onset map is active: breath gaps and density thinning
  // are already handled by buildRunBasedOnsetMap() in this mode.
  bool run_based_active =
      (phrase_plan != nullptr && !phrase_plan->phrases.empty() &&
       ctx.paradigm == GenerationParadigm::RhythmSync &&
       ctx.motif_params != nullptr &&
       ctx.motif_params->rhythm_template != MotifRhythmTemplate::None &&
       ctx.vocal_style != VocalStylePreset::UltraVocaloid);

  size_t i = 0;
  while (i < onsets.size()) {
    float beat = onsets[i];

    // Insert breath at phrase boundaries by shortening previous note.
    // When run-based onset map is active, breath gaps are already handled by
    // buildRunBasedOnsetMap(), so skip retroactive breath insertion.
    bool breath_handled_by_plan = run_based_active;
    if (i > 0 && boundary_set.count(beat) > 0 && !notes.empty() && !breath_handled_by_plan) {
      Tick min_duration = TICK_SIXTEENTH;
      if (notes.back().duration > breath_duration + min_duration) {
        notes.back().duration -= breath_duration;
      }
    }

    Tick tick = section.start_tick + static_cast<Tick>(beat * TICKS_PER_BEAT);
    Tick section_end = section.endTick();

    // Compute base available_span (to next onset)
    size_t next_idx = i + 1;
    Tick immediate_next = (next_idx < onsets.size())
        ? section.start_tick + static_cast<Tick>(onsets[next_idx] * TICKS_PER_BEAT)
        : section_end;
    Tick base_span = (immediate_next > tick) ? (immediate_next - tick) : TICK_SIXTEENTH;

    Tick base_duration = static_cast<Tick>(base_span * gate_ratio);
    base_duration = std::max(base_duration, TICK_SIXTEENTH);

    // ======================================================================
    // Evaluate long-note desire BEFORE pitch selection.
    // For high-probability positions (phrase-end, section-end), we use the
    // extended duration for pitch candidate lookup so the chosen pitch is
    // guaranteed safe for the full extension.
    // ======================================================================
    // When buildRunBasedOnsetMap has already controlled density (RhythmSync
    // with PhrasePlan), skip evaluateLongNoteDesire to prevent double-thinning.
    bool onset_pre_thinned = run_based_active;

    LongNoteDesire desire{0, 0.0f};
    if (!onset_pre_thinned) {
      desire = evaluateLongNoteDesire(i, onsets, section, boundary_set, onsets_since_long, ctx.bpm,
                                       phrase_start_beats);
    }

    Tick candidate_duration = base_duration;
    bool using_extended_candidates = false;
    if (desire.max_skip > 0 && desire.probability >= 0.3f) {
      // For likely-long notes, compute extended duration for pitch selection
      size_t ext_active = std::min(i + 1 + static_cast<size_t>(desire.max_skip),
                                   onsets.size());
      Tick ext_onset = (ext_active < onsets.size())
          ? section.start_tick + static_cast<Tick>(onsets[ext_active] * TICKS_PER_BEAT)
          : section_end;
      if (ext_onset > tick) {
        candidate_duration = ext_onset - tick;
        using_extended_candidates = true;
      }
    }

    // Get chord at this position for provenance tracking
    [[maybe_unused]] int8_t chord_degree = harmony.getChordDegreeAt(tick);

    // Apply pitch safety check to avoid collisions with other tracks.
    // When using extended candidates, fetch with the longer duration so the
    // selected pitch is safe across the full extension.
    auto candidates = getSafePitchCandidates(harmony, prev_pitch, tick, candidate_duration,
                                              TrackRole::Vocal, ctx.vocal_low, ctx.vocal_high,
                                              PitchPreference::Default, 10);

    // Fallback: if extended search yields no candidates, try with base duration
    if (candidates.empty() && using_extended_candidates) {
      candidates = getSafePitchCandidates(harmony, prev_pitch, tick, base_duration,
                                          TrackRole::Vocal, ctx.vocal_low, ctx.vocal_high,
                                          PitchPreference::Default, 10);
      desire.max_skip = 0;  // Can't extend with any pitch
      using_extended_candidates = false;
    }

    // Prefer diatonic (scale tone) candidates for vocal track.
    // Non-diatonic pitches (e.g. F# in C major) can appear in collision-safe
    // candidates but sound out of place in pop vocal melodies.
    {
      auto it = std::remove_if(candidates.begin(), candidates.end(),
                               [](const PitchCandidate& c) { return !c.is_scale_tone; });
      if (it != candidates.begin()) {
        // At least one diatonic candidate exists - remove non-diatonic ones
        candidates.erase(it, candidates.end());
      }
    }

    if (candidates.empty()) {
      ++i;
      onsets_since_long++;
      continue;  // No safe pitch available for this onset
    }

    // Select pitch with probabilistic element to ensure variety across candidates
    uint8_t safe_pitch;
    Tick hint_duration = using_extended_candidates ? candidate_duration : base_duration;

    // Force movement after 3 consecutive same pitches
    if (same_pitch_streak >= 3 && candidates.size() > 1) {
      std::vector<uint8_t> different_pitches;
      for (const auto& c : candidates) {
        if (c.pitch != prev_pitch) {
          different_pitches.push_back(c.pitch);
        }
      }
      if (!different_pitches.empty()) {
        safe_pitch = rng_util::selectRandom(rng, different_pitches);
      } else {
        PitchSelectionHints hints;
        hints.prev_pitch = static_cast<int8_t>(prev_pitch);
        hints.note_duration = hint_duration;
        hints.tessitura_center = ctx.tessitura.center;
        hints.same_pitch_streak = static_cast<int8_t>(same_pitch_streak);
        if (direction_inertia > 0) hints.contour_direction = 1;
        else if (direction_inertia < 0) hints.contour_direction = -1;
        // Apply phrase contour from PhrasePlan
        if (phrase_plan != nullptr && i < onset_contours.size()) {
          applyContourToHints(onset_contours[i], hints);
        }
        safe_pitch = selectBestCandidate(candidates, prev_pitch, hints);
      }
    } else {
      PitchSelectionHints hints;
      hints.prev_pitch = static_cast<int8_t>(prev_pitch);
      hints.note_duration = hint_duration;
      hints.tessitura_center = ctx.tessitura.center;
      hints.same_pitch_streak = static_cast<int8_t>(same_pitch_streak);
      if (direction_inertia > 0) hints.contour_direction = 1;
      else if (direction_inertia < 0) hints.contour_direction = -1;
      // Apply phrase contour from PhrasePlan
      if (phrase_plan != nullptr && i < onset_contours.size()) {
        const auto& ci = onset_contours[i];
        hints.phrase_position = ci.phrase_position;
        switch (ci.contour) {
          case ContourType::Ascending:  hints.contour_direction = 1;  break;
          case ContourType::Descending: hints.contour_direction = -1; break;
          case ContourType::Peak:
            hints.contour_direction = (ci.phrase_position < 0.6f) ? 1 : -1;
            break;
          case ContourType::Valley:
            hints.contour_direction = (ci.phrase_position < 0.4f) ? -1 : 1;
            break;
          default: break;  // Plateau: keep direction_inertia based value
        }
      }

      // Add randomness: 70% best candidate, 30% random from top 3
      if (candidates.size() >= 3 && rng_util::rollProbability(rng, 0.3f)) {
        size_t rand_idx = rng_util::rollRange(rng, 0,
            static_cast<int>(std::min(static_cast<size_t>(2), candidates.size() - 1)));
        safe_pitch = candidates[rand_idx].pitch;
      } else {
        safe_pitch = selectBestCandidate(candidates, prev_pitch, hints);
      }
    }

    // ======================================================================
    // Compute actual skips with the chosen pitch
    // ======================================================================
    int actual_skips = 0;
    if (desire.max_skip > 0 && rng_util::rollProbability(rng, desire.probability)) {
      actual_skips = computeSafeSkipCount(
          safe_pitch, tick, onsets, i, desire.max_skip, section, harmony);
    }

    // Compute actual next_onset and available_span based on skips
    size_t next_active = i + 1 + static_cast<size_t>(actual_skips);
    Tick next_onset;
    bool is_last_note;
    if (next_active < onsets.size()) {
      next_onset = section.start_tick +
                   static_cast<Tick>(onsets[next_active] * TICKS_PER_BEAT);
      is_last_note = false;
    } else {
      next_onset = section_end;
      is_last_note = true;
    }
    Tick available_span = (next_onset > tick) ? (next_onset - tick) : TICK_SIXTEENTH;

    // Determine if this is a phrase-end note.
    // Use range-based check: any boundary between current onset and next active
    // onset triggers phrase-end handling (boundaries may not align exactly with onsets).
    bool is_phrase_end = false;
    if (!is_last_note) {
      float current_beat = onsets[i];
      float look_ahead = (next_active < onsets.size()) ? onsets[next_active]
                                                        : section_beats;
      constexpr float kEps = 0.01f;
      for (float boundary : boundary_set) {
        if (boundary > current_beat + kEps && boundary <= look_ahead + kEps) {
          is_phrase_end = true;
          break;
        }
      }
    }

    // Compute final duration
    Tick duration;
    if (is_last_note) {
      duration = section_end - tick;
    } else if (is_phrase_end) {
      // Phrase-end note: sustain with breath gap before next phrase
      Tick breath_gap = breath_duration;
      if (available_span > breath_gap + TICK_SIXTEENTH) {
        duration = available_span - breath_gap;
      } else {
        // Very short span: use full span with gate ratio, no room for breath
        duration = static_cast<Tick>(available_span * gate_ratio);
      }
      duration = std::max(duration, phrase_end_min);
      if (tick + duration > next_onset) {
        duration = next_onset - tick;
      }
    } else {
      // Same pitch as previous: legato (no gap) to avoid unnatural micro-splits.
      // In singing, consecutive same-pitch notes are sustained as one long note.
      if (safe_pitch == prev_pitch) {
        duration = available_span;
      } else {
        duration = static_cast<Tick>(available_span * gate_ratio);
      }
      duration = std::max(duration, TICK_SIXTEENTH);
      if (tick + duration > next_onset) {
        duration = next_onset - tick;
      }
    }

    // Note: Track collision clip (getMaxSafeEnd) is intentionally omitted for
    // the final duration. In RhythmSync, the Motif plays dense 8th-note patterns
    // and brief passing dissonance with a sustained vocal note is musically normal.
    // Extension safety is handled by computeSafeSkipCount() which checks both
    // chord boundary AND inter-track collision before allowing note extension.

    // Update direction inertia based on movement
    int movement = static_cast<int>(safe_pitch) - static_cast<int>(prev_pitch);
    if (movement > 0) {
      direction_inertia = std::min(direction_inertia + 1, 3);
      same_pitch_streak = 0;
    } else if (movement < 0) {
      direction_inertia = std::max(direction_inertia - 1, -3);
      same_pitch_streak = 0;
    } else {
      if (direction_inertia > 0) direction_inertia--;
      if (direction_inertia < 0) direction_inertia++;
      same_pitch_streak++;
    }

    // Calculate velocity: use Motif template accent pattern if available (RhythmSync),
    // otherwise fall back to beat-position based velocity.
    uint8_t velocity = 80;
    bool accent_applied = false;
    if (ctx.paradigm == GenerationParadigm::RhythmSync) {
      const auto& motif_params = ctx.motif_params;
      if (motif_params != nullptr &&
          motif_params->rhythm_template != MotifRhythmTemplate::None) {
        const auto& tmpl_config =
            motif_detail::getTemplateConfig(motif_params->rhythm_template);
        float beat_in_bar = std::fmod(beat, 4.0f);
        float best_dist = 100.0f;
        int best_idx = -1;
        for (uint8_t ti = 0; ti < tmpl_config.note_count; ++ti) {
          if (tmpl_config.beat_positions[ti] < 0) break;
          float dist = std::abs(beat_in_bar - tmpl_config.beat_positions[ti]);
          if (dist < best_dist) {
            best_dist = dist;
            best_idx = static_cast<int>(ti);
          }
        }
        if (best_idx >= 0 && best_dist < 0.2f) {
          float accent = tmpl_config.accent_weights[best_idx];
          velocity = static_cast<uint8_t>(75 + accent * 20.0f);
          accent_applied = true;
        }
      }
    }
    if (!accent_applied) {
      float beat_in_bar = std::fmod(beat, 4.0f);
      if (beat_in_bar < 0.1f || std::abs(beat_in_bar - 2.0f) < 0.1f) {
        velocity = 95;  // Strong beats
      } else if (std::abs(beat_in_bar - 1.0f) < 0.1f ||
                 std::abs(beat_in_bar - 3.0f) < 0.1f) {
        velocity = 85;  // Medium beats
      }
    }

    NoteEvent note = createNoteWithoutHarmony(tick, duration, safe_pitch, velocity);
#ifdef MIDISKETCH_NOTE_PROVENANCE
    note.prov_source = static_cast<uint8_t>(NoteSource::MelodyPhrase);
    note.prov_chord_degree = chord_degree;
    note.prov_lookup_tick = tick;
    note.prov_original_pitch = safe_pitch;
#endif
    notes.push_back(note);
    prev_pitch = safe_pitch;

    // Advance: skip consumed onsets
    onsets_since_long = (actual_skips > 0) ? 0 : onsets_since_long + 1;
    i += 1 + static_cast<size_t>(actual_skips);
  }

  // ======================================================================
  // Post-process: ensure phrase-end resolution.
  // Scan for phrase boundaries (gap >= TICK_EIGHTH between notes). If the
  // tail (last 2 beats, matching analyzer criterion) lacks a sustained note
  // (>= 1 beat), merge 2-3 adjacent notes within the tail into one longer
  // note. The phrase boundary gap is preserved.
  // ======================================================================
  std::set<size_t> indices_to_remove;
  for (size_t ni = 1; ni < notes.size(); ++ni) {
    Tick gap = notes[ni].start_tick - (notes[ni - 1].start_tick + notes[ni - 1].duration);
    if (gap < TICK_EIGHTH) continue;  // Not a phrase boundary

    // Found phrase boundary before notes[ni].
    Tick phrase_end_tick = notes[ni - 1].start_tick + notes[ni - 1].duration;
    Tick tail_start = (phrase_end_tick > TICKS_PER_BEAT * 2)
        ? (phrase_end_tick - TICKS_PER_BEAT * 2) : 0;

    // Find tail note indices
    size_t tail_begin = ni;
    for (size_t k = ni; k > 0; --k) {
      if (notes[k - 1].start_tick < tail_start) break;
      tail_begin = k - 1;
    }

    // Check if tail already has a sustained note
    bool has_sustained = false;
    for (size_t k = tail_begin; k < ni; ++k) {
      if (notes[k].duration >= TICKS_PER_BEAT) {
        has_sustained = true;
        break;
      }
    }
    if (has_sustained) continue;

    // No sustained note in tail. Merge within the tail: find 2-3 adjacent
    // notes that, when combined, reach >= TICKS_PER_BEAT. Extend the first
    // note of the group to cover the others, and remove the rest.
    bool merged = false;
    for (size_t start = tail_begin; start + 1 < ni && !merged; ++start) {
      // Try merging 2 then 3 notes from 'start'
      for (size_t count = 2; count <= 3 && start + count <= ni; ++count) {
        size_t end_idx = start + count - 1;  // Last note in merge group
        // Extend first note to cover last note's onset + its original gate ratio
        Tick extend_to;
        if (end_idx < ni - 1) {
          // Not the last note before gap: extend to next note's onset with gate ratio
          extend_to = notes[end_idx + 1].start_tick;
          Tick ext_span = extend_to - notes[start].start_tick;
          extend_to = notes[start].start_tick + static_cast<Tick>(ext_span * gate_ratio);
        } else {
          // Last note before gap: extend within available span (keep gap)
          extend_to = notes[ni].start_tick - TICK_SIXTEENTH;
        }
        Tick new_dur = (extend_to > notes[start].start_tick)
            ? (extend_to - notes[start].start_tick) : notes[start].duration;
        if (new_dur >= TICKS_PER_BEAT) {
          notes[start].duration = new_dur;
          for (size_t rm = start + 1; rm <= end_idx; ++rm) {
            indices_to_remove.insert(rm);
          }
          merged = true;
          break;
        }
      }
    }
  }
  // Remove marked notes in reverse order to preserve indices
  for (auto it = indices_to_remove.rbegin(); it != indices_to_remove.rend(); ++it) {
    notes.erase(notes.begin() + static_cast<ptrdiff_t>(*it));
  }

  return notes;
}

/**
 * @brief Generate notes using locked rhythm with evaluation and candidate selection.
 *
 * This is the improved version that addresses the melodic quality issues:
 * 1. Generates multiple candidates (20) instead of single deterministic output
 * 2. Evaluates each candidate using MelodyEvaluator
 * 3. Selects best candidate probabilistically
 *
 * @param rhythm Locked rhythm pattern to use
 * @param section Current section
 * @param designer Melody designer for pitch selection
 * @param harmony Harmony context
 * @param ctx Section context
 * @param rng Random number generator
 * @param phrase_plan Optional pre-planned phrase boundaries (nullptr = use detection fallback)
 * @return Best-scoring candidate notes
 */
static std::vector<NoteEvent> generateLockedRhythmWithEvaluation(
    const CachedRhythmPattern& rhythm, const Section& section, MelodyDesigner& designer,
    const IHarmonyContext& harmony, const MelodyDesigner::SectionContext& ctx, std::mt19937& rng,
    const PhrasePlan* phrase_plan = nullptr) {

  constexpr int kCandidateCount = 20;  // 1/5 of normal mode (100) for performance

  // Generate multiple candidates
  std::vector<std::pair<std::vector<NoteEvent>, float>> candidates;
  candidates.reserve(static_cast<size_t>(kCandidateCount));

  for (int i = 0; i < kCandidateCount; ++i) {
    std::vector<NoteEvent> melody = generateLockedRhythmCandidate(
        rhythm, section, designer, harmony, ctx, rng, phrase_plan);

    if (melody.empty()) {
      continue;
    }

    // Evaluate the candidate
    // Style evaluation: positive features
    MelodyScore style_score = MelodyEvaluator::evaluate(melody, harmony);
    float style_total = style_score.total();  // Use simple average

    // Culling evaluation: penalty-based
    Tick phrase_duration = section.endTick() - section.start_tick;
    float culling_score = MelodyEvaluator::evaluateForCulling(
        melody, harmony, phrase_duration, ctx.vocal_style);

    // GlobalMotif bonus if available
    float motif_bonus = 0.0f;
    if (designer.getCachedGlobalMotif().has_value() &&
        designer.getCachedGlobalMotif()->isValid()) {
      motif_bonus = melody::evaluateWithGlobalMotif(
          melody, *designer.getCachedGlobalMotif());
    }

    // Combined score: 35% style, 40% culling, 25% motif
    // Higher motif weight strengthens RhythmSync "riff addiction" quality
    float combined_score = style_total * 0.35f + culling_score * 0.40f + motif_bonus * 0.25f;

    candidates.emplace_back(std::move(melody), combined_score);
  }

  if (candidates.empty()) {
    // Fallback: generate single candidate without evaluation
    return generateLockedRhythmCandidate(rhythm, section, designer, harmony, ctx, rng, phrase_plan);
  }

  // Sort by score (highest first)
  std::sort(candidates.begin(), candidates.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

  // Keep top half
  size_t keep_count = std::max(static_cast<size_t>(1), candidates.size() / 2);

  // Weighted probabilistic selection from top candidates
  float total_weight = 0.0f;
  for (size_t i = 0; i < keep_count; ++i) {
    total_weight += candidates[i].second;
  }

  if (total_weight > 0.0f) {
    float roll = rng_util::rollFloat(rng, 0.0f, total_weight);
    float cumulative = 0.0f;
    for (size_t i = 0; i < keep_count; ++i) {
      cumulative += candidates[i].second;
      if (roll <= cumulative) {
        return std::move(candidates[i].first);
      }
    }
  }

  // Fallback: return best candidate
  return std::move(candidates[0].first);
}

// =============================================================================
// Extracted sub-methods for doGenerateFullTrack decomposition
// =============================================================================

MelodyDesigner::SectionContext VocalGenerator::buildSectionContext(
    const Section& section, const GeneratorParams& params,
    const Song& song, const TessituraRange& tessitura,
    uint8_t vocal_low, uint8_t vocal_high, int8_t chord_degree,
    int occurrence, const DrumGrid* drum_grid,
    const MelodyDesigner& designer) const {
  MelodyDesigner::SectionContext sctx;
  sctx.section_type = section.type;
  sctx.section_start = section.start_tick;
  sctx.section_end = section.endTick();
  sctx.section_bars = section.bars;
  sctx.chord_degree = chord_degree;
  sctx.key_offset = 0;  // Always C major internally
  sctx.tessitura = tessitura;
  sctx.vocal_low = vocal_low;
  sctx.vocal_high = vocal_high;
  sctx.mood = params.mood;  // For harmonic rhythm alignment
  sctx.bpm = params.bpm;
  // Apply section's density_percent to density modifier (with SectionModifier)
  float base_density = getDensityModifier(section.type, params.melody_params);
  uint8_t effective_density = section.getModifiedDensity(section.density_percent);
  float density_factor = effective_density / 100.0f;
  sctx.density_modifier = base_density * density_factor;
  sctx.thirtysecond_ratio = getThirtysecondRatio(section.type, params.melody_params);
  sctx.consecutive_same_note_prob =
      getConsecutiveSameNoteProb(section.type, params.melody_params);
  sctx.disable_vowel_constraints = params.melody_params.disable_vowel_constraints;
  sctx.disable_breathing_gaps = params.melody_params.disable_breathing_gaps;
  // Wire StyleMelodyParams zombie parameters to SectionContext
  sctx.chorus_long_tones = params.melody_params.chorus_long_tones;
  sctx.allow_bar_crossing = params.melody_params.allow_bar_crossing;
  sctx.min_note_division = params.melody_params.min_note_division;
  // High-energy idol at fast BPM: allow 16th notes (enables run_window effect)
  // min_note_division: higher = finer allowed (8=eighth, 16=sixteenth, 32=thirty-second)
  if (params.bpm >= 145 && isHighEnergyVocalStyle(params.vocal_style) &&
      sctx.min_note_division > 0 && sctx.min_note_division < 16) {
    sctx.min_note_division = 16;
  }
  sctx.tension_usage = params.melody_params.tension_usage;
  sctx.syncopation_prob = params.melody_params.syncopation_prob;
  if (params.melody_long_note_ratio_override) {
    sctx.long_note_ratio_override = params.melody_params.long_note_ratio;
  }
  sctx.phrase_length_bars = params.melody_params.phrase_length_bars;
  // allow_unison_repeat: when false, hard-disable consecutive same notes
  if (!params.melody_params.allow_unison_repeat) {
    sctx.consecutive_same_note_prob = 0.0f;
  }
  // note_density: apply as additional multiplier to density_modifier
  sctx.density_modifier *= params.melody_params.note_density;
  sctx.vocal_attitude = params.vocal_attitude;
  sctx.hook_intensity = params.hook_intensity;  // For HookSkeleton selection
  // RhythmSync support
  sctx.paradigm = params.paradigm;
  sctx.drum_grid = drum_grid;
  // Motif template for accent-linked velocity (RhythmSync)
  if (params.paradigm == GenerationParadigm::RhythmSync) {
    sctx.motif_params = &params.motif;
  }
  // Behavioral Loop support
  sctx.addictive_mode = params.addictive_mode;
  // Vocal groove feel for syncopation control
  sctx.vocal_groove = params.vocal_groove;
  // Syncopation enable flag
  sctx.enable_syncopation = params.enable_syncopation;
  // Drive feel for timing and syncopation modulation
  sctx.drive_feel = params.drive_feel;

  // Vocal style for physics parameters (breath, timing, pitch bend)
  sctx.vocal_style = params.vocal_style;

  // Occurrence count for occurrence-dependent embellishment density
  sctx.section_occurrence = occurrence;

  // Apply melodic leap constraint: user override > blueprint > default
  if (params.melody_max_leap_override) {
    sctx.max_leap_semitones = params.melody_params.max_leap_interval;
  } else if (params.blueprint_ref != nullptr) {
    sctx.max_leap_semitones = params.blueprint_ref->constraints.max_leap_semitones;
  }
  if (params.blueprint_ref != nullptr) {
    sctx.prefer_stepwise = params.blueprint_ref->constraints.prefer_stepwise;
  }

  // Wire guide tone rate from section
  sctx.guide_tone_rate = section.guide_tone_rate;

  // Set anticipation rest mode based on groove feel and drive
  // Driving/Syncopated grooves benefit from anticipation rests for "tame" effect
  // Higher drive_feel increases anticipation intensity
  if (params.vocal_groove == VocalGrooveFeel::Driving16th) {
    sctx.anticipation_rest = (params.drive_feel >= 70) ? AnticipationRestMode::Moderate
                                                        : AnticipationRestMode::Subtle;
  } else if (params.vocal_groove == VocalGrooveFeel::Syncopated) {
    sctx.anticipation_rest = AnticipationRestMode::Moderate;
  } else if (params.drive_feel >= 80) {
    // High drive with any groove gets subtle anticipation
    sctx.anticipation_rest = AnticipationRestMode::Subtle;
  }

  // Set phrase contour template based on section type
  // Common J-POP practice:
  // - Chorus: Peak (arch shape) for memorable hook contour
  // - A (Verse): Ascending for storytelling build
  // - B (Pre-chorus): Ascending to build tension before chorus
  // - Bridge: Descending for contrast
  switch (section.type) {
    case SectionType::Chorus:
      sctx.forced_contour = ContourType::Peak;
      break;
    case SectionType::A:
      sctx.forced_contour = ContourType::Ascending;
      break;
    case SectionType::B:
      sctx.forced_contour = ContourType::Ascending;
      break;
    case SectionType::Bridge:
      sctx.forced_contour = ContourType::Descending;
      break;
    default:
      sctx.forced_contour = std::nullopt;  // Use default section-aware bias
      break;
  }

  // Enable motif fragment enforcement for A/B sections after first chorus
  // This creates song-wide melodic unity by echoing chorus motif fragments
  if (designer.getCachedGlobalMotif().has_value() &&
      (section.type == SectionType::A || section.type == SectionType::B)) {
    sctx.enforce_motif_fragments = true;
  }

  // Set transition info for next section (if any)
  const auto& sections = song.arrangement().sections();
  for (size_t idx = 0; idx < sections.size(); ++idx) {
    if (&sections[idx] == &section && idx + 1 < sections.size()) {
      sctx.transition_to_next = getTransition(section.type, sections[idx + 1].type);
      break;
    }
  }

  return sctx;
}

CachedRhythmPattern* VocalGenerator::resolveRhythmLock(
    const Section& section, const GeneratorParams& params,
    const Song& song, const FullTrackContext& ctx,
    CachedRhythmPattern& motif_storage,
    bool use_per_section_type_lock,
    std::unordered_map<SectionType, CachedRhythmPattern>& section_type_locks,
    CachedRhythmPattern* active_rhythm_lock,
    Tick section_start, Tick section_end) const {
  CachedRhythmPattern* current_rhythm_lock = nullptr;

  // RhythmSync paradigm: extract rhythm from Motif track (coordinate axis)
  // Try ctx.motif_track first (from Coordinator), then fall back to motif_track_ member
  const MidiTrack* motif_ref = ctx.motif_track;
  if (motif_ref == nullptr) {
    motif_ref = motif_track_;
  }
  if (params.paradigm == GenerationParadigm::RhythmSync && motif_ref != nullptr &&
      !motif_ref->empty()) {
    // Extract Motif's rhythm pattern for this section
    motif_storage = extractRhythmPatternFromTrack(
        motif_ref->notes(), section_start, section_end);
    if (motif_storage.isValid()) {
      current_rhythm_lock = &motif_storage;
    }
  }

  // Fallback: use stored Motif base pattern (available even when Motif is muted)
  if (current_rhythm_lock == nullptr && params.paradigm == GenerationParadigm::RhythmSync) {
    const auto& base_pattern = song.motifPattern();
    if (!base_pattern.empty()) {
      uint8_t pattern_beats = static_cast<uint8_t>(
          (base_pattern.back().start_tick + base_pattern.back().duration + TICKS_PER_BEAT - 1) /
          TICKS_PER_BEAT);
      if (pattern_beats > 0) {
        motif_storage = extractRhythmPattern(base_pattern, 0, pattern_beats);
        if (motif_storage.isValid()) {
          current_rhythm_lock = &motif_storage;
        }
      }
    }
  }

  // Fallback: use cached Vocal rhythm if Motif pattern not available
  if (current_rhythm_lock == nullptr) {
    if (use_per_section_type_lock) {
      // Per-section-type lock: look up by section type
      auto iter = section_type_locks.find(section.type);
      if (iter != section_type_locks.end() && iter->second.isValid()) {
        current_rhythm_lock = &iter->second;
      }
    } else if (active_rhythm_lock->isValid()) {
      // Global lock: use single rhythm pattern
      current_rhythm_lock = active_rhythm_lock;
    }
  }

  return current_rhythm_lock;
}

void VocalGenerator::postProcessVocalNotes(
    std::vector<NoteEvent>& all_notes, MidiTrack& track,
    const Song& song, const GeneratorParams& params,
    IHarmonyContext& harmony, std::mt19937& rng,
    float velocity_scale,
    uint8_t effective_vocal_low, uint8_t effective_vocal_high) const {
  // Apply section-end sustain - extend final notes of each section
  applySectionEndSustain(all_notes, song.arrangement().sections(), harmony);

  // Apply groove feel timing adjustments
  applyGrooveFeel(all_notes, params.vocal_groove);

  // Remove overlapping notes
  // UltraVocaloid allows 32nd notes (60 ticks), standard vocals need 16th notes (120 ticks)
  Tick min_note_duration =
      (params.vocal_style == VocalStylePreset::UltraVocaloid) ? TICK_32ND : TICK_SIXTEENTH;
  NoteTimeline::fixOverlapsWithMinDuration(all_notes, min_note_duration);

  // Safety net: enforce maximum phrase duration for very long sections.
  // Inter-section breaths are handled during generation; this catches edge cases
  // within individual sections that exceed max_phrase_bars.
  VocalPhysicsParams physics = getVocalPhysicsParams(params.vocal_style);
  if (physics.requires_breath && physics.max_phrase_bars < 255) {
    melody::enforceMaxPhraseDuration(all_notes, physics.max_phrase_bars, TICK_EIGHTH);
  }

  // Vocal-friendly post-processing:
  // Merge same-pitch notes with BPM-aware gap threshold.
  // At fast tempos, gate_ratio creates larger tick gaps that should still be merged.
  // SKIP for UltraVocaloid: same-pitch rapid-fire is intentional (machine-gun style)
  if (params.vocal_style != VocalStylePreset::UltraVocaloid) {
    // BPM-aware merge gap: ~50ms in real time, minimum 30 ticks
    Tick merge_gap = static_cast<Tick>(std::max(
        30.0f, 0.05f * params.bpm * TICKS_PER_BEAT / 60.0f));
    mergeSamePitchNotes(all_notes, merge_gap);
  }

  // NOTE: resolveIsolatedShortNotes() removed - short notes are often
  // intentional articulation (staccato bursts, rhythmic motifs).

  // Apply velocity scale
  applyVelocityBalance(all_notes, velocity_scale);

  // Enforce pitch constraints (interval limits and scale enforcement)
  enforceVocalPitchConstraints(all_notes, params, harmony);

  // Break up excessive consecutive same-pitch notes (RhythmSync compatibility)
  // This addresses monotonous melody issues in RhythmSync paradigm where
  // collision avoidance can cause long runs of the same pitch.
  // max_consecutive=3 means 4th note onwards gets alternated for melodic interest.
  breakConsecutiveSamePitch(all_notes, harmony, effective_vocal_low, effective_vocal_high, 3);

  // Final overlap check - ensures no overlaps after all processing
  NoteTimeline::fixOverlapsWithMinDuration(all_notes, min_note_duration);

  // Add notes to track
  // Note: Registration with HarmonyContext is handled by Coordinator after generateFullTrack()
  // to avoid double registration and ensure MidiTrack and HarmonyContext are in sync
  for (const auto& note : all_notes) {
    track.addNote(note);
  }

  // Apply pitch bend expressions (scoop-up, fall-off, vibrato, portamento)
  const auto* sections_ptr = &song.arrangement().sections();
  applyVocalPitchBendExpressions(track, all_notes, params, rng, sections_ptr);
}

void VocalGenerator::doGenerateFullTrack(MidiTrack& track, const FullTrackContext& ctx) {
  Song& song = *ctx.song;
  const GeneratorParams& params = *ctx.params;
  std::mt19937& rng = *ctx.rng;
  IHarmonyContext& harmony = *ctx.harmony;
  const DrumGrid* drum_grid = ctx.drum_grid;

  // Calculate effective vocal range (extracted helper)
  VocalRangeResult range = calculateEffectiveVocalRange(params, song, motif_track_);
  uint8_t effective_vocal_low = range.effective_low;
  uint8_t effective_vocal_high = range.effective_high;
  float velocity_scale = range.velocity_scale;

  // Get chord progression
  const auto& progression = getChordProgression(params.chord_id);

  // Create MelodyDesigner
  MelodyDesigner designer;

  // Collect all notes
  std::vector<NoteEvent> all_notes;

  // Phrase cache for section repetition (V2: extended key with bars + chord_degree)
  std::unordered_map<PhraseCacheKey, CachedPhrase, PhraseCacheKeyHash> phrase_cache;

  // Check if rhythm lock should be used
  bool use_rhythm_lock = shouldLockVocalRhythm(params);
  bool use_per_section_type_lock = shouldUsePerSectionTypeRhythmLock(params);
  // Local rhythm lock cache if none provided externally
  CachedRhythmPattern local_rhythm_lock;
  CachedRhythmPattern* active_rhythm_lock = &local_rhythm_lock;
  // Per-section-type rhythm lock map (for UltraVocaloid)
  std::unordered_map<SectionType, CachedRhythmPattern> section_type_rhythm_locks;

  // Clear existing phrase boundaries for fresh generation
  song.clearPhraseBoundaries();

  // Track section type occurrences for progressive tessitura shift
  // J-POP practice: later choruses are often sung higher for emotional build-up
  std::unordered_map<SectionType, int> section_occurrence_count;

  // Track whether previous section had vocal notes (for inter-section breath)
  bool has_prev_vocal_section = false;
  SectionType prev_vocal_section_type = SectionType::Intro;

  // Process each section
  for (const auto& section : song.arrangement().sections()) {
    // Skip sections without vocals (by type)
    if (!sectionHasVocals(section.type)) {
      continue;
    }
    // Skip sections where vocal is disabled by track_mask
    if (shouldSkipSection(section)) {
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
    Tick section_end = section.endTick();

    // Get chord for this section
    int chord_idx = section.start_bar % progression.length;
    int8_t chord_degree = progression.at(chord_idx);

    // Track occurrence count for this section type (1-based)
    int occurrence = ++section_occurrence_count[section.type];

    // Apply register shift for section (clamped to original range)
    // Includes progressive tessitura shift for later occurrences
    int8_t register_shift = getRegisterShift(section.type, params.melody_params, occurrence);

    // ========================================================================
    // Climax Range Expansion (Task 3.11)
    // For the last Chorus (peak_level=Max): allow vocal_high + 2 semitones
    // This gives the vocalist room to "break out" at the climax
    // ========================================================================
    int climax_extension = 0;
    if (section.type == SectionType::Chorus && section.peak_level == PeakLevel::Max) {
      climax_extension = 2;  // +2 semitones for climax
    }

    // Register shift adjusts the preferred center but must not exceed original range
    // (except for climax extension which allows exceeding the range)
    uint8_t section_vocal_low = static_cast<uint8_t>(
        std::clamp(static_cast<int>(effective_vocal_low) + register_shift,
                   static_cast<int>(effective_vocal_low),
                   static_cast<int>(effective_vocal_high) - 6));  // At least 6 semitone range
    uint8_t section_vocal_high = static_cast<uint8_t>(
        std::clamp(static_cast<int>(effective_vocal_high) + register_shift + climax_extension,
                   static_cast<int>(effective_vocal_low) + 6,
                   static_cast<int>(effective_vocal_high) + climax_extension));

    // Apply vocal_range_span constraint
    if (section.vocal_range_span > 0) {
      int span = section.vocal_range_span;
      if (static_cast<int>(section_vocal_high) - static_cast<int>(section_vocal_low) > span) {
        section_vocal_high = static_cast<uint8_t>(section_vocal_low + span);
      }
    }

    // Verse/Bridge ceiling: prevent non-Chorus sections from reaching Chorus highs.
    // This ensures the Chorus climax is perceptually distinct.
    constexpr int kVerseCeilingMarginSt = 3;
    if (section.type == SectionType::A || section.type == SectionType::Bridge) {
      int chorus_ceiling = static_cast<int>(effective_vocal_high)
                           + params.melody_params.chorus_register_shift;
      int ceiling = chorus_ceiling - kVerseCeilingMarginSt;
      if (static_cast<int>(section_vocal_high) > ceiling) {
        section_vocal_high = static_cast<uint8_t>(
            std::max(ceiling, static_cast<int>(section_vocal_low) + 6));
      }
    }

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

      // Select variation based on reuse count and occurrence
      // (later choruses get progressively more variation)
      PhraseVariation variation = selectPhraseVariation(cached.reuse_count, occurrence, rng);
      cached.reuse_count++;

      // Shift timing to current section start
      section_notes = shiftTiming(cached.notes, section_start);

      // Apply subtle variation for interest while maintaining recognizability
      applyPhraseVariation(section_notes, variation, rng);

      // Adjust pitch range if different
      section_notes = adjustPitchRange(section_notes, cached.vocal_low, cached.vocal_high,
                                       section_vocal_low, section_vocal_high);

      // Re-apply collision avoidance (chord context may differ)
      applyCollisionAvoidanceWithIntervalConstraint(section_notes, harmony, section_vocal_low,
                                                    section_vocal_high);
    } else {
      // Cache miss: generate new melody
      MelodyDesigner::SectionContext sctx = buildSectionContext(
          section, params, song, section_tessitura,
          section_vocal_low, section_vocal_high, chord_degree,
          occurrence, drum_grid, designer);

      // Resolve rhythm lock for this section
      CachedRhythmPattern motif_rhythm_pattern;  // Local storage for Motif-derived pattern
      CachedRhythmPattern* current_rhythm_lock = nullptr;
      if (use_rhythm_lock) {
        current_rhythm_lock = resolveRhythmLock(
            section, params, song, ctx, motif_rhythm_pattern,
            use_per_section_type_lock, section_type_rhythm_locks,
            active_rhythm_lock, section_start, section_end);
      }

      // Build phrase plan for this section (uses rhythm lock if available)
      PhrasePlan phrase_plan = PhrasePlanner::buildPlan(
          section.type, section_start, section_end, section.bars,
          params.mood, params.vocal_style,
          current_rhythm_lock, params.bpm);

      // Mark first chorus phrase as hold-burst entry if previous section was B
      if (section.type == SectionType::Chorus && !phrase_plan.phrases.empty()) {
        const auto& sections = song.arrangement().sections();
        for (size_t si = 0; si < sections.size(); ++si) {
          if (&sections[si] == &section && si > 0 &&
              sections[si - 1].type == SectionType::B) {
            phrase_plan.phrases[0].is_hold_burst_entry = true;
            phrase_plan.phrases[0].density_modifier *= 1.3f;
            break;
          }
        }
      }

      // Run-based onset selection for RhythmSync (skip for UltraVocaloid)
      CachedRhythmPattern run_filtered_pattern;
      if (current_rhythm_lock != nullptr &&
          params.paradigm == GenerationParadigm::RhythmSync &&
          params.motif.rhythm_template != MotifRhythmTemplate::None &&
          params.vocal_style != VocalStylePreset::UltraVocaloid) {
        const auto& tmpl = motif_detail::getTemplateConfig(params.motif.rhythm_template);
        run_filtered_pattern = buildRunBasedOnsetMap(
            *current_rhythm_lock, phrase_plan, tmpl, params.bpm, section_start);
        current_rhythm_lock = &run_filtered_pattern;
      }

      if (current_rhythm_lock != nullptr) {
        // Use locked rhythm pattern with evaluation-based pitch selection
        section_notes =
            generateLockedRhythmWithEvaluation(*current_rhythm_lock, section, designer, harmony, sctx, rng,
                                               &phrase_plan);
      } else {
        // Generate melody with evaluation (candidate count varies by section importance)
        int candidate_count = MelodyDesigner::getCandidateCountForSection(section.type);
        section_notes = designer.generateSectionWithEvaluation(
            section_tmpl, sctx, harmony, rng, params.vocal_style, params.melodic_complexity,
            candidate_count);

        // Cache rhythm pattern for subsequent sections
        // Validate density before locking to prevent sparse patterns from propagating
        constexpr float kMinRhythmLockDensity = 3.0f;  // Minimum notes per bar
        if (use_rhythm_lock && !section_notes.empty()) {
          CachedRhythmPattern candidate =
              extractRhythmPattern(section_notes, section_start, section.bars * 4);
          float density = calculatePatternDensity(candidate);

          if (density >= kMinRhythmLockDensity) {
            if (use_per_section_type_lock) {
              // Cache per section type
              section_type_rhythm_locks[section.type] = std::move(candidate);
            } else if (!active_rhythm_lock->isValid()) {
              // Cache globally
              *active_rhythm_lock = std::move(candidate);
            }
          }
          // If density is too low, don't lock - let subsequent sections generate fresh
        }
      }

      // Apply transition approach if transition info was set
      if (sctx.transition_to_next) {
        designer.applyTransitionApproach(section_notes, sctx, harmony);
      }

      // Apply HarmonyContext collision avoidance with interval constraint
      applyCollisionAvoidanceWithIntervalConstraint(section_notes, harmony, section_vocal_low,
                                                    section_vocal_high);

      // Extract GlobalMotif from first Chorus for song-wide melodic unity
      // Subsequent sections will receive bonus for similar contour/intervals
      if (section.type == SectionType::Chorus && !designer.getCachedGlobalMotif().has_value()) {
        GlobalMotif motif = melody::extractGlobalMotif(section_notes);
        if (motif.isValid()) {
          designer.setGlobalMotif(motif);
        }
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

    // Ensure inter-section breath gap by shortening previous section's last note
    if (has_prev_vocal_section && !all_notes.empty() && !section_notes.empty()) {
      Tick prev_end = all_notes.back().start_tick + all_notes.back().duration;
      Tick next_start = section_notes.front().start_tick;
      Tick current_gap = (next_start > prev_end) ? (next_start - prev_end) : 0;

      // Calculate desired breath using existing BreathContext
      BreathContext breath_ctx;
      breath_ctx.is_section_boundary = true;
      breath_ctx.next_section = section.type;
      // Compute prev_phrase_high from last ~8 notes in all_notes
      uint8_t prev_high = 60;
      for (int k = static_cast<int>(all_notes.size()) - 1;
           k >= 0 && k >= static_cast<int>(all_notes.size()) - 8; --k) {
        prev_high = std::max(prev_high, all_notes[k].note);
      }
      breath_ctx.prev_phrase_high = prev_high;

      Tick desired_breath = melody::getBreathDuration(
          prev_vocal_section_type, params.mood, 0.5f, prev_high,
          &breath_ctx, params.vocal_style, params.bpm);
      desired_breath = std::max(desired_breath, TICK_EIGHTH);  // floor: 240 ticks

      if (current_gap < desired_breath) {
        Tick shorten_by = desired_breath - current_gap;
        NoteEvent& last = all_notes.back();
        // Duration floor: never shorten below TICK_SIXTEENTH (120 ticks)
        Tick min_remaining = TICK_SIXTEENTH;
        if (last.duration > shorten_by + min_remaining) {
          last.duration -= shorten_by;
        } else if (last.duration > min_remaining) {
          last.duration = min_remaining;
        }
        // else: note is already very short, don't touch it
      }
    }

    // Add to collected notes
    // Check interval between last note of previous section and first note of this section
    if (!all_notes.empty() && !section_notes.empty()) {
      // kMaxMelodicInterval from pitch_utils.h
      int prev_note = all_notes.back().note;
      int first_note = section_notes.front().note;
      int interval = std::abs(first_note - prev_note);
      if (interval > kMaxMelodicInterval) {
        // Get chord degree at first note's position
        int8_t first_note_chord_degree = harmony.getChordDegreeAt(section_notes.front().start_tick);
        // Use nearestChordToneWithinInterval to stay on chord tones
#ifdef MIDISKETCH_NOTE_PROVENANCE
        uint8_t old_pitch = section_notes.front().note;
#endif
        int new_pitch = nearestChordToneWithinInterval(
            first_note, prev_note, first_note_chord_degree, kMaxMelodicInterval, section_vocal_low,
            section_vocal_high, nullptr);
        // Re-verify collision safety after interval fix
        if (!harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(new_pitch),
                                                 section_notes.front().start_tick,
                                                 section_notes.front().duration, TrackRole::Vocal)) {
          new_pitch = first_note;  // Keep original if fix introduces collision
        }
        section_notes.front().note = static_cast<uint8_t>(new_pitch);
#ifdef MIDISKETCH_NOTE_PROVENANCE
        if (old_pitch != section_notes.front().note) {
          section_notes.front().prov_original_pitch = old_pitch;
          section_notes.front().addTransformStep(TransformStepType::IntervalFix, old_pitch,
                                                  section_notes.front().note, 0, 0);
        }
#endif
      }
    }
    // Determine if chromatic approach is enabled for this mood
    EmbellishmentConfig emb_config = MelodicEmbellisher::getConfigForMood(params.mood);
    bool allow_chromatic = emb_config.chromatic_approach;

    for (size_t ni = 0; ni < section_notes.size(); ++ni) {
      auto& note = section_notes[ni];

      // Check if this note qualifies as a chromatic passing tone that should be preserved
      bool preserve_chromatic = false;
      if (allow_chromatic) {
        int snapped_check = snapToNearestScaleTone(note.note, 0);
        bool is_chromatic = (snapped_check != note.note);

        if (is_chromatic) {
          // Preserve if on a weak beat (not beats 1 or 3) and resolves by half-step
          // to the next note (which should be a scale tone)
          Tick pos_in_bar = positionInBar(note.start_tick);
          bool is_weak = (pos_in_bar >= TICKS_PER_BEAT / 2) &&
                         !(pos_in_bar >= 2 * TICKS_PER_BEAT &&
                           pos_in_bar < 2 * TICKS_PER_BEAT + TICKS_PER_BEAT / 2);

          if (is_weak && ni + 1 < section_notes.size()) {
            int next_pitch = section_notes[ni + 1].note;
            int interval = std::abs(static_cast<int>(note.note) - next_pitch);
            // Half-step resolution to a diatonic note
            if (interval <= 2 && snapToNearestScaleTone(next_pitch, 0) == next_pitch) {
              preserve_chromatic = true;
            }
          }
        }
      }

      if (!preserve_chromatic) {
        // ABSOLUTE CONSTRAINT: Ensure pitch is on scale (prevents chromatic notes)
#ifdef MIDISKETCH_NOTE_PROVENANCE
        uint8_t old_pitch = note.note;
#endif
        int snapped = snapToNearestScaleTone(note.note, 0);  // Always C major internally
        uint8_t snapped_clamped = static_cast<uint8_t>(std::clamp(snapped, static_cast<int>(section_vocal_low),
                                                    static_cast<int>(section_vocal_high)));
        // Re-verify collision safety after scale snap
        if (snapped_clamped != note.note &&
            !harmony.isConsonantWithOtherTracks(snapped_clamped, note.start_tick, note.duration,
                                                 TrackRole::Vocal)) {
          // Scale snap would introduce collision - keep original pitch
          snapped_clamped = note.note;
        }
        note.note = snapped_clamped;
#ifdef MIDISKETCH_NOTE_PROVENANCE
        if (old_pitch != note.note) {
          note.prov_original_pitch = old_pitch;
          note.addTransformStep(TransformStepType::ScaleSnap, old_pitch, note.note, 0, 0);
        }
#endif
      } else {
        // Clamp to range even for chromatic tones
#ifdef MIDISKETCH_NOTE_PROVENANCE
        uint8_t old_pitch = note.note;
#endif
        uint8_t clamped = static_cast<uint8_t>(std::clamp(static_cast<int>(note.note),
                                                          static_cast<int>(section_vocal_low),
                                                          static_cast<int>(section_vocal_high)));
        // Re-verify collision safety after range clamp
        if (clamped != note.note &&
            !harmony.isConsonantWithOtherTracks(clamped, note.start_tick, note.duration,
                                                 TrackRole::Vocal)) {
          // Clamp would introduce collision - keep original pitch
          clamped = note.note;
        }
        note.note = clamped;
#ifdef MIDISKETCH_NOTE_PROVENANCE
        if (old_pitch != note.note) {
          note.prov_original_pitch = old_pitch;
          note.addTransformStep(TransformStepType::RangeClamp, old_pitch, note.note,
                                static_cast<int8_t>(section_vocal_low),
                                static_cast<int8_t>(section_vocal_high));
        }
#endif
      }
      all_notes.push_back(note);
    }

    // Update inter-section breath tracking
    if (!section_notes.empty()) {
      has_prev_vocal_section = true;
      prev_vocal_section_type = section.type;
    }
  }

  // NOTE: Modulation is NOT applied internally.
  // MidiWriter applies modulation to all tracks when generating MIDI bytes.
  // This ensures consistent behavior and avoids double-modulation.

  // Final post-processing: sustain, groove, overlaps, breath, merge, velocity,
  // pitch constraints, same-pitch breaks, and pitch bend expressions
  postProcessVocalNotes(all_notes, track, song, params, harmony, rng,
                        velocity_scale, effective_vocal_low, effective_vocal_high);
}

}  // namespace midisketch
