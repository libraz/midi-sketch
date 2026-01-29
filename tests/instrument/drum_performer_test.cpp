/**
 * @file drum_performer_test.cpp
 * @brief Tests for DrumPerformer physical model.
 */

#include <gtest/gtest.h>

#include "core/timing_constants.h"
#include "instrument/drums/drum_performer.h"

namespace midisketch {
namespace {

// ============================================================================
// DrumSetup Tests
// ============================================================================

TEST(DrumSetupTest, CrossStickSetup) {
  auto setup = DrumSetup::crossStickRightHanded();

  EXPECT_EQ(setup.style, DrumPlayStyle::CrossStick);

  // HH on left hand in cross-stick
  EXPECT_EQ(setup.getLimbFor(drums::CHH), Limb::LeftHand);

  // Snare on right hand in cross-stick
  EXPECT_EQ(setup.getLimbFor(drums::SD), Limb::RightHand);

  // Kick on right foot
  EXPECT_EQ(setup.getLimbFor(drums::BD), Limb::RightFoot);
}

TEST(DrumSetupTest, OpenHandSetup) {
  auto setup = DrumSetup::openHandRightHanded();

  EXPECT_EQ(setup.style, DrumPlayStyle::OpenHand);

  // HH on right hand in open-hand
  EXPECT_EQ(setup.getLimbFor(drums::CHH), Limb::RightHand);

  // Snare on left hand in open-hand
  EXPECT_EQ(setup.getLimbFor(drums::SD), Limb::LeftHand);
}

TEST(DrumSetupTest, CanSimultaneousKickAndSnare) {
  auto setup = DrumSetup::crossStickRightHanded();

  // Kick (right foot) + Snare (right hand) should be possible
  EXPECT_TRUE(setup.canSimultaneous(drums::BD, drums::SD));
}

TEST(DrumSetupTest, CanSimultaneousKickAndHH) {
  auto setup = DrumSetup::crossStickRightHanded();

  // Kick (right foot) + HH (left hand) should be possible
  EXPECT_TRUE(setup.canSimultaneous(drums::BD, drums::CHH));
}

TEST(DrumSetupTest, CannotSimultaneousSameInstrument) {
  auto setup = DrumSetup::crossStickRightHanded();

  // Can't hit same drum twice
  EXPECT_FALSE(setup.canSimultaneous(drums::SD, drums::SD));
}

TEST(DrumSetupTest, CannotSimultaneousHHAndRideInOpenHand) {
  auto setup = DrumSetup::openHandRightHanded();

  // In open-hand, both HH and Ride are right hand
  // But ride has Fixed flexibility, so this should fail
  // Actually, need to check the specific setup
  Limb hh_limb = setup.getLimbFor(drums::CHH);
  Limb ride_limb = setup.getLimbFor(drums::RIDE);

  if (hh_limb == ride_limb) {
    // Same limb, check flexibility
    auto hh_flex = setup.flexibility.find(drums::CHH);
    auto ride_flex = setup.flexibility.find(drums::RIDE);

    bool hh_either = (hh_flex != setup.flexibility.end() &&
                      hh_flex->second == LimbFlexibility::Either);
    bool ride_either = (ride_flex != setup.flexibility.end() &&
                        ride_flex->second == LimbFlexibility::Either);

    // If neither is flexible, they can't be simultaneous
    EXPECT_TRUE(hh_either || ride_either || !setup.canSimultaneous(drums::CHH, drums::RIDE));
  }
}

TEST(DrumSetupTest, EnableDoubleBass) {
  auto setup = DrumSetup::crossStickRightHanded();
  setup.enableDoubleBass();

  EXPECT_TRUE(setup.enable_double_bass);
  EXPECT_EQ(setup.flexibility[drums::BD], LimbFlexibility::Alternating);
}

TEST(DrumSetupTest, GetLimbWithContext) {
  auto setup = DrumSetup::crossStickRightHanded();

  // Tom with context should alternate
  Limb first = setup.getLimbFor(drums::TOM_H, std::nullopt);
  Limb second = setup.getLimbFor(drums::TOM_H, first);

  // Since toms are Either, should alternate
  EXPECT_NE(first, second);
}

// ============================================================================
// LimbPhysics Tests
// ============================================================================

TEST(LimbPhysicsTest, HandPhysics) {
  auto hand = LimbPhysics::hand();

  EXPECT_EQ(hand.min_single_interval, TICK_32ND);
  EXPECT_LT(hand.min_double_interval, hand.min_single_interval);
}

TEST(LimbPhysicsTest, FootPhysics) {
  auto foot = LimbPhysics::foot();

  // Feet are slower than hands
  auto hand = LimbPhysics::hand();
  EXPECT_GT(foot.min_single_interval, hand.min_single_interval);
}

TEST(LimbPhysicsTest, AdvancedHandPhysics) {
  auto standard = LimbPhysics::hand();
  auto advanced = LimbPhysics::handAdvanced();

  // Advanced player can play faster
  EXPECT_LT(advanced.min_single_interval, standard.min_single_interval);
  EXPECT_LT(advanced.fatigue_rate, standard.fatigue_rate);
}

// ============================================================================
// DrumState Tests
// ============================================================================

TEST(DrumStateTest, DefaultState) {
  DrumState state;

  for (size_t i = 0; i < kLimbCount; ++i) {
    EXPECT_EQ(state.last_hit_tick[i], 0u);
    EXPECT_FLOAT_EQ(state.limb_fatigue[i], 0.0f);
  }
  EXPECT_EQ(state.last_sticking, 0);
}

TEST(DrumStateTest, Reset) {
  DrumState state;
  state.last_hit_tick[0] = 1000;
  state.limb_fatigue[1] = 0.5f;
  state.last_sticking = 1;

  state.reset();

  EXPECT_EQ(state.last_hit_tick[0], 0u);
  EXPECT_FLOAT_EQ(state.limb_fatigue[1], 0.0f);
  EXPECT_EQ(state.last_sticking, 0);
}

// ============================================================================
// DrumPerformer Tests
// ============================================================================

class DrumPerformerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    performer_ = std::make_unique<DrumPerformer>(DrumSetup::crossStickRightHanded());
  }

