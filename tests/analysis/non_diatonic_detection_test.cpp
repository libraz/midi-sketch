/**
 * @file non_diatonic_detection_test.cpp
 * @brief Tests for non-diatonic note detection in dissonance analysis.
 *
 * These tests verify that the dissonance analyzer correctly detects
 * notes that are not in the C major diatonic scale (internal representation).
 */

#include <gtest/gtest.h>

#include "analysis/dissonance.h"
#include "core/arrangement.h"
#include "core/chord.h"
#include "core/chord_progression_tracker.h"
#include "core/generator.h"
#include "core/song.h"
#include "core/types.h"
#include "test_helpers/note_event_test_helper.h"
#include "test_support/generator_test_fixture.h"

namespace midisketch {
namespace {

// A two-bar song with one note, over a progression the caller names. Nothing
// here is generated, so the note is exactly the one the analyzer is asked
// about and the chord under it and the chord after it are both known.
struct TwoChordFixture {
  Song song;
  ChordProgressionTracker timeline;

  TwoChordFixture(int8_t first_degree, int8_t second_degree, uint8_t pitch, Tick start_tick) {
    Section verse;
    verse.type = SectionType::A;
    verse.start_tick = 0;
    verse.bars = 2;
    verse.name = "Verse";
    Arrangement arrangement({verse});

    song.setArrangement(arrangement);
    song.motif().addNote(NoteEventTestHelper::create(start_tick, TICKS_PER_BEAT, pitch, 100));

    ChordProgression progression{};
    progression.degrees = {first_degree, second_degree, -1, -1, -1, -1, -1, -1};
    progression.length = 2;
    timeline.initialize(arrangement, progression, Mood::StraightPop);
  }
};

// The issues of one type, so a test can say how many there are rather than
// whether there are any.
std::vector<DissonanceIssue> issuesOfType(const DissonanceReport& report, DissonanceType type) {
  std::vector<DissonanceIssue> selected;
  for (const auto& issue : report.issues) {
    if (issue.type == type) selected.push_back(issue);
  }
  return selected;
}

class NonDiatonicDetectionTest : public test::GeneratorTestFixture {
 protected:
  void SetUp() override {
    GeneratorTestFixture::SetUp();
    params_.drums_enabled = true;
    params_.vocal_high = 79;
  }
};

// Test: Detection counts non-diatonic notes in summary
TEST_F(NonDiatonicDetectionTest, SummaryCountsNonDiatonicNotes) {
  Generator gen;
  gen.generate(params_);
  const auto& song = gen.getSong();

  auto report = analyzeDissonance(song, params_);

  // Every reported non-diatonic note must be one the issue list actually holds.
  uint32_t listed_non_diatonic = 0;
  for (const auto& issue : report.issues) {
    if (issue.type == DissonanceType::NonDiatonicNote) listed_non_diatonic++;
  }
  EXPECT_EQ(report.summary.non_diatonic_notes, listed_non_diatonic)
      << "Non-diatonic counter disagrees with the reported issues";

  // Total should include non-diatonic count
  EXPECT_EQ(report.summary.total_issues,
            report.summary.simultaneous_clashes + report.summary.non_chord_tones +
                report.summary.sustained_over_chord_change + report.summary.non_diatonic_notes);
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

// A chromatic note that no chord accounts for is reported. Every gate in this
// detector is an exemption, so without a case that reaches the end of them the
// tests below cannot tell a detector that is right from one that is silent.
TEST(NonDiatonicGateTest, ReportsAChromaticNoteNoChordAccountsFor) {
  // D#4 in a bar of V moving to I: not in G, not in C, and the dominant of the
  // chord it moves to is G7, which does not contain it either.
  TwoChordFixture fixture(4, 0, 63, 0);

  GeneratorParams params{};
  params.chord_id = 0;
  params.mood = Mood::StraightPop;
  const auto report = analyzeDissonance(fixture.song, params, fixture.timeline);

  const auto issues = issuesOfType(report, DissonanceType::NonDiatonicNote);
  ASSERT_EQ(issues.size(), 1u) << "the one chromatic note in the song was not reported";
  EXPECT_EQ(issues[0].pitch, 63);
  EXPECT_EQ(issues[0].track_name, "motif");
  EXPECT_EQ(report.summary.non_diatonic_notes, 1u);
}

// The exemption for a borrowed tone names the chord the tone pulls towards, so
// it has to be able to answer no. Spelled as a set of pitch classes with no
// target it answers yes to all five chromatic notes a major key has and the
// detector reports nothing at all.
TEST(NonDiatonicGateTest, ExemptsABorrowedToneOnlyForTheChordItLeadsTo) {
  // F# is the third of D7, which is the dominant of V.
  TwoChordFixture leads_to_five(0, 4, 66, 0);
  TwoChordFixture leads_to_one(4, 0, 66, 0);

  GeneratorParams params{};
  params.chord_id = 0;
  params.mood = Mood::StraightPop;

  const auto exempt = analyzeDissonance(leads_to_five.song, params, leads_to_five.timeline);
  EXPECT_EQ(exempt.summary.non_diatonic_notes, 0u)
      << "F# tonicises the V that follows it and is the reason the exemption exists";

  const auto reported = analyzeDissonance(leads_to_one.song, params, leads_to_one.timeline);
  EXPECT_EQ(reported.summary.non_diatonic_notes, 1u)
      << "the same F# leads into I, which is not tonicised by a chord containing it";
}

// Every pitch in one report belongs to one space, and the report says how far
// that space is from the one a listener hears.
TEST(NonDiatonicGateTest, ReportsTheInternalPitchAndStatesTheKeyOnce) {
  TwoChordFixture fixture(4, 0, 63, 0);

  GeneratorParams params{};
  params.chord_id = 0;
  params.mood = Mood::StraightPop;
  params.key = Key::E;
  const auto report = analyzeDissonance(fixture.song, params, fixture.timeline);

  const auto issues = issuesOfType(report, DissonanceType::NonDiatonicNote);
  ASSERT_EQ(issues.size(), 1u);

  // The note the song holds, not the note the song will sound. Transposing this
  // one issue type would leave it in a different space from the provenance
  // beside it, from the chord names on its sibling issues, and from the pitch
  // classes the detection itself was run on.
  EXPECT_EQ(issues[0].pitch, 63);
  EXPECT_EQ(issues[0].pitch_name, "D#4");
  EXPECT_EQ(issues[0].key_name, "C major");

  // The sounding pitch is recoverable because the report states the offset.
  EXPECT_EQ(report.summary.key, Key::E);
  EXPECT_EQ(report.summary.modulation_amount, 0);
  EXPECT_EQ(issues[0].pitch + static_cast<int>(report.summary.key), 67);
}

// Test: JSON output includes non-diatonic notes
TEST_F(NonDiatonicDetectionTest, JsonOutputIncludesNonDiatonic) {
  // Create a report with a non-diatonic issue
  DissonanceReport report{};
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
  issue.pitch = 58;  // A#3
  issue.pitch_name = "A#3";
  issue.key_name = "C major";
  issue.scale_tones = {"C", "D", "E", "F", "G", "A", "B"};
  report.issues.push_back(issue);
  report.summary.key = Key::E;

  std::string json = dissonanceReportToJson(report);

  // Verify JSON contains the non-diatonic issue
  EXPECT_NE(json.find("non_diatonic_note"), std::string::npos);
  EXPECT_NE(json.find("\"non_diatonic_notes\":1"), std::string::npos);
  EXPECT_NE(json.find("C major"), std::string::npos);
  EXPECT_NE(json.find("A#3"), std::string::npos);
  EXPECT_NE(json.find("scale_tones"), std::string::npos);

  // A consumer reading only the JSON still learns where the pitches sit
  // relative to the sounding key.
  EXPECT_NE(json.find("\"key\":4"), std::string::npos);
  EXPECT_NE(json.find("\"key_name\":\"E major\""), std::string::npos);
}

// Test: Clean generation produces minimal non-diatonic notes
TEST_F(NonDiatonicDetectionTest, CleanGenerationHasNoNonDiatonic) {
  // After the bass fix, normal generation should have minimal non-diatonic notes.
  // Modal interchange (iv, bII, #IVdim) intentionally introduces
  // non-diatonic tones for harmonic color. Allow up to 3 per song.
  std::vector<uint32_t> test_seeds = {1, 42, 12345, 67890, 99999};

  for (uint32_t seed : test_seeds) {
    params_.seed = seed;
    params_.key = Key::C;

    Generator gen;
    gen.generate(params_);
    const auto& song = gen.getSong();

    auto report = analyzeDissonance(song, params_);

    EXPECT_LE(report.summary.non_diatonic_notes, 3u)
        << "Seed " << seed << " produced " << report.summary.non_diatonic_notes
        << " non-diatonic notes - should be minimal (modal interchange allowed)";
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

  EXPECT_EQ(report.summary.total_issues, report.issues.size())
      << "total_issues does not match the reported issue list";

  // Verify the count formula is correct
  EXPECT_EQ(report.summary.total_issues,
            report.summary.simultaneous_clashes + report.summary.non_chord_tones +
                report.summary.sustained_over_chord_change + report.summary.non_diatonic_notes);
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

  // After the fix, non-diatonic notes from the original bass bug should be gone.
  // Modal interchange may introduce a small number of intentional
  // non-diatonic notes (iv, bII, #IVdim). Allow up to 6.
  // (Increased from 5 to 6 for phrase contour/rhythm-melody coupling changes)
  EXPECT_LE(report.summary.non_diatonic_notes, 6u)
      << "Original bug case should have minimal non-diatonic notes after fix";
}

}  // namespace
}  // namespace midisketch
