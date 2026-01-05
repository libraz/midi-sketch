#include "track/arpeggio.h"
#include "core/chord.h"
#include "core/velocity.h"
#include <algorithm>
#include <vector>

namespace midisketch {

namespace {

// Get note duration based on arpeggio speed
Tick getNoteDuration(ArpeggioSpeed speed) {
  switch (speed) {
    case ArpeggioSpeed::Eighth:
      return TICKS_PER_BEAT / 2;     // 8th note = 240 ticks
    case ArpeggioSpeed::Sixteenth:
      return TICKS_PER_BEAT / 4;     // 16th note = 120 ticks
    case ArpeggioSpeed::Triplet:
      return TICKS_PER_BEAT / 3;     // Triplet = 160 ticks
  }
  return TICKS_PER_BEAT / 4;  // Default to 16th
}

// Build chord note array from chord intervals
std::vector<uint8_t> buildChordNotes(uint8_t root, const Chord& chord,
                                      uint8_t octave_range) {
  std::vector<uint8_t> notes;

  for (uint8_t octave = 0; octave < octave_range; ++octave) {
    for (uint8_t i = 0; i < chord.note_count; ++i) {
      if (chord.intervals[i] >= 0) {
        int note = root + chord.intervals[i] + (octave * 12);
        if (note >= 0 && note <= 127) {
          notes.push_back(static_cast<uint8_t>(note));
        }
      }
    }
  }

  return notes;
}

// Arrange notes according to pattern
std::vector<uint8_t> arrangeByPattern(const std::vector<uint8_t>& notes,
                                       ArpeggioPattern pattern,
                                       std::mt19937& rng) {
  if (notes.empty()) return notes;

  std::vector<uint8_t> result = notes;

  switch (pattern) {
    case ArpeggioPattern::Up:
      std::sort(result.begin(), result.end());
      break;

    case ArpeggioPattern::Down:
      std::sort(result.begin(), result.end(), std::greater<uint8_t>());
      break;

    case ArpeggioPattern::UpDown: {
      std::sort(result.begin(), result.end());
      // Add descending notes (excluding first and last to avoid duplicates)
      std::vector<uint8_t> down_part;
      for (int i = static_cast<int>(result.size()) - 2; i > 0; --i) {
        down_part.push_back(result[i]);
      }
      result.insert(result.end(), down_part.begin(), down_part.end());
      break;
    }

    case ArpeggioPattern::Random:
      std::shuffle(result.begin(), result.end(), rng);
      break;
  }

  return result;
}

// Calculate velocity based on section and position
uint8_t calculateArpeggioVelocity(uint8_t base_velocity, SectionType section,
                                   int note_in_pattern) {
  float section_mult = 1.0f;
  switch (section) {
    case SectionType::Intro:
    case SectionType::Interlude:
      section_mult = 0.75f;
      break;
    case SectionType::Outro:
      section_mult = 0.80f;
      break;
    case SectionType::A:
      section_mult = 0.85f;
      break;
    case SectionType::B:
      section_mult = 0.90f;
      break;
    case SectionType::Chorus:
      section_mult = 1.0f;
      break;
    case SectionType::Bridge:
      section_mult = 0.85f;
      break;
  }

  // Add slight accent on beat 1 notes
  float accent = (note_in_pattern == 0) ? 1.1f : 1.0f;

  int velocity = static_cast<int>(base_velocity * section_mult * accent);
  return static_cast<uint8_t>(std::clamp(velocity, 40, 127));
}

}  // namespace

void generateArpeggioTrack(MidiTrack& track, const Song& song,
                           const GeneratorParams& params, std::mt19937& rng) {
  const auto& sections = song.arrangement().sections();
  if (sections.empty()) return;

  const auto& progression = getChordProgression(params.chord_id);
  const ArpeggioParams& arp = params.arpeggio;

  Tick note_duration = getNoteDuration(arp.speed);
  Tick gated_duration = static_cast<Tick>(note_duration * arp.gate);

  // Base octave for arpeggio (higher than vocal to avoid melodic collision)
  // Moved from C4(60) to C5(72) for 1-octave separation from vocal range
  constexpr uint8_t BASE_OCTAVE = 72;  // C5

  // When sync_chord is false, build one arpeggio pattern for section and continue
  // When sync_chord is true, rebuild pattern each bar based on current chord
  std::vector<uint8_t> persistent_arp_notes;
  int persistent_pattern_index = 0;

  for (const auto& section : sections) {
    // Intro/Outro: generate arpeggio with reduced intensity
    // Velocity is adjusted via calculateArpeggioVelocity() based on section type

    Tick section_end = section.start_tick + (section.bars * TICKS_PER_BAR);

    // When not syncing with chords, build pattern once at section start
    if (!arp.sync_chord && persistent_arp_notes.empty()) {
      // Use first chord of progression for the persistent pattern
      int8_t degree = progression.at(0);
      uint8_t root = degreeToRoot(degree, Key::C);
      while (root < BASE_OCTAVE) root += 12;
      while (root >= BASE_OCTAVE + 12) root -= 12;
      Chord chord = getChordNotes(degree);
      std::vector<uint8_t> chord_notes = buildChordNotes(root, chord, arp.octave_range);
      persistent_arp_notes = arrangeByPattern(chord_notes, arp.pattern, rng);
    }

    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + (bar * TICKS_PER_BAR);

      std::vector<uint8_t> arp_notes;
      int pattern_index;

      if (arp.sync_chord) {
        // Sync with chord: rebuild pattern each bar
        int chord_idx = bar % progression.length;
        int8_t degree = progression.at(chord_idx);
        // Internal processing is always in C major; transpose at MIDI output time
        uint8_t root = degreeToRoot(degree, Key::C);

        // Adjust root to base octave
        while (root < BASE_OCTAVE) root += 12;
        while (root >= BASE_OCTAVE + 12) root -= 12;

        Chord chord = getChordNotes(degree);

        // Build arpeggio notes for this chord
        std::vector<uint8_t> chord_notes = buildChordNotes(root, chord, arp.octave_range);
        arp_notes = arrangeByPattern(chord_notes, arp.pattern, rng);
        pattern_index = 0;  // Reset pattern index each bar
      } else {
        // No sync: continue with persistent pattern
        arp_notes = persistent_arp_notes;
        pattern_index = persistent_pattern_index;
      }

      if (arp_notes.empty()) continue;

      // Generate arpeggio pattern for this bar
      Tick pos = bar_start;

      while (pos < bar_start + TICKS_PER_BAR && pos < section_end) {
        uint8_t note = arp_notes[pattern_index % arp_notes.size()];
        uint8_t velocity = calculateArpeggioVelocity(
            arp.base_velocity, section.type, pattern_index % arp_notes.size());

        track.addNote(pos, gated_duration, note, velocity);

        pos += note_duration;
        pattern_index++;
      }

      // Update persistent index if not syncing
      if (!arp.sync_chord) {
        persistent_pattern_index = pattern_index;
      }
    }
  }
}

}  // namespace midisketch
