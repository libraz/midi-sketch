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

// === Phase 3 Tests: Track Pair Severity Adjustment ===

// Test: Aux track issues should always be Low severity
TEST(DissonanceTest, AuxTrackIssuesAreLowSeverity) {
  // Generate a song and check that any Aux track issues are Low severity
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::FullPop;
  params.mood = Mood::StraightPop;
  params.chord_id = 0;
  params.key = Key::C;
  params.drums_enabled = true;
  params.vocal_low = 60;
  params.vocal_high = 79;
  params.seed = 54321;

  gen.generate(params);
  const auto& song = gen.getSong();

  auto report = analyzeDissonance(song, params);

  for (const auto& issue : report.issues) {
    // Check if Aux track is involved
    bool aux_involved = false;
    if (issue.type == DissonanceType::SimultaneousClash) {
      for (const auto& note_info : issue.notes) {
        if (note_info.track_name == "aux") {
          aux_involved = true;
          break;
        }
      }
    } else if (issue.type == DissonanceType::NonChordTone) {
      aux_involved = (issue.track_name == "aux");
    }

    if (aux_involved) {
      EXPECT_EQ(issue.severity, DissonanceSeverity::Low)
          << "Aux track issues should be Low severity, but got "
          << (issue.severity == DissonanceSeverity::High ? "High" : "Medium")
          << " at tick " << issue.tick;
    }
  }
}

// Test: Background-background clashes have reduced severity
TEST(DissonanceTest, BackgroundClashesHaveReducedSeverity) {
  // Generate a dense song to increase chance of background-background clashes
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::FullPop;
  params.mood = Mood::EnergeticDance;  // Dense arrangement
  params.chord_id = 0;
  params.key = Key::C;
  params.drums_enabled = true;
  params.vocal_low = 60;
  params.vocal_high = 79;
  params.seed = 99999;

  gen.generate(params);
  const auto& song = gen.getSong();

  auto report = analyzeDissonance(song, params);

  // Count High severity background-background clashes
  // (there should be few or none due to severity reduction)
  int high_background_clashes = 0;
  for (const auto& issue : report.issues) {
    if (issue.type == DissonanceType::SimultaneousClash &&
        issue.severity == DissonanceSeverity::High) {
      // Check if both tracks are background tracks
      bool both_background = true;
      for (const auto& note_info : issue.notes) {
        if (note_info.track_name != "motif" &&
            note_info.track_name != "arpeggio" &&
            note_info.track_name != "aux" &&
            note_info.track_name != "chord") {
          both_background = false;
          break;
        }
      }
      if (both_background) {
        high_background_clashes++;
      }
    }
  }

  // Background-background clashes should have reduced severity
  EXPECT_EQ(high_background_clashes, 0)
      << "Background-background clashes should not have High severity";
}

// ============================================================================
// ParsedMidi Analysis Tests
// ============================================================================

TEST(DissonanceTest, AnalyzeFromParsedMidiBasic) {
  // Create a ParsedMidi with a known clash
  ParsedMidi midi;
  midi.format = 1;
  midi.num_tracks = 2;
  midi.division = 480;
  midi.bpm = 120;

  // Track 1: Vocal with E4
  ParsedTrack vocal_track;
  vocal_track.name = "Vocal";
  vocal_track.channel = 0;
  ParsedNote note1{64, 100, 0, 480};  // E4 at tick 0
  vocal_track.notes.push_back(note1);
  midi.tracks.push_back(vocal_track);

  // Track 2: Chord with F4 (minor 2nd clash)
  ParsedTrack chord_track;
  chord_track.name = "Chord";
  chord_track.channel = 1;
  ParsedNote note2{65, 80, 0, 480};  // F4 at tick 0
  chord_track.notes.push_back(note2);
  midi.tracks.push_back(chord_track);

  auto report = analyzeDissonanceFromParsedMidi(midi);

  // Should detect the minor 2nd clash
  EXPECT_GE(report.summary.total_issues, 1u);
  EXPECT_GE(report.summary.simultaneous_clashes, 1u);

  // Find the clash and verify it's High severity
  bool found_clash = false;
  for (const auto& issue : report.issues) {
    if (issue.type == DissonanceType::SimultaneousClash &&
        issue.interval_semitones == 1) {
      found_clash = true;
      EXPECT_EQ(issue.severity, DissonanceSeverity::High);
      EXPECT_EQ(issue.interval_name, "minor 2nd");
      break;
    }
  }
  EXPECT_TRUE(found_clash) << "Minor 2nd clash should be detected";
}

