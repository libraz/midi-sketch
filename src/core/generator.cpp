#include "core/generator.h"
#include "core/chord.h"
#include "core/preset_data.h"
#include "core/structure.h"
#include "core/velocity.h"
#include <algorithm>
#include <array>
#include <chrono>

namespace midisketch {

Generator::Generator() : rng_(42) {}

uint32_t Generator::resolveSeed(uint32_t seed) {
  if (seed == 0) {
    return static_cast<uint32_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
  }
  return seed;
}

void Generator::generate(const GeneratorParams& params) {
  params_ = params;

  // シード初期化
  uint32_t seed = resolveSeed(params.seed);
  rng_.seed(seed);

  // BPM 解決
  result_.bpm = params.bpm;
  if (result_.bpm == 0) {
    result_.bpm = getMoodDefaultBpm(params.mood);
  }

  // 曲構成を生成
  result_.sections = buildStructure(params.structure);
  result_.total_ticks = calculateTotalTicks(result_.sections);

  // 転調設定を計算
  calculateModulation();

  // トラック初期化
  result_.vocal = TrackData{{}, 0, 0};   // Piano
  result_.chord = TrackData{{}, 1, 4};   // Electric Piano
  result_.bass = TrackData{{}, 2, 33};   // Electric Bass
  result_.drums = TrackData{{}, 9, 0};   // Drums
  result_.se = TrackData{{}, 15, 0};     // SE

  // 各トラック生成
  generateChord();
  generateBass();
  generateVocal();

  if (params.drums_enabled) {
    generateDrums();
  }

  generateSE();
}

void Generator::regenerateMelody(uint32_t new_seed) {
  uint32_t seed = resolveSeed(new_seed);
  rng_.seed(seed);
  generateVocal();
}

void Generator::generateVocal() {
  result_.vocal.notes.clear();

  constexpr Tick TICKS_PER_BAR = TICKS_PER_BEAT * 4;
  constexpr Tick EIGHTH = TICKS_PER_BEAT / 2;

  // Major scale semitones (relative to tonic)
  constexpr int SCALE[7] = {0, 2, 4, 5, 7, 9, 11};

  const auto& progression = getChordProgression(params_.chord_id);
  float density = getMoodDensity(params_.mood);

  // Convert key to semitone offset
  int key_offset = static_cast<int>(params_.key);

  // Helper: find scale degree (0-6) from pitch
  auto pitchToScaleDegree = [&](uint8_t pitch) -> int {
    int rel = (pitch - key_offset) % 12;
    if (rel < 0) rel += 12;
    for (int i = 0; i < 7; ++i) {
      if (SCALE[i] == rel) return i;
    }
    return -1;  // Not in scale
  };

  // Helper: snap pitch to nearest scale note within range
  auto snapToScale = [&](int target_pitch) -> uint8_t {
    // Clamp to range
    if (target_pitch < params_.vocal_low) target_pitch = params_.vocal_low;
    if (target_pitch > params_.vocal_high) target_pitch = params_.vocal_high;

    // Find nearest scale note
    int rel = (target_pitch - key_offset) % 12;
    if (rel < 0) rel += 12;

    int best_diff = 12;
    int best_offset = 0;
    for (int sc : SCALE) {
      int diff = std::abs(rel - sc);
      if (diff > 6) diff = 12 - diff;
      if (diff < best_diff) {
        best_diff = diff;
        best_offset = (sc - rel + 12) % 12;
        if (best_offset > 6) best_offset -= 12;
      }
    }
    target_pitch += best_offset;

    // Final clamp
    if (target_pitch < params_.vocal_low) target_pitch += 12;
    if (target_pitch > params_.vocal_high) target_pitch -= 12;

    return static_cast<uint8_t>(std::clamp(target_pitch, (int)params_.vocal_low, (int)params_.vocal_high));
  };

  // Helper: get chord tones for a bar
  auto getChordTones = [&](int bar_in_section) -> std::array<int, 3> {
    int chord_idx = bar_in_section % 4;
    int8_t degree = progression.degrees[chord_idx];
    Chord chord = getChordNotes(degree);

    // Get root in key
    int root_rel = 0;
    if (degree == 10) {
      root_rel = 10;  // bVII
    } else if (degree >= 0 && degree < 7) {
      root_rel = SCALE[degree];
    }
    root_rel = (root_rel + key_offset) % 12;

    std::array<int, 3> tones = {-1, -1, -1};
    for (int i = 0; i < std::min((int)chord.note_count, 3); ++i) {
      if (chord.intervals[i] >= 0) {
        tones[i] = (root_rel + chord.intervals[i]) % 12;
      }
    }
    return tones;
  };

  // Helper: check if pitch is a chord tone
  auto isChordTone = [](uint8_t pitch, const std::array<int, 3>& chord_tones) -> bool {
    int pc = pitch % 12;
    for (int ct : chord_tones) {
      if (ct >= 0 && pc == ct) return true;
    }
    return false;
  };

  // Previous pitch for step-wise motion
  int prev_pitch = (params_.vocal_low + params_.vocal_high) / 2;

  for (const auto& section : result_.sections) {
    // Section-specific melodic behavior
    float section_density = density;
    int motion_range = 4;  // Max step size in scale degrees

    switch (section.type) {
      case SectionType::Intro:
        section_density *= 0.5f;
        motion_range = 2;
        break;
      case SectionType::A:
        section_density *= 0.8f;
        motion_range = 3;
        break;
      case SectionType::B:
        section_density *= 0.9f;
        motion_range = 4;
        break;
      case SectionType::Chorus:
        section_density *= 1.0f;
        motion_range = 5;
        break;
    }

    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;
      auto chord_tones = getChordTones(bar);

      // Phrase structure: rest at end of 4-bar phrase occasionally
      bool is_phrase_end = ((bar + 1) % 4 == 0);

      for (uint8_t beat = 0; beat < 4; ++beat) {
        Tick beat_tick = bar_start + beat * TICKS_PER_BEAT;

        // Phrase-end rest
        if (is_phrase_end && beat == 3) {
          std::uniform_real_distribution<float> rest_dist(0.0f, 1.0f);
          if (rest_dist(rng_) < 0.6f) continue;
        }

        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        if (dist(rng_) >= section_density) continue;

        // Strong beat (0, 2) prefers chord tones
        bool prefer_chord = (beat == 0 || beat == 2);

        // Calculate target pitch with step-wise motion preference
        int target_pitch;
        int current_degree = pitchToScaleDegree(static_cast<uint8_t>(prev_pitch));

        if (current_degree >= 0) {
          // Step-wise motion: move by 0-motion_range scale degrees
          std::uniform_int_distribution<int> step_dist(-motion_range, motion_range);
          int step = step_dist(rng_);

          // Bias toward smaller steps (more natural)
          if (std::abs(step) > 2) {
            if (dist(rng_) < 0.6f) step = step > 0 ? 2 : -2;
          }

          int new_degree = (current_degree + step + 7) % 7;
          int octave = prev_pitch / 12;

          // Adjust octave if crossing boundaries
          if (step > 3 && new_degree < current_degree) octave++;
          if (step < -3 && new_degree > current_degree) octave--;

          target_pitch = octave * 12 + SCALE[new_degree] + key_offset;
        } else {
          // Fallback: random scale note
          std::uniform_int_distribution<int> deg_dist(0, 6);
          int deg = deg_dist(rng_);
          int octave = (params_.vocal_low + params_.vocal_high) / 2 / 12;
          target_pitch = octave * 12 + SCALE[deg] + key_offset;
        }

        uint8_t pitch = snapToScale(target_pitch);

        // For strong beats, try to find a chord tone nearby
        if (prefer_chord && !isChordTone(pitch, chord_tones)) {
          // Look for chord tone within ±2 semitones
          for (int offset = -2; offset <= 2; ++offset) {
            int try_pitch = pitch + offset;
            if (try_pitch >= params_.vocal_low && try_pitch <= params_.vocal_high) {
              uint8_t snapped = snapToScale(try_pitch);
              if (isChordTone(snapped, chord_tones)) {
                pitch = snapped;
                break;
              }
            }
          }
        }

        prev_pitch = pitch;

        // Velocity
        uint8_t velocity = calculateVelocity(section.type, beat, params_.mood);

        // Duration: varies by beat position
        Tick duration;
        if (beat == 0) {
          // Downbeat: longer notes
          std::uniform_int_distribution<int> dur_dist(2, 4);
          duration = TICKS_PER_BEAT * dur_dist(rng_) / 2;
        } else if (beat == 2) {
          // Beat 3: medium
          std::uniform_int_distribution<int> dur_dist(1, 3);
          duration = TICKS_PER_BEAT * dur_dist(rng_) / 2;
        } else {
          // Weak beats: shorter
          std::uniform_int_distribution<int> dur_dist(1, 2);
          duration = EIGHTH * dur_dist(rng_);
        }

        // Occasional 8th note anticipation
        Tick note_start = beat_tick;
        if (beat > 0 && dist(rng_) < 0.15f) {
          note_start -= EIGHTH;
          if (note_start < bar_start) note_start = bar_start;
        }

        result_.vocal.notes.push_back({pitch, velocity, note_start, duration});
      }
    }
  }
}

