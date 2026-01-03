#include "track/vocal.h"
#include "core/chord.h"
#include "core/velocity.h"
#include <algorithm>
#include <array>
#include <map>
#include <vector>

namespace midisketch {

namespace {

// Major scale semitones (relative to tonic)
constexpr int SCALE[7] = {0, 2, 4, 5, 7, 9, 11};

// Chord tones for each scale degree (0=root, 2=3rd, 4=5th in scale degrees)
// For major chord: root, major 3rd, perfect 5th
// For minor chord: root, minor 3rd, perfect 5th
struct ChordTones {
  std::array<int, 3> degrees;  // Scale degrees that are chord tones
};

// Get chord tones for a chord built on given scale degree
ChordTones getChordTones(int root_degree, bool /* is_minor */) {
  // Chord tones in scale degrees relative to the key
  // root = root_degree
  // 3rd = root_degree + 2 (scale degrees)
  // 5th = root_degree + 4 (scale degrees)
  // Note: is_minor currently unused as we use scale degrees, not semitones
  return {{root_degree, root_degree + 2, root_degree + 4}};
}

// Check if a scale degree is a chord tone
bool isChordTone(int scale_degree, int chord_root, bool is_minor) {
  ChordTones ct = getChordTones(chord_root, is_minor);
  int normalized = ((scale_degree % 7) + 7) % 7;
  for (int tone : ct.degrees) {
    if (((tone % 7) + 7) % 7 == normalized) return true;
  }
  return false;
}

// Get nearest chord tone to a given scale degree
int nearestChordTone(int scale_degree, int chord_root, bool is_minor) {
  ChordTones ct = getChordTones(chord_root, is_minor);
  int best = ct.degrees[0];
  int best_dist = 100;
  for (int tone : ct.degrees) {
    int dist = std::abs(scale_degree - tone);
    if (dist < best_dist) {
      best = tone;
      best_dist = dist;
    }
  }
  return best;
}

// Convert scale degree to pitch
int degreeToPitch(int degree, int octave, int key_offset) {
  int d = ((degree % 7) + 7) % 7;
  int oct_adjust = degree / 7;
  if (degree < 0 && degree % 7 != 0) oct_adjust--;
  return (octave + oct_adjust) * 12 + SCALE[d] + key_offset;
}

// Non-harmonic tone type
enum class NonHarmonicType {
  None,         // Regular note
  Suspension,   // Held from previous chord, resolves down
  Anticipation  // Early arrival of next chord tone
};

// Rhythm note structure
struct RhythmNote {
  float beat;      // 0.0-7.5 (in quarter notes, 2 bars)
  int eighths;     // duration in eighth notes
  bool strong;     // true if on strong beat (1 or 3)
  NonHarmonicType non_harmonic = NonHarmonicType::None;
};

// Get rhythm patterns with strong beat marking
std::vector<std::vector<RhythmNote>> getRhythmPatterns() {
  return {
    // Pattern 0: Quarter note melody (simple, singable)
    {{0.0f, 2, true}, {1.0f, 2, false}, {2.0f, 2, true}, {3.0f, 2, false},
     {4.0f, 2, true}, {5.0f, 2, false}, {6.0f, 4, true}},
    // Pattern 1: Syncopated (more rhythmic interest)
    {{0.0f, 3, true}, {1.5f, 1, false}, {2.0f, 2, true}, {3.0f, 2, false},
     {4.0f, 3, true}, {5.5f, 1, false}, {6.0f, 4, true}},
    // Pattern 2: Long-short alternating
    {{0.0f, 3, true}, {1.5f, 1, false}, {2.0f, 3, true}, {3.5f, 1, false},
     {4.0f, 3, true}, {5.5f, 1, false}, {6.0f, 2, true}, {7.0f, 2, false}},
    // Pattern 3: Sparse (for verses)
    {{0.0f, 4, true}, {2.0f, 2, true}, {3.0f, 2, false},
     {4.0f, 4, true}, {6.0f, 4, true}},
  };
}

// Melodic contour that respects chord tones on strong beats
struct MelodicContour {
  std::vector<int> degrees;    // Relative scale degrees
  std::vector<bool> use_chord_tone;  // Force chord tone at this position
};

// Get contour patterns with chord tone markers
std::vector<MelodicContour> getMelodicContours() {
  return {
    // Contour 0: Arch shape - chord tones on strong beats
    {{0, 1, 2, 4, 2, 1, 0}, {true, false, true, true, true, false, true}},
    // Contour 1: Ascending then resolve
    {{0, 1, 2, 2, 4, 2, 0}, {true, false, true, false, true, false, true}},
    // Contour 2: Descending from 5th
    {{4, 2, 1, 0, 2, 1, 0}, {true, false, true, false, true, false, true}},
    // Contour 3: Neighbor tone motion
    {{0, 1, 0, -1, 0, 1, 0}, {true, false, true, false, true, false, true}},
    // Contour 4: Leap then step (expressive)
    {{0, 4, 2, 1, 2, 1, 0}, {true, true, true, false, true, false, true}},
  };
}

// Phrase ending contours - always end on stable chord tone
std::vector<MelodicContour> getEndingContours() {
  return {
    // End on root (resolution)
    {{2, 1, 0, -1, 0, 1, 0}, {true, false, true, false, true, false, true}},
    // 5th then root (authentic cadence feel)
    {{4, 2, 4, 2, 1, 0, 0}, {true, true, true, true, false, true, true}},
    // Stepwise descent to root
    {{2, 1, 0, 2, 1, 0, 0}, {true, false, true, true, false, true, true}},
  };
}

// Apply suspension: use 4th instead of 3rd, then resolve
// Returns: {suspension_degree, resolution_degree, resolution_duration_eighths}
struct SuspensionResult {
  int suspension_degree;    // The suspended note (usually 4th = root + 3)
  int resolution_degree;    // The resolution (usually 3rd = root + 2)
  int suspension_eighths;   // Duration of suspension
  int resolution_eighths;   // Duration of resolution
};

SuspensionResult applySuspension(int chord_root, int original_duration_eighths) {
  // 4-3 suspension: hold the 4th, resolve to 3rd
  int suspension = chord_root + 3;  // 4th scale degree above root
  int resolution = chord_root + 2;  // 3rd scale degree above root

  // Split duration: suspension takes most, resolution takes rest
  int sus_dur = std::max(1, original_duration_eighths * 2 / 3);
  int res_dur = std::max(1, original_duration_eighths - sus_dur);

  return {suspension, resolution, sus_dur, res_dur};
}

// Apply anticipation: shift the note earlier and use next chord's tone
struct AnticipationResult {
  float beat_offset;        // How much earlier (negative in beats)
  int degree;               // The anticipated note (from next chord)
  int duration_eighths;     // Duration of anticipation
};

AnticipationResult applyAnticipation(int next_chord_root, int original_duration_eighths) {
  // Anticipate by an eighth note
  float offset = -0.5f;  // Half beat earlier

  // Use the root of the next chord as the anticipated note
  int anticipated = next_chord_root;

  // Short duration for the anticipation
  int dur = std::min(1, original_duration_eighths);

  return {offset, anticipated, dur};
}

// Check if suspension is appropriate at this position
bool shouldUseSuspension(float beat, SectionType section, std::mt19937& rng) {
  // Suspensions work best on strong beats at phrase beginnings
  bool is_strong = (static_cast<int>(beat) % 2 == 0);

  // More likely in emotional sections
  float prob = 0.0f;
  if (section == SectionType::B || section == SectionType::Chorus) {
    prob = is_strong ? 0.15f : 0.05f;
  } else {
    prob = is_strong ? 0.08f : 0.0f;
  }

  std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  return dist(rng) < prob;
}

// Check if anticipation is appropriate at this position
bool shouldUseAnticipation(float beat, SectionType section, std::mt19937& rng) {
  // Anticipations work best on off-beats near chord changes
  bool near_bar_end = (beat >= 3.0f && beat < 4.0f) ||
                      (beat >= 7.0f && beat < 8.0f);

  float prob = 0.0f;
  if (section == SectionType::Chorus) {
    prob = near_bar_end ? 0.2f : 0.05f;
  } else if (section == SectionType::B) {
    prob = near_bar_end ? 0.12f : 0.03f;
  }

  std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  return dist(rng) < prob;
}

}  // namespace

void generateVocalTrack(MidiTrack& track, Song& song,
                        const GeneratorParams& params, std::mt19937& rng,
                        const MidiTrack* motif_track) {
  // BackgroundMotif suppression settings
  const bool is_background_motif =
      params.composition_style == CompositionStyle::BackgroundMotif;
  const MotifVocalParams& vocal_params = params.motif_vocal;

  // Calculate max interval in scale degrees
  int max_interval_degrees = is_background_motif
                                 ? (vocal_params.interval_limit <= 4 ? 2 : 4)
                                 : 7;

  // Velocity reduction for background
  float velocity_scale = (is_background_motif &&
                          vocal_params.prominence == VocalProminence::Background)
                             ? 0.7f
                             : 1.0f;

  // Effective vocal range (adjusted based on motif track if present)
  uint8_t effective_vocal_low = params.vocal_low;
  uint8_t effective_vocal_high = params.vocal_high;

  // Adjust vocal range to avoid collision with motif track
  if (is_background_motif && motif_track != nullptr && !motif_track->empty()) {
    auto [motif_low, motif_high] = motif_track->analyzeRange();

    // If motif is in high register (above C5 = 72)
    if (motif_high > 72) {
      // Limit vocal high to avoid overlap
      effective_vocal_high = std::min(effective_vocal_high, static_cast<uint8_t>(72));
      // Ensure minimum range of one octave
      if (effective_vocal_high - effective_vocal_low < 12) {
        effective_vocal_low = std::max(static_cast<uint8_t>(48),
                                        static_cast<uint8_t>(effective_vocal_high - 12));
      }
    }
    // If motif is in low register (below C4 = 60)
    else if (motif_low < 60) {
      // Raise vocal low to avoid overlap
      effective_vocal_low = std::max(effective_vocal_low, static_cast<uint8_t>(65));
      // Ensure minimum range of one octave
      if (effective_vocal_high - effective_vocal_low < 12) {
        effective_vocal_high = std::min(static_cast<uint8_t>(96),
                                         static_cast<uint8_t>(effective_vocal_low + 12));
      }
    }
  }

  const auto& progression = getChordProgression(params.chord_id);
  int key_offset = static_cast<int>(params.key);

  // Helper: clamp pitch to effective vocal range
  auto clampPitch = [&](int pitch) -> uint8_t {
    return static_cast<uint8_t>(
        std::clamp(pitch, (int)effective_vocal_low, (int)effective_vocal_high));
  };

  // Helper: get chord info for a bar
  auto getChordInfo = [&](int bar_in_section) -> std::pair<int, bool> {
    int chord_idx = bar_in_section % 4;
    int8_t degree = progression.degrees[chord_idx];
    Chord chord = getChordNotes(degree);
    int root = (degree == 10) ? 6 : degree;  // bVII -> treat as 6
    bool is_minor = (chord.intervals[1] == 3);
    return {root, is_minor};
  };

  auto rhythm_patterns = getRhythmPatterns();
  auto melody_contours = getMelodicContours();
  auto ending_contours = getEndingContours();

  // Phrase cache for repetition
  std::map<SectionType, std::vector<NoteEvent>> phrase_cache;
  std::map<SectionType, int> section_occurrence;

  // Starting octave based on vocal range
  int center_pitch = (effective_vocal_low + effective_vocal_high) / 2;
  int base_octave = center_pitch / 12;

  // Track previous pitch for leap checking
  int prev_pitch = -1;
  int prev_interval = 0;

  const auto& sections = song.arrangement().sections();
  for (const auto& section : sections) {
    // Skip instrumental sections (no vocal melody)
    if (section.type == SectionType::Intro ||
        section.type == SectionType::Interlude ||
        section.type == SectionType::Outro) {
      continue;
    }

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
      // Compute section-specific range for proper clamping
      // (must match the range used when the phrase was originally generated)
      int8_t cache_register_shift = 0;
      switch (section.type) {
        case SectionType::A: cache_register_shift = -2; break;
        case SectionType::B: cache_register_shift = 2; break;
        case SectionType::Chorus: cache_register_shift = 5; break;
        default: break;
      }
      int cache_vocal_low = std::clamp(
          static_cast<int>(effective_vocal_low) + cache_register_shift, 36, 96);
      int cache_vocal_high = std::clamp(
          static_cast<int>(effective_vocal_high) + cache_register_shift, 36, 96);

      const auto& cached = phrase_cache[section.type];
      for (const auto& note : cached) {
        Tick absolute_tick = section.start_tick + note.startTick;
        int transposed_pitch = note.note + transpose;
        // Clamp to section-specific range (same as when originally generated)
        transposed_pitch = std::clamp(transposed_pitch, cache_vocal_low, cache_vocal_high);
        track.addNote(absolute_tick, note.duration,
                      static_cast<uint8_t>(transposed_pitch), note.velocity);
      }
      continue;
    }

    // Generate new phrase
    std::vector<NoteEvent> phrase_notes;
    prev_pitch = -1;
    prev_interval = 0;

    // Select patterns based on section type
    int rhythm_pattern_idx = 0;
    int contour_variation = 0;
    float note_density = 1.0f;

    // Section-specific melody parameters
    // register_shift: shift vocal range (semitones, positive = higher)
    // interval_boost: allow larger melodic leaps
    int8_t register_shift = 0;
    int8_t interval_boost = 0;

    switch (section.type) {
      case SectionType::Intro:
      case SectionType::Interlude:
      case SectionType::Outro:
        // These are skipped above, but handle for completeness
        rhythm_pattern_idx = 3;
        note_density = 0.5f;
        break;
      case SectionType::A:
        // A melody: restrained, mid-low register
        rhythm_pattern_idx = 0;
        contour_variation = 0;
        note_density = 0.7f;      // More sparse (was 0.85)
        register_shift = -2;      // Lower register
        interval_boost = 0;
        break;
      case SectionType::B:
        // B melody: building tension, rising register
        rhythm_pattern_idx = 1;
        contour_variation = 1;
        note_density = 0.85f;     // Medium density
        register_shift = 2;       // Slightly higher
        interval_boost = 1;       // Slightly larger leaps
        break;
      case SectionType::Chorus:
        // Chorus: climactic, high register, emphatic
        rhythm_pattern_idx = 2;
        contour_variation = 2;
        note_density = 1.0f;      // Full density
        register_shift = 5;       // Higher register (+5 semitones)
        interval_boost = 2;       // Allow bigger leaps for emotion
        break;
      case SectionType::Bridge:
        // Bridge: contrasting, reflective
        rhythm_pattern_idx = 3;
        contour_variation = 3;
        note_density = 0.6f;      // Sparse
        register_shift = 0;
        interval_boost = 0;
        break;
    }

    // Apply register shift to effective range for this section
    int section_vocal_low = static_cast<int>(effective_vocal_low) + register_shift;
    int section_vocal_high = static_cast<int>(effective_vocal_high) + register_shift;
    // Clamp to valid MIDI range
    section_vocal_low = std::clamp(section_vocal_low, 36, 96);
    section_vocal_high = std::clamp(section_vocal_high, 36, 96);

    // Apply BackgroundMotif suppression
    if (is_background_motif) {
      switch (vocal_params.rhythm_bias) {
        case VocalRhythmBias::Sparse:
          rhythm_pattern_idx = 3;
          note_density *= 0.5f;
          break;
        case VocalRhythmBias::OnBeat:
          rhythm_pattern_idx = 0;
          note_density *= 0.7f;
          break;
        case VocalRhythmBias::OffBeat:
          rhythm_pattern_idx = 1;
          note_density *= 0.7f;
          break;
      }
    }

    // Chorus hook: store first 2-bar phrase and repeat it
    std::vector<NoteEvent> chorus_hook_notes;
    bool is_chorus = (section.type == SectionType::Chorus);

    // Process 2-bar motifs
    for (uint8_t motif_start = 0; motif_start < section.bars; motif_start += 2) {
      bool is_phrase_ending = ((motif_start + 2) % 4 == 0);
      uint8_t bars_in_motif =
          std::min((uint8_t)2, (uint8_t)(section.bars - motif_start));

      Tick motif_start_tick = section.start_tick + motif_start * TICKS_PER_BAR;
      Tick relative_motif_start = motif_start * TICKS_PER_BAR;

      // For chorus: repeat the hook phrase every 4 bars (except phrase endings)
      bool use_chorus_hook = is_chorus && motif_start > 0 &&
                             !chorus_hook_notes.empty() &&
                             (motif_start % 4 == 0) && !is_phrase_ending;

      if (use_chorus_hook) {
        // Repeat the chorus hook with slight variation
        for (const auto& note : chorus_hook_notes) {
          Tick absolute_tick = motif_start_tick + note.startTick;
          // Apply slight pitch variation for interest
          int varied_pitch = note.note;
          if (motif_start >= 4) {
            // Second repetition: transpose up slightly for climax
            varied_pitch = std::min(127, note.note + 2);
          }
          varied_pitch = std::clamp(varied_pitch, section_vocal_low, section_vocal_high);
          track.addNote(absolute_tick, note.duration,
                        static_cast<uint8_t>(varied_pitch), note.velocity);
          phrase_notes.push_back({note.startTick + relative_motif_start,
                                  note.duration,
                                  static_cast<uint8_t>(varied_pitch),
                                  note.velocity});
        }
        continue;
      }

      // Get chord info for this 2-bar segment
      auto [chord_root1, is_minor1] = getChordInfo(motif_start);
      auto [chord_root2, is_minor2] = getChordInfo(motif_start + 1);

      // Select contour
      MelodicContour contour;
      if (is_phrase_ending) {
        std::uniform_int_distribution<size_t> end_dist(
            0, ending_contours.size() - 1);
        contour = ending_contours[end_dist(rng)];
      } else {
        std::uniform_int_distribution<size_t> cont_dist(
            0, melody_contours.size() - 1);
        size_t idx = (cont_dist(rng) + contour_variation) % melody_contours.size();
        contour = melody_contours[idx];
      }

      // Apply interval limiting for BackgroundMotif
      if (is_background_motif) {
        for (auto& degree : contour.degrees) {
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

        // Get the chord for this bar
        int current_chord_root = (bar_offset == 0) ? chord_root1 : chord_root2;
        bool current_is_minor = (bar_offset == 0) ? is_minor1 : is_minor2;

        // Get contour degree
        int contour_degree = contour.degrees[contour_idx % contour.degrees.size()];
        bool force_chord_tone = contour.use_chord_tone[contour_idx % contour.use_chord_tone.size()];

        // Calculate scale degree
        int scale_degree = current_chord_root + contour_degree;

        // On strong beats or marked positions, use chord tones
        if (rn.strong || force_chord_tone) {
          if (!isChordTone(scale_degree, current_chord_root, current_is_minor)) {
            scale_degree = nearestChordTone(scale_degree, current_chord_root, current_is_minor);
          }
        }

        contour_idx++;

        // Convert to pitch
        int pitch = degreeToPitch(scale_degree, base_octave, key_offset);

        // Check for large leap (6+ semitones) and apply step-back rule
        if (prev_pitch > 0) {
          int interval = pitch - prev_pitch;

          // If previous was a large leap, move in opposite direction by step
          if (std::abs(prev_interval) >= 7) {  // 5th or larger
            if (prev_interval > 0 && interval > 0) {
              // Was ascending leap, should descend
              pitch = prev_pitch - 2;  // Step down
            } else if (prev_interval < 0 && interval < 0) {
              // Was descending leap, should ascend
              pitch = prev_pitch + 2;  // Step up
            }
          }

          prev_interval = pitch - prev_pitch;
        }

        // Ensure within section-specific vocal range
        while (pitch < section_vocal_low) pitch += 12;
        while (pitch > section_vocal_high) pitch -= 12;
        pitch = std::clamp(pitch, section_vocal_low, section_vocal_high);

        prev_pitch = pitch;

        // Duration
        Tick duration = static_cast<Tick>(rn.eighths * TICKS_PER_BEAT / 2);

        // Velocity - stronger on chord tones
        uint8_t beat_num = static_cast<uint8_t>(beat_in_bar);
        uint8_t velocity = calculateVelocity(section.type, beat_num, params.mood);

        // Apply velocity scaling for BackgroundMotif
        velocity = static_cast<uint8_t>(std::clamp(
            static_cast<int>(velocity * velocity_scale), 40, 127));

        // Determine next chord for anticipation (if applicable)
        int next_chord_root = (bar_offset == 0) ? chord_root2 : chord_root1;

        // Check for suspension or anticipation (not in BackgroundMotif mode)
        bool use_suspension = !is_background_motif &&
                              rn.eighths >= 2 &&
                              shouldUseSuspension(beat_in_motif, section.type, rng);
        bool use_anticipation = !is_background_motif &&
                                !use_suspension &&
                                shouldUseAnticipation(beat_in_motif, section.type, rng);

        if (use_suspension) {
          // Apply 4-3 suspension: suspended note + resolution
          SuspensionResult sus = applySuspension(current_chord_root, rn.eighths);

          int sus_pitch = degreeToPitch(sus.suspension_degree, base_octave, key_offset);
          while (sus_pitch < effective_vocal_low) sus_pitch += 12;
          while (sus_pitch > effective_vocal_high) sus_pitch -= 12;
          sus_pitch = std::clamp(sus_pitch, (int)effective_vocal_low, (int)effective_vocal_high);

          int res_pitch = degreeToPitch(sus.resolution_degree, base_octave, key_offset);
          while (res_pitch < effective_vocal_low) res_pitch += 12;
          while (res_pitch > effective_vocal_high) res_pitch -= 12;
          res_pitch = std::clamp(res_pitch, (int)effective_vocal_low, (int)effective_vocal_high);

          Tick sus_duration = static_cast<Tick>(sus.suspension_eighths * TICKS_PER_BEAT / 2);
          Tick res_duration = static_cast<Tick>(sus.resolution_eighths * TICKS_PER_BEAT / 2);

          // Add suspension note
          track.addNote(note_tick, sus_duration, clampPitch(sus_pitch), velocity);
          phrase_notes.push_back({relative_tick, sus_duration, clampPitch(sus_pitch), velocity});

          // Add resolution note
          Tick res_tick = note_tick + sus_duration;
          Tick relative_res_tick = relative_tick + sus_duration;
          uint8_t res_vel = static_cast<uint8_t>(velocity * 0.9f);
          track.addNote(res_tick, res_duration, clampPitch(res_pitch), res_vel);
          phrase_notes.push_back({relative_res_tick, res_duration, clampPitch(res_pitch), res_vel});

          prev_pitch = res_pitch;
        } else if (use_anticipation && beat_in_motif >= 0.5f) {
          // Apply anticipation: early arrival of next chord tone
          AnticipationResult ant = applyAnticipation(next_chord_root, rn.eighths);

          int ant_pitch = degreeToPitch(ant.degree, base_octave, key_offset);
          while (ant_pitch < effective_vocal_low) ant_pitch += 12;
          while (ant_pitch > effective_vocal_high) ant_pitch -= 12;
          ant_pitch = std::clamp(ant_pitch, (int)effective_vocal_low, (int)effective_vocal_high);

          // Shift note earlier by the offset
          Tick ant_offset_ticks = static_cast<Tick>(std::abs(ant.beat_offset) * TICKS_PER_BEAT);
          Tick ant_tick = (note_tick > ant_offset_ticks) ? (note_tick - ant_offset_ticks) : note_tick;
          Tick relative_ant_tick = (relative_tick > ant_offset_ticks) ?
                                   (relative_tick - ant_offset_ticks) : relative_tick;

          Tick ant_duration = static_cast<Tick>(ant.duration_eighths * TICKS_PER_BEAT / 2);

          // Add anticipation note
          track.addNote(ant_tick, ant_duration, clampPitch(ant_pitch), velocity);
          phrase_notes.push_back({relative_ant_tick, ant_duration, clampPitch(ant_pitch), velocity});

          prev_pitch = ant_pitch;
        } else {
          // Regular note
          track.addNote(note_tick, duration, clampPitch(pitch), velocity);
          phrase_notes.push_back(
              {relative_tick, duration, clampPitch(pitch), velocity});

          // Store notes for chorus hook (first 2-bar phrase only)
          if (is_chorus && motif_start == 0) {
            // Use relative tick within the motif (not section)
            Tick motif_relative_tick = relative_tick - relative_motif_start;
            chorus_hook_notes.push_back(
                {motif_relative_tick, duration, clampPitch(pitch), velocity});
          }
        }
      }
    }

    phrase_cache[section.type] = std::move(phrase_notes);
  }
}

}  // namespace midisketch
