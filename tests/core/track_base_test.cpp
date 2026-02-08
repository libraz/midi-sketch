/**
 * @file track_base_test.cpp
 * @brief Unit tests for TrackBase utilities (trackRoleToMask, shouldSkipSection).
 */

#include <gtest/gtest.h>

#include "core/section_types.h"
#include "core/track_base.h"

namespace midisketch {
namespace {

// ============================================================================
// trackRoleToMask Tests
// ============================================================================

TEST(TrackRoleToMaskTest, AllRolesMapCorrectly) {
  EXPECT_EQ(trackRoleToMask(TrackRole::Vocal), TrackMask::Vocal);
  EXPECT_EQ(trackRoleToMask(TrackRole::Chord), TrackMask::Chord);
  EXPECT_EQ(trackRoleToMask(TrackRole::Bass), TrackMask::Bass);
  EXPECT_EQ(trackRoleToMask(TrackRole::Drums), TrackMask::Drums);
  EXPECT_EQ(trackRoleToMask(TrackRole::SE), TrackMask::SE);
  EXPECT_EQ(trackRoleToMask(TrackRole::Motif), TrackMask::Motif);
  EXPECT_EQ(trackRoleToMask(TrackRole::Arpeggio), TrackMask::Arpeggio);
  EXPECT_EQ(trackRoleToMask(TrackRole::Aux), TrackMask::Aux);
  EXPECT_EQ(trackRoleToMask(TrackRole::Guitar), TrackMask::Guitar);
}

TEST(TrackRoleToMaskTest, MasksAreSingleBits) {
  // Each mask should be a power of 2
  for (size_t idx = 0; idx < kTrackCount; ++idx) {
    auto role = static_cast<TrackRole>(idx);
    auto mask = static_cast<uint16_t>(trackRoleToMask(role));
    // A power of 2 has exactly one bit set
    EXPECT_NE(mask, 0u) << "Role " << idx << " maps to None";
    EXPECT_EQ(mask & (mask - 1), 0u) << "Role " << idx << " is not a single bit";
  }
}

TEST(TrackRoleToMaskTest, RoundTripWithHasTrack) {
  // Verify that hasTrack(TrackMask::All, trackRoleToMask(role)) is true for all roles
  for (size_t idx = 0; idx < kTrackCount; ++idx) {
    auto role = static_cast<TrackRole>(idx);
    EXPECT_TRUE(hasTrack(TrackMask::All, trackRoleToMask(role)))
        << "Role " << idx << " not found in TrackMask::All";
  }
}

TEST(TrackRoleToMaskTest, AllMasksCoverAllBits) {
  // Combined mask of all roles should equal TrackMask::All
  uint16_t combined = 0;
  for (size_t idx = 0; idx < kTrackCount; ++idx) {
    auto role = static_cast<TrackRole>(idx);
    combined |= static_cast<uint16_t>(trackRoleToMask(role));
  }
  EXPECT_EQ(combined, static_cast<uint16_t>(TrackMask::All));
}

// ============================================================================
// shouldSkipSection Tests (via concrete generator)
// ============================================================================

/// @brief Minimal concrete TrackBase subclass for testing shouldSkipSection.
///
/// Exposes the protected shouldSkipSection as public for testing.
class TestableTrack : public TrackBase {
 public:
  explicit TestableTrack(TrackRole role) : role_(role) {}
  TrackRole getRole() const override { return role_; }
  TrackPriority getDefaultPriority() const override { return TrackPriority::Medium; }
  PhysicalModel getPhysicalModel() const override { return PhysicalModels::kVocal; }

  // Expose protected method for testing
  using TrackBase::shouldSkipSection;

 protected:
  void doGenerateFullTrack(MidiTrack&, const FullTrackContext&) override {}

 private:
  TrackRole role_;
};

TEST(ShouldSkipSectionTest, SkipsWhenTrackNotInMask) {
  TestableTrack vocal_gen(TrackRole::Vocal);

  Section section;
  section.type = SectionType::A;
  section.track_mask = TrackMask::Bass | TrackMask::Drums;  // No Vocal

  EXPECT_TRUE(vocal_gen.shouldSkipSection(section));
}

TEST(ShouldSkipSectionTest, DoesNotSkipWhenTrackInMask) {
  TestableTrack bass_gen(TrackRole::Bass);

  Section section;
  section.type = SectionType::A;
  section.track_mask = TrackMask::Basic;  // Includes Bass

  EXPECT_FALSE(bass_gen.shouldSkipSection(section));
}

TEST(ShouldSkipSectionTest, AllMaskNeverSkips) {
  Section section;
  section.type = SectionType::Chorus;
  section.track_mask = TrackMask::All;

  for (size_t idx = 0; idx < kTrackCount; ++idx) {
    auto role = static_cast<TrackRole>(idx);
    TestableTrack gen(role);
    EXPECT_FALSE(gen.shouldSkipSection(section))
        << "Role " << idx << " should not be skipped when mask is All";
  }
}

TEST(ShouldSkipSectionTest, NoneMaskAlwaysSkips) {
  Section section;
  section.type = SectionType::Interlude;
  section.track_mask = TrackMask::None;

  for (size_t idx = 0; idx < kTrackCount; ++idx) {
    auto role = static_cast<TrackRole>(idx);
    TestableTrack gen(role);
    EXPECT_TRUE(gen.shouldSkipSection(section))
        << "Role " << idx << " should be skipped when mask is None";
  }
}

TEST(ShouldSkipSectionTest, MinimalMaskOnlyDrums) {
  Section section;
  section.type = SectionType::Intro;
  section.track_mask = TrackMask::Minimal;  // Drums only

  TestableTrack drums_gen(TrackRole::Drums);
  EXPECT_FALSE(drums_gen.shouldSkipSection(section));

  TestableTrack vocal_gen(TrackRole::Vocal);
  EXPECT_TRUE(vocal_gen.shouldSkipSection(section));

  TestableTrack bass_gen(TrackRole::Bass);
  EXPECT_TRUE(bass_gen.shouldSkipSection(section));
}

}  // namespace
}  // namespace midisketch
