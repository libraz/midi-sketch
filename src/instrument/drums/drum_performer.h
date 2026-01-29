/**
 * @file drum_performer.h
 * @brief Drum physical performer model.
 *
 * Models the physical constraints of human drumming:
 * - Limb allocation (hands/feet)
 * - Simultaneous hit constraints
 * - Stroke speed limits
 * - Fatigue accumulation
 */

#ifndef MIDISKETCH_INSTRUMENT_DRUMS_DRUM_PERFORMER_H
#define MIDISKETCH_INSTRUMENT_DRUMS_DRUM_PERFORMER_H

#include <map>
#include <memory>
#include <set>
#include <vector>

#include "core/basic_types.h"
#include "instrument/common/physical_performer.h"
#include "instrument/drums/drum_types.h"

namespace midisketch {

/// @brief Physical performer model for drums.
///
/// Models human drumming constraints:
/// - Four limbs: 2 hands, 2 feet
/// - Simultaneous hit limits (one hit per limb)
/// - Stroke speed limits per limb
/// - Movement time between drums
/// - Fatigue from fast playing
class DrumPerformer : public IPhysicalPerformer {
 public:
  /// @brief Construct with drum setup.
  /// @param setup Drum kit configuration
  explicit DrumPerformer(const DrumSetup& setup = DrumSetup::crossStickRightHanded());

  // IPhysicalPerformer implementation
  PerformerType getType() const override { return PerformerType::Drums; }

  bool canPerform(uint8_t pitch, Tick start, Tick duration) const override;

  float calculateCost(uint8_t pitch, Tick start, Tick duration,
                      const PerformerState& state) const override;

  std::vector<uint8_t> suggestAlternatives(uint8_t desired_pitch, Tick start, Tick duration,
                                           uint8_t range_low, uint8_t range_high) const override;

  void updateState(PerformerState& state, uint8_t pitch, Tick start,
                   Tick duration) const override;

  std::unique_ptr<PerformerState> createInitialState() const override;

  uint8_t getMinPitch() const override { return 35; }  // GM drum range start
  uint8_t getMaxPitch() const override { return 81; }  // GM drum range end

  // Drum-specific methods

  /// @brief Check if multiple notes can be hit simultaneously.
  /// @param notes Vector of GM drum notes
  /// @return true if all notes can be hit at once
  bool canSimultaneousHit(const std::vector<uint8_t>& notes) const;

  /// @brief Optimize limb allocation for a pattern.
  /// @param pattern Sequence of (tick, note) pairs
  /// @return Map from pattern index to assigned limb
  std::map<size_t, Limb> optimizeLimbAllocation(
      const std::vector<std::pair<Tick, uint8_t>>& pattern) const;

  /// @brief Generate sticking pattern for consecutive hits.
  /// @param timings Hit timings
  /// @param technique Technique being used
  /// @return Vector of limbs (R/L pattern)
  std::vector<Limb> generateSticking(const std::vector<Tick>& timings,
                                     DrumTechnique technique) const;

  /// @brief Get the drum setup.
  const DrumSetup& getSetup() const { return setup_; }

  /// @brief Set limb physics for hands.
  void setHandPhysics(const LimbPhysics& physics) { hand_physics_ = physics; }

  /// @brief Set limb physics for feet.
  void setFootPhysics(const LimbPhysics& physics) { foot_physics_ = physics; }

 private:
  /// @brief Get physics for a specific limb.
  const LimbPhysics& getPhysicsFor(Limb limb) const;

  /// @brief Check if note is a foot instrument.
  bool isFootInstrument(uint8_t note) const;

  DrumSetup setup_;
  LimbPhysics hand_physics_ = LimbPhysics::hand();
  LimbPhysics foot_physics_ = LimbPhysics::foot();
};

}  // namespace midisketch

#endif  // MIDISKETCH_INSTRUMENT_DRUMS_DRUM_PERFORMER_H