TEST(DissonanceTest, AnalyzeFromParsedMidiNoDrums) {
  // Create a ParsedMidi with drums - drums should be skipped
  ParsedMidi midi;
  midi.format = 1;
  midi.num_tracks = 2;
  midi.division = 480;
  midi.bpm = 120;

  // Track 1: Drums (channel 9)
  ParsedTrack drums_track;
  drums_track.name = "Drums";
  drums_track.channel = 9;
  ParsedNote kick{36, 100, 0, 240};
  ParsedNote snare{38, 100, 0, 240};  // Same time as kick
  drums_track.notes.push_back(kick);
  drums_track.notes.push_back(snare);
  midi.tracks.push_back(drums_track);

  // Track 2: Melodic track
  ParsedTrack melody_track;
  melody_track.name = "Melody";
  melody_track.channel = 0;
  ParsedNote note{60, 100, 0, 480};
  melody_track.notes.push_back(note);
  midi.tracks.push_back(melody_track);

  auto report = analyzeDissonanceFromParsedMidi(midi);

  // Drums should not cause clashes
  for (const auto& issue : report.issues) {
    if (issue.type == DissonanceType::SimultaneousClash) {
      for (const auto& note_info : issue.notes) {
        EXPECT_NE(note_info.track_name, "Drums")
            << "Drums track should be excluded from clash detection";
      }
    }
  }
}

TEST(DissonanceTest, AnalyzeFromParsedMidiEmptyTracks) {
  ParsedMidi midi;
  midi.format = 1;
  midi.num_tracks = 0;
  midi.division = 480;
  midi.bpm = 120;

  auto report = analyzeDissonanceFromParsedMidi(midi);

  EXPECT_EQ(report.summary.total_issues, 0u);
  EXPECT_TRUE(report.issues.empty());
}

TEST(DissonanceTest, AnalyzeFromParsedMidiNoClash) {
  // Create a ParsedMidi with consonant intervals
  ParsedMidi midi;
  midi.format = 1;
  midi.num_tracks = 2;
  midi.division = 480;
  midi.bpm = 120;

  // Track 1: C4
  ParsedTrack track1;
  track1.name = "Track1";
  track1.channel = 0;
  ParsedNote note1{60, 100, 0, 480};  // C4
  track1.notes.push_back(note1);
  midi.tracks.push_back(track1);

  // Track 2: E4 (major 3rd - consonant)
  ParsedTrack track2;
  track2.name = "Track2";
  track2.channel = 1;
  ParsedNote note2{64, 80, 0, 480};  // E4
  track2.notes.push_back(note2);
  midi.tracks.push_back(track2);

  auto report = analyzeDissonanceFromParsedMidi(midi);

  // Major 3rd is consonant, should not be flagged as high severity
  for (const auto& issue : report.issues) {
    if (issue.type == DissonanceType::SimultaneousClash) {
      EXPECT_NE(issue.severity, DissonanceSeverity::High)
          << "Major 3rd should not be flagged as high severity";
    }
  }
}

