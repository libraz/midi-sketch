/**
 * @file motif.cpp
 * @brief Implementation of motif variation operations.
 */

#include "core/motif.h"

#include <algorithm>

#include "core/chord_utils.h"
#include "core/note_creator.h"
#include "core/pitch_utils.h"

namespace midisketch {

Motif applyVariation(const Motif& original, MotifVariation variation, int8_t param,
                     std::mt19937& rng) {
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
        pitch = static_cast<uint8_t>(std::clamp(static_cast<int>(pitch) + param, 0, 127));
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
        rn.eighths = std::max(0.5f, rn.eighths / 2.0f);
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
      // Apply sequential transposition: each note position shifts by (index * param / 4)
      // The "/4" divisor reduces the transposition step to create gradual contour change:
      //   - param=12 (octave): shifts 0, 3, 6, 9... semitones (gentle rise)
      //   - param=4 (M3): shifts 0, 1, 2, 3... semitones (very subtle)
      // Without division, param=12 would create 0, 12, 24... (too extreme for a sequence)
      for (size_t i = 0; i < result.contour_degrees.size(); ++i) {
        result.contour_degrees[i] += static_cast<int8_t>(i) * param / 4;
      }
      for (size_t i = 0; i < result.absolute_pitches.size(); ++i) {
        int offset = static_cast<int>(i) * param / 4;
        result.absolute_pitches[i] = static_cast<uint8_t>(
            std::clamp(static_cast<int>(result.absolute_pitches[i]) + offset, 0, 127));
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
              result.absolute_pitches[i] = static_cast<uint8_t>(
                  std::clamp(static_cast<int>(result.absolute_pitches[i]) + delta, 0, 127));
            }
          }
        }
      }
      break;
  }

  return result;
}

// Memorable hook contour patterns (J-POP/K-POP style)
// Each pattern is 6 notes with clear melodic structure
// Values are semitone offsets from base pitch
constexpr int8_t kMemorableHookContours[][6] = {
    // Type 0: Pedal Tone - repetition with color notes (default for hook_repetition)
    // "Earworm" style - same note returns, small variations between
    // Uses only 3 distinct values (0, 2, 4) for memorability
    {0, 0, 2, 0, 4, 0},

    // Type 1: Rising Arch - gradual rise to peak, then resolve
    // Classic J-POP pattern: builds tension, releases at end
    {0, 2, 4, 5, 4, 0},

    // Type 2: Question-Answer - ascending question, descending answer
    // Creates call-response feel within the hook
    {0, 4, 2, 0, 4, 0},

    // Type 3: Leap-Step - dramatic leap then stepwise return
    // High impact opening, memorable first impression
    {0, 7, 5, 4, 2, 0},

    // Type 4: Wave - gentle oscillation building to peak
    // Flowing, singable melodic line
    {0, 2, 0, 4, 2, 0},

    // Type 5: Climax Rush - steady climb to high point
    // Dramatic buildup effect
    {0, 2, 4, 5, 7, 5},
};
constexpr size_t kMemorableHookContourCount =
    sizeof(kMemorableHookContours) / sizeof(kMemorableHookContours[0]);

Motif designChorusHook(const StyleMelodyParams& params, std::mt19937& rng) {
  Motif hook;
  hook.length_beats = 8;  // 2-bar hook
  hook.ends_on_chord_tone = true;

  // Select contour pattern:
  // - hook_repetition=true: use fixed pattern (Type 0) for maximum memorability
  // - hook_repetition=false: random selection for variety
  size_t contour_idx = 0;
  if (!params.hook_repetition) {
    std::uniform_int_distribution<size_t> contour_dist(
        1, kMemorableHookContourCount - 1);  // Skip Type 0 (reserved for repetition)
    contour_idx = contour_dist(rng);
  }
  const int8_t* selected_contour = kMemorableHookContours[contour_idx];

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
  } else {
    // Standard style: syncopated feel
    hook.rhythm = {
        {0.0f, 2, true},   // Beat 1: quarter note
        {1.0f, 2, false},  // Beat 2: quarter note
        {2.0f, 2, true},   // Beat 3: quarter note
        {3.0f, 2, false},  // Beat 4: quarter note
        {4.0f, 3, true},   // Beat 1 (bar 2): dotted quarter - climax
        {5.5f, 3, true},   // Beat 2.5: dotted quarter
    };
    hook.climax_index = 4;  // Fifth note is the climax
  }

  // Use complete contour pattern (no zero-padding!)
  hook.contour_degrees.clear();
  for (size_t i = 0; i < 6 && i < hook.rhythm.size(); ++i) {
    hook.contour_degrees.push_back(selected_contour[i]);
  }

  // If rhythm has more notes than contour, use ABAB structure
  // (repeat the pattern with slight variation)
  while (hook.contour_degrees.size() < hook.rhythm.size()) {
    size_t idx = hook.contour_degrees.size() % 6;
    // Second half: slight variation (-2 semitones for "answer" feel)
    int8_t varied = selected_contour[idx];
    if (hook.contour_degrees.size() >= 6) {
      varied = std::max(static_cast<int8_t>(-2), static_cast<int8_t>(varied - 2));
    }
    hook.contour_degrees.push_back(varied);
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
  return variation == MotifVariation::Exact || variation == MotifVariation::Fragmented;
}