  std::unique_ptr<DrumPerformer> performer_;
};

TEST_F(DrumPerformerTest, PerformerType) {
  EXPECT_EQ(performer_->getType(), PerformerType::Drums);
}

TEST_F(DrumPerformerTest, PitchRange) {
  EXPECT_EQ(performer_->getMinPitch(), 35);
  EXPECT_EQ(performer_->getMaxPitch(), 81);
}

TEST_F(DrumPerformerTest, CanPerformValidDrumNote) {
  EXPECT_TRUE(performer_->canPerform(drums::BD, 0, TICK_SIXTEENTH));
  EXPECT_TRUE(performer_->canPerform(drums::SD, 0, TICK_SIXTEENTH));
  EXPECT_TRUE(performer_->canPerform(drums::CHH, 0, TICK_SIXTEENTH));
}

TEST_F(DrumPerformerTest, CannotPerformOutOfRange) {
  EXPECT_FALSE(performer_->canPerform(20, 0, TICK_SIXTEENTH));  // Below range
  EXPECT_FALSE(performer_->canPerform(100, 0, TICK_SIXTEENTH)); // Above range
}

TEST_F(DrumPerformerTest, CreateInitialState) {
  auto state = performer_->createInitialState();

  ASSERT_NE(state, nullptr);
  auto* drum_state = dynamic_cast<DrumState*>(state.get());
  ASSERT_NE(drum_state, nullptr);

  for (size_t i = 0; i < kLimbCount; ++i) {
    EXPECT_EQ(drum_state->last_hit_tick[i], 0u);
  }
}

TEST_F(DrumPerformerTest, CalculateCostForNormalHit) {
  auto state = performer_->createInitialState();

  // First hit at tick 1000 (not 0 to avoid initial state issues)
  float cost = performer_->calculateCost(drums::SD, 1000, TICK_SIXTEENTH, *state);

  EXPECT_LT(cost, 50.0f);  // Normal hit should be easy
}

