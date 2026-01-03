#include "track/vocal.h"
#include "core/chord.h"
#include "core/velocity.h"
#include <algorithm>
#include <map>
#include <vector>

namespace midisketch {

void generateVocalTrack(MidiTrack& track, Song& song,
                        const GeneratorParams& params, std::mt19937& rng) {
  // Major scale semitones (relative to tonic)
  constexpr int SCALE[7] = {0, 2, 4, 5, 7, 9, 11};
  // Scale degree names: 0=Do, 1=Re, 2=Mi, 3=Fa, 4=Sol, 5=La, 6=Ti

  // BackgroundMotif suppression settings
  const bool is_background_motif =
      params.composition_style == CompositionStyle::BackgroundMotif;
  const MotifVocalParams& vocal_params = params.motif_vocal;

  // Calculate max interval in scale degrees from semitones
  // 4 semitones = 3rd = ~2 scale degrees
  // 7 semitones = 5th = ~4 scale degrees
  int max_interval_degrees = is_background_motif
                                 ? (vocal_params.interval_limit <= 4 ? 2 : 4)
                                 : 7;

  // Velocity reduction for background
  float velocity_scale = (is_background_motif &&
                          vocal_params.prominence == VocalProminence::Background)
                             ? 0.7f
                             : 1.0f;

  const auto& progression = getChordProgression(params.chord_id);
  int key_offset = static_cast<int>(params.key);

  // Helper: convert scale degree to pitch in given octave
  auto degreeToPitch = [&](int degree, int octave) -> int {
    int d = ((degree % 7) + 7) % 7;
    int oct_adjust = degree / 7;
    if (degree < 0 && degree % 7 != 0) oct_adjust--;
    return (octave + oct_adjust) * 12 + SCALE[d] + key_offset;
  };

  // Helper: clamp pitch to vocal range
  auto clampPitch = [&](int pitch) -> uint8_t {
    return static_cast<uint8_t>(
        std::clamp(pitch, (int)params.vocal_low, (int)params.vocal_high));
  };

  // Helper: get chord root degree and type for a bar
  auto getChordInfo = [&](int bar_in_section) -> std::pair<int, bool> {
    int chord_idx = bar_in_section % 4;
    int8_t degree = progression.degrees[chord_idx];
    Chord chord = getChordNotes(degree);
    // Return (root degree 0-6, is_minor)
    int root = (degree == 10) ? 6 : degree;  // bVII -> treat as 6
    bool is_minor = (chord.intervals[1] == 3);  // minor 3rd
    return {root, is_minor};
  };

  // Rhythmic patterns for 2-bar motifs (beat positions where notes occur)
  // Format: {beat, duration_eighths, is_rest}
  struct RhythmNote {
    float beat;      // 0.0-3.5 (in quarter notes)
    int eighths;     // duration in eighth notes
  };

  // Different rhythm patterns for variety
  std::vector<std::vector<RhythmNote>> rhythm_patterns = {
    // Pattern 0: Quarter note melody (simple, singable)
    {{0.0f, 2}, {1.0f, 2}, {2.0f, 2}, {3.0f, 2},
     {4.0f, 2}, {5.0f, 2}, {6.0f, 4}},
    // Pattern 1: Syncopated (more rhythmic interest)
    {{0.0f, 3}, {1.5f, 1}, {2.0f, 2}, {3.0f, 2},
     {4.0f, 3}, {5.5f, 1}, {6.0f, 4}},
    // Pattern 2: Long-short alternating
    {{0.0f, 3}, {1.5f, 1}, {2.0f, 3}, {3.5f, 1},
     {4.0f, 3}, {5.5f, 1}, {6.0f, 2}, {7.0f, 2}},
    // Pattern 3: Sparse (for verses)
    {{0.0f, 4}, {2.0f, 2}, {3.0f, 2},
     {4.0f, 4}, {6.0f, 4}},
  };

  // Melodic contour patterns (scale degree movements relative to chord root)
  // Each pattern is for 2 bars, ending patterns resolve
  std::vector<std::vector<int>> melody_contours = {
    // Contour 0: Arch shape (rise then fall) - stable
    {0, 1, 2, 4, 2, 1, 0},
    // Contour 1: Ascending then resolve
    {0, 1, 2, 2, 4, 2, 0},
    // Contour 2: Descending from 5th
    {4, 2, 1, 0, 2, 1, 0},
    // Contour 3: Neighbor tone motion
    {0, 1, 0, -1, 0, 1, 0},
    // Contour 4: Leap then step (expressive)
    {0, 4, 2, 1, 2, 1, 0},
  };

  // Phrase ending contours (last 2 bars of 4-bar phrase) - always resolve
  std::vector<std::vector<int>> ending_contours = {
    // End on root
    {2, 1, 0, -1, 0, 1, 0},
    // End on 5th then root
    {4, 2, 4, 2, 1, 0, 0},
    // Stepwise descent to root
    {2, 1, 0, 2, 1, 0, 0},
  };

  // Phrase cache for repetition
  std::map<SectionType, std::vector<NoteEvent>> phrase_cache;
  std::map<SectionType, int> section_occurrence;

  // Starting octave based on vocal range
  int center_pitch = (params.vocal_low + params.vocal_high) / 2;
  int base_octave = center_pitch / 12;

  const auto& sections = song.arrangement().sections();
  for (const auto& section : sections) {
    section_occurrence[section.type]++;
    bool is_repeat = (section_occurrence[section.type] > 1);
    bool use_cached = is_repeat &&
                      (phrase_cache.find(section.type) != phrase_cache.end());

    // Calculate transpose for modulation
    int8_t transpose = 0;
    if (song.modulationTick() > 0 &&
        section.start_tick >= song.modulationTick()) {
      transpose = song.modulationAmount();
    }

    if (use_cached) {
      const auto& cached = phrase_cache[section.type];
      for (const auto& note : cached) {
        Tick absolute_tick = section.start_tick + note.startTick;
        int transposed_pitch = note.note + transpose;
        transposed_pitch = std::clamp(transposed_pitch, (int)params.vocal_low,
                                      (int)params.vocal_high);
        track.addNote(absolute_tick, note.duration,
                      static_cast<uint8_t>(transposed_pitch), note.velocity);
      }
      continue;
    }

    // Generate new phrase
    std::vector<NoteEvent> phrase_notes;

    // Select patterns based on section type
    int rhythm_pattern_idx = 0;
    int contour_variation = 0;
    float note_density = 1.0f;

    switch (section.type) {
      case SectionType::Intro:
        rhythm_pattern_idx = 3;  // Sparse
        note_density = 0.5f;
        break;
      case SectionType::A:
        rhythm_pattern_idx = 0;  // Simple quarter notes
        contour_variation = 0;
        note_density = 0.85f;
        break;
      case SectionType::B:
        rhythm_pattern_idx = 1;  // Syncopated
        contour_variation = 1;
        note_density = 0.9f;
        break;
      case SectionType::Chorus:
        rhythm_pattern_idx = 2;  // More active
        contour_variation = 2;
        note_density = 1.0f;
        break;
    }

    // Apply BackgroundMotif suppression
    if (is_background_motif) {
      switch (vocal_params.rhythm_bias) {
        case VocalRhythmBias::Sparse:
          rhythm_pattern_idx = 3;  // Force sparse pattern
          note_density *= 0.5f;
          break;
        case VocalRhythmBias::OnBeat:
          rhythm_pattern_idx = 0;  // Quarter notes on beat
          note_density *= 0.7f;
          break;
        case VocalRhythmBias::OffBeat:
          rhythm_pattern_idx = 1;  // Syncopated
          note_density *= 0.7f;
          break;
      }
    }

    // Process 2-bar motifs
    for (uint8_t motif_start = 0; motif_start < section.bars; motif_start += 2) {
      bool is_phrase_ending = ((motif_start + 2) % 4 == 0);
      uint8_t bars_in_motif =
          std::min((uint8_t)2, (uint8_t)(section.bars - motif_start));

      Tick motif_start_tick = section.start_tick + motif_start * TICKS_PER_BAR;
      Tick relative_motif_start = motif_start * TICKS_PER_BAR;

      // Get chord info for this 2-bar segment
      auto [chord_root1, is_minor1] = getChordInfo(motif_start);
      auto [chord_root2, is_minor2] = getChordInfo(motif_start + 1);

      // Select contour
      std::vector<int> contour;
      if (is_phrase_ending) {
        std::uniform_int_distribution<size_t> end_dist(
            0, ending_contours.size() - 1);
        contour = ending_contours[end_dist(rng)];
      } else {
        std::uniform_int_distribution<size_t> cont_dist(
            0, melody_contours.size() - 1);
        size_t idx =
            (cont_dist(rng) + contour_variation) % melody_contours.size();
        contour = melody_contours[idx];
      }

      // Apply interval limiting for BackgroundMotif
      if (is_background_motif) {
        for (auto& degree : contour) {
          degree = std::clamp(degree, -max_interval_degrees, max_interval_degrees);
        }
      }

      // Select rhythm pattern
      std::uniform_int_distribution<int> rhythm_var(0, 1);
      int actual_rhythm = (rhythm_pattern_idx + rhythm_var(rng)) %
                          static_cast<int>(rhythm_patterns.size());
      const auto& rhythm = rhythm_patterns[actual_rhythm];

      // Generate notes for this motif
      size_t contour_idx = 0;
      for (const auto& rn : rhythm) {
        // Skip some notes based on density
        std::uniform_real_distribution<float> skip_dist(0.0f, 1.0f);
        if (skip_dist(rng) > note_density) continue;

        float beat_in_motif = rn.beat;
        int bar_offset = static_cast<int>(beat_in_motif / 4.0f);
        if (bar_offset >= bars_in_motif) continue;

        float beat_in_bar = beat_in_motif - bar_offset * 4.0f;
        Tick note_tick = motif_start_tick + bar_offset * TICKS_PER_BAR +
                         static_cast<Tick>(beat_in_bar * TICKS_PER_BEAT);
        Tick relative_tick = relative_motif_start + bar_offset * TICKS_PER_BAR +
                             static_cast<Tick>(beat_in_bar * TICKS_PER_BEAT);

        // Get the chord root for this bar
        int current_chord_root = (bar_offset == 0) ? chord_root1 : chord_root2;

        // Get contour degree (relative to chord root)
        int contour_degree = contour[contour_idx % contour.size()];
        contour_idx++;

        // Calculate actual scale degree
        int scale_degree = current_chord_root + contour_degree;

        // Convert to pitch
        int pitch = degreeToPitch(scale_degree, base_octave);

        // Ensure within vocal range
        while (pitch < params.vocal_low) pitch += 12;
        while (pitch > params.vocal_high) pitch -= 12;
        pitch = std::clamp(pitch, (int)params.vocal_low,
                           (int)params.vocal_high);

        // Duration
        Tick duration = static_cast<Tick>(rn.eighths * TICKS_PER_BEAT / 2);

        // Velocity
        uint8_t beat_num = static_cast<uint8_t>(beat_in_bar);
        uint8_t velocity = calculateVelocity(section.type, beat_num,
                                             params.mood);

        // Apply velocity scaling for BackgroundMotif
        velocity = static_cast<uint8_t>(std::clamp(
            static_cast<int>(velocity * velocity_scale), 40, 127));

        track.addNote(note_tick, duration, clampPitch(pitch), velocity);
        phrase_notes.push_back(
            {relative_tick, duration, clampPitch(pitch), velocity});
      }
    }

    phrase_cache[section.type] = std::move(phrase_notes);
  }
}

}  // namespace midisketch