void Generator::generateChord() {
  result_.chord.notes.clear();

  constexpr Tick TICKS_PER_BAR = TICKS_PER_BEAT * 4;
  const auto& progression = getChordProgression(params_.chord_id);

  for (const auto& section : result_.sections) {
    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;

      // 4小節で1パターン
      int chord_idx = bar % 4;
      int8_t degree = progression.degrees[chord_idx];

      // ルート音を計算
      uint8_t root = degreeToRoot(degree, params_.key);
      Chord chord = getChordNotes(degree);

      // ベロシティ
      uint8_t velocity = calculateVelocity(section.type, 0, params_.mood);

      // コード構成音を追加（全音符）
      for (uint8_t i = 0; i < chord.note_count; ++i) {
        if (chord.intervals[i] >= 0) {
          uint8_t pitch = root + chord.intervals[i];
          result_.chord.notes.push_back({pitch, velocity, bar_start, TICKS_PER_BAR});
        }
      }
    }
  }
}

void Generator::generateBass() {
  result_.bass.notes.clear();

  constexpr Tick TICKS_PER_BAR = TICKS_PER_BEAT * 4;
  const auto& progression = getChordProgression(params_.chord_id);

  for (const auto& section : result_.sections) {
    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;

      int chord_idx = bar % 4;
      int8_t degree = progression.degrees[chord_idx];

      // ルート音（1オクターブ下）
      uint8_t root = degreeToRoot(degree, params_.key) - 12;
      if (root < 28) root = 28;  // E1

      uint8_t velocity = calculateVelocity(section.type, 0, params_.mood);

      // 2分音符 × 2
      result_.bass.notes.push_back({root, velocity, bar_start, TICKS_PER_BAR / 2});
      result_.bass.notes.push_back({root, velocity, bar_start + TICKS_PER_BAR / 2, TICKS_PER_BAR / 2});
    }
  }
}

