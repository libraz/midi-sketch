/**
 * @file drum_types.h
 * @brief Types for drum physical modeling.
 *
 * Defines drum-specific types including limb allocation,
 * drum setup configurations, and physical constraints.
 */

#ifndef MIDISKETCH_INSTRUMENT_DRUMS_DRUM_TYPES_H
#define MIDISKETCH_INSTRUMENT_DRUMS_DRUM_TYPES_H

#include <array>
#include <cstdint>
#include <map>
#include <optional>

#include "core/basic_types.h"
#include "core/timing_constants.h"
#include "track/drums/drum_constants.h"
#include "instrument/common/performer_types.h"

namespace midisketch {

/// @brief Limb identifier for drum playing.
enum class Limb : uint8_t {
  RightHand = 0,
  LeftHand = 1,
  RightFoot = 2,
  LeftFoot = 3
};

/// @brief Number of limbs.
constexpr size_t kLimbCount = 4;

/// @brief Drum playing style configuration.
enum class DrumPlayStyle : uint8_t {
  CrossStick,  ///< Traditional: right hand crosses to snare, left on HH
  OpenHand     ///< Modern: right on HH, left on snare (no crossing)
};

/// @brief Limb flexibility for a drum part.
enum class LimbFlexibility : uint8_t {
  Fixed,        ///< Only assigned limb can play (kick = right foot)
  Either,       ///< Either hand/foot can play (toms, crash)
  Alternating   ///< Must alternate (double bass, high-speed rolls)
};

/// @brief Physical constraints for a limb.
struct LimbPhysics {
  Tick min_single_interval;   ///< Minimum interval for single strokes (RLRL)
  Tick min_double_interval;   ///< Minimum interval for double strokes (RRLL)
  Tick min_triple_interval;   ///< Minimum interval for 3+ consecutive hits
  float fatigue_rate;         ///< Fatigue accumulation per fast hit
  float recovery_rate;        ///< Recovery rate per beat of rest

  /// @brief Hand physics for average player.
  static LimbPhysics hand() {
    return {
        TICK_32ND,       // 60 ticks at BPM 120 = 16 hits/sec
        TICK_64TH,       // 30 ticks (double stroke half)
        TICK_32ND + 10,  // 70 ticks (triples are slower)
        0.015f,          // Low fatigue rate
        0.02f            // Fast recovery
    };
  }

  /// @brief Hand physics for advanced player.
  static LimbPhysics handAdvanced() {
    return {
        TICK_32ND - 10,  // 50 ticks
        TICK_64TH - 5,   // 25 ticks
        TICK_32ND,       // 60 ticks
        0.01f,           // Very low fatigue
        0.025f           // Very fast recovery
    };
  }

  /// @brief Foot physics for average player.
  static LimbPhysics foot() {
    return {
        TICK_SIXTEENTH,       // 120 ticks (feet are slower)
        TICK_32ND,            // 60 ticks (heel-toe)
        TICK_SIXTEENTH + 20,  // 140 ticks
        0.025f,               // Higher fatigue
        0.01f                 // Slower recovery
    };
  }

  /// @brief Foot physics for advanced player (double bass).
  static LimbPhysics footAdvanced() {
    return {
        TICK_32ND + 20,  // 80 ticks
        TICK_32ND,       // 60 ticks
        TICK_32ND + 30,  // 90 ticks
        0.02f,
        0.015f
    };
  }
};

/// @brief Drum rudiment parameters.
namespace Rudiment {
/// @brief Flam grace note offset (ticks before main note).
constexpr Tick kFlamGraceOffset = 15;
/// @brief Flam grace note velocity.
constexpr uint8_t kFlamGraceVelocity = 40;
/// @brief Drag first grace offset.
constexpr Tick kDragFirstGrace = 30;
/// @brief Drag second grace offset.
constexpr Tick kDragSecondGrace = 15;
/// @brief Drag grace velocity.
constexpr uint8_t kDragGraceVelocity = 35;
/// @brief Ghost note velocity.
constexpr uint8_t kGhostNoteVelocity = 25;
/// @brief Ghost note max duration.
constexpr Tick kGhostNoteMaxDuration = 40;
}  // namespace Rudiment

/// @brief Drum technique type.
enum class DrumTechnique : uint8_t {
  // Stroke types
  Single,            ///< Single stroke (RLRL)
  Double,            ///< Double stroke (RRLL)
  Triple,            ///< Triple stroke (RRR or LLL)

