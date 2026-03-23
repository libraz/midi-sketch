/**
 * @file vocal_post_process.cpp
 * @brief Vocal post-processing: pitch constraints, monotony breaking, pitch bend.
 *
 * Extracted from vocal.cpp to improve modularity and testability.
 */

#include "track/vocal/vocal_post_process.h"

#include <algorithm>
#include <cmath>

#include "core/chord_utils.h"
#include "core/i_harmony_context.h"
#include "core/note_timeline_utils.h"
#include "core/pitch_bend_curves.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/structure.h"
#include "core/timing_constants.h"
#include "core/velocity.h"
#include "core/midi_track.h"
#include "core/note_source.h"

namespace midisketch {

void enforceVocalPitchConstraints(std::vector<NoteEvent>& all_notes,
                                   const GeneratorParams& params,
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

void breakConsecutiveSamePitch(std::vector<NoteEvent>& all_notes, const IHarmonyContext& harmony,
                                uint8_t vocal_low, uint8_t vocal_high, int max_consecutive) {
  if (all_notes.size() < static_cast<size_t>(max_consecutive + 1)) return;

  // Sort by time first
  NoteTimeline::sortByStartTick(all_notes);

  size_t streak_start = 0;
  int streak_count = 1;
  uint8_t streak_pitch = all_notes[0].note;
#ifdef MIDISKETCH_NOTE_PROVENANCE
  // Syllabic subdivision notes are intentional same-pitch rearticulation;
  // the first note of a subdivision group should not seed a monotony streak.
  if (all_notes[0].prov_source == static_cast<uint8_t>(NoteSource::SyllabicSub)) {
    streak_count = 0;
  }
#endif

  for (size_t i = 1; i <= all_notes.size(); ++i) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
    // Syllabic subdivision notes are intentional same-pitch rearticulation;
    // they should not count toward monotony streaks.
    if (i < all_notes.size() &&
        all_notes[i].prov_source == static_cast<uint8_t>(NoteSource::SyllabicSub)) {
      continue;
    }
#endif
    bool streak_continues = (i < all_notes.size() && all_notes[i].note == streak_pitch);

    if (streak_continues) {
      streak_count++;
    }

    // Process streak when it ends or at the last note
    if (!streak_continues || i == all_notes.size()) {
      if (streak_count > max_consecutive) {
        // Break up the streak: modify every other note starting from position max_consecutive
        for (size_t j = streak_start + static_cast<size_t>(max_consecutive); j < i; j += 2) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
          // Never change pitch of syllabic subdivision notes.
          if (all_notes[j].prov_source == static_cast<uint8_t>(NoteSource::SyllabicSub)) {
            continue;
          }
#endif
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

void applyVocalPitchBendExpressions(MidiTrack& track, const std::vector<NoteEvent>& all_notes,
                                     const GeneratorParams& params, std::mt19937& rng,
                                     const std::vector<Section>* sections) {
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

}  // namespace midisketch
