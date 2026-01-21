/**
 * @file phrase_ending_test.cpp
 * @brief Tests for phrase ending behavior in vocal melody generation.
 *
 * Verifies that phrase endings follow pop music conventions:
 * 1. Final notes land on strong beats (integer beat positions)
 * 2. Final notes are at least a quarter note in duration
 * 3. No notes start on fractional beats like 4.82 at phrase end
 */

#include <gtest/gtest.h>

#include <cmath>
#include <random>

#include "core/melody_templates.h"
#include "core/melody_types.h"
#include "core/timing_constants.h"
#include "track/melody_designer.h"

namespace midisketch {
namespace {

// ============================================================================
// Test: Phrase rhythm generation ends on strong beats
// ============================================================================

TEST(PhraseEndingTest, FinalNoteOnStrongBeat) {
  MelodyDesigner designer;

  // Test across multiple seeds
  for (uint32_t seed = 1; seed <= 20; ++seed) {
    std::mt19937 rng(seed);

    // Test various phrase lengths
    for (uint8_t phrase_beats : {4, 8}) {
      const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::SparseAnchor);

      // Generate rhythm
      auto rhythm = designer.generatePhraseRhythm(tmpl, phrase_beats, 1.0f, 0.0f, rng);

      ASSERT_GT(rhythm.size(), 0u)
          << "Seed " << seed << ", " << static_cast<int>(phrase_beats) << " beats";

      // Check final note position
      const auto& final_note = rhythm.back();
      float final_beat = final_note.beat;

      // Final beat should be on an integer position (strong beat)
      float fractional = final_beat - std::floor(final_beat);
      EXPECT_LT(fractional, 0.01f)
          << "Seed " << seed << ", " << static_cast<int>(phrase_beats) << " beats: "
          << "Final note at beat " << final_beat << " should be on integer beat";
    }
  }
}

// ============================================================================
// Test: Final note has minimum quarter note duration
// ============================================================================

TEST(PhraseEndingTest, FinalNoteMinimumQuarterNote) {
  MelodyDesigner designer;

  for (uint32_t seed = 1; seed <= 20; ++seed) {
    std::mt19937 rng(seed);

    for (uint8_t phrase_beats : {4, 8}) {
      const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::PlateauTalk);

      auto rhythm = designer.generatePhraseRhythm(tmpl, phrase_beats, 1.0f, 0.0f, rng);

      ASSERT_GT(rhythm.size(), 0u);

      const auto& final_note = rhythm.back();

      // Final note should be at least 2 eighths (quarter note)
      EXPECT_GE(final_note.eighths, 2)
          << "Seed " << seed << ", " << static_cast<int>(phrase_beats) << " beats: "
          << "Final note eighths=" << final_note.eighths << " should be >= 2";
    }
  }
}

// ============================================================================
// Test: No fractional beat positions at phrase end
// ============================================================================

TEST(PhraseEndingTest, NoFractionalBeatAtPhraseEnd) {
  MelodyDesigner designer;

  for (uint32_t seed = 1; seed <= 20; ++seed) {
    std::mt19937 rng(seed);

    const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::RunUpTarget);

    // Use 4-beat phrase (typical for pop)
    auto rhythm = designer.generatePhraseRhythm(tmpl, 4, 1.0f, 0.0f, rng);

    ASSERT_GT(rhythm.size(), 0u);

    // Check that the last note doesn't start at positions like 3.82 or 4.82
    const auto& final_note = rhythm.back();
    float fractional = final_note.beat - std::floor(final_note.beat);

    // Fractional part should be very small (within 0.25 for eighth notes)
    // Positions like 0.5 (half beat) are acceptable, but not 0.82
    bool acceptable_position = (fractional < 0.01f) ||                 // On the beat
                               (std::abs(fractional - 0.5f) < 0.01f);  // Half beat

    EXPECT_TRUE(acceptable_position)
        << "Seed " << seed << ": Final note at beat " << final_note.beat
        << " has unacceptable fractional position " << fractional;
  }
}

// ============================================================================
// Test: Final note marked as strong beat
// ============================================================================

TEST(PhraseEndingTest, FinalNoteMarkedStrong) {
  MelodyDesigner designer;

  for (uint32_t seed = 1; seed <= 10; ++seed) {
    std::mt19937 rng(seed);

    const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::SparseAnchor);

    auto rhythm = designer.generatePhraseRhythm(tmpl, 4, 1.0f, 0.0f, rng);

    ASSERT_GT(rhythm.size(), 0u);

    const auto& final_note = rhythm.back();

    // Final note should be marked as strong for emphasis
    EXPECT_TRUE(final_note.strong)
        << "Seed " << seed << ": Final note should be marked as strong beat";
  }
}

// ============================================================================
// Test: Phrase body doesn't extend into final beat
// ============================================================================

TEST(PhraseEndingTest, FinalNoteStartsAfterPhraseBody) {
  MelodyDesigner designer;

  for (uint32_t seed = 1; seed <= 10; ++seed) {
    std::mt19937 rng(seed);

    const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::PlateauTalk);

    auto rhythm = designer.generatePhraseRhythm(tmpl, 4, 1.0f, 0.0f, rng);

    if (rhythm.size() >= 2) {
      // The final note should start at or after the second-to-last note ends
      // (no overlapping rhythm positions)
      const auto& second_last = rhythm[rhythm.size() - 2];
      const auto& final_note = rhythm.back();

      EXPECT_GE(final_note.beat, second_last.beat)
          << "Seed " << seed << ": Final note at beat " << final_note.beat
          << " should be after second-to-last at beat " << second_last.beat;
    }
  }
}

}  // namespace
}  // namespace midisketch
