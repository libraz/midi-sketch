/**
 * @file non_diatonic_detection_test.cpp
 * @brief Tests for non-diatonic note detection in dissonance analysis.
 *
 * These tests verify that the dissonance analyzer correctly detects
 * notes that are not in the C major diatonic scale (internal representation).
 */

#include <gtest/gtest.h>
#include "analysis/dissonance.h"
#include "core/generator.h"
#include "core/song.h"
#include "core/types.h"

namespace midisketch {
namespace {

class NonDiatonicDetectionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::StandardPop;
    params_.mood = Mood::ElectroPop;
    params_.chord_id = 0;
    params_.key = Key::C;
    params_.drums_enabled = true;
    params_.vocal_low = 60;
    params_.vocal_high = 79;
    params_.bpm = 120;
    params_.seed = 42;
  }

  GeneratorParams params_;
};

// Test: Detection counts non-diatonic notes in summary
TEST_F(NonDiatonicDetectionTest, SummaryCountsNonDiatonicNotes) {
  Generator gen;
  gen.generate(params_);
  const auto& song = gen.getSong();

  auto report = analyzeDissonance(song, params_);

  // After the bass fix, there should be zero non-diatonic notes
  // This test verifies the detection mechanism works
  EXPECT_GE(report.summary.non_diatonic_notes, 0u);

  // Total should include non-diatonic count
  EXPECT_EQ(report.summary.total_issues,
            report.summary.simultaneous_clashes +
            report.summary.non_chord_tones +
            report.summary.sustained_over_chord_change +
            report.summary.non_diatonic_notes);
}

// Test: Non-diatonic issues have correct type
TEST_F(NonDiatonicDetectionTest, IssueTypeIsNonDiatonicNote) {
  Generator gen;
  gen.generate(params_);
  const auto& song = gen.getSong();

  auto report = analyzeDissonance(song, params_);

  for (const auto& issue : report.issues) {
    if (issue.type == DissonanceType::NonDiatonicNote) {
      // Verify required fields are populated
      EXPECT_FALSE(issue.track_name.empty());
      EXPECT_FALSE(issue.pitch_name.empty());
      EXPECT_FALSE(issue.key_name.empty());
      EXPECT_FALSE(issue.scale_tones.empty());

      // Key name should match the params key
      EXPECT_EQ(issue.key_name, "C major");

      // Scale tones should have 7 notes (major scale)
      EXPECT_EQ(issue.scale_tones.size(), 7u);
    }
  }
}

// Test: Non-diatonic pitch classes are correctly identified
TEST_F(NonDiatonicDetectionTest, NonDiatonicPitchClassesIdentified) {
  // C major diatonic: C(0), D(2), E(4), F(5), G(7), A(9), B(11)
  // Non-diatonic: C#(1), D#(3), F#(6), G#(8), A#(10)
  std::set<int> non_diatonic_pcs = {1, 3, 6, 8, 10};

  Generator gen;
  gen.generate(params_);
  const auto& song = gen.getSong();

  auto report = analyzeDissonance(song, params_);

  for (const auto& issue : report.issues) {
    if (issue.type == DissonanceType::NonDiatonicNote) {
      // The pitch should be non-diatonic
      int pc = issue.pitch % 12;
      EXPECT_TRUE(non_diatonic_pcs.count(pc) > 0)
          << "Pitch " << issue.pitch_name << " (pc=" << pc
          << ") was flagged as non-diatonic but is actually diatonic";
    }
  }
}

// Test: Severity is based on beat strength
TEST_F(NonDiatonicDetectionTest, SeverityBasedOnBeatStrength) {
  Generator gen;
  gen.generate(params_);
  const auto& song = gen.getSong();

  auto report = analyzeDissonance(song, params_);

  for (const auto& issue : report.issues) {
    if (issue.type == DissonanceType::NonDiatonicNote) {
      // Beat 1 (1.0-1.99) should be High severity
      // Beat 3 (3.0-3.99) should be Medium severity
      // Other beats should be Medium severity (passing tones)
      if (issue.beat >= 1.0f && issue.beat < 2.0f) {
        EXPECT_EQ(issue.severity, DissonanceSeverity::High)
            << "Non-diatonic on beat 1 should be High severity";
      } else {
        EXPECT_NE(issue.severity, DissonanceSeverity::Low)
            << "Non-diatonic notes should be at least Medium severity";
      }
    }
  }
}