TEST(DissonanceTest, AnalyzeFromParsedMidiTritone) {
  // Test tritone detection
  ParsedMidi midi;
  midi.format = 1;
  midi.num_tracks = 2;
  midi.division = 480;
  midi.bpm = 120;

  // Track 1: C4
  ParsedTrack track1;
  track1.name = "Track1";
  track1.channel = 0;
  ParsedNote note1{60, 100, 0, 480};  // C4
  track1.notes.push_back(note1);
  midi.tracks.push_back(track1);

  // Track 2: F#4 (tritone)
  ParsedTrack track2;
  track2.name = "Track2";
  track2.channel = 1;
  ParsedNote note2{66, 80, 0, 480};  // F#4
  track2.notes.push_back(note2);
  midi.tracks.push_back(track2);

  auto report = analyzeDissonanceFromParsedMidi(midi);

  // Should detect tritone (may be medium severity in context)
  bool found_tritone = false;
  for (const auto& issue : report.issues) {
    if (issue.type == DissonanceType::SimultaneousClash &&
        issue.interval_semitones == 6) {
      found_tritone = true;
      EXPECT_EQ(issue.interval_name, "tritone");
      break;
    }
  }
  EXPECT_TRUE(found_tritone) << "Tritone should be detected";
}

TEST(DissonanceTest, AnalyzeFromParsedMidiMajor7th) {
  // Test major 7th detection
  ParsedMidi midi;
  midi.format = 1;
  midi.num_tracks = 2;
  midi.division = 480;
  midi.bpm = 120;

  // Track 1: C4
  ParsedTrack track1;
  track1.name = "Track1";
  track1.channel = 0;
  ParsedNote note1{60, 100, 0, 480};  // C4
  track1.notes.push_back(note1);
  midi.tracks.push_back(track1);

  // Track 2: B4 (major 7th)
  ParsedTrack track2;
  track2.name = "Track2";
  track2.channel = 1;
  ParsedNote note2{71, 80, 0, 480};  // B4
  track2.notes.push_back(note2);
  midi.tracks.push_back(track2);

  auto report = analyzeDissonanceFromParsedMidi(midi);

  // Should detect major 7th
  // Note: Without chord info, defaults to I chord (degree 0), where major 7th
  // is considered part of Imaj7 voicing and gets Medium severity
  bool found_major7th = false;
  for (const auto& issue : report.issues) {
    if (issue.type == DissonanceType::SimultaneousClash &&
        issue.interval_semitones == 11) {
      found_major7th = true;
      EXPECT_EQ(issue.interval_name, "major 7th");
      // On I chord context, major 7th is downgraded to Medium (Imaj7 voicing)
      EXPECT_EQ(issue.severity, DissonanceSeverity::Medium);
      break;
    }
  }
  EXPECT_TRUE(found_major7th) << "Major 7th should be detected";
}

TEST(DissonanceTest, AnalyzeFromParsedMidiNonOverlappingNotes) {
  // Notes that don't overlap should not clash
  ParsedMidi midi;
  midi.format = 1;
  midi.num_tracks = 2;
  midi.division = 480;
  midi.bpm = 120;

  // Track 1: E4 at tick 0
  ParsedTrack track1;
  track1.name = "Track1";
  track1.channel = 0;
  ParsedNote note1{64, 100, 0, 480};  // E4, ends at 480
  track1.notes.push_back(note1);
  midi.tracks.push_back(track1);

  // Track 2: F4 at tick 480 (starts after first note ends)
  ParsedTrack track2;
  track2.name = "Track2";
  track2.channel = 1;
  ParsedNote note2{65, 80, 480, 480};  // F4, starts at 480
  track2.notes.push_back(note2);
  midi.tracks.push_back(track2);

  auto report = analyzeDissonanceFromParsedMidi(midi);

  // No clash should be detected between non-overlapping notes
  for (const auto& issue : report.issues) {
    if (issue.type == DissonanceType::SimultaneousClash) {
      // Check if both notes are involved
      bool e4_involved = false;
      bool f4_involved = false;
      for (const auto& note_info : issue.notes) {
        if (note_info.pitch == 64) e4_involved = true;
        if (note_info.pitch == 65) f4_involved = true;
      }
      EXPECT_FALSE(e4_involved && f4_involved)
          << "Non-overlapping E4 and F4 should not clash";
    }
  }
}

}  // namespace
}  // namespace midisketch
