/**
 * @file motif_types.h
 * @brief Motif-related type definitions.
 */

#ifndef MIDISKETCH_CORE_MOTIF_TYPES_H
#define MIDISKETCH_CORE_MOTIF_TYPES_H

#include <cstdint>
#include <map>
#include <vector>

#include "core/basic_types.h"    // For NoteEvent, HihatDensity, Tick, PhraseBoundary
#include "core/json_helpers.h"   // For json::Writer, json::Parser
#include "core/melody_types.h"   // For VocalProminence, VocalRhythmBias

namespace midisketch {

/// @brief Motif length in bars.
enum class MotifLength : uint8_t {
  Bars1 = 1,  ///< 1-bar motif (dense, continuous patterns)
  Bars2 = 2,
  Bars4 = 4
};

/// @brief Motif rhythm density.
enum class MotifRhythmDensity : uint8_t {
  Sparse,  ///< Quarter note based
  Medium,  ///< Eighth note based
  Driving  ///< Eighth + light 16th
};

/// @brief Motif melodic motion.
enum class MotifMotion : uint8_t {
  Stepwise,   ///< Scale steps only (2nd intervals)
  GentleLeap, ///< Up to 3rd intervals
  WideLeap,   ///< Up to 5th intervals (more dramatic)
  NarrowStep, ///< Tight scale-degree motion (±1 degree, jazzy/tense feel)
  Disjunct    ///< Irregular leaps (experimental, avant-garde)
};

/// @brief Motif repetition scope.
enum class MotifRepeatScope : uint8_t {
  FullSong,  ///< Same motif throughout
  Section    ///< Regenerate per section
};

/// @brief Motif (background melody) configuration.
/// Only active when composition_style = BackgroundMotif.
struct MotifParams {
  MotifLength length = MotifLength::Bars2;
  uint8_t note_count = 6;      ///< 3-8 notes per motif cycle
  bool register_high = false;  ///< false=mid, true=high
  MotifRhythmDensity rhythm_density = MotifRhythmDensity::Medium;
  MotifMotion motion = MotifMotion::Stepwise;
  MotifRepeatScope repeat_scope = MotifRepeatScope::FullSong;
  bool octave_layering_chorus = true;  ///< Double at chorus
  bool velocity_fixed = true;          ///< Fixed velocity (groove via drums)

  /// Melodic freedom in RhythmSync mode (0.0-1.0).
  /// 0.0 = all notes snapped to chord tones (root, 3rd, 5th)
  /// 1.0 = all scale tones allowed (includes passing tones: 2nd, 4th, 6th, 7th)
  /// Default 0.4 allows some passing tones for melodic interest while
  /// maintaining harmonic stability appropriate for background motifs.
  float melodic_freedom = 0.4f;

  // Vocal coordination parameters (MelodyLead mode)
  bool response_mode = true;           ///< Increase activity during vocal rests
  float response_probability = 0.6f;   ///< Probability of playing during vocal rests
  bool contrary_motion = true;         ///< Apply contrary motion to vocal direction
  float contrary_motion_strength = 0.5f;  ///< Strength of contrary motion adjustment
  bool dynamic_register = true;        ///< Dynamically adjust register to avoid vocal
  int8_t register_offset = 0;          ///< Additional register offset in semitones

  void writeTo(json::Writer& w) const {
    w.write("length", static_cast<int>(length))
        .write("note_count", static_cast<int>(note_count))
        .write("register_high", register_high)
        .write("rhythm_density", static_cast<int>(rhythm_density))
        .write("motion", static_cast<int>(motion))
        .write("repeat_scope", static_cast<int>(repeat_scope))
        .write("octave_layering_chorus", octave_layering_chorus)
        .write("velocity_fixed", velocity_fixed)
        .write("melodic_freedom", melodic_freedom)
        .write("response_mode", response_mode)
        .write("response_probability", response_probability)
        .write("contrary_motion", contrary_motion)
        .write("contrary_motion_strength", contrary_motion_strength)
        .write("dynamic_register", dynamic_register)
        .write("register_offset", static_cast<int>(register_offset));
  }