void Generator::generateDrums() {
  result_.drums.notes.clear();

  constexpr Tick TICKS_PER_BAR = TICKS_PER_BEAT * 4;
  constexpr Tick EIGHTH = TICKS_PER_BEAT / 2;
  constexpr uint8_t BD = 36;    // Bass Drum
  constexpr uint8_t SD = 38;    // Snare
  constexpr uint8_t CHH = 42;   // Closed Hi-Hat
  constexpr uint8_t OHH = 46;   // Open Hi-Hat
  constexpr uint8_t CRASH = 49; // Crash Cymbal
  constexpr uint8_t TOM_H = 50; // High Tom
  constexpr uint8_t TOM_M = 47; // Mid Tom
  constexpr uint8_t TOM_L = 45; // Low Tom

  float base_density = getMoodDensity(params_.mood);

  for (size_t sec_idx = 0; sec_idx < result_.sections.size(); ++sec_idx) {
    const auto& section = result_.sections[sec_idx];
    bool is_last_section = (sec_idx == result_.sections.size() - 1);

    // Section-specific density multiplier
    float density_mult = 1.0f;
    switch (section.type) {
      case SectionType::Intro:
        density_mult = 0.6f;
        break;
      case SectionType::A:
        density_mult = 0.8f;
        break;
      case SectionType::B:
        density_mult = 0.9f;
        break;
      case SectionType::Chorus:
        density_mult = 1.0f;
        break;
    }

    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;
      bool is_section_last_bar = (bar == section.bars - 1);

      // Crash on first beat of Chorus
      if (bar == 0 && section.type == SectionType::Chorus) {
        uint8_t crash_vel = calculateVelocity(section.type, 0, params_.mood);
        result_.drums.notes.push_back({CRASH, crash_vel, bar_start, EIGHTH});
      }

      for (uint8_t beat = 0; beat < 4; ++beat) {
        Tick beat_tick = bar_start + beat * TICKS_PER_BEAT;
        uint8_t velocity = calculateVelocity(section.type, beat, params_.mood);

        // Fill pattern on last bar of section (before next section)
        if (is_section_last_bar && !is_last_section && beat >= 2) {
          uint8_t fill_vel = static_cast<uint8_t>(velocity * 0.9f);
          if (beat == 2) {
            result_.drums.notes.push_back({SD, fill_vel, beat_tick, EIGHTH});
            result_.drums.notes.push_back({SD, static_cast<uint8_t>(fill_vel - 10), beat_tick + EIGHTH, EIGHTH});
          } else {  // beat == 3
            result_.drums.notes.push_back({TOM_H, fill_vel, beat_tick, EIGHTH / 2});
            result_.drums.notes.push_back({TOM_M, static_cast<uint8_t>(fill_vel - 5), beat_tick + EIGHTH / 2, EIGHTH / 2});
            result_.drums.notes.push_back({TOM_L, static_cast<uint8_t>(fill_vel - 10), beat_tick + EIGHTH, EIGHTH / 2});
            result_.drums.notes.push_back({BD, velocity, beat_tick + EIGHTH + EIGHTH / 2, EIGHTH / 2});
          }
          continue;
        }

        // BD pattern varies by section
        bool play_bd = false;
        if (section.type == SectionType::Intro) {
          // Intro: BD on 1 only
          play_bd = (beat == 0);
        } else if (section.type == SectionType::Chorus) {
          // Chorus: BD on 1, 3, and occasional &3
          play_bd = (beat == 0 || beat == 2);
          if (beat == 2 && base_density > 0.5f) {
            // Add syncopated BD on &2 for high-energy moods
            std::uniform_real_distribution<float> dist(0.0f, 1.0f);
            if (dist(rng_) < base_density * 0.3f) {
              result_.drums.notes.push_back({BD, static_cast<uint8_t>(velocity - 15), beat_tick - EIGHTH, EIGHTH});
            }
          }
        } else {
          // A, B: BD on 1, 3
          play_bd = (beat == 0 || beat == 2);
        }

        if (play_bd) {
          result_.drums.notes.push_back({BD, velocity, beat_tick, EIGHTH});
        }

        // SD on 2, 4 (skip for Intro first bar)
        if ((beat == 1 || beat == 3) && !(section.type == SectionType::Intro && bar == 0)) {
          result_.drums.notes.push_back({SD, velocity, beat_tick, EIGHTH});
        }

        // 8th note hi-hats with accent pattern
        for (int eighth = 0; eighth < 2; ++eighth) {
          Tick hh_tick = beat_tick + eighth * EIGHTH;
          uint8_t hh_vel = static_cast<uint8_t>(velocity * density_mult);

          // Accent on downbeats
          if (eighth == 0) {
            hh_vel = static_cast<uint8_t>(hh_vel * 0.9f);
          } else {
            hh_vel = static_cast<uint8_t>(hh_vel * 0.7f);
          }

          // Intro uses quarter notes only
          if (section.type == SectionType::Intro && eighth == 1) {
            continue;
          }

          // Open hi-hat on &4 occasionally for Chorus
          if (section.type == SectionType::Chorus && beat == 3 && eighth == 1) {
            std::uniform_real_distribution<float> dist(0.0f, 1.0f);
            if (dist(rng_) < 0.3f) {
              result_.drums.notes.push_back({OHH, hh_vel, hh_tick, EIGHTH});
              continue;
            }
          }

          result_.drums.notes.push_back({CHH, hh_vel, hh_tick, EIGHTH / 2});
        }
      }
    }
  }
}

