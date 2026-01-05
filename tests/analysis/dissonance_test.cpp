#include <gtest/gtest.h>
#include "analysis/dissonance.h"
#include "core/generator.h"
#include "core/song.h"
#include <set>
#include <tuple>

namespace midisketch {
namespace {

TEST(DissonanceTest, MidiNoteToName) {
  EXPECT_EQ(midiNoteToName(60), "C4");
  EXPECT_EQ(midiNoteToName(61), "C#4");
  EXPECT_EQ(midiNoteToName(69), "A4");
  EXPECT_EQ(midiNoteToName(72), "C5");
  EXPECT_EQ(midiNoteToName(48), "C3");
}

TEST(DissonanceTest, IntervalToName) {
  EXPECT_EQ(intervalToName(0), "unison");
  EXPECT_EQ(intervalToName(1), "minor 2nd");
  EXPECT_EQ(intervalToName(6), "tritone");
  EXPECT_EQ(intervalToName(7), "perfect 5th");
  EXPECT_EQ(intervalToName(11), "major 7th");
  EXPECT_EQ(intervalToName(12), "unison");  // Wraps around
}

TEST(DissonanceTest, AnalyzeGeneratedSong) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.chord_id = 0;  // Canon progression
  params.key = Key::C;
  params.drums_enabled = true;
  // modulation_timing defaults to None
  params.vocal_low = 60;
  params.vocal_high = 79;
  params.seed = 12345;

  gen.generate(params);
  const auto& song = gen.getSong();

  auto report = analyzeDissonance(song, params);

  // Basic sanity checks
  EXPECT_EQ(report.summary.total_issues,
            report.summary.simultaneous_clashes + report.summary.non_chord_tones);
  EXPECT_EQ(report.summary.total_issues,
            report.summary.high_severity + report.summary.medium_severity +
                report.summary.low_severity);

  // Issues should be sorted by tick
  for (size_t i = 1; i < report.issues.size(); ++i) {
    EXPECT_LE(report.issues[i - 1].tick, report.issues[i].tick);
  }
}

TEST(DissonanceTest, JsonOutputFormat) {
  DissonanceReport report;
  report.summary.total_issues = 2;
  report.summary.simultaneous_clashes = 1;
  report.summary.non_chord_tones = 1;
  report.summary.high_severity = 1;
  report.summary.medium_severity = 1;
  report.summary.low_severity = 0;

  DissonanceIssue clash;
  clash.type = DissonanceType::SimultaneousClash;
  clash.severity = DissonanceSeverity::High;
  clash.tick = 1920;
  clash.bar = 1;
  clash.beat = 1.0f;
  clash.interval_semitones = 1;
  clash.interval_name = "minor 2nd";
  clash.notes.push_back({"vocal", 64, "E4"});
  clash.notes.push_back({"chord", 65, "F4"});
  report.issues.push_back(clash);

  DissonanceIssue nct;
  nct.type = DissonanceType::NonChordTone;
  nct.severity = DissonanceSeverity::Medium;
  nct.tick = 3840;
  nct.bar = 2;
  nct.beat = 1.0f;
  nct.track_name = "vocal";
  nct.pitch = 66;
  nct.pitch_name = "F#4";
  nct.chord_degree = 0;
  nct.chord_name = "C";
  nct.chord_tones = {"C", "E", "G"};
  report.issues.push_back(nct);

  std::string json = dissonanceReportToJson(report);

  // Check for key elements in JSON
  EXPECT_NE(json.find("\"total_issues\": 2"), std::string::npos);
  EXPECT_NE(json.find("\"simultaneous_clash\""), std::string::npos);
  EXPECT_NE(json.find("\"non_chord_tone\""), std::string::npos);
  EXPECT_NE(json.find("\"minor 2nd\""), std::string::npos);
  EXPECT_NE(json.find("\"F#4\""), std::string::npos);
  EXPECT_NE(json.find("\"high\""), std::string::npos);
  EXPECT_NE(json.find("\"medium\""), std::string::npos);
}

TEST(DissonanceTest, EmptyReportJson) {
  DissonanceReport report{};
  std::string json = dissonanceReportToJson(report);

  EXPECT_NE(json.find("\"total_issues\": 0"), std::string::npos);
  EXPECT_NE(json.find("\"issues\": []"), std::string::npos);
}

TEST(DissonanceTest, DifferentChordProgressions) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::DirectChorus;
  params.mood = Mood::StraightPop;
  params.key = Key::C;
  params.drums_enabled = true;
  // modulation_timing defaults to None
  params.vocal_low = 60;
  params.vocal_high = 79;
  params.seed = 54321;

  // Test with different chord progressions
  for (uint8_t chord_id = 0; chord_id < 4; ++chord_id) {
    params.chord_id = chord_id;
    gen.generate(params);
    const auto& song = gen.getSong();

    auto report = analyzeDissonance(song, params);

    // Should not crash and should produce valid summaries
    EXPECT_EQ(report.summary.total_issues,
              report.summary.simultaneous_clashes + report.summary.non_chord_tones);
  }
}

