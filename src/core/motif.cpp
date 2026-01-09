/**
 * @file motif.cpp
 * @brief Implementation of motif variation operations.
 */

#include "core/motif.h"
#include "core/pitch_utils.h"
#include <algorithm>

namespace midisketch {

Motif applyVariation(const Motif& original, MotifVariation variation,
                     int8_t param, std::mt19937& rng) {
  Motif result = original;

  switch (variation) {
    case MotifVariation::Exact:
      // No changes needed
      break;

    case MotifVariation::Transposed:
      // Shift all contour degrees by param
      for (auto& degree : result.contour_degrees) {
        degree += param;
      }
      // Also transpose absolute pitches
      for (auto& pitch : result.absolute_pitches) {
        pitch = static_cast<uint8_t>(std::clamp(
            static_cast<int>(pitch) + param, 0, 127));
      }
      break;

    case MotifVariation::Inverted:
      // Invert contour around the first note
      if (!result.contour_degrees.empty()) {
        int8_t pivot = result.contour_degrees[0];
        for (auto& degree : result.contour_degrees) {
          degree = pivot - (degree - pivot);
        }
      }
      // Also invert absolute pitches
      if (!result.absolute_pitches.empty()) {
        int pivot = result.absolute_pitches[0];
        for (auto& pitch : result.absolute_pitches) {
          int inverted = pivot - (static_cast<int>(pitch) - pivot);
          pitch = static_cast<uint8_t>(std::clamp(inverted, 0, 127));
        }
      }
      break;

    case MotifVariation::Augmented:
      // Double all durations
      for (auto& rn : result.rhythm) {
        rn.eighths *= 2;
        rn.beat *= 2.0f;
      }
      result.length_beats *= 2;
      break;

    case MotifVariation::Diminished:
      // Halve all durations
      for (auto& rn : result.rhythm) {
        rn.eighths = std::max(1, rn.eighths / 2);
        rn.beat /= 2.0f;
      }
      result.length_beats /= 2;
      break;

    case MotifVariation::Fragmented:
      // Use only the first half
      if (result.rhythm.size() > 2) {
        size_t half = result.rhythm.size() / 2;
        result.rhythm.resize(half);
        if (result.contour_degrees.size() > half) {
          result.contour_degrees.resize(half);
        }
        if (result.absolute_pitches.size() > half) {
          result.absolute_pitches.resize(half);
        }
        result.length_beats /= 2;
      }
      break;

    case MotifVariation::Sequenced:
      // Apply sequential transposition (each repetition shifts by param)
      // This version just applies a single step
      for (size_t i = 0; i < result.contour_degrees.size(); ++i) {
        result.contour_degrees[i] += static_cast<int8_t>(i) * param / 4;
      }
      for (size_t i = 0; i < result.absolute_pitches.size(); ++i) {
        int offset = static_cast<int>(i) * param / 4;
        result.absolute_pitches[i] = static_cast<uint8_t>(std::clamp(
            static_cast<int>(result.absolute_pitches[i]) + offset, 0, 127));
      }
      break;

    case MotifVariation::Embellished:
      // Add passing tones (simplified: just add some variation to degrees)
      {
        std::uniform_int_distribution<int> dist(-1, 1);
        for (size_t i = 1; i < result.contour_degrees.size() - 1; ++i) {
          if (!result.rhythm[i].strong) {
            int8_t delta = static_cast<int8_t>(dist(rng));
            result.contour_degrees[i] += delta;
            if (i < result.absolute_pitches.size()) {
              result.absolute_pitches[i] = static_cast<uint8_t>(std::clamp(
                  static_cast<int>(result.absolute_pitches[i]) + delta, 0, 127));
            }
          }
        }
      }
      break;
  }

  return result;
}

Motif designChorusHook(const StyleMelodyParams& params, [[maybe_unused]] std::mt19937& rng) {
  Motif hook;
  hook.length_beats = 8;  // 2-bar hook
  hook.ends_on_chord_tone = true;

  if (params.hook_repetition) {
    // Idol/Anime style: catchy, repetitive rhythm
    hook.rhythm = {
        {0.0f, 4, true},   // Beat 1: half note
        {2.0f, 2, true},   // Beat 3: quarter note
        {3.0f, 2, false},  // Beat 4: quarter note
        {4.0f, 4, true},   // Beat 1 (bar 2): half note - climax
        {6.0f, 2, true},   // Beat 3: quarter note
        {7.0f, 2, false},  // Beat 4: quarter note
    };
    hook.climax_index = 3;  // Fourth note is the climax

    // Short hook with wider intervals: root -> 4th -> 3rd (3 notes, memorable)
    // Uses 4th (5 semitones) as passing tone for melodic interest
    // Stays within major 6th constraint after chord tone snapping
    hook.contour_degrees = {0, 5, 4};
  } else {
    // Standard style: gradual arch contour
    hook.rhythm = {
        {0.0f, 2, true},   // Beat 1: quarter note
        {1.0f, 2, false},  // Beat 2: quarter note
        {2.0f, 2, true},   // Beat 3: quarter note
        {3.0f, 2, false},  // Beat 4: quarter note
        {4.0f, 3, true},   // Beat 1 (bar 2): dotted quarter - climax
        {5.5f, 2, false},  // Beat 2.5: quarter note
        {6.5f, 3, true},   // Beat 3.5: dotted quarter
    };
    hook.climax_index = 4;  // Fifth note is the climax

    // Arch contour: root -> 3rd -> 4th (3 notes)
    // 3rd on weak beat adds melodic interest, 4th provides gentle rise
    // Stays within major 6th interval constraint
    hook.contour_degrees = {0, 4, 5};
  }

  // Adjust contour size to match rhythm size
  while (hook.contour_degrees.size() < hook.rhythm.size()) {
    hook.contour_degrees.push_back(0);
  }
  if (hook.contour_degrees.size() > hook.rhythm.size()) {
    hook.contour_degrees.resize(hook.rhythm.size());
  }

  return hook;
}

MotifVariation selectHookVariation(std::mt19937& rng) {
  // "Variation is the enemy, Exact is justice"
  // 80% Exact (main), 20% Fragmented (allowed exception)
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  if (dist(rng) < 0.8f) {
    return MotifVariation::Exact;
  }
  return MotifVariation::Fragmented;
}

bool isHookAppropriateVariation(MotifVariation variation) {
  // Only Exact and Fragmented preserve hook identity
  // All others (Inverted, Sequenced, Embellished, Transposed, etc.)
  // make the melody sound different and reduce memorability
  return variation == MotifVariation::Exact ||
         variation == MotifVariation::Fragmented;
}

Motif extractMotifFromChorus(const std::vector<NoteEvent>& chorus_notes,
                              size_t max_notes) {
  Motif motif;

  if (chorus_notes.empty()) {
    return motif;
  }

  // Take the first max_notes notes
  size_t note_count = std::min(chorus_notes.size(), max_notes);

  // Use first note as reference pitch
  int reference_pitch = chorus_notes[0].note;
  Tick reference_start = chorus_notes[0].start_tick;

  motif.rhythm.clear();
  motif.contour_degrees.clear();
  motif.absolute_pitches.clear();

  for (size_t i = 0; i < note_count; ++i) {
    const auto& note = chorus_notes[i];

    // Calculate relative timing in beats
    Tick relative_tick = note.start_tick - reference_start;
    float beat_pos = static_cast<float>(relative_tick) / TICKS_PER_BEAT;

    // Determine eighths and strong beat status
    int eighths = static_cast<int>(note.duration / (TICKS_PER_BEAT / 2));
    eighths = std::max(1, std::min(8, eighths));  // Clamp to 1-8

    bool is_strong = (relative_tick % (TICKS_PER_BEAT * 2)) == 0;

    motif.rhythm.push_back({beat_pos, eighths, is_strong});

    // Calculate relative degree (from reference pitch)
    int8_t degree = static_cast<int8_t>(note.note - reference_pitch);
    motif.contour_degrees.push_back(degree);

    // Store absolute pitch for faithful melodic reproduction
    motif.absolute_pitches.push_back(note.note);
  }

  // Find climax (highest pitch)
  if (!motif.contour_degrees.empty()) {
    auto max_it = std::max_element(motif.contour_degrees.begin(),
                                   motif.contour_degrees.end());
    motif.climax_index = static_cast<uint8_t>(
        std::distance(motif.contour_degrees.begin(), max_it));
  }

  // Calculate total length in beats
  if (!motif.rhythm.empty()) {
    const auto& last_rhythm = motif.rhythm.back();
    float last_beat = last_rhythm.beat + last_rhythm.eighths * 0.5f;
    motif.length_beats = static_cast<uint8_t>(std::ceil(last_beat));
    // Round up to 4 or 8
    if (motif.length_beats <= 4) {
      motif.length_beats = 4;
    } else if (motif.length_beats <= 8) {
      motif.length_beats = 8;
    }
  }

  return motif;
}

std::vector<NoteEvent> placeMotifInIntro(const Motif& motif,
                                          Tick intro_start,
                                          Tick intro_end,
                                          uint8_t base_pitch,
                                          uint8_t velocity) {
  std::vector<NoteEvent> result;

  if (motif.rhythm.empty()) {
    return result;
  }

  // Prefer absolute pitches for faithful melodic reproduction
  bool use_absolute = !motif.absolute_pitches.empty() &&
                      motif.absolute_pitches.size() >= motif.rhythm.size();

  // Calculate octave offset to transpose motif to target register
  int octave_offset = 0;
  if (use_absolute && !motif.absolute_pitches.empty()) {
    // Find the average pitch of the motif
    int sum = 0;
    for (uint8_t p : motif.absolute_pitches) {
      sum += p;
    }
    int avg_pitch = sum / static_cast<int>(motif.absolute_pitches.size());

    // Calculate octave offset to bring motif near base_pitch
    // Negative offset = lower register (aux typically plays below vocal)
    octave_offset = ((static_cast<int>(base_pitch) - avg_pitch) / 12) * 12;
  }

  Tick motif_length_ticks = motif.length_beats * TICKS_PER_BEAT;
  Tick current_start = intro_start;

  // Repeat motif until we fill the intro section
  while (current_start + motif_length_ticks <= intro_end) {
    size_t note_count = motif.rhythm.size();
    if (!use_absolute) {
      note_count = std::min(note_count, motif.contour_degrees.size());
    }

    for (size_t i = 0; i < note_count; ++i) {
      const auto& rn = motif.rhythm[i];
      Tick note_start = current_start + static_cast<Tick>(rn.beat * TICKS_PER_BEAT);

      if (note_start >= intro_end) break;

      NoteEvent note;
      note.start_tick = note_start;
      note.duration = rn.eighths * (TICKS_PER_BEAT / 2);

      int raw_pitch;
      if (use_absolute) {
        // Use absolute pitch with octave transposition
        // This preserves the exact melodic contour of the original
        raw_pitch = static_cast<int>(motif.absolute_pitches[i]) + octave_offset;
      } else {
        // Fallback: use relative contour degrees
        raw_pitch = static_cast<int>(base_pitch) + motif.contour_degrees[i];
      }

      // Snap to scale for harmonic consistency
      raw_pitch = snapToNearestScaleTone(raw_pitch, 0);
      note.note = static_cast<uint8_t>(std::clamp(raw_pitch, 0, 127));
      note.velocity = velocity;

      result.push_back(note);
    }

    current_start += motif_length_ticks;
  }

  return result;
}

std::vector<NoteEvent> placeMotifInAux(const Motif& motif,
                                        Tick section_start,
                                        Tick section_end,
                                        uint8_t base_pitch,
                                        float velocity_ratio) {
  // Base velocity for aux track (softer than main)
  uint8_t aux_velocity = static_cast<uint8_t>(80 * velocity_ratio);

  // Use the same placement logic as intro
  return placeMotifInIntro(motif, section_start, section_end, base_pitch, aux_velocity);
}

}  // namespace midisketch
