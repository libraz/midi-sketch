#include "track/chord_track.h"
#include "core/chord.h"
#include "core/preset_data.h"
#include "core/velocity.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace midisketch {

namespace {

// Timing constants
constexpr Tick WHOLE = TICKS_PER_BAR;
constexpr Tick HALF = TICKS_PER_BAR / 2;
constexpr Tick QUARTER = TICKS_PER_BEAT;
constexpr Tick EIGHTH = TICKS_PER_BEAT / 2;

// Chord voicing range
constexpr uint8_t CHORD_LOW = 48;   // C3
constexpr uint8_t CHORD_HIGH = 84;  // C6

// Voicing type for different musical contexts
enum class VoicingType {
  Close,    // Standard close voicing
  Open,     // Open voicing (wider spread)
  Rootless  // 4-voice rootless (root omitted, bass handles it)
};

// A voiced chord with absolute pitches
struct VoicedChord {
  std::array<uint8_t, 4> pitches;  // Up to 4 voices
  uint8_t count;                   // Number of voices
  VoicingType type;                // Voicing style used
};

// Calculate distance between two voicings (for voice leading)
int voicingDistance(const VoicedChord& prev, const VoicedChord& next) {
  int total = 0;
  size_t min_count = std::min(prev.count, next.count);
  for (size_t i = 0; i < min_count; ++i) {
    int diff = static_cast<int>(next.pitches[i]) - static_cast<int>(prev.pitches[i]);
    total += std::abs(diff);
  }
  return total;
}

// Count common tones between two voicings
int countCommonTones(const VoicedChord& prev, const VoicedChord& next) {
  int common = 0;
  for (size_t i = 0; i < prev.count; ++i) {
    for (size_t j = 0; j < next.count; ++j) {
      // Consider octave equivalence
      if (prev.pitches[i] % 12 == next.pitches[j] % 12) {
        common++;
        break;
      }
    }
  }
  return common;
}

// Generate close voicings for a chord
std::vector<VoicedChord> generateCloseVoicings(uint8_t root, const Chord& chord) {
  std::vector<VoicedChord> voicings;

  for (int inversion = 0; inversion < chord.note_count; ++inversion) {
    for (uint8_t base_octave = CHORD_LOW; base_octave <= CHORD_HIGH - 12; base_octave += 12) {
      VoicedChord v{};
      v.count = chord.note_count;
      v.type = VoicingType::Close;

      bool valid = true;
      for (uint8_t i = 0; i < chord.note_count; ++i) {
        if (chord.intervals[i] < 0) {
          v.count = i;
          break;
        }

        uint8_t voice_idx = (i + inversion) % chord.note_count;
        int pitch = root + chord.intervals[voice_idx];

        if (i == 0) {
          pitch = base_octave + (pitch % 12);
        } else {
          pitch = base_octave + (pitch % 12);
          while (pitch <= v.pitches[i - 1]) {
            pitch += 12;
          }
        }

        if (pitch < CHORD_LOW || pitch > CHORD_HIGH) {
          valid = false;
          break;
        }

        v.pitches[i] = static_cast<uint8_t>(pitch);
      }

      if (valid && v.count >= 3) {
        voicings.push_back(v);
      }
    }
  }

  return voicings;
}

// Generate open voicings (wider spread, drop 2 style)
std::vector<VoicedChord> generateOpenVoicings(uint8_t root, const Chord& chord) {
  std::vector<VoicedChord> voicings;

  for (uint8_t base_octave = CHORD_LOW; base_octave <= CHORD_HIGH - 24; base_octave += 12) {
    // Drop 2 voicing: drop the second voice from top down an octave
    // Result: bass-low-high pattern with wider spread
    VoicedChord v{};
    v.count = chord.note_count;
    v.type = VoicingType::Open;

    bool valid = true;
    std::array<int, 4> raw_pitches{};

    // First, calculate close position
    for (uint8_t i = 0; i < chord.note_count && i < 4; ++i) {
      if (chord.intervals[i] < 0) {
        v.count = i;
        break;
      }
      int pitch = root + chord.intervals[i];
      raw_pitches[i] = base_octave + (pitch % 12);
      // Stack in close position first
      if (i > 0 && raw_pitches[i] <= raw_pitches[i - 1]) {
        raw_pitches[i] += 12;
      }
    }

    // Drop the second voice down an octave for open voicing
    if (v.count >= 3) {
      // Original: [bass, 3rd, 5th] -> Open: [bass, 5th, 3rd+8va]
      int bass = raw_pitches[0];
      int dropped = raw_pitches[1];  // 3rd drops down
      int top = raw_pitches[2];      // 5th stays

      // Reorder: bass, dropped-octave, top
      v.pitches[0] = static_cast<uint8_t>(bass);
      v.pitches[1] = static_cast<uint8_t>(dropped + 12);  // Move 3rd up instead
      v.pitches[2] = static_cast<uint8_t>(top + 12);      // Move 5th up too

      // Sort ascending
      std::sort(v.pitches.begin(), v.pitches.begin() + v.count);

      // Validate range
      for (uint8_t i = 0; i < v.count; ++i) {
        if (v.pitches[i] < CHORD_LOW || v.pitches[i] > CHORD_HIGH) {
          valid = false;
          break;
        }
      }
    }

    if (valid && v.count >= 3) {
      voicings.push_back(v);
    }
  }

  return voicings;
}

// Generate rootless voicings (4-voice, root omitted for bass)
std::vector<VoicedChord> generateRootlessVoicings(uint8_t root, const Chord& chord) {
  std::vector<VoicedChord> voicings;

  // Rootless voicing: omit root, add 7th or 9th
  // For triads: 3rd, 5th, 7th (add implied 7th)
  for (uint8_t base_octave = CHORD_LOW; base_octave <= CHORD_HIGH - 12; base_octave += 12) {
    VoicedChord v{};
    v.type = VoicingType::Rootless;

    bool is_minor = (chord.note_count >= 2 && chord.intervals[1] == 3);

    // Build rootless voicing: 3rd, 5th, 7th
    std::array<int, 4> intervals_rootless{};
    if (is_minor) {
      // Minor: m3, P5, m7
      intervals_rootless = {3, 7, 10, -1};
      v.count = 3;
    } else {
      // Major: M3, P5, M7 (or dom7 for V chord)
      intervals_rootless = {4, 7, 11, -1};
      v.count = 3;
    }

    bool valid = true;
    for (uint8_t i = 0; i < v.count; ++i) {
      if (intervals_rootless[i] < 0) {
        v.count = i;
        break;
      }
      int pitch = root + intervals_rootless[i];
      pitch = base_octave + (pitch % 12);
      if (i > 0 && pitch <= v.pitches[i - 1]) {
        pitch += 12;
      }

      if (pitch < CHORD_LOW || pitch > CHORD_HIGH) {
        valid = false;
        break;
      }
      v.pitches[i] = static_cast<uint8_t>(pitch);
    }

    if (valid && v.count >= 3) {
      voicings.push_back(v);
    }
  }

  return voicings;
}

// Generate all possible voicings for a chord
std::vector<VoicedChord> generateVoicings(uint8_t root, const Chord& chord,
                                           VoicingType preferred_type) {
  std::vector<VoicedChord> voicings;

  // Always include close voicings as fallback
  auto close = generateCloseVoicings(root, chord);
  voicings.insert(voicings.end(), close.begin(), close.end());

  if (preferred_type == VoicingType::Open) {
    auto open = generateOpenVoicings(root, chord);
    voicings.insert(voicings.end(), open.begin(), open.end());
  } else if (preferred_type == VoicingType::Rootless) {
    auto rootless = generateRootlessVoicings(root, chord);
    voicings.insert(voicings.end(), rootless.begin(), rootless.end());
  }

  return voicings;
}

// Select voicing type based on section and mood
VoicingType selectVoicingType(SectionType section, Mood mood) {
  bool is_ballad = (mood == Mood::Ballad || mood == Mood::Sentimental ||
                    mood == Mood::Chill);
  bool is_dramatic = (mood == Mood::Dramatic || mood == Mood::Nostalgic);

  // Rootless voicing for dramatic or nostalgic moods in B/Chorus
  if (is_dramatic && (section == SectionType::B || section == SectionType::Chorus)) {
    return VoicingType::Rootless;
  }

  // Open voicing for ballads in chorus (more spacious sound)
  if (is_ballad && section == SectionType::Chorus) {
    return VoicingType::Open;
  }

  // Open voicing for B section to add variety
  if (section == SectionType::B && !is_ballad) {
    return VoicingType::Open;
  }

  return VoicingType::Close;
}

// Select best voicing considering voice leading from previous chord
VoicedChord selectVoicing(uint8_t root, const Chord& chord,
                          const VoicedChord& prev_voicing, bool has_prev,
                          VoicingType preferred_type) {
  std::vector<VoicedChord> candidates = generateVoicings(root, chord, preferred_type);

  if (candidates.empty()) {
    // Fallback: simple root position
    VoicedChord fallback{};
    fallback.count = 0;
    fallback.type = VoicingType::Close;
    for (uint8_t i = 0; i < chord.note_count && i < 4; ++i) {
      if (chord.intervals[i] >= 0) {
        fallback.pitches[fallback.count] = static_cast<uint8_t>(
            std::clamp(root + chord.intervals[i], (int)CHORD_LOW, (int)CHORD_HIGH));
        fallback.count++;
      }
    }
    return fallback;
  }

  if (!has_prev) {
    // First chord: prefer the preferred type in middle register
    VoicedChord* best = &candidates[0];
    int best_score = -1000;
    for (auto& v : candidates) {
      int dist = std::abs(v.pitches[0] - 60);  // Distance from C4
      int type_bonus = (v.type == preferred_type) ? 50 : 0;
      int score = type_bonus - dist;
      if (score > best_score) {
        best = &v;
        best_score = score;
      }
    }
    return *best;
  }

  // Voice leading: prefer common tones, minimal movement, and preferred type
  VoicedChord* best = &candidates[0];
  int best_score = -1000;

  for (auto& v : candidates) {
    int common = countCommonTones(prev_voicing, v);
    int distance = voicingDistance(prev_voicing, v);
    int type_bonus = (v.type == preferred_type) ? 30 : 0;

    // Score: prioritize type match, common tones, then minimal movement
    int score = type_bonus + common * 100 - distance;

    if (score > best_score) {
      best = &v;
      best_score = score;
    }
  }

  return *best;
}

// Chord rhythm pattern types
enum class ChordRhythm {
  Whole,      // Intro: whole note
  Half,       // A section: half notes
  Quarter,    // B section: quarter notes
  Eighth      // Chorus: eighth note pulse
};

// Select rhythm pattern based on section and mood
ChordRhythm selectRhythm(SectionType section, Mood mood) {
  bool is_ballad = (mood == Mood::Ballad || mood == Mood::Sentimental ||
                    mood == Mood::Chill);
  bool is_energetic = (mood == Mood::EnergeticDance || mood == Mood::IdolPop ||
                       mood == Mood::BrightUpbeat);

  switch (section) {
    case SectionType::Intro:
      return ChordRhythm::Whole;
    case SectionType::A:
      return is_ballad ? ChordRhythm::Whole : ChordRhythm::Half;
    case SectionType::B:
      return is_ballad ? ChordRhythm::Half : ChordRhythm::Quarter;
    case SectionType::Chorus:
      if (is_ballad) return ChordRhythm::Half;
      if (is_energetic) return ChordRhythm::Eighth;
      return ChordRhythm::Quarter;
    default:
      return ChordRhythm::Half;
  }
}

// Generate chord notes for one bar
void generateChordBar(MidiTrack& track, Tick bar_start,
                      const VoicedChord& voicing, ChordRhythm rhythm,
                      SectionType section, Mood mood) {
  uint8_t vel = calculateVelocity(section, 0, mood);
  uint8_t vel_weak = static_cast<uint8_t>(vel * 0.8f);

  switch (rhythm) {
    case ChordRhythm::Whole:
      // Whole note chord
      for (size_t i = 0; i < voicing.count; ++i) {
        track.addNote(bar_start, WHOLE, voicing.pitches[i], vel);
      }
      break;

    case ChordRhythm::Half:
      // Two half notes
      for (size_t i = 0; i < voicing.count; ++i) {
        track.addNote(bar_start, HALF, voicing.pitches[i], vel);
        track.addNote(bar_start + HALF, HALF, voicing.pitches[i], vel_weak);
      }
      break;

    case ChordRhythm::Quarter:
      // Four quarter notes with accents on 1 and 3
      for (int beat = 0; beat < 4; ++beat) {
        Tick tick = bar_start + beat * QUARTER;
        uint8_t beat_vel = (beat == 0 || beat == 2) ? vel : vel_weak;
        for (size_t i = 0; i < voicing.count; ++i) {
          track.addNote(tick, QUARTER, voicing.pitches[i], beat_vel);
        }
      }
      break;

    case ChordRhythm::Eighth:
      // Eighth note pulse with syncopation
      for (int eighth = 0; eighth < 8; ++eighth) {
        Tick tick = bar_start + eighth * EIGHTH;
        uint8_t beat_vel = vel_weak;

        // Accents on beats 1 and 3
        if (eighth == 0 || eighth == 4) {
          beat_vel = vel;
        }
        // Slight accent on off-beats for energy
        else if (eighth == 3 || eighth == 7) {
          beat_vel = static_cast<uint8_t>(vel * 0.7f);
        } else {
          beat_vel = static_cast<uint8_t>(vel * 0.6f);
        }

        for (size_t i = 0; i < voicing.count; ++i) {
          track.addNote(tick, EIGHTH, voicing.pitches[i], beat_vel);
        }
      }
      break;
  }
}

}  // namespace

void generateChordTrack(MidiTrack& track, const Song& song,
                        const GeneratorParams& params) {
  const auto& progression = getChordProgression(params.chord_id);
  const auto& sections = song.arrangement().sections();

  VoicedChord prev_voicing{};
  bool has_prev = false;

  for (const auto& section : sections) {
    ChordRhythm rhythm = selectRhythm(section.type, params.mood);
    VoicingType voicing_type = selectVoicingType(section.type, params.mood);

    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;

      int chord_idx = bar % 4;
      int8_t degree = progression.degrees[chord_idx];

      uint8_t root = degreeToRoot(degree, params.key);
      Chord chord = getChordNotes(degree);

      // Select voicing with voice leading and type consideration
      VoicedChord voicing = selectVoicing(root, chord, prev_voicing, has_prev,
                                          voicing_type);

      generateChordBar(track, bar_start, voicing, rhythm, section.type, params.mood);

      prev_voicing = voicing;
      has_prev = true;
    }
  }
}

}  // namespace midisketch