  void readFrom(const json::Parser& p) {
    length = static_cast<MotifLength>(p.getInt("length", 2));
    note_count = static_cast<uint8_t>(p.getInt("note_count", 6));
    register_high = p.getBool("register_high", false);
    rhythm_density = static_cast<MotifRhythmDensity>(p.getInt("rhythm_density", 1));
    motion = static_cast<MotifMotion>(p.getInt("motion", 0));
    repeat_scope = static_cast<MotifRepeatScope>(p.getInt("repeat_scope", 0));
    octave_layering_chorus = p.getBool("octave_layering_chorus", true);
    velocity_fixed = p.getBool("velocity_fixed", true);
    melodic_freedom = p.getFloat("melodic_freedom", 0.4f);
    response_mode = p.getBool("response_mode", true);
    response_probability = p.getFloat("response_probability", 0.6f);
    contrary_motion = p.getBool("contrary_motion", true);
    contrary_motion_strength = p.getFloat("contrary_motion_strength", 0.5f);
    dynamic_register = p.getBool("dynamic_register", true);
    register_offset = p.getInt8("register_offset", 0);
  }
};

/// @brief Context for vocal-aware motif generation in MelodyLead mode.
/// Similar to AuxContext pattern for passing vocal analysis to track generators.
struct MotifContext {
  /// Phrase boundaries from vocal generation (for breath coordination).
  const std::vector<PhraseBoundary>* phrase_boundaries = nullptr;
  /// Tick positions where vocal rests begin (for response mode).
  const std::vector<Tick>* rest_positions = nullptr;
  /// Lowest MIDI pitch in vocal track.
  uint8_t vocal_low = 60;
  /// Highest MIDI pitch in vocal track.
  uint8_t vocal_high = 72;
  /// Vocal note density (0.0-1.0).
  float vocal_density = 0.5f;
  /// Tick-indexed vocal direction: +1=up, -1=down, 0=same.
  const std::map<Tick, int8_t>* direction_at_tick = nullptr;
};

/// @brief Background motif specific chord constraints.
struct MotifChordParams {
  bool fixed_progression = true;  ///< Same progression all sections
  uint8_t max_chord_count = 4;    ///< Max 4 for motif style

  void writeTo(json::Writer& w) const {
    w.write("fixed_progression", fixed_progression)
        .write("max_chord_count", static_cast<int>(max_chord_count));
  }

  void readFrom(const json::Parser& p) {
    fixed_progression = p.getBool("fixed_progression", true);
    max_chord_count = static_cast<uint8_t>(p.getInt("max_chord_count", 4));
  }
};

/// @brief Background motif drum configuration.
struct MotifDrumParams {
  bool hihat_drive = true;  ///< Hi-hat is primary driver
  HihatDensity hihat_density = HihatDensity::Eighth;

  void writeTo(json::Writer& w) const {
    w.write("hihat_drive", hihat_drive)
        .write("hihat_density", static_cast<int>(hihat_density));
  }

  void readFrom(const json::Parser& p) {
    hihat_drive = p.getBool("hihat_drive", true);
    hihat_density = static_cast<HihatDensity>(p.getInt("hihat_density", 0));
  }
};

/// @brief Background motif vocal suppression.
struct MotifVocalParams {
  VocalProminence prominence = VocalProminence::Background;
  VocalRhythmBias rhythm_bias = VocalRhythmBias::Sparse;
  uint8_t interval_limit = 4;  ///< Max interval in semitones (3rd=4, 5th=7)

  void writeTo(json::Writer& w) const {
    w.write("prominence", static_cast<int>(prominence))
        .write("rhythm_bias", static_cast<int>(rhythm_bias))
        .write("interval_limit", static_cast<int>(interval_limit));
  }

  void readFrom(const json::Parser& p) {
    prominence = static_cast<VocalProminence>(p.getInt("prominence", 1));
    rhythm_bias = static_cast<VocalRhythmBias>(p.getInt("rhythm_bias", 2));
    interval_limit = static_cast<uint8_t>(p.getInt("interval_limit", 4));
  }
};

/// @brief Motif data for saving/restoring motif patterns.
struct MotifData {
  uint32_t seed;
  std::vector<NoteEvent> pattern;  ///< Base motif pattern (one cycle)
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_MOTIF_TYPES_H
