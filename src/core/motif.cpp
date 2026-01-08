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
      break;

    case MotifVariation::Inverted:
      // Invert contour around the first note
      if (!result.contour_degrees.empty()) {
        int8_t pivot = result.contour_degrees[0];
        for (auto& degree : result.contour_degrees) {
          degree = pivot - (degree - pivot);
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
        result.length_beats /= 2;
      }
      break;

    case MotifVariation::Sequenced:
      // Apply sequential transposition (each repetition shifts by param)
      // This version just applies a single step
      for (size_t i = 0; i < result.contour_degrees.size(); ++i) {
        result.contour_degrees[i] += static_cast<int8_t>(i) * param / 4;
      }
      break;

    case MotifVariation::Embellished:
      // Add passing tones (simplified: just add some variation to degrees)
      {
        std::uniform_int_distribution<int> dist(-1, 1);
        for (size_t i = 1; i < result.contour_degrees.size() - 1; ++i) {
          if (!result.rhythm[i].strong) {
            result.contour_degrees[i] += static_cast<int8_t>(dist(rng));
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

    // Ice Cream-style: short, repetitive contour (2-3 notes)
    hook.contour_degrees = {0, 0, 2};
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

    // Ice Cream-style: short, repetitive contour (2-3 notes)
    hook.contour_degrees = {0, 2, 0};
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
  Tick reference_start = chorus_notes[0].startTick;

  motif.rhythm.clear();
  motif.contour_degrees.clear();

  for (size_t i = 0; i < note_count; ++i) {
    const auto& note = chorus_notes[i];

    // Calculate relative timing in beats
    Tick relative_tick = note.startTick - reference_start;
    float beat_pos = static_cast<float>(relative_tick) / TICKS_PER_BEAT;

    // Determine eighths and strong beat status
    int eighths = static_cast<int>(note.duration / (TICKS_PER_BEAT / 2));
    eighths = std::max(1, std::min(8, eighths));  // Clamp to 1-8

    bool is_strong = (relative_tick % (TICKS_PER_BEAT * 2)) == 0;

    motif.rhythm.push_back({beat_pos, eighths, is_strong});

    // Calculate relative degree (from reference pitch)
    int8_t degree = static_cast<int8_t>(note.note - reference_pitch);
    motif.contour_degrees.push_back(degree);
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

  if (motif.rhythm.empty() || motif.contour_degrees.empty()) {
    return result;
  }

  Tick motif_length_ticks = motif.length_beats * TICKS_PER_BEAT;
  Tick current_start = intro_start;

  // Repeat motif until we fill the intro section
  while (current_start + motif_length_ticks <= intro_end) {
    size_t note_count = std::min(motif.rhythm.size(), motif.contour_degrees.size());

    for (size_t i = 0; i < note_count; ++i) {
      const auto& rn = motif.rhythm[i];
      Tick note_start = current_start + static_cast<Tick>(rn.beat * TICKS_PER_BEAT);

      if (note_start >= intro_end) break;

      NoteEvent note;
      note.startTick = note_start;
      note.duration = rn.eighths * (TICKS_PER_BEAT / 2);
      // Calculate pitch and snap to scale (key_offset=0 for C major)
      int raw_pitch = static_cast<int>(base_pitch) + motif.contour_degrees[i];
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