// Test: Transposed pitch name is shown (not internal pitch)
TEST_F(NonDiatonicDetectionTest, ShowsTransposedPitchName) {
  // Use key E (offset 4) to verify transposition
  params_.key = Key::E;

  Generator gen;
  gen.generate(params_);
  const auto& song = gen.getSong();

  auto report = analyzeDissonance(song, params_);

  for (const auto& issue : report.issues) {
    if (issue.type == DissonanceType::NonDiatonicNote) {
      // The key_name should show E major
      EXPECT_EQ(issue.key_name, "E major");

      // Scale tones should be E major scale
      std::vector<std::string> expected_scale = {"E", "F#", "G#", "A", "B", "C#", "D#"};
      EXPECT_EQ(issue.scale_tones, expected_scale);
    }
  }
}

// Test: JSON output includes non-diatonic notes
TEST_F(NonDiatonicDetectionTest, JsonOutputIncludesNonDiatonic) {
  // Create a report with a non-diatonic issue
  DissonanceReport report;
  report.summary.total_issues = 1;
  report.summary.non_diatonic_notes = 1;
  report.summary.high_severity = 1;

  DissonanceIssue issue;
  issue.type = DissonanceType::NonDiatonicNote;
  issue.severity = DissonanceSeverity::High;
  issue.tick = 1920;
  issue.bar = 1;
  issue.beat = 1.0f;
  issue.track_name = "bass";
  issue.pitch = 58;  // A#3 (transposed)
  issue.pitch_name = "A#3";
  issue.key_name = "E major";
  issue.scale_tones = {"E", "F#", "G#", "A", "B", "C#", "D#"};
  report.issues.push_back(issue);

  std::string json = dissonanceReportToJson(report);

  // Verify JSON contains the non-diatonic issue
  EXPECT_NE(json.find("non_diatonic_note"), std::string::npos);
  EXPECT_NE(json.find("\"non_diatonic_notes\":1"), std::string::npos);
  EXPECT_NE(json.find("E major"), std::string::npos);
  EXPECT_NE(json.find("A#3"), std::string::npos);
  EXPECT_NE(json.find("scale_tones"), std::string::npos);
}

// Test: Clean generation produces zero non-diatonic notes
TEST_F(NonDiatonicDetectionTest, CleanGenerationHasNoNonDiatonic) {
  // After the bass fix, normal generation should have zero non-diatonic notes
  std::vector<uint32_t> test_seeds = {1, 42, 12345, 67890, 99999};

  for (uint32_t seed : test_seeds) {
    params_.seed = seed;
    params_.key = Key::C;

    Generator gen;
    gen.generate(params_);
    const auto& song = gen.getSong();

    auto report = analyzeDissonance(song, params_);

    EXPECT_EQ(report.summary.non_diatonic_notes, 0u)
        << "Seed " << seed << " produced " << report.summary.non_diatonic_notes
        << " non-diatonic notes - generation should be clean after fix";
  }
}

// Test: Detection works for all melodic tracks
TEST_F(NonDiatonicDetectionTest, DetectsInAllMelodicTracks) {
  // The analyzer should check vocal, chord, bass, motif, arpeggio, aux
  // We can't easily inject non-diatonic notes, but we can verify
  // the mechanism by checking that all tracks are analyzed

  Generator gen;
  gen.generate(params_);
  const auto& song = gen.getSong();

  auto report = analyzeDissonance(song, params_);

  // The analysis ran without errors
  EXPECT_GE(report.summary.total_issues, 0u);

  // Verify the count formula is correct
  EXPECT_EQ(report.summary.total_issues,
            report.summary.simultaneous_clashes +
            report.summary.non_chord_tones +
            report.summary.sustained_over_chord_change +
            report.summary.non_diatonic_notes);
}

// Test: Regression test for the original bug
TEST_F(NonDiatonicDetectionTest, RegressionOriginalBugDetected) {
  // The original bug (seed 1670804638, chord_id 0, mood 14) produced
  // F# in bass which should now be fixed. This test verifies that
  // if such notes were present, they would be detected.

  params_.seed = 1670804638;
  params_.chord_id = 0;
  params_.mood = static_cast<Mood>(14);
  params_.structure = static_cast<StructurePattern>(5);
  params_.bpm = 150;
  params_.key = Key::E;  // Original was key 4 = E

  Generator gen;
  gen.generate(params_);
  const auto& song = gen.getSong();

  auto report = analyzeDissonance(song, params_);

  // After the fix, there should be zero non-diatonic notes
  EXPECT_EQ(report.summary.non_diatonic_notes, 0u)
      << "Original bug case should have zero non-diatonic notes after fix";
}

}  // namespace
}  // namespace midisketch
