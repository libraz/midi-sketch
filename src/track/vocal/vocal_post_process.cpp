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
#include "core/midi_track.h"
#include "core/note_source.h"
#include "core/note_timeline_utils.h"
#include "core/pitch_bend_curves.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/structure.h"
#include "core/timing_constants.h"
#include "core/velocity.h"
#include "track/melody/melody_utils.h"

namespace midisketch {

namespace {

SectionType sectionTypeAt(Tick tick, const std::vector<Section>* sections) {
  if (sections == nullptr) return SectionType::A;
  for (const auto& section : *sections) {
    Tick section_end = section.start_tick + static_cast<Tick>(section.bars) * TICKS_PER_BAR;
    if (tick >= section.start_tick && tick < section_end) {
      return section.type;
    }
  }
  return SectionType::A;
}

int effectiveMaxIntervalAt(Tick tick, const std::vector<Section>* sections, uint8_t ctx_max_leap) {
  return melody::getEffectiveMaxInterval(sectionTypeAt(tick, sections), ctx_max_leap);
}

}  // namespace

void enforceVocalPitchConstraints(std::vector<NoteEvent>& all_notes, const GeneratorParams& params,
                                  IHarmonyContext& harmony, const std::vector<Section>* sections) {
  uint8_t ctx_max_leap = melody::resolveContextMaxLeap(params);

  // FINAL INTERVAL ENFORCEMENT: section/blueprint-aware singability limit.
  for (size_t i = 1; i < all_notes.size(); ++i) {
    int prev_pitch = all_notes[i - 1].note;
    int curr_pitch = all_notes[i].note;
    int interval = std::abs(curr_pitch - prev_pitch);
    int max_interval = effectiveMaxIntervalAt(all_notes[i].start_tick, sections, ctx_max_leap);
    if (interval > max_interval) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
      uint8_t old_pitch = all_notes[i].note;
#endif
      melody::MelodicNeighborhood neighborhood;
      neighborhood.start = all_notes[i].start_tick;
      neighborhood.duration = all_notes[i].duration;
      neighborhood.prev_pitch = prev_pitch;
      if (i + 1 < all_notes.size()) {
        Tick cur_end = all_notes[i].start_tick + all_notes[i].duration;
        neighborhood.next_pitch = static_cast<int>(all_notes[i + 1].note);
        neighborhood.next_start = all_notes[i + 1].start_tick;
        neighborhood.gap_to_next =
            all_notes[i + 1].start_tick > cur_end ? all_notes[i + 1].start_tick - cur_end : 0;
      }
      // Collision safety and chord legality are decided inside the search, so a
      // pitch it returns is already admissible; an unchanged pitch means the
      // bound could not be closed without breaking something that outranks it.
      int fixed_pitch = melody::resolveLeapWithinBound(
          harmony, neighborhood, curr_pitch, max_interval, params.vocal_low, params.vocal_high);
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
      uint8_t snapped_clamped = static_cast<uint8_t>(std::clamp(
          snapped, static_cast<int>(params.vocal_low), static_cast<int>(params.vocal_high)));
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
                               uint8_t vocal_low, uint8_t vocal_high, int max_consecutive,
                               const std::vector<Section>* sections, uint8_t ctx_max_leap) {
  if (all_notes.size() < static_cast<size_t>(max_consecutive + 1)) return;

  // Sort by time first
  NoteTimeline::sortByStartTick(all_notes);

  size_t streak_start = 0;
  int streak_count = 1;
  uint8_t streak_pitch = all_notes[0].note;
  // Syllabic subdivision notes are intentional same-pitch rearticulation;
  // the first note of a subdivision group should not seed a monotony streak.
  if (all_notes[0].is_syllabic_subdivision) {
    streak_count = 0;
  }

  for (size_t i = 1; i <= all_notes.size(); ++i) {
    // Syllabic subdivision notes are intentional same-pitch rearticulation;
    // they should not count toward monotony streaks.
    if (i < all_notes.size() && all_notes[i].is_syllabic_subdivision) {
      continue;
    }
    bool streak_continues = false;
    if (i < all_notes.size() && all_notes[i].note == streak_pitch) {
      Tick previous_end = all_notes[i - 1].start_tick + all_notes[i - 1].duration;
      Tick gap =
          all_notes[i].start_tick > previous_end ? all_notes[i].start_tick - previous_end : 0;
      streak_continues = gap <= TICKS_PER_BEAT;
    }

    if (streak_continues) {
      streak_count++;
    }

    // Process streak when it ends or at the last note
    if (!streak_continues || i == all_notes.size()) {
      if (streak_count > max_consecutive) {
        // Break up the streak: modify every other note starting from position max_consecutive
        for (size_t j = streak_start + static_cast<size_t>(max_consecutive); j < i; j += 2) {
          // Never change pitch of syllabic subdivision notes.
          if (all_notes[j].is_syllabic_subdivision) {
            continue;
          }
          Tick tick = all_notes[j].start_tick;
          Tick duration = all_notes[j].duration;

          // Find nearby chord tones as alternatives
          auto chord_tones = melody::vocalChordTonesAt(harmony, tick);
          if (chord_tones.empty()) continue;

          // Neighbor pitches for interval/non-chord-tone legality checks: the
          // alternation note must stay singable relative to BOTH neighbors
          // (an offset picked against streak_pitch alone can land 10+
          // semitones from the note that follows the streak).
          melody::MelodicNeighborhood neighborhood;
          neighborhood.start = tick;
          neighborhood.duration = duration;
          neighborhood.prev_pitch = (j > 0) ? static_cast<int>(all_notes[j - 1].note) : -1;
          if (j + 1 < all_notes.size()) {
            Tick cur_end = tick + duration;
            neighborhood.next_pitch = static_cast<int>(all_notes[j + 1].note);
            neighborhood.next_start = all_notes[j + 1].start_tick;
            neighborhood.gap_to_next =
                all_notes[j + 1].start_tick > cur_end ? all_notes[j + 1].start_tick - cur_end : 0;
          }
          const int prev_pitch = neighborhood.prev_pitch;
          const int next_pitch = neighborhood.next_pitch;

          // Step-first candidate order: an adjacent scale tone preserves the
          // conjunct motion of the line (neighbor-tone figure); chord-tone
          // zigzags (+/-3..7) are the fallback for notes that must stay on
          // chord tones (strong beat / long / phrase-final).
          int best_alt = -1;
          for (int interval : {1, -1, 2, -2, 3, -3, 4, -4, 5, -5, 7, -7}) {
            int candidate = static_cast<int>(streak_pitch) + interval;
            if (candidate < static_cast<int>(vocal_low) || candidate > static_cast<int>(vocal_high))
              continue;

            // The vocal stays diatonic, so chromatic chord tones (e.g.
            // secondary dominant 3rds) are excluded even when they belong to
            // the current chord.
            int pc = candidate % 12;
            if (!isScaleTone(pc)) continue;
            // A replacement is admissible on exactly the terms every other
            // vocal pass uses: a chord tone, or a figure the shared legality
            // rule licenses (appoggiatura, suspension, passing/neighbor tone).
            if (!melody::isVocalToneLegal(harmony, candidate, neighborhood)) {
              continue;
            }
            // Keep the line singable relative to both neighbors.
            int max_interval = effectiveMaxIntervalAt(tick, sections, ctx_max_leap);
            if (prev_pitch >= 0 && std::abs(candidate - prev_pitch) > max_interval) continue;
            if (next_pitch >= 0 && std::abs(candidate - next_pitch) > max_interval) continue;
            // Verify no harsh collision.
            if (!harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(candidate), tick, duration,
                                                    TrackRole::Vocal)) {
              continue;
            }
            best_alt = candidate;  // offsets are distance-ordered: first hit wins
            break;
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

void breakSameDirectionLeapChains(std::vector<NoteEvent>& all_notes, const IHarmonyContext& harmony,
                                  uint8_t vocal_low, uint8_t vocal_high) {
  if (all_notes.size() < 4) return;

  NoteTimeline::sortByStartTick(all_notes);

  int chain_count = 0;
  int chain_sign = 0;
  for (size_t i = 1; i < all_notes.size(); ++i) {
    int prev_pitch = static_cast<int>(all_notes[i - 1].note);
    int curr_pitch = static_cast<int>(all_notes[i].note);
    int interval = curr_pitch - prev_pitch;
    int sign = (interval > 0) ? 1 : (interval < 0 ? -1 : 0);
    bool is_leap = std::abs(interval) >= 3;

    if (is_leap && sign != 0 && (chain_count == 0 || sign == chain_sign)) {
      ++chain_count;
      chain_sign = sign;
    } else {
      chain_count = (is_leap && sign != 0) ? 1 : 0;
      chain_sign = sign;
    }

    if (chain_count < 3) continue;

    melody::MelodicNeighborhood neighborhood;
    neighborhood.start = all_notes[i].start_tick;
    neighborhood.duration = all_notes[i].duration;
    neighborhood.prev_pitch = prev_pitch;
    if (i + 1 < all_notes.size()) {
      Tick cur_end = all_notes[i].start_tick + all_notes[i].duration;
      neighborhood.next_pitch = static_cast<int>(all_notes[i + 1].note);
      neighborhood.next_start = all_notes[i + 1].start_tick;
      neighborhood.gap_to_next =
          all_notes[i + 1].start_tick > cur_end ? all_notes[i + 1].start_tick - cur_end : 0;
    }

    const int offsets_up_chain[] = {-1, -2, 0, 1, 2};
    const int offsets_down_chain[] = {1, 2, 0, -1, -2};
    const int* offsets = chain_sign > 0 ? offsets_up_chain : offsets_down_chain;
    int fixed_pitch = -1;
    for (size_t oi = 0; oi < 5; ++oi) {
      int candidate = prev_pitch + offsets[oi];
      if (candidate < static_cast<int>(vocal_low) || candidate > static_cast<int>(vocal_high)) {
        continue;
      }
      if (!isScaleTone(candidate % 12)) continue;
      // Same admissibility rule as every other vocal pitch-moving pass.
      if (!melody::isVocalToneLegal(harmony, candidate, neighborhood)) {
        continue;
      }
      if (!harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(candidate),
                                              all_notes[i].start_tick, all_notes[i].duration,
                                              TrackRole::Vocal)) {
        continue;
      }
      fixed_pitch = candidate;
      break;
    }

    if (fixed_pitch < 0) continue;

#ifdef MIDISKETCH_NOTE_PROVENANCE
    uint8_t old_pitch = all_notes[i].note;
#endif
    all_notes[i].note = static_cast<uint8_t>(fixed_pitch);
#ifdef MIDISKETCH_NOTE_PROVENANCE
    if (old_pitch != all_notes[i].note) {
      all_notes[i].prov_original_pitch = old_pitch;
      all_notes[i].addTransformStep(TransformStepType::IntervalFix, old_pitch, all_notes[i].note, 0,
                                    0);
    }
#endif

    int new_interval = fixed_pitch - prev_pitch;
    int new_sign = (new_interval > 0) ? 1 : (new_interval < 0 ? -1 : 0);
    bool new_is_leap = std::abs(new_interval) >= 3;
    chain_count = new_is_leap && new_sign != 0 ? 1 : 0;
    chain_sign = new_sign;
  }
}

void resolveNotesTheChordRefuses(std::vector<NoteEvent>& all_notes, const IHarmonyContext& harmony,
                                 uint8_t vocal_low, const std::vector<Section>* sections,
                                 uint8_t ctx_max_leap) {
  if (all_notes.empty()) return;
  NoteTimeline::sortByStartTick(all_notes);

  for (size_t i = 0; i < all_notes.size(); ++i) {
    NoteEvent& note = all_notes[i];
    const melody::MelodicNeighborhood neighborhood = melody::neighborhoodAt(all_notes, i);
    if (melody::classifyVocalTone(harmony, note.note, neighborhood) !=
        melody::ToneLegality::Illegal) {
      continue;
    }

    // The reach is the perfect 5th the ceiling walk uses: past that the
    // replacement is a different gesture rather than a correction.
    constexpr int kReach = 7;
    const int max_interval = effectiveMaxIntervalAt(note.start_tick, sections, ctx_max_leap);
    const int floor_pitch =
        std::max(static_cast<int>(vocal_low), static_cast<int>(note.note) - kReach);
    for (int alt = static_cast<int>(note.note) - 1; alt >= floor_pitch; --alt) {
      if (!isScaleTone(getPitchClass(static_cast<uint8_t>(alt)))) continue;
      if (neighborhood.prev_pitch >= 0 && std::abs(alt - neighborhood.prev_pitch) > max_interval) {
        continue;
      }
      if (neighborhood.next_pitch >= 0 && std::abs(alt - neighborhood.next_pitch) > max_interval) {
        continue;
      }
      if (!harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(alt), note.start_tick,
                                              note.duration, TrackRole::Vocal)) {
        continue;
      }
      if (melody::classifyVocalTone(harmony, alt, neighborhood) == melody::ToneLegality::Illegal) {
        continue;
      }
#ifdef MIDISKETCH_NOTE_PROVENANCE
      const uint8_t before = note.note;
#endif
      note.note = static_cast<uint8_t>(alt);
#ifdef MIDISKETCH_NOTE_PROVENANCE
      note.recordPitchMove(TransformStepType::ChordToneSnap, before, note.note);
#endif
      break;
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
    if (is_phrase_start && note.duration >= TICK_EIGHTH &&
        rng_util::rollProbability(rng, scoop_prob)) {
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