void Generator::calculateModulation() {
  result_.modulation_tick = 0;
  result_.modulation_amount = 0;

  if (!params_.modulation) {
    return;
  }

  // Determine modulation amount based on mood
  // Ballad = +2 semitones, others = +1 semitone
  if (params_.mood == Mood::Ballad) {
    result_.modulation_amount = 2;
  } else {
    result_.modulation_amount = 1;
  }

  // Find modulation point based on structure
  switch (params_.structure) {
    case StructurePattern::RepeatChorus: {
      // Between first and second Chorus
      int chorus_count = 0;
      for (const auto& section : result_.sections) {
        if (section.type == SectionType::Chorus) {
          chorus_count++;
          if (chorus_count == 2) {
            result_.modulation_tick = section.start_tick;
            break;
          }
        }
      }
      break;
    }
    case StructurePattern::StandardPop:
    case StructurePattern::BuildUp: {
      // Between B and Chorus
      for (size_t i = 0; i < result_.sections.size(); ++i) {
        if (result_.sections[i].type == SectionType::Chorus) {
          // Check if previous section was B
          if (i > 0 && result_.sections[i - 1].type == SectionType::B) {
            result_.modulation_tick = result_.sections[i].start_tick;
            break;
          }
        }
      }
      break;
    }
    case StructurePattern::DirectChorus:
    case StructurePattern::ShortForm:
      // No modulation for short structures
      result_.modulation_tick = 0;
      result_.modulation_amount = 0;
      break;
  }
}

void Generator::generateSE() {
  result_.se.notes.clear();
  result_.markers.clear();

  // Add section markers for each section
  for (const auto& section : result_.sections) {
    std::string name;
    switch (section.type) {
      case SectionType::Intro:
        name = "Intro";
        break;
      case SectionType::A:
        name = "A";
        break;
      case SectionType::B:
        name = "B";
        break;
      case SectionType::Chorus:
        name = "Chorus";
        break;
    }
    result_.markers.push_back({section.start_tick, name});
  }

  // Add modulation marker if applicable
  if (result_.modulation_tick > 0 && result_.modulation_amount > 0) {
    std::string mod_text = "Mod+" + std::to_string(result_.modulation_amount);
    result_.markers.push_back({result_.modulation_tick, mod_text});
  }
}

}  // namespace midisketch