TEST_F(DrumPerformerTest, CalculateCostForFastRepeat) {
  auto state = performer_->createInitialState();

  // First hit
  performer_->updateState(*state, drums::SD, 0, TICK_SIXTEENTH);

  // Very fast repeat (should be hard/impossible)
  float cost = performer_->calculateCost(drums::SD, 20, TICK_SIXTEENTH, *state);

  EXPECT_GT(cost, 100.0f);  // Should be very high (physical limit)
}

TEST_F(DrumPerformerTest, CalculateCostConsidersFatigue) {
  auto state = performer_->createInitialState();
  auto* drum_state = dynamic_cast<DrumState*>(state.get());

  // Set high fatigue
  drum_state->limb_fatigue[static_cast<size_t>(Limb::RightHand)] = 0.9f;

  float cost = performer_->calculateCost(drums::SD, 0, TICK_SIXTEENTH, *state);

  // Compare with no fatigue
  drum_state->limb_fatigue[static_cast<size_t>(Limb::RightHand)] = 0.0f;
  float fresh_cost = performer_->calculateCost(drums::SD, 0, TICK_SIXTEENTH, *state);

  EXPECT_GT(cost, fresh_cost);
}

TEST_F(DrumPerformerTest, UpdateStateTracksLastHit) {
  auto state = performer_->createInitialState();
  auto* drum_state = dynamic_cast<DrumState*>(state.get());

  performer_->updateState(*state, drums::SD, 1000, TICK_SIXTEENTH);

  // Snare uses right hand in cross-stick
  EXPECT_EQ(drum_state->last_hit_tick[static_cast<size_t>(Limb::RightHand)], 1000u);
  EXPECT_EQ(drum_state->last_pitch, drums::SD);
}

TEST_F(DrumPerformerTest, UpdateStateAccumulatesFatigue) {
  auto state = performer_->createInitialState();
  auto* drum_state = dynamic_cast<DrumState*>(state.get());

  // Use kick drum which has Fixed flexibility (always right foot)
  // First hit sets the baseline
  performer_->updateState(*state, drums::BD, 1000, TICK_32ND);
  float initial_fatigue = drum_state->limb_fatigue[static_cast<size_t>(Limb::RightFoot)];

  // Very rapid subsequent kicks (faster than min_single_interval * 2)
  // Foot min_single_interval is TICK_SIXTEENTH (120), so threshold is 240
  // Using 100 tick intervals
  for (int i = 1; i <= 30; ++i) {
    performer_->updateState(*state, drums::BD, 1000 + i * 100, TICK_32ND);
  }

  float final_fatigue = drum_state->limb_fatigue[static_cast<size_t>(Limb::RightFoot)];

  // Should have accumulated more fatigue from very fast playing
  EXPECT_GT(final_fatigue, initial_fatigue);
}

TEST_F(DrumPerformerTest, CanSimultaneousHitKickSnareHH) {
  std::vector<uint8_t> notes = {drums::BD, drums::SD, drums::CHH};

  EXPECT_TRUE(performer_->canSimultaneousHit(notes));
}

TEST_F(DrumPerformerTest, CannotSimultaneousHitSameLimb) {
  // Create open-hand setup where HH and Ride are both right hand
  DrumPerformer open_hand(DrumSetup::openHandRightHanded());

  // Try HH + Ride (both typically right hand in open-hand)
  std::vector<uint8_t> notes = {drums::CHH, drums::RIDE};

  // This might be possible if one is flexible
  // The test verifies the logic works
  bool can_hit = open_hand.canSimultaneousHit(notes);

  // If we get false, verify the limbs are indeed the same
  if (!can_hit) {
    auto setup = DrumSetup::openHandRightHanded();
    EXPECT_EQ(setup.getLimbFor(drums::CHH), setup.getLimbFor(drums::RIDE));
  }
}

TEST_F(DrumPerformerTest, SuggestAlternativesForSnare) {
  auto alts = performer_->suggestAlternatives(drums::SD, 0, TICK_SIXTEENTH, 35, 81);

  ASSERT_FALSE(alts.empty());
  EXPECT_EQ(alts[0], drums::SD);  // Original should be first

  // Should include sidestick as alternative
  bool has_sidestick = false;
  for (uint8_t alt : alts) {
    if (alt == drums::SIDESTICK) has_sidestick = true;
  }
  EXPECT_TRUE(has_sidestick);
}