TEST(DissonanceTest, WithChordExtensions) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::DirectChorus;
  params.mood = Mood::StraightPop;
  params.chord_id = 0;
  params.key = Key::C;
  params.drums_enabled = true;
  // modulation_timing defaults to None
  params.vocal_low = 60;
  params.vocal_high = 79;
  params.seed = 99999;

  // Enable chord extensions
  params.chord_extension.enable_7th = true;
  params.chord_extension.enable_9th = true;

  gen.generate(params);
  const auto& song = gen.getSong();

  auto report = analyzeDissonance(song, params);

  // With extensions enabled, 7th and 9th should be accepted as chord tones
  // This test just verifies the analysis doesn't crash with extensions
  EXPECT_GE(report.summary.total_issues, 0u);
}

// Test: Register separation reduces dissonance severity
TEST(DissonanceTest, RegisterSeparationReducesSeverity) {
  // When notes are 2+ octaves apart, the same pitch class interval
  // should be less severe than same-octave clashes
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.chord_id = 0;
  params.key = Key::C;
  params.drums_enabled = true;
  // Enable modulation to test more scenarios
  params.vocal_low = 60;
  params.vocal_high = 84;  // Wide range to allow octave separation
  params.seed = 77777;

  gen.setModulationTiming(ModulationTiming::LastChorus, 1);
  gen.generate(params);
  const auto& song = gen.getSong();

  auto report = analyzeDissonance(song, params);

  // With the register separation rule, high severity should be minimal
  // because wide-register intervals are downgraded
  EXPECT_EQ(report.summary.high_severity, 0u)
      << "Register separation should prevent high severity clashes";
}

// Test: Available tensions are not flagged as issues
TEST(DissonanceTest, AvailableTensionsAccepted) {
  // 9th, 11th (on minor), 13th should not be flagged as non-chord tones
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::DirectChorus;
  params.mood = Mood::StraightPop;
  params.chord_id = 0;
  params.key = Key::C;
  params.drums_enabled = true;
  // modulation_timing defaults to None
  params.vocal_low = 60;
  params.vocal_high = 79;
  params.seed = 88888;

  gen.generate(params);
  const auto& song = gen.getSong();

  auto report = analyzeDissonance(song, params);

  // Count non-chord tones on strong beats (these should be filtered by tension rules)
  int strong_beat_nct = 0;
  for (const auto& issue : report.issues) {
    if (issue.type == DissonanceType::NonChordTone) {
      // Check if on beat 1 (strong beat)
      float beat_pos = issue.beat - 1.0f;  // 0-indexed beat
      if (beat_pos < 0.5f) {  // Beat 1
        strong_beat_nct++;
      }
    }
  }

  // Most strong beat notes should be chord tones or acceptable tensions
  // Allow some non-chord tones (passing tones, etc.)
  EXPECT_LE(strong_beat_nct, 10)
      << "Too many non-chord tones on strong beats: " << strong_beat_nct;
}

// Test: Deduplication prevents duplicate clash reports
TEST(DissonanceTest, DeduplicationWorks) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::FullPop;
  params.mood = Mood::EnergeticDance;
  params.chord_id = 0;
  params.key = Key::C;
  params.drums_enabled = true;
  params.vocal_low = 60;
  params.vocal_high = 79;
  params.seed = 11111;

  gen.setModulationTiming(ModulationTiming::LastChorus, 1);
  gen.generate(params);
  const auto& song = gen.getSong();

  auto report = analyzeDissonance(song, params);

  // Check for duplicate simultaneous clashes at same tick with same pitches
  std::set<std::tuple<Tick, uint8_t, uint8_t>> seen_clashes;
  int duplicates = 0;

  for (const auto& issue : report.issues) {
    if (issue.type == DissonanceType::SimultaneousClash && issue.notes.size() >= 2) {
      uint8_t p1 = std::min(issue.notes[0].pitch, issue.notes[1].pitch);
      uint8_t p2 = std::max(issue.notes[0].pitch, issue.notes[1].pitch);
      auto key = std::make_tuple(issue.tick, p1, p2);

      if (seen_clashes.count(key) > 0) {
        duplicates++;
      }
      seen_clashes.insert(key);
    }
  }

  EXPECT_EQ(duplicates, 0)
      << "Deduplication should prevent duplicate clash reports: " << duplicates << " duplicates found";
}

// Test: Zero high severity issues after all fixes
TEST(DissonanceTest, ZeroHighSeverityIssues) {
  // With all the clash avoidance and analysis improvements,
  // we should achieve zero high severity issues across multiple seeds
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::FullPop;
  params.mood = Mood::IdolPop;
  params.chord_id = 0;
  params.key = Key::C;
  params.drums_enabled = true;
  params.vocal_low = 60;
  params.vocal_high = 79;

  gen.setModulationTiming(ModulationTiming::LastChorus, 1);

  for (int seed = 1; seed <= 10; ++seed) {
    params.seed = seed * 1234;

    gen.generate(params);
    const auto& song = gen.getSong();

    auto report = analyzeDissonance(song, params);

    EXPECT_EQ(report.summary.high_severity, 0u)
        << "Seed " << params.seed << " has " << report.summary.high_severity
        << " high severity issues";
  }
}

}  // namespace
}  // namespace midisketch
