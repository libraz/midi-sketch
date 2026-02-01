/**
 * @file i_track_base.h
 * @brief Base interface for all track generators.
 *
 * Provides:
 * - Physical model constraints (pitch range, velocity, note duration)
 * - Safe note creation with collision checking
 * - Priority-based generation order
 */

#ifndef MIDISKETCH_CORE_I_TRACK_BASE_H
#define MIDISKETCH_CORE_I_TRACK_BASE_H

#include <optional>
#include <random>

#include "core/basic_types.h"
#include "core/i_harmony_coordinator.h"

namespace midisketch {

// Forward declarations
class MidiTrack;
struct Section;
class IHarmonyCoordinator;

/// @brief Physical model constraints for an instrument.
///
/// Enforces realistic instrument capabilities:
/// - Pitch range (e.g., bass guitar: E1-G4)
/// - Velocity range (e.g., pad: 40-100)
/// - Minimum note duration (e.g., staccato limit)
/// - Legato capability
struct PhysicalModel {
  uint8_t pitch_low = 0;              ///< Lowest playable pitch
  uint8_t pitch_high = 127;           ///< Highest playable pitch
  uint8_t velocity_min = 1;           ///< Minimum velocity
  uint8_t velocity_max = 127;         ///< Maximum velocity
  Tick min_note_duration = 60;        ///< Minimum note duration (ticks)
  bool supports_legato = true;        ///< Can play legato passages
  int8_t vocal_ceiling_offset = 0;    ///< Offset from vocal high (-2 = 2 semitones below)

  /// @brief Clamp a pitch to the valid range.
  uint8_t clampPitch(uint8_t pitch) const {
    if (pitch < pitch_low) return pitch_low;
    if (pitch > pitch_high) return pitch_high;
    return pitch;
  }

  /// @brief Clamp a velocity to the valid range.
  uint8_t clampVelocity(uint8_t velocity) const {
    if (velocity < velocity_min) return velocity_min;
    if (velocity > velocity_max) return velocity_max;
    return velocity;
  }

  /// @brief Check if a pitch is within range.
  bool isPitchInRange(uint8_t pitch) const {
    return pitch >= pitch_low && pitch <= pitch_high;
  }

  /// @brief Get effective upper limit considering vocal ceiling.
  /// @param vocal_high Vocal track's highest pitch
  /// @return Effective upper pitch limit
  uint8_t getEffectiveHigh(uint8_t vocal_high) const {
    if (vocal_ceiling_offset == 0) return pitch_high;
    int effective = static_cast<int>(vocal_high) + vocal_ceiling_offset;
    if (effective < pitch_low) return pitch_low;
    if (effective > pitch_high) return pitch_high;
    return static_cast<uint8_t>(effective);
  }
};

/// @brief Default physical models for common instruments.
namespace PhysicalModels {

/// Electric Bass: E1 (28) to G4 (67)
inline constexpr PhysicalModel kElectricBass = {
    28, 67, 40, 127, 120, true, 0
};

/// Synth Bass: C1 (24) to C4 (60)
inline constexpr PhysicalModel kSynthBass = {
    24, 60, 50, 127, 60, true, 0
};

/// Electric Piano: C3 (48) to C6 (84), respects vocal ceiling
inline constexpr PhysicalModel kElectricPiano = {
    48, 84, 40, 110, 60, true, -2  // 2 semitones below vocal high
};

/// Acoustic Guitar: E2 (40) to B5 (83)
inline constexpr PhysicalModel kAcousticGuitar = {
    40, 83, 30, 100, 120, true, 3  // 3 semitones above vocal low
};

/// Synth Pad: C2 (36) to C7 (96)
inline constexpr PhysicalModel kSynthPad = {
    36, 96, 40, 100, 480, true, 0
};

/// Synth Lead: C3 (48) to C7 (96)
inline constexpr PhysicalModel kSynthLead = {
    48, 96, 60, 127, 60, true, 0
};

/// Vocal: C4 (60) to G5 (79) default, configurable
inline constexpr PhysicalModel kVocal = {
    60, 79, 50, 127, 120, true, 0
};

/// Aux Vocal: Similar to main vocal
inline constexpr PhysicalModel kAuxVocal = {
    55, 84, 40, 110, 120, true, 0
};

/// Motif Synth: C3 (48) to C6 (84)
inline constexpr PhysicalModel kMotifSynth = {
    48, 84, 60, 100, 60, false, 0
};

/// Arpeggio Synth: C3 (48) to C8 (108)
inline constexpr PhysicalModel kArpeggioSynth = {
    48, 108, 60, 100, 30, false, 0
};

}  // namespace PhysicalModels

/// @brief Track configuration for generation.
struct TrackConfig {
  uint8_t vocal_low = 60;             ///< Vocal range low
  uint8_t vocal_high = 79;            ///< Vocal range high
  uint8_t base_velocity = 80;         ///< Base velocity
  float density = 1.0f;               ///< Note density multiplier
  bool is_coordinate_axis = false;    ///< True if this track is the axis (no adjustment)
};

/// @brief Track generation context.
struct TrackContext {
  IHarmonyCoordinator* harmony = nullptr;  ///< Harmony coordinator
  const PhysicalModel* model = nullptr;    ///< Physical model constraints
  TrackConfig config;                      ///< Track configuration
};

// Forward declarations for FullTrackContext
class Song;
struct GeneratorParams;

/// @brief Full track generation context for generateFullTrack().
///
/// Contains all parameters needed for full-track generation,
/// allowing Coordinator to call generators with a unified interface.
struct FullTrackContext {
  Song* song = nullptr;                    ///< Song (mutable for setMotifPattern etc.)
  const GeneratorParams* params = nullptr; ///< Generation parameters
  std::mt19937* rng = nullptr;             ///< Random number generator
  IHarmonyCoordinator* harmony = nullptr;  ///< Harmony coordinator

