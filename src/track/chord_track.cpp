#include "track/chord_track.h"
#include "core/chord.h"
#include "core/preset_data.h"
#include "core/velocity.h"
#include "track/bass.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <random>

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

// Select voicing type based on section, mood, and bass pattern
// @param bass_has_root True if bass is playing the root note
VoicingType selectVoicingType(SectionType section, Mood mood, bool bass_has_root) {
  bool is_ballad = (mood == Mood::Ballad || mood == Mood::Sentimental ||
                    mood == Mood::Chill);
  bool is_dramatic = (mood == Mood::Dramatic || mood == Mood::Nostalgic);

  // Intro/Interlude/Outro: always close voicing for stability
  if (section == SectionType::Intro || section == SectionType::Interlude ||
      section == SectionType::Outro) {
    return VoicingType::Close;
  }

  // When bass has root, prefer rootless voicing in B/Chorus for cleaner sound
  if (bass_has_root && (section == SectionType::B || section == SectionType::Chorus)) {
    // Higher probability of rootless when bass covers root
    if (!is_ballad) {
      return VoicingType::Rootless;
    }
  }

  // Rootless voicing for dramatic or nostalgic moods in B/Chorus
  if (is_dramatic && (section == SectionType::B || section == SectionType::Chorus)) {
    return VoicingType::Rootless;
  }

  // Open voicing for ballads in chorus (more spacious sound)
  if (is_ballad && section == SectionType::Chorus) {
    return VoicingType::Open;
  }

  // Open voicing for B section and Bridge to add variety
  if ((section == SectionType::B || section == SectionType::Bridge) && !is_ballad) {
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

// Harmonic rhythm: how often chords change
enum class HarmonicDensity {
  Slow,       // Chord changes every 2 bars (Intro)
  Normal,     // Chord changes every bar (A, B)
  Dense       // Chord may change mid-bar at phrase ends (B end, Chorus)
};

// Determines harmonic density based on section and mood
struct HarmonicRhythmInfo {
  HarmonicDensity density;
  bool double_at_phrase_end;  // Add extra chord change at phrase end

  static HarmonicRhythmInfo forSection(SectionType section, Mood mood) {
    bool is_ballad = (mood == Mood::Ballad || mood == Mood::Sentimental ||
                      mood == Mood::Chill);

    switch (section) {
      case SectionType::Intro:
      case SectionType::Interlude:
        return {HarmonicDensity::Slow, false};
      case SectionType::Outro:
        return {HarmonicDensity::Slow, false};
      case SectionType::A:
        return {HarmonicDensity::Normal, false};
      case SectionType::B:
        return {HarmonicDensity::Normal, !is_ballad};
      case SectionType::Chorus:
        return {is_ballad ? HarmonicDensity::Normal : HarmonicDensity::Dense,
                !is_ballad};
      case SectionType::Bridge:
        return {HarmonicDensity::Normal, false};
    }
    return {HarmonicDensity::Normal, false};
  }
};

// Check if a chord degree is the dominant (V)
bool isDominant(int8_t degree) {
  return degree == 4;  // V chord
}

// Select appropriate chord extension based on context
ChordExtension selectChordExtension(int8_t degree, SectionType section,
                                     int bar_in_section, int section_bars,
                                     const ChordExtensionParams& ext_params,
                                     std::mt19937& rng) {
  if (!ext_params.enable_sus && !ext_params.enable_7th && !ext_params.enable_9th) {
    return ChordExtension::None;
  }

  std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  float roll = dist(rng);

  // Determine if chord is major or minor based on degree
  bool is_minor = (degree == 1 || degree == 2 || degree == 5);
  bool is_dominant = (degree == 4);  // V chord
  bool is_tonic = (degree == 0);     // I chord

  // Sus chords work well on:
  // - First bar of section (suspension before resolution)
  // - Pre-cadence positions (bar before section end)
  if (ext_params.enable_sus) {
    bool is_sus_context = (bar_in_section == 0) ||
                          (bar_in_section == section_bars - 2);

    if (is_sus_context && !is_minor && roll < ext_params.sus_probability) {
      // sus4 more common than sus2
      return (dist(rng) < 0.7f) ? ChordExtension::Sus4 : ChordExtension::Sus2;
    }
  }

  // 7th chords work well on:
  // - Dominant (V7) - very common
  // - ii7 and vi7 - common in jazz/pop
  // - B section and Chorus for richer harmony
  if (ext_params.enable_7th) {
    bool is_seventh_context =
        (section == SectionType::B || section == SectionType::Chorus) ||
        is_dominant;

    float adjusted_prob = ext_params.seventh_probability;
    if (is_dominant) {
      adjusted_prob *= 2.0f;  // Double probability for V chord
    }

    if (is_seventh_context && roll < adjusted_prob) {
      if (is_dominant) {
        return ChordExtension::Dom7;  // V7
      } else if (is_minor) {
        return ChordExtension::Min7;  // ii7, iii7, vi7
      } else if (is_tonic) {
        return ChordExtension::Maj7;  // Imaj7
      } else {
        // IV chord - major 7th sounds good
        return ChordExtension::Maj7;
      }
    }
  }

  // 9th chords work well on:
  // - Dominant (V9) - jazz/pop feel
  // - Tonic (Imaj9) - lush sound in chorus
  // - Minor chords (ii9, vi9) - sophisticated harmony
  if (ext_params.enable_9th) {
    bool is_ninth_context =
        (section == SectionType::Chorus) ||
        (section == SectionType::B && is_dominant);

    float ninth_roll = dist(rng);
    if (is_ninth_context && ninth_roll < ext_params.ninth_probability) {
      if (is_dominant) {
        return ChordExtension::Dom9;  // V9
      } else if (is_minor) {
        return ChordExtension::Min9;  // ii9, vi9
      } else if (is_tonic) {
        return ChordExtension::Maj9;  // Imaj9
      } else {
        // IV chord - add9 for color
        return ChordExtension::Add9;
      }
    }
  }

  return ChordExtension::None;
}

// Check if the next section is a Chorus (for cadence preparation)
bool shouldAddDominantPreparation(SectionType current, SectionType next,
                                   int8_t current_degree, Mood mood) {
  // Only add dominant preparation before Chorus
  if (next != SectionType::Chorus) return false;

  // Skip for ballads (too dramatic)
  if (mood == Mood::Ballad || mood == Mood::Sentimental) return false;

  // Don't add if already on dominant
  if (isDominant(current_degree)) return false;

  // Add for B -> Chorus transition
  return current == SectionType::B;
}

// Check if section ending needs a cadence fix for irregular progression lengths
// Returns true if the progression ends mid-cycle at section end
bool needsCadenceFix(uint8_t section_bars, uint8_t progression_length,
                     SectionType section, SectionType next_section) {
  // Only apply to main content sections
  if (section == SectionType::Intro || section == SectionType::Interlude ||
      section == SectionType::Outro) {
    return false;
  }

  // Check if progression divides evenly into section
  if (section_bars % progression_length == 0) {
    return false;  // Progression completes naturally
  }

  // Only apply before sections that need resolution (A, Chorus)
  if (next_section == SectionType::Intro || next_section == SectionType::Outro) {
    return false;
  }

  return true;  // Need to insert cadence
}

// Select rhythm pattern based on section, mood, and backing density
ChordRhythm selectRhythm(SectionType section, Mood mood,
                          BackingDensity backing_density) {
  bool is_ballad = (mood == Mood::Ballad || mood == Mood::Sentimental ||
                    mood == Mood::Chill);
  bool is_energetic = (mood == Mood::EnergeticDance || mood == Mood::IdolPop ||
                       mood == Mood::BrightUpbeat);

  ChordRhythm base_rhythm = ChordRhythm::Half;

  switch (section) {
    case SectionType::Intro:
    case SectionType::Interlude:
      base_rhythm = ChordRhythm::Whole;
      break;
    case SectionType::Outro:
      base_rhythm = ChordRhythm::Half;
      break;
    case SectionType::A:
      base_rhythm = is_ballad ? ChordRhythm::Whole : ChordRhythm::Half;
      break;
    case SectionType::B:
      base_rhythm = is_ballad ? ChordRhythm::Half : ChordRhythm::Quarter;
      break;
    case SectionType::Chorus:
      if (is_ballad) base_rhythm = ChordRhythm::Half;
      else if (is_energetic) base_rhythm = ChordRhythm::Eighth;
      else base_rhythm = ChordRhythm::Quarter;
      break;
    case SectionType::Bridge:
      base_rhythm = is_ballad ? ChordRhythm::Whole : ChordRhythm::Half;
      break;
  }

  // Adjust rhythm based on backing density
  if (backing_density == BackingDensity::Thin) {
    // Reduce density: move one level sparser
    switch (base_rhythm) {
      case ChordRhythm::Eighth: return ChordRhythm::Quarter;
      case ChordRhythm::Quarter: return ChordRhythm::Half;
      case ChordRhythm::Half: return ChordRhythm::Whole;
      case ChordRhythm::Whole: return ChordRhythm::Whole;
    }
  } else if (backing_density == BackingDensity::Thick) {
    // Increase density: move one level denser
    switch (base_rhythm) {
      case ChordRhythm::Whole: return ChordRhythm::Half;
      case ChordRhythm::Half: return ChordRhythm::Quarter;
      case ChordRhythm::Quarter: return ChordRhythm::Eighth;
      case ChordRhythm::Eighth: return ChordRhythm::Eighth;
    }
  }

  return base_rhythm;
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
                        const GeneratorParams& params,
                        std::mt19937& rng,
                        const MidiTrack* bass_track) {
  const auto& progression = getChordProgression(params.chord_id);
  const auto& sections = song.arrangement().sections();

  VoicedChord prev_voicing{};
  bool has_prev = false;

  for (size_t sec_idx = 0; sec_idx < sections.size(); ++sec_idx) {
    const auto& section = sections[sec_idx];
    SectionType next_section_type = (sec_idx + 1 < sections.size())
                                        ? sections[sec_idx + 1].type
                                        : section.type;

    ChordRhythm rhythm = selectRhythm(section.type, params.mood,
                                       section.backing_density);
    HarmonicRhythmInfo harmonic = HarmonicRhythmInfo::forSection(section.type, params.mood);

    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;

      // Harmonic rhythm: determine chord index
      int chord_idx;
      if (harmonic.density == HarmonicDensity::Slow) {
        // Slow: chord changes every 2 bars
        chord_idx = (bar / 2) % progression.length;
      } else {
        // Normal/Dense: chord changes every bar
        chord_idx = bar % progression.length;
      }

      int8_t degree = progression.at(chord_idx);
      // Internal processing is always in C major; transpose at MIDI output time
      uint8_t root = degreeToRoot(degree, Key::C);

      // Select chord extension based on context
      ChordExtension extension = selectChordExtension(
          degree, section.type, bar, section.bars,
          params.chord_extension, rng);
      Chord chord = getExtendedChord(degree, extension);

      // Analyze bass pattern for this bar if bass track is available
      bool bass_has_root = true;  // Default assumption
      if (bass_track != nullptr) {
        uint8_t bass_root = static_cast<uint8_t>(
            std::clamp(static_cast<int>(root) - 12, 28, 55));
        BassAnalysis bass_analysis =
            BassAnalysis::analyzeBar(*bass_track, bar_start, bass_root);
        bass_has_root = bass_analysis.has_root_on_beat1;
      }

      // Select voicing type with bass coordination
      VoicingType voicing_type = selectVoicingType(section.type, params.mood,
                                                    bass_has_root);

      // Select voicing with voice leading and type consideration
      VoicedChord voicing = selectVoicing(root, chord, prev_voicing, has_prev,
                                          voicing_type);

      // Check if this is the last bar of the section (for cadence preparation)
      bool is_section_last_bar = (bar == section.bars - 1);

      // Add dominant preparation before Chorus
      if (is_section_last_bar &&
          shouldAddDominantPreparation(section.type, next_section_type, degree, params.mood)) {
        // Insert V chord in the second half of the last bar
        uint8_t vel = calculateVelocity(section.type, 0, params.mood);

        // First half: current chord
        for (size_t i = 0; i < voicing.count; ++i) {
          track.addNote(bar_start, HALF, voicing.pitches[i], vel);
        }

        // Second half: dominant (V) chord - use Dom7 if 7th extensions enabled
        int8_t dominant_degree = 4;  // V
        uint8_t dom_root = degreeToRoot(dominant_degree, Key::C);
        ChordExtension dom_ext = params.chord_extension.enable_7th
                                     ? ChordExtension::Dom7
                                     : ChordExtension::None;
        Chord dom_chord = getExtendedChord(dominant_degree, dom_ext);
        VoicedChord dom_voicing = selectVoicing(dom_root, dom_chord, voicing,
                                                 true, voicing_type);

        uint8_t vel_accent = static_cast<uint8_t>(std::min(127, vel + 5));
        for (size_t i = 0; i < dom_voicing.count; ++i) {
          track.addNote(bar_start + HALF, HALF, dom_voicing.pitches[i], vel_accent);
        }

        prev_voicing = dom_voicing;
        has_prev = true;
        continue;  // Skip normal generation for this bar
      }

      // Fix cadence for irregular progression lengths (e.g., 5-chord in 8-bar section)
      // Insert ii-V in last 2 bars when progression ends mid-cycle
      bool is_second_last_bar = (bar == section.bars - 2);
      if (is_section_last_bar && !isDominant(degree) &&
          needsCadenceFix(section.bars, progression.length, section.type, next_section_type)) {
        // Last bar: insert V chord
        int8_t dominant_degree = 4;  // V
        uint8_t dom_root = degreeToRoot(dominant_degree, Key::C);
        ChordExtension dom_ext = params.chord_extension.enable_7th
                                     ? ChordExtension::Dom7
                                     : ChordExtension::None;
        Chord dom_chord = getExtendedChord(dominant_degree, dom_ext);
        VoicedChord dom_voicing = selectVoicing(dom_root, dom_chord, prev_voicing,
                                                 has_prev, voicing_type);

        generateChordBar(track, bar_start, dom_voicing, rhythm, section.type, params.mood);
        prev_voicing = dom_voicing;
        has_prev = true;
        continue;
      }

      if (is_second_last_bar &&
          needsCadenceFix(section.bars, progression.length, section.type, next_section_type)) {
        // Second-to-last bar: insert ii chord (subdominant preparation)
        int8_t ii_degree = 1;  // ii
        uint8_t ii_root = degreeToRoot(ii_degree, Key::C);
        ChordExtension ii_ext = params.chord_extension.enable_7th
                                    ? ChordExtension::Min7
                                    : ChordExtension::None;
        Chord ii_chord = getExtendedChord(ii_degree, ii_ext);
        VoicedChord ii_voicing = selectVoicing(ii_root, ii_chord, prev_voicing,
                                               has_prev, voicing_type);

        generateChordBar(track, bar_start, ii_voicing, rhythm, section.type, params.mood);
        prev_voicing = ii_voicing;
        has_prev = true;
        continue;
      }

      // Check if this is a phrase-ending bar
      // Phrase end occurs at:
      // 1. Standard 4-bar phrase boundaries (bar 3, 7, etc.)
      // 2. Chord progression cycle boundaries (last chord of progression)
      bool is_4bar_phrase_end = (bar % 4 == 3);
      bool is_chord_cycle_end = (bar % progression.length == progression.length - 1);
      bool is_phrase_end = harmonic.double_at_phrase_end &&
                           (is_4bar_phrase_end || is_chord_cycle_end) &&
                           (bar < section.bars - 1);

      // Dense harmonic rhythm: also allow mid-bar changes on even bars in Chorus
      // for energetic moods (more dynamic harmonic motion)
      bool is_dense_extra = (harmonic.density == HarmonicDensity::Dense) &&
                            (section.type == SectionType::Chorus) &&
                            (bar % 2 == 0) && (bar > 0) &&
                            (params.mood == Mood::EnergeticDance ||
                             params.mood == Mood::IdolPop ||
                             params.mood == Mood::Yoasobi ||
                             params.mood == Mood::FutureBass);

      if ((is_phrase_end || is_dense_extra) && harmonic.density == HarmonicDensity::Dense) {
        // Dense harmonic rhythm at phrase end: split bar into two chords
        // First half: current chord
        uint8_t vel = calculateVelocity(section.type, 0, params.mood);
        for (size_t i = 0; i < voicing.count; ++i) {
          track.addNote(bar_start, HALF, voicing.pitches[i], vel);
        }

        // Second half: next chord (anticipation)
        int next_chord_idx = (chord_idx + 1) % progression.length;
        int8_t next_degree = progression.at(next_chord_idx);
        uint8_t next_root = degreeToRoot(next_degree, Key::C);
        ChordExtension next_ext = selectChordExtension(
            next_degree, section.type, bar + 1, section.bars,
            params.chord_extension, rng);
        Chord next_chord = getExtendedChord(next_degree, next_ext);
        VoicedChord next_voicing = selectVoicing(next_root, next_chord, voicing,
                                                  true, voicing_type);

        uint8_t vel_weak = static_cast<uint8_t>(vel * 0.85f);
        for (size_t i = 0; i < next_voicing.count; ++i) {
          track.addNote(bar_start + HALF, HALF, next_voicing.pitches[i], vel_weak);
        }

        prev_voicing = next_voicing;
      } else {
        // Normal chord generation for this bar
        generateChordBar(track, bar_start, voicing, rhythm, section.type, params.mood);
        prev_voicing = voicing;
      }

      has_prev = true;
    }
  }
}

}  // namespace midisketch