Motif extractMotifFromChorus(const std::vector<NoteEvent>& chorus_notes, size_t max_notes) {
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
    float eighths = static_cast<float>(note.duration) / (TICKS_PER_BEAT / 2);
    eighths = std::max(0.5f, std::min(8.0f, eighths));  // Clamp to 0.5-8

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
    auto max_it = std::max_element(motif.contour_degrees.begin(), motif.contour_degrees.end());
    motif.climax_index = static_cast<uint8_t>(std::distance(motif.contour_degrees.begin(), max_it));
  }

  // Calculate total length in beats
  if (!motif.rhythm.empty()) {
    const auto& last_rhythm = motif.rhythm.back();
    float last_beat = last_rhythm.beat + last_rhythm.eighths * 0.5f;
    motif.length_beats = static_cast<uint8_t>(std::ceil(last_beat));
    // Round up to 4 or 8, cap at 8 for practical placement
    if (motif.length_beats <= 4) {
      motif.length_beats = 4;
    } else {
      // Cap at 8 beats (2 bars) - longer motifs are truncated for intro placement
      motif.length_beats = 8;
    }
  }

  return motif;
}

std::vector<NoteEvent> placeMotifInIntro(const Motif& motif, Tick intro_start, Tick intro_end,
                                         uint8_t base_pitch, uint8_t velocity) {
  std::vector<NoteEvent> result;

  if (motif.rhythm.empty()) {
    return result;
  }

  // Prefer absolute pitches for faithful melodic reproduction
  bool use_absolute =
      !motif.absolute_pitches.empty() && motif.absolute_pitches.size() >= motif.rhythm.size();

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

      NoteEvent note = NoteEventBuilder::createDefault();
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

std::vector<NoteEvent> placeMotifInAux(const Motif& motif, Tick section_start, Tick section_end,
                                       uint8_t base_pitch, float velocity_ratio) {
  // Base velocity for aux track (softer than main)
  uint8_t aux_velocity = static_cast<uint8_t>(80 * velocity_ratio);

  // Use the same placement logic as intro
  return placeMotifInIntro(motif, section_start, section_end, base_pitch, aux_velocity);
}

std::vector<NoteEvent> placeMotifInBridge(const Motif& motif, Tick section_start, Tick section_end,
                                          uint8_t base_pitch, uint8_t velocity, std::mt19937& rng,
                                          const IHarmonyContext& harmony, TrackRole track) {
  // Apply variation for Bridge: 50% Inverted, 50% Fragmented
  // This creates contrast while maintaining thematic connection
  std::uniform_int_distribution<int> var_dist(0, 1);
  MotifVariation variation =
      (var_dist(rng) == 0) ? MotifVariation::Inverted : MotifVariation::Fragmented;

  Motif varied = applyVariation(motif, variation, 0, rng);

  // Place the varied motif
  // For Bridge, use slightly softer velocity for contemplative feel
  uint8_t bridge_velocity = static_cast<uint8_t>(velocity * 0.85f);
  auto notes = placeMotifInIntro(varied, section_start, section_end, base_pitch, bridge_velocity);

  // Snap each note to chord tone and check for collisions
  // This matches the working Intro implementation in aux_track.cpp
  std::vector<NoteEvent> safe_notes;
  for (auto& note : notes) {
    int8_t chord_degree = harmony.getChordDegreeAt(note.start_tick);

    // Step 1: Snap to nearest chord tone (same as Intro)
    int snapped = nearestChordTonePitch(note.note, chord_degree);
    snapped = std::clamp(snapped, 36, 108);

    // Step 2: Check for collisions and find safe pitch if needed
    uint8_t final_pitch = static_cast<uint8_t>(snapped);
    auto candidates = getSafePitchCandidates(harmony, final_pitch, note.start_tick, note.duration,
                                              track, 36, 96);
    if (candidates.empty()) {
      continue;  // No safe pitch available
    }
    final_pitch = candidates[0].pitch;

    note.note = final_pitch;
    safe_notes.push_back(note);
  }

  return safe_notes;
}

std::vector<NoteEvent> placeMotifInFinalChorus(const Motif& motif, Tick section_start,
                                                Tick section_end, uint8_t base_pitch,
                                                uint8_t velocity, const IHarmonyContext& harmony,
                                                TrackRole track) {
  std::vector<NoteEvent> result;

  if (motif.rhythm.empty()) {
    return result;
  }

  // Enhanced velocity for climax (boost by 10-15%)
  uint8_t enhanced_velocity = static_cast<uint8_t>(std::min(127, static_cast<int>(velocity) + 12));

  // Prefer absolute pitches for faithful melodic reproduction
  bool use_absolute =
      !motif.absolute_pitches.empty() && motif.absolute_pitches.size() >= motif.rhythm.size();

  // Calculate octave offset to transpose motif to target register
  int octave_offset = 0;
  if (use_absolute && !motif.absolute_pitches.empty()) {
    int sum = 0;
    for (uint8_t p : motif.absolute_pitches) {
      sum += p;
    }
    int avg_pitch = sum / static_cast<int>(motif.absolute_pitches.size());
    octave_offset = ((static_cast<int>(base_pitch) - avg_pitch) / 12) * 12;
  }

  Tick motif_length_ticks = motif.length_beats * TICKS_PER_BEAT;
  Tick current_start = section_start;

  // Repeat motif until we fill the section
  while (current_start + motif_length_ticks <= section_end) {
    size_t note_count = motif.rhythm.size();
    if (!use_absolute) {
      note_count = std::min(note_count, motif.contour_degrees.size());
    }

    for (size_t i = 0; i < note_count; ++i) {
      const auto& rn = motif.rhythm[i];
      Tick note_start = current_start + static_cast<Tick>(rn.beat * TICKS_PER_BEAT);

      if (note_start >= section_end) break;

      int raw_pitch;
      if (use_absolute) {
        raw_pitch = static_cast<int>(motif.absolute_pitches[i]) + octave_offset;
      } else {
        raw_pitch = static_cast<int>(base_pitch) + motif.contour_degrees[i];
      }

      // Snap to scale first
      raw_pitch = snapToNearestScaleTone(raw_pitch, 0);
      Tick duration = rn.eighths * (TICKS_PER_BEAT / 2);

      // Get chord degree at this tick and snap to chord tone
      int8_t chord_degree = harmony.getChordDegreeAt(note_start);
      int snapped = nearestChordTonePitch(raw_pitch, chord_degree);
      snapped = std::clamp(snapped, 36, 108);

      // Check for collisions and find safe pitch if needed
      uint8_t final_pitch = static_cast<uint8_t>(snapped);
      auto candidates = getSafePitchCandidates(harmony, final_pitch, note_start, duration,
                                                track, 36, 96);
      if (candidates.empty()) {
        continue;  // No safe pitch available
      }
      final_pitch = candidates[0].pitch;

      // Primary note with provenance
      auto primary_note = createNoteWithoutHarmony(note_start, duration, final_pitch, enhanced_velocity);
#ifdef MIDISKETCH_NOTE_PROVENANCE
      primary_note.prov_source = static_cast<uint8_t>(NoteSource::Motif);
#endif
      result.push_back(primary_note);

      // Octave doubling for climactic impact - only add if within range AND safe
      int octave_pitch = final_pitch + 12;
      if (octave_pitch <= 108 && harmony.isPitchSafe(static_cast<uint8_t>(octave_pitch), note_start,
                                                     duration, track)) {
        auto octave_note = createNoteWithoutHarmony(note_start, duration,
                                                    static_cast<uint8_t>(octave_pitch),
                                                    static_cast<uint8_t>(enhanced_velocity * 0.85f));
#ifdef MIDISKETCH_NOTE_PROVENANCE
        octave_note.prov_source = static_cast<uint8_t>(NoteSource::Motif);
#endif
        result.push_back(octave_note);
      }
    }

    current_start += motif_length_ticks;
  }

  return result;
}

}  // namespace midisketch