  // Track-specific options (set by Coordinator based on paradigm)
  bool skip_collision_avoidance = false;   ///< For vocal-first mode
  const void* drum_grid = nullptr;         ///< DrumGrid* for RhythmSync
  const void* kick_cache = nullptr;        ///< KickPatternCache* for bass-kick sync
  const void* vocal_ctx = nullptr;         ///< MotifContext* for motif generation
  const void* vocal_analysis = nullptr;    ///< VocalAnalysis* for adapting to vocal

  // Call system options (for SE track)
  bool call_enabled = false;
  bool call_notes_enabled = true;
  uint8_t intro_chant = 0;   // IntroChant enum value
  uint8_t mix_pattern = 0;   // MixPattern enum value
  uint8_t call_density = 0;  // CallDensity enum value
};

/// @brief Base interface for all track generators.
///
/// All track generators implement this interface to ensure:
/// - Consistent physical model constraints
/// - Priority-based collision avoidance
/// - Safe note creation
class ITrackBase {
 public:
  virtual ~ITrackBase();

  /// @brief Get the track role this generator handles.
  virtual TrackRole getRole() const = 0;

  /// @brief Get the default priority for this track.
  virtual TrackPriority getDefaultPriority() const = 0;

  /// @brief Get the physical model for this track's instrument.
  virtual PhysicalModel getPhysicalModel() const = 0;

  /// @brief Configure the generator with parameters.
  /// @param config Track configuration
  virtual void configure(const TrackConfig& config) = 0;

  /// @brief Generate notes for a section.
  /// @param track Target MIDI track
  /// @param section Section to generate
  /// @param ctx Track generation context
  virtual void generateSection(MidiTrack& track, const Section& section,
                                TrackContext& ctx) = 0;

  /// @brief Clamp a pitch to the physical model range.
  /// @param pitch Input pitch
  /// @param ctx Track context
  /// @return Clamped pitch
  uint8_t clampToRange(uint8_t pitch, const TrackContext& ctx) const {
    if (!ctx.model) return pitch;
    return ctx.model->clampPitch(pitch);
  }

  /// @brief Generate full track (all sections).
  ///
  /// Override for tracks that need section-spanning logic (phrases, pattern caching).
  /// TrackBase provides a default implementation that loops through sections.
  ///
  /// @param track Target MIDI track
  /// @param ctx Full track generation context
  virtual void generateFullTrack(MidiTrack& track, const FullTrackContext& ctx) = 0;
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_I_TRACK_BASE_H
