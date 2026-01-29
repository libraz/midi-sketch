/**
 * @file hihat_control.h
 * @brief Hi-hat pattern generation and control.
 */

#ifndef MIDISKETCH_TRACK_DRUMS_HIHAT_CONTROL_H
#define MIDISKETCH_TRACK_DRUMS_HIHAT_CONTROL_H

#include <random>

#include "core/midi_track.h"
#include "core/preset_data.h"
#include "core/section_types.h"
#include "core/types.h"

namespace midisketch {
namespace drums {

/// @brief Hi-hat subdivision level.
enum class HiHatLevel {
  Quarter,   ///< Quarter notes only
  Eighth,    ///< 8th notes
  Sixteenth  ///< 16th notes
};

/// @brief Hi-hat type for section-aware timekeeping.
enum class HiHatType : uint8_t {
  Closed,     ///< Standard closed HH (GM 42)
  Pedal,      ///< Foot/pedal HH (GM 44) - subtle, short
  Open,       ///< Open HH (GM 46) - bright, sustaining
  HalfOpen,   ///< Half-open: emulated with Closed HH at 70-80% velocity
  Ride        ///< Ride cymbal (GM 51) - for Bridge/contrast
};

// BPM threshold for 16th note hi-hat playability
constexpr uint16_t HH_16TH_BPM_THRESHOLD = 150;

// Foot hi-hat velocity range
constexpr uint8_t FHH_VEL_MIN = 45;
constexpr uint8_t FHH_VEL_MAX = 60;

// Open hi-hat velocity boost
constexpr uint8_t OHH_VEL_BOOST = 7;

/// @brief Adjust hi-hat level one step sparser.
HiHatLevel adjustHiHatSparser(HiHatLevel level);

/// @brief Adjust hi-hat level one step denser.
HiHatLevel adjustHiHatDenser(HiHatLevel level);

/// @brief Get hi-hat level with randomized variation.
HiHatLevel getHiHatLevel(SectionType section, DrumStyle style, BackingDensity backing_density,
                         uint16_t bpm, std::mt19937& rng,
                         GenerationParadigm paradigm = GenerationParadigm::Traditional);

/// @brief Get hi-hat velocity multiplier for 16th note position.
float getHiHatVelocityMultiplier(int sixteenth, std::mt19937& rng);

/// @brief Determine the open hi-hat interval for a section.
int getOpenHiHatBarInterval(SectionType section, DrumStyle style);

/// @brief Determine which beat gets the open hi-hat within a bar.
uint8_t getOpenHiHatBeat(SectionType section, int bar, std::mt19937& rng);

/// @brief Check if a section should use foot hi-hat.
bool shouldUseFootHiHat(SectionType section, DrumRole drum_role);

/// @brief Get the primary hi-hat type for a section.
HiHatType getSectionHiHatType(SectionType section, DrumRole drum_role);

/// @brief Get the GM note number for a hi-hat type.
uint8_t getHiHatNote(HiHatType type);

/// @brief Get velocity multiplier for hi-hat type.
float getHiHatVelocityMultiplierForType(HiHatType type);

/// @brief Check if open HH accent should be added at this beat.
bool shouldAddOpenHHAccent(SectionType section, int beat, int bar, std::mt19937& rng);

/// @brief Get foot hi-hat velocity with slight humanization.
uint8_t getFootHiHatVelocity(std::mt19937& rng);

/// @brief Check if a crash cymbal exists at the given tick.
bool hasCrashAtTick(const MidiTrack& track, Tick tick);

/// @brief Check if ride should be used for section.
bool shouldUseRideForSection(SectionType section, DrumStyle style);

/// @brief Check if bridge cross-stick should be used.
bool shouldUseBridgeCrossStick(SectionType section, uint8_t beat);

/// @brief Get the appropriate timekeeping instrument.
uint8_t getTimekeepingInstrument(SectionType section, DrumStyle style, DrumRole role);

}  // namespace drums
}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_DRUMS_HIHAT_CONTROL_H