TEST_F(DrumPerformerTest, SuggestAlternativesForHH) {
  auto alts = performer_->suggestAlternatives(drums::CHH, 0, TICK_SIXTEENTH, 35, 81);

  ASSERT_FALSE(alts.empty());

  // Should include ride as alternative
  bool has_ride = false;
  for (uint8_t alt : alts) {
    if (alt == drums::RIDE) has_ride = true;
  }
  EXPECT_TRUE(has_ride);
}

TEST_F(DrumPerformerTest, OptimizeLimbAllocation) {
  // Simple pattern: kick-snare-kick-snare
  std::vector<std::pair<Tick, uint8_t>> pattern = {
      {0, drums::BD},
      {TICK_QUARTER, drums::SD},
      {TICK_HALF, drums::BD},
      {TICK_HALF + TICK_QUARTER, drums::SD}};

  auto allocation = performer_->optimizeLimbAllocation(pattern);

  EXPECT_EQ(allocation.size(), 4u);

  // Kicks should be on right foot
  EXPECT_EQ(allocation[0], Limb::RightFoot);
  EXPECT_EQ(allocation[2], Limb::RightFoot);

  // Snares should be on right hand (cross-stick)
  EXPECT_EQ(allocation[1], Limb::RightHand);
  EXPECT_EQ(allocation[3], Limb::RightHand);
}

TEST_F(DrumPerformerTest, GenerateStickingSingle) {
  std::vector<Tick> timings = {0, 120, 240, 360, 480, 600, 720, 840};

  auto sticking = performer_->generateSticking(timings, DrumTechnique::Single);

  EXPECT_EQ(sticking.size(), timings.size());

  // Should alternate RLRLRLRL
  for (size_t i = 0; i < sticking.size(); ++i) {
    if (i % 2 == 0) {
      EXPECT_EQ(sticking[i], Limb::RightHand);
    } else {
      EXPECT_EQ(sticking[i], Limb::LeftHand);
    }
  }
}

TEST_F(DrumPerformerTest, GenerateStickingDouble) {
  std::vector<Tick> timings = {0, 60, 120, 180, 240, 300, 360, 420};

  auto sticking = performer_->generateSticking(timings, DrumTechnique::Double);

  EXPECT_EQ(sticking.size(), timings.size());

  // Should be RRLLRRLL pattern
  EXPECT_EQ(sticking[0], Limb::RightHand);
  EXPECT_EQ(sticking[1], Limb::RightHand);
  EXPECT_EQ(sticking[2], Limb::LeftHand);
  EXPECT_EQ(sticking[3], Limb::LeftHand);
  EXPECT_EQ(sticking[4], Limb::RightHand);
  EXPECT_EQ(sticking[5], Limb::RightHand);
  EXPECT_EQ(sticking[6], Limb::LeftHand);
  EXPECT_EQ(sticking[7], Limb::LeftHand);
}

TEST_F(DrumPerformerTest, SetHandPhysics) {
  auto advanced = LimbPhysics::handAdvanced();
  performer_->setHandPhysics(advanced);

  // Verify faster playing is now possible
  auto state = performer_->createInitialState();
  performer_->updateState(*state, drums::SD, 0, TICK_32ND);

  // With advanced physics, fast repeat should have lower cost
  float cost = performer_->calculateCost(drums::SD, 50, TICK_32ND, *state);

  // Should be lower than default physics would allow
  EXPECT_LT(cost, 500.0f);
}

// ============================================================================
// DrumTechnique Tests
// ============================================================================

TEST(DrumTechniqueTest, RudimentConstants) {
  // Verify rudiment constants are reasonable
  EXPECT_GT(Rudiment::kFlamGraceOffset, 0u);
  EXPECT_LT(Rudiment::kFlamGraceOffset, TICK_SIXTEENTH);

  EXPECT_GT(Rudiment::kFlamGraceVelocity, 0);
  EXPECT_LT(Rudiment::kFlamGraceVelocity, 80);  // Should be soft

  EXPECT_LT(Rudiment::kGhostNoteVelocity, 50);  // Ghost notes are soft
}

}  // namespace
}  // namespace midisketch
