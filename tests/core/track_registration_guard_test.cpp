/**
 * @file track_registration_guard_test.cpp
 * @brief Tests for TrackRegistrationGuard RAII class.
 */

#include "core/track_registration_guard.h"

#include <gtest/gtest.h>

#include "core/midi_track.h"
#include "test_support/stub_harmony_context.h"

namespace midisketch {
namespace {

class TrackRegistrationGuardTest : public ::testing::Test {
 protected:
  test::StubHarmonyContext stub_;
  MidiTrack track_;
};

TEST_F(TrackRegistrationGuardTest, RegistersOnDestruction) {
  ASSERT_EQ(stub_.getRegisteredTrackCount(), 0);

  {
    TrackRegistrationGuard guard(stub_, track_, TrackRole::Vocal);
    track_.addNote(0, 480, 60, 100);
    // Guard goes out of scope here
  }

  EXPECT_EQ(stub_.getRegisteredTrackCount(), 1);
}

TEST_F(TrackRegistrationGuardTest, CancelPreventsRegistration) {
  ASSERT_EQ(stub_.getRegisteredTrackCount(), 0);

  {
    TrackRegistrationGuard guard(stub_, track_, TrackRole::Bass);
    track_.addNote(0, 480, 36, 100);
    guard.cancel();
    // Guard goes out of scope but was cancelled
  }

  EXPECT_EQ(stub_.getRegisteredTrackCount(), 0);
}

TEST_F(TrackRegistrationGuardTest, RegisterNowPreventsDoubleRegistration) {
  ASSERT_EQ(stub_.getRegisteredTrackCount(), 0);

  {
    TrackRegistrationGuard guard(stub_, track_, TrackRole::Chord);
    track_.addNote(0, 480, 60, 100);
    guard.registerNow();  // Explicit registration
    // Guard goes out of scope but won't register again
  }

  EXPECT_EQ(stub_.getRegisteredTrackCount(), 1);  // Only once
}

TEST_F(TrackRegistrationGuardTest, MoveConstructorTransfersOwnership) {
  ASSERT_EQ(stub_.getRegisteredTrackCount(), 0);

  {
    TrackRegistrationGuard guard1(stub_, track_, TrackRole::Aux);
    track_.addNote(0, 480, 72, 100);
    TrackRegistrationGuard guard2(std::move(guard1));
    // guard1 is now invalid, guard2 owns the registration
  }

  EXPECT_EQ(stub_.getRegisteredTrackCount(), 1);  // Only one registration
}

TEST_F(TrackRegistrationGuardTest, MoveAssignmentTransfersOwnership) {
  MidiTrack track2;
  track2.addNote(0, 480, 48, 100);

  ASSERT_EQ(stub_.getRegisteredTrackCount(), 0);

  {
    TrackRegistrationGuard guard1(stub_, track_, TrackRole::Vocal);
    track_.addNote(0, 480, 60, 100);

    TrackRegistrationGuard guard2(stub_, track2, TrackRole::Bass);

    guard2 = std::move(guard1);
    // guard2 registered track2 (Bass) before taking ownership of track_ (Vocal)
    // guard1 is now invalid
  }

  EXPECT_EQ(stub_.getRegisteredTrackCount(), 2);  // Both tracks registered
}

TEST_F(TrackRegistrationGuardTest, EmptyTrackCanBeRegistered) {
  ASSERT_EQ(stub_.getRegisteredTrackCount(), 0);
  ASSERT_TRUE(track_.empty());

  {
    TrackRegistrationGuard guard(stub_, track_, TrackRole::Motif);
    // Don't add any notes - track stays empty
  }

  EXPECT_EQ(stub_.getRegisteredTrackCount(), 1);
}

}  // namespace
}  // namespace midisketch
