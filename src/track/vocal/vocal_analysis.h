/**
 * @file vocal_analysis.h
 * @brief Vocal analysis for rhythm adaptation and counterpoint.
 */

#ifndef MIDISKETCH_TRACK_VOCAL_VOCAL_ANALYSIS_H
#define MIDISKETCH_TRACK_VOCAL_VOCAL_ANALYSIS_H

#include <cstdint>
#include <map>
#include <random>
#include <vector>

#include "core/midi_track.h"
#include "core/section_types.h"

namespace midisketch {

/// Voice leading motion type (counterpoint). Oblique ~40%, Contrary ~30%, Similar ~20%, Parallel
/// ~10%.
/// Note: Classical parallel 5th/octave avoidance is intentionally NOT enforced.
/// Pop music regularly uses parallel perfect intervals (e.g., power chords, octave doubling).
/// See bass.cpp:adjustPitchForMotion() for detailed design rationale.
enum class MotionType : uint8_t {
  Oblique,   ///< One sustains, other moves (most common in pop)
  Contrary,  ///< Opposite directions (best independence)
  Similar,   ///< Same direction, different intervals
  Parallel   ///< Same interval - 3rds/6ths sound good, P5/P8 acceptable in pop context
};

/// Phrase boundary extracted from vocal. Detected by gaps >= half bar.
struct VocalPhraseInfo {
  Tick start_tick;        ///< Phrase start position in ticks
  Tick end_tick;          ///< Phrase end position in ticks
  float density;          ///< Note coverage ratio within phrase (0.0-1.0)
  uint8_t lowest_pitch;   ///< Lowest MIDI pitch in phrase (0-127)
  uint8_t highest_pitch;  ///< Highest MIDI pitch in phrase (0-127)
};

/// Complete vocal analysis for accompaniment adaptation (vocal-first workflow).
struct VocalAnalysis {
  float density;           ///< Note coverage ratio (0.0-1.0)
  float average_duration;  ///< Mean note duration in ticks
  uint8_t lowest_pitch;    ///< Lowest MIDI pitch (0-127)
  uint8_t highest_pitch;   ///< Highest MIDI pitch (0-127)

  std::vector<VocalPhraseInfo> phrases;  ///< Detected phrase boundaries
  std::vector<Tick> rest_positions;      ///< Tick positions where rests begin

  std::vector<int8_t> pitch_directions;  ///< Per-note direction: +1=up, -1=down, 0=same

  // Tick-indexed lookups for O(log n) queries
  std::map<Tick, uint8_t> pitch_at_tick;     ///< Note start -> pitch
  std::map<Tick, Tick> note_end_at_tick;     ///< Note start -> end tick
  std::map<Tick, int8_t> direction_at_tick;  ///< Note start -> direction
};

/// Analyze vocal track for accompaniment adaptation.
VocalAnalysis analyzeVocal(const MidiTrack& vocal_track);

/// Get vocal note density for a section (0.0-1.0).
float getVocalDensityForSection(const VocalAnalysis& va, const Section& section);

/// Get vocal pitch direction at tick: +1=up, -1=down, 0=none.
int8_t getVocalDirectionAt(const VocalAnalysis& va, Tick tick);

/// Get vocal MIDI pitch at tick (0-127), or 0 if silent.
uint8_t getVocalPitchAt(const VocalAnalysis& va, Tick tick);

/// Check if vocal is resting at tick.
bool isVocalRestingAt(const VocalAnalysis& va, Tick tick);

/// Select bass motion type based on vocal direction (weighted random).
MotionType selectMotionType(int8_t vocal_direction, int bar_position, std::mt19937& rng);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_VOCAL_VOCAL_ANALYSIS_H