  // Rudiments
  Paradiddle,        ///< Paradiddle (RLRR LRLL)
  Flam,              ///< Flam (grace + main)
  Drag,              ///< Drag (2 grace + main)
  Ruff,              ///< Ruff (3 grace + main)

  // Rolls
  SingleStrokeRoll,  ///< Fast RLRL
  DoubleStrokeRoll,  ///< RRLLRRLL
  BuzzRoll,          ///< Press roll (no interval constraint)

  // Accents
  Accent,            ///< High velocity hit
  GhostNote,         ///< Low velocity hit
  RimShot,           ///< Snare head + rim
  CrossStick,        ///< Side stick

  // Special
  Choke,             ///< Cymbal grab
  Normal             ///< Standard hit
};

/// @brief Drum kit setup configuration.
///
/// Maps drum parts to limbs and defines flexibility.
struct DrumSetup {
  /// @brief Primary limb for each drum note.
  std::map<uint8_t, Limb> primary_limb;

  /// @brief Flexibility for each drum note.
  std::map<uint8_t, LimbFlexibility> flexibility;

  /// @brief Playing style (affects HH/snare assignment).
  DrumPlayStyle style = DrumPlayStyle::CrossStick;

  /// @brief Enable double bass pedal (both feet for kick).
  bool enable_double_bass = false;

  /// @brief Standard right-handed cross-stick setup.
  ///
  /// Traditional setup:
  /// - HH: Left hand
  /// - Snare: Right hand (crosses over)
  /// - Kick: Right foot
  /// - HH pedal: Left foot
  static DrumSetup crossStickRightHanded() {
    DrumSetup setup;
    setup.style = DrumPlayStyle::CrossStick;

    // Hi-hat: left hand (fixed in cross-stick)
    setup.primary_limb[drums::CHH] = Limb::LeftHand;
    setup.primary_limb[drums::OHH] = Limb::LeftHand;
    setup.primary_limb[drums::FHH] = Limb::LeftFoot;
    setup.flexibility[drums::CHH] = LimbFlexibility::Fixed;
    setup.flexibility[drums::OHH] = LimbFlexibility::Fixed;
    setup.flexibility[drums::FHH] = LimbFlexibility::Fixed;

    // Snare: right hand (can use left for ghosts)
    setup.primary_limb[drums::SD] = Limb::RightHand;
    setup.primary_limb[drums::SIDESTICK] = Limb::RightHand;
    setup.flexibility[drums::SD] = LimbFlexibility::Either;
    setup.flexibility[drums::SIDESTICK] = LimbFlexibility::Fixed;

    // Kick: right foot
    setup.primary_limb[drums::BD] = Limb::RightFoot;
    setup.flexibility[drums::BD] = LimbFlexibility::Fixed;

    // Ride: right hand
    setup.primary_limb[drums::RIDE] = Limb::RightHand;
    setup.flexibility[drums::RIDE] = LimbFlexibility::Fixed;

    // Crash: either hand
    setup.primary_limb[drums::CRASH] = Limb::RightHand;
    setup.flexibility[drums::CRASH] = LimbFlexibility::Either;

    // Toms: context-dependent
    setup.primary_limb[drums::TOM_H] = Limb::RightHand;
    setup.primary_limb[drums::TOM_M] = Limb::RightHand;
    setup.primary_limb[drums::TOM_L] = Limb::LeftHand;
    setup.flexibility[drums::TOM_H] = LimbFlexibility::Either;
    setup.flexibility[drums::TOM_M] = LimbFlexibility::Either;
    setup.flexibility[drums::TOM_L] = LimbFlexibility::Either;

    return setup;
  }

