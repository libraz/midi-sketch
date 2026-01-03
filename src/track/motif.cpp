#include "track/motif.h"
#include <algorithm>
#include <vector>

namespace midisketch {

namespace {

// Major scale semitones (relative to tonic)
constexpr int SCALE[7] = {0, 2, 4, 5, 7, 9, 11};

// Convert scale degree to pitch
int degreeToPitch(int degree, int base_note) {
  int d = ((degree % 7) + 7) % 7;
  int oct_adjust = degree / 7;
  if (degree < 0 && degree % 7 != 0) oct_adjust--;
  return base_note + oct_adjust * 12 + SCALE[d];
}

// Generate rhythm positions based on density
std::vector<Tick> generateRhythmPositions(MotifRhythmDensity density,
                                           MotifLength length,
                                           uint8_t note_count,
                                           std::mt19937& rng) {
  Tick motif_ticks = static_cast<Tick>(length) * TICKS_PER_BAR;
  std::vector<Tick> positions;

  switch (density) {
    case MotifRhythmDensity::Sparse: {
      // Quarter note grid with some gaps
      Tick step = TICKS_PER_BEAT;
      std::vector<Tick> grid;
      for (Tick t = 0; t < motif_ticks; t += step) {
        grid.push_back(t);
      }
      // Shuffle and take note_count positions
      std::shuffle(grid.begin(), grid.end(), rng);
      for (uint8_t i = 0; i < std::min(note_count, static_cast<uint8_t>(grid.size())); ++i) {
        positions.push_back(grid[i]);
      }
      break;
    }
    case MotifRhythmDensity::Medium: {
      // Eighth note grid
      Tick step = TICKS_PER_BEAT / 2;
      std::vector<Tick> grid;
      for (Tick t = 0; t < motif_ticks; t += step) {
        grid.push_back(t);
      }
      std::shuffle(grid.begin(), grid.end(), rng);
      for (uint8_t i = 0; i < std::min(note_count, static_cast<uint8_t>(grid.size())); ++i) {
        positions.push_back(grid[i]);
      }
      break;
    }
    case MotifRhythmDensity::Driving: {
      // Mix of eighth and sixteenth notes
      Tick step = TICKS_PER_BEAT / 4;  // 16th note grid
      std::vector<Tick> grid;
      for (Tick t = 0; t < motif_ticks; t += step) {
        // Prefer on-beat and off-beat 8th positions
        if (t % (TICKS_PER_BEAT / 2) == 0) {
          grid.push_back(t);
          grid.push_back(t);  // Weight on-beat positions
        } else {
          grid.push_back(t);
        }
      }
      std::shuffle(grid.begin(), grid.end(), rng);
      // Remove duplicates after shuffle
      std::sort(grid.begin(), grid.end());
      grid.erase(std::unique(grid.begin(), grid.end()), grid.end());
      std::shuffle(grid.begin(), grid.end(), rng);
      for (uint8_t i = 0; i < std::min(note_count, static_cast<uint8_t>(grid.size())); ++i) {
        positions.push_back(grid[i]);
      }
      break;
    }
  }

  std::sort(positions.begin(), positions.end());
  return positions;
}

// Generate pitch sequence based on motion type
std::vector<int> generatePitchSequence(uint8_t note_count,
                                        MotifMotion motion,
                                        std::mt19937& rng) {
  std::vector<int> degrees;
  degrees.push_back(0);  // Start on root

  int current_degree = 0;

  for (uint8_t i = 1; i < note_count; ++i) {
    int step = 0;
    switch (motion) {
      case MotifMotion::Stepwise: {
        // Steps of -2, -1, +1, +2 (scale degrees)
        std::uniform_int_distribution<int> dist(-2, 2);
        step = dist(rng);
        if (step == 0) step = 1;  // No repeated notes
        break;
      }
      case MotifMotion::GentleLeap: {
        // Steps of -3 to +3 (up to 3rd)
        std::uniform_int_distribution<int> dist(-3, 3);
        step = dist(rng);
        if (step == 0) step = 1;
        break;
      }
    }

    current_degree += step;
    // Keep within reasonable range (-7 to +7 scale degrees)
    current_degree = std::clamp(current_degree, -7, 7);
    degrees.push_back(current_degree);
  }

  // End on stable tone (root, 3rd, or 5th)
  std::uniform_int_distribution<int> end_dist(0, 2);
  int end_options[] = {0, 2, 4};  // root, 3rd, 5th
  degrees.back() = end_options[end_dist(rng)];

  return degrees;
}

}  // namespace

std::vector<NoteEvent> generateMotifPattern(const GeneratorParams& params,
                                             std::mt19937& rng) {
  const MotifParams& motif_params = params.motif;
  std::vector<NoteEvent> pattern;

  // Determine base note (register)
  uint8_t base_note = motif_params.register_high ? 67 : 60;  // G4 or C4

  // Generate rhythm positions
  std::vector<Tick> positions = generateRhythmPositions(
      motif_params.rhythm_density, motif_params.length,
      motif_params.note_count, rng);

  // Generate pitch sequence
  std::vector<int> degrees = generatePitchSequence(
      motif_params.note_count, motif_params.motion, rng);

  // Calculate note duration (based on rhythm density)
  Tick note_duration = TICKS_PER_BEAT / 2;  // Default: eighth note
  switch (motif_params.rhythm_density) {
    case MotifRhythmDensity::Sparse:
      note_duration = TICKS_PER_BEAT;  // Quarter note
      break;
    case MotifRhythmDensity::Medium:
    case MotifRhythmDensity::Driving:
      note_duration = TICKS_PER_BEAT / 2;  // Eighth note
      break;
  }

  // Velocity
  uint8_t velocity = motif_params.velocity_fixed ? 80 : 75;

  // Create notes
  size_t pitch_idx = 0;
  for (Tick pos : positions) {
    int degree = degrees[pitch_idx % degrees.size()];
    int pitch = degreeToPitch(degree, base_note);

    // Clamp to reasonable MIDI range
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
  Tick motif_length = static_cast<Tick>(motif_params.length) * TICKS_PER_BAR;

  const auto& sections = song.arrangement().sections();

  for (const auto& section : sections) {
    Tick section_end = section.start_tick + section.bars * TICKS_PER_BAR;
    bool is_chorus = (section.type == SectionType::Chorus);

    // Apply octave layering for chorus if enabled
    bool add_octave = is_chorus && motif_params.octave_layering_chorus;

    // Repeat motif across the section
    for (Tick pos = section.start_tick; pos < section_end; pos += motif_length) {
      for (const auto& note : pattern) {
        Tick absolute_tick = pos + note.startTick;
        if (absolute_tick >= section_end) continue;

        // Add main note
        track.addNote(absolute_tick, note.duration, note.note, note.velocity);

        // Add octave doubling for chorus
        if (add_octave) {
          uint8_t octave_pitch = note.note + 12;
          if (octave_pitch <= 108) {  // Stay in reasonable range
            // Slightly lower velocity for octave doubling
            uint8_t octave_vel = static_cast<uint8_t>(note.velocity * 0.85);
            track.addNote(absolute_tick, note.duration, octave_pitch, octave_vel);
          }
        }
      }
    }
  }
}

}  // namespace midisketch
