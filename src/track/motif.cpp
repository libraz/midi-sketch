#include "track/motif.h"
#include "core/chord.h"
#include <algorithm>
#include <array>
#include <vector>

namespace midisketch {

namespace {

// Major scale semitones (relative to tonic)
constexpr int SCALE[7] = {0, 2, 4, 5, 7, 9, 11};

// Avoid notes for common chords (relative to chord root in semitones)
// These notes create dissonance when held against the chord
constexpr int AVOID_MAJOR = 5;  // Perfect 4th above root (e.g., F over C major)
constexpr int AVOID_MINOR = 8;  // Minor 6th above root (e.g., C over E minor - clashes with 5th)

// Tension intervals in semitones from chord root
constexpr int TENSION_9TH = 14;   // 9th = 2nd + octave (14 semitones from root)
constexpr int TENSION_11TH = 17;  // 11th = 4th + octave (17 semitones)
constexpr int TENSION_13TH = 21;  // 13th = 6th + octave (21 semitones)

// Chord quality for tension selection
enum class ChordQuality {
  Major,
  Minor,
  Diminished
};

// Get available tensions for a chord quality
std::vector<int> getAvailableTensions(ChordQuality quality) {
  switch (quality) {
    case ChordQuality::Major:
      // Major chords: 9th and 13th work well
      return {TENSION_9TH, TENSION_13TH};
    case ChordQuality::Minor:
      // Minor chords: 9th and 11th work well
      return {TENSION_9TH, TENSION_11TH};
    case ChordQuality::Diminished:
      // Diminished: limited tensions, b9 is possible but dissonant
      // Use 9th carefully (whole step above root)
      return {TENSION_9TH};
  }
  return {};
}

// Determine chord quality from chord info
ChordQuality getChordQuality(const Chord& chord) {
  if (chord.note_count >= 2) {
    // Check 3rd interval: 3 = minor, 4 = major
    if (chord.intervals[1] == 3) {
      // Check if diminished (3rd is 3, 5th is 6)
      if (chord.note_count >= 3 && chord.intervals[2] == 6) {
        return ChordQuality::Diminished;
      }
      return ChordQuality::Minor;
    }
  }
  return ChordQuality::Major;
}

// Apply 9th or other tension to a pitch based on chord quality
int applyTension(int base_pitch, uint8_t chord_root, ChordQuality quality,
                 std::mt19937& rng) {
  auto tensions = getAvailableTensions(quality);
  if (tensions.empty()) return base_pitch;

  // Randomly select a tension
  std::uniform_int_distribution<size_t> dist(0, tensions.size() - 1);
  int tension_interval = tensions[dist(rng)];

  // Calculate tension pitch (tension is relative to chord root)
  int tension_pitch = chord_root + tension_interval;

  // Adjust to be close to base pitch (within reasonable range)
  while (tension_pitch > base_pitch + 12) tension_pitch -= 12;
  while (tension_pitch < base_pitch - 12) tension_pitch += 12;

  return tension_pitch;
}

// Convert scale degree to pitch with key offset
int degreeToPitch(int degree, int base_note, int key_offset) {
  int d = ((degree % 7) + 7) % 7;
  int oct_adjust = degree / 7;
  if (degree < 0 && degree % 7 != 0) oct_adjust--;
  return base_note + oct_adjust * 12 + SCALE[d] + key_offset;
}

// Check if a pitch is an avoid note for the given chord
bool isAvoidNote(int pitch, uint8_t chord_root, bool is_minor) {
  int interval = ((pitch - chord_root) % 12 + 12) % 12;
  if (is_minor) {
    return interval == AVOID_MINOR;
  }
  return interval == AVOID_MAJOR;
}

// Get chord tone pitch classes for a chord
// Returns root, 3rd, 5th as pitch classes (0-11)
std::array<int, 3> getChordPitchClasses(uint8_t chord_root, bool is_minor) {
  int root_pc = chord_root % 12;
  int third_offset = is_minor ? 3 : 4;  // minor 3rd or major 3rd
  return {{
    root_pc,
    (root_pc + third_offset) % 12,
    (root_pc + 7) % 12  // perfect 5th
  }};
}

// Adjust pitch to avoid dissonance by resolving to nearest chord tone
int adjustForChord(int pitch, uint8_t chord_root, bool is_minor) {
  if (!isAvoidNote(pitch, chord_root, is_minor)) {
    return pitch;
  }

  // Find nearest chord tone
  auto chord_tones = getChordPitchClasses(chord_root, is_minor);
  int octave = pitch / 12;

  int best_pitch = pitch;
  int best_dist = 100;

  for (int ct_pc : chord_tones) {
    // Check same octave and adjacent octaves
    for (int oct_offset = -1; oct_offset <= 1; ++oct_offset) {
      int candidate = (octave + oct_offset) * 12 + ct_pc;
      int dist = std::abs(candidate - pitch);
      if (dist < best_dist) {
        best_dist = dist;
        best_pitch = candidate;
      }
    }
  }

  return best_pitch;
}

// Generate rhythm positions with musical structure
std::vector<Tick> generateRhythmPositions(MotifRhythmDensity density,
                                           MotifLength length,
                                           uint8_t note_count,
                                           std::mt19937& /* rng */) {
  Tick motif_ticks = static_cast<Tick>(length) * TICKS_PER_BAR;
  std::vector<Tick> positions;

  // Create structured rhythm patterns instead of random
  switch (density) {
    case MotifRhythmDensity::Sparse: {
      // Quarter note based - predictable pattern
      Tick step = TICKS_PER_BEAT;
      std::vector<Tick> base_pattern;
      // Always start on beat 1, then distribute remaining notes
      base_pattern.push_back(0);
      for (Tick t = step; t < motif_ticks && base_pattern.size() < note_count; t += step) {
        base_pattern.push_back(t);
      }
      positions = base_pattern;
      break;
    }
    case MotifRhythmDensity::Medium: {
      // Eighth note grid with emphasis on downbeats
      Tick step = TICKS_PER_BEAT / 2;
      // Pattern: start, then fill with preference for on-beat
      positions.push_back(0);
      for (Tick t = step; t < motif_ticks && positions.size() < note_count; t += step) {
        // Prefer on-beat positions (every other eighth)
        bool on_beat = (t % TICKS_PER_BEAT == 0);
        if (on_beat || positions.size() < note_count / 2) {
          positions.push_back(t);
        }
      }
      // Fill remaining from off-beats if needed
      for (Tick t = step; t < motif_ticks && positions.size() < note_count; t += step) {
        if (std::find(positions.begin(), positions.end(), t) == positions.end()) {
          positions.push_back(t);
        }
      }
      break;
    }
    case MotifRhythmDensity::Driving: {
      // Consistent eighth note pattern for driving feel
      Tick step = TICKS_PER_BEAT / 2;
      for (Tick t = 0; t < motif_ticks && positions.size() < note_count; t += step) {
        positions.push_back(t);
      }
      break;
    }
  }

  std::sort(positions.begin(), positions.end());
  return positions;
}

// Generate pitch sequence with antecedent-consequent structure
std::vector<int> generatePitchSequence(uint8_t note_count,
                                        MotifMotion motion,
                                        std::mt19937& rng) {
  std::vector<int> degrees;

  // Split into "question" (first half) and "answer" (second half)
  uint8_t half = note_count / 2;

  // Question phrase - ends on unstable tone
  degrees.push_back(0);  // Start on root
  int current = 0;

  for (uint8_t i = 1; i < half; ++i) {
    int step = 0;
    switch (motion) {
      case MotifMotion::Stepwise: {
        std::uniform_int_distribution<int> dist(-2, 2);
        step = dist(rng);
        if (step == 0) step = 1;
        break;
      }
      case MotifMotion::GentleLeap: {
        std::uniform_int_distribution<int> dist(-3, 3);
        step = dist(rng);
        if (step == 0) step = 1;
        break;
      }
    }
    current += step;
    current = std::clamp(current, -4, 7);
    degrees.push_back(current);
  }

  // End question on 2nd or 4th (unstable)
  std::uniform_int_distribution<int> q_end(0, 1);
  int question_endings[] = {1, 3};  // 2nd, 4th scale degrees
  degrees.push_back(question_endings[q_end(rng)]);

  // Answer phrase - similar motion but ends on stable tone
  current = degrees.back();
  for (uint8_t i = half + 1; i < note_count - 1; ++i) {
    int step = 0;
    switch (motion) {
      case MotifMotion::Stepwise: {
        std::uniform_int_distribution<int> dist(-2, 2);
        step = dist(rng);
        if (step == 0) step = -1;  // Tend downward for resolution
        break;
      }
      case MotifMotion::GentleLeap: {
        std::uniform_int_distribution<int> dist(-3, 2);
        step = dist(rng);
        if (step == 0) step = -1;
        break;
      }
    }
    current += step;
    current = std::clamp(current, -4, 7);
    degrees.push_back(current);
  }

  // End answer on stable tone (root, 3rd, or 5th)
  std::uniform_int_distribution<int> a_end(0, 2);
  int answer_endings[] = {0, 2, 4};
  degrees.push_back(answer_endings[a_end(rng)]);

  return degrees;
}

}  // namespace

std::vector<NoteEvent> generateMotifPattern(const GeneratorParams& params,
                                             std::mt19937& rng) {
  const MotifParams& motif_params = params.motif;
  std::vector<NoteEvent> pattern;

  // Internal processing is always in C major; transpose at MIDI output time
  int key_offset = 0;
  uint8_t base_note = motif_params.register_high ? 67 : 60;  // G4 or C4

  // Generate rhythm positions
  std::vector<Tick> positions = generateRhythmPositions(
      motif_params.rhythm_density, motif_params.length,
      motif_params.note_count, rng);

  // Generate pitch sequence with structure
  std::vector<int> degrees = generatePitchSequence(
      motif_params.note_count, motif_params.motion, rng);

  // Calculate note duration
  Tick note_duration = TICKS_PER_BEAT / 2;
  switch (motif_params.rhythm_density) {
    case MotifRhythmDensity::Sparse:
      note_duration = TICKS_PER_BEAT;
      break;
    case MotifRhythmDensity::Medium:
    case MotifRhythmDensity::Driving:
      note_duration = TICKS_PER_BEAT / 2;
      break;
  }

  uint8_t velocity = motif_params.velocity_fixed ? 80 : 75;

  // Create notes
  size_t pitch_idx = 0;
  for (Tick pos : positions) {
    int degree = degrees[pitch_idx % degrees.size()];
    int pitch = degreeToPitch(degree, base_note, key_offset);

    pitch = std::clamp(pitch, 36, 96);

    pattern.push_back({pos, note_duration, static_cast<uint8_t>(pitch), velocity});
    pitch_idx++;
  }

  return pattern;
}

void generateMotifTrack(MidiTrack& track, Song& song,
                        const GeneratorParams& params, std::mt19937& rng) {
  // Generate base motif pattern
  std::vector<NoteEvent> pattern = generateMotifPattern(params, rng);
  song.setMotifPattern(pattern);

  if (pattern.empty()) return;

  const MotifParams& motif_params = params.motif;
  const auto& progression = getChordProgression(params.chord_id);
  Tick motif_length = static_cast<Tick>(motif_params.length) * TICKS_PER_BAR;

  const auto& sections = song.arrangement().sections();

  for (const auto& section : sections) {
    Tick section_end = section.start_tick + section.bars * TICKS_PER_BAR;
    bool is_chorus = (section.type == SectionType::Chorus);

    // Apply octave layering for chorus if enabled
    bool add_octave = is_chorus && motif_params.octave_layering_chorus;

    // Repeat motif across the section
    for (Tick pos = section.start_tick; pos < section_end; pos += motif_length) {
      // Calculate which bar we're in for chord info
      uint32_t bar_in_section = (pos - section.start_tick) / TICKS_PER_BAR;

      for (const auto& note : pattern) {
        Tick absolute_tick = pos + note.startTick;
        if (absolute_tick >= section_end) continue;

        // Determine which bar this note falls in
        uint32_t note_bar = bar_in_section + (note.startTick / TICKS_PER_BAR);
        int chord_idx = note_bar % progression.length;
        int8_t degree = progression.at(chord_idx);

        // Get chord info (use Key::C for internal processing)
        uint8_t chord_root = degreeToRoot(degree, Key::C);
        Chord chord = getChordNotes(degree);
        bool is_minor = (chord.intervals[1] == 3);
        ChordQuality quality = getChordQuality(chord);

        // Adjust pitch to avoid dissonance with current chord
        int adjusted_pitch = adjustForChord(note.note, chord_root, is_minor);
        adjusted_pitch = std::clamp(adjusted_pitch, 36, 108);

        // Occasionally apply tension based on chord quality (9th, 11th, 13th)
        std::uniform_real_distribution<float> tension_prob(0.0f, 1.0f);
        bool use_tension = (section.type == SectionType::B ||
                            section.type == SectionType::Chorus) &&
                           tension_prob(rng) < 0.15f;  // 15% chance in B/Chorus

        if (use_tension) {
          adjusted_pitch = applyTension(adjusted_pitch, chord_root, quality, rng);
          adjusted_pitch = std::clamp(adjusted_pitch, 36, 108);
        }

        // Add main note
        track.addNote(absolute_tick, note.duration,
                      static_cast<uint8_t>(adjusted_pitch), note.velocity);

        // Add octave doubling for chorus
        if (add_octave) {
          int octave_pitch = adjusted_pitch + 12;
          if (octave_pitch <= 108) {
            uint8_t octave_vel = static_cast<uint8_t>(note.velocity * 0.85);
            track.addNote(absolute_tick, note.duration,
                          static_cast<uint8_t>(octave_pitch), octave_vel);
          }
        }
      }
    }
  }
}

}  // namespace midisketch