  /// @brief Open-hand right-handed setup.
  ///
  /// Modern setup:
  /// - HH: Right hand (no crossing)
  /// - Snare: Left hand
  /// - Kick: Right foot
  static DrumSetup openHandRightHanded() {
    DrumSetup setup;
    setup.style = DrumPlayStyle::OpenHand;

    // Hi-hat: right hand
    setup.primary_limb[drums::CHH] = Limb::RightHand;
    setup.primary_limb[drums::OHH] = Limb::RightHand;
    setup.primary_limb[drums::FHH] = Limb::LeftFoot;
    setup.flexibility[drums::CHH] = LimbFlexibility::Fixed;
    setup.flexibility[drums::OHH] = LimbFlexibility::Fixed;
    setup.flexibility[drums::FHH] = LimbFlexibility::Fixed;

    // Snare: left hand
    setup.primary_limb[drums::SD] = Limb::LeftHand;
    setup.primary_limb[drums::SIDESTICK] = Limb::LeftHand;
    setup.flexibility[drums::SD] = LimbFlexibility::Either;
    setup.flexibility[drums::SIDESTICK] = LimbFlexibility::Fixed;

    // Kick: right foot
    setup.primary_limb[drums::BD] = Limb::RightFoot;
    setup.flexibility[drums::BD] = LimbFlexibility::Fixed;

    // Ride: right hand
    setup.primary_limb[drums::RIDE] = Limb::RightHand;
    setup.flexibility[drums::RIDE] = LimbFlexibility::Fixed;

    // Crash: left hand preferred (closer in open hand)
    setup.primary_limb[drums::CRASH] = Limb::LeftHand;
    setup.flexibility[drums::CRASH] = LimbFlexibility::Either;

    // Toms: context-dependent
    setup.primary_limb[drums::TOM_H] = Limb::RightHand;
    setup.primary_limb[drums::TOM_M] = Limb::LeftHand;
    setup.primary_limb[drums::TOM_L] = Limb::LeftHand;
    setup.flexibility[drums::TOM_H] = LimbFlexibility::Either;
    setup.flexibility[drums::TOM_M] = LimbFlexibility::Either;
    setup.flexibility[drums::TOM_L] = LimbFlexibility::Either;

    return setup;
  }

  /// @brief Enable double bass (both feet for kick).
  void enableDoubleBass() {
    enable_double_bass = true;
    flexibility[drums::BD] = LimbFlexibility::Alternating;
  }

  /// @brief Get limb for a drum note with context.
  /// @param note GM drum note number
  /// @param context Previous limb used (for alternation)
  Limb getLimbFor(uint8_t note, std::optional<Limb> context = std::nullopt) const {
    auto it = primary_limb.find(note);
    if (it == primary_limb.end()) return Limb::RightHand;

    auto flex_it = flexibility.find(note);
    if (flex_it != flexibility.end()) {
      if (flex_it->second == LimbFlexibility::Either && context) {
        // Alternate for variety
        if (*context == Limb::RightHand) return Limb::LeftHand;
        if (*context == Limb::LeftHand) return Limb::RightHand;
      }
      if (flex_it->second == LimbFlexibility::Alternating && context) {
        // Force alternation
        if (*context == Limb::RightFoot) return Limb::LeftFoot;
        if (*context == Limb::LeftFoot) return Limb::RightFoot;
        if (*context == Limb::RightHand) return Limb::LeftHand;
        if (*context == Limb::LeftHand) return Limb::RightHand;
      }
    }

    return it->second;
  }

  /// @brief Check if two notes can be hit simultaneously.
  bool canSimultaneous(uint8_t note1, uint8_t note2) const {
    if (note1 == note2) return false;  // Same instrument can't hit twice

    Limb limb1 = getLimbFor(note1);
    Limb limb2 = getLimbFor(note2);

    if (limb1 != limb2) return true;  // Different limbs OK

    // Same primary limb - check flexibility
    auto flex1 = flexibility.find(note1);
    auto flex2 = flexibility.find(note2);
    bool either1 = (flex1 != flexibility.end() && flex1->second == LimbFlexibility::Either);
    bool either2 = (flex2 != flexibility.end() && flex2->second == LimbFlexibility::Either);

    return either1 || either2;  // One can use alternate limb
  }
};

/// @brief Drum state during performance.
struct DrumState : public PerformerState {
  std::array<Tick, kLimbCount> last_hit_tick = {0, 0, 0, 0};
  std::array<float, kLimbCount> limb_fatigue = {0, 0, 0, 0};
  uint8_t last_sticking = 0;  ///< 0 = Right, 1 = Left

  void reset() override {
    PerformerState::reset();
    last_hit_tick.fill(0);
    limb_fatigue.fill(0);
    last_sticking = 0;
  }
};

}  // namespace midisketch

#endif  // MIDISKETCH_INSTRUMENT_DRUMS_DRUM_TYPES_H
