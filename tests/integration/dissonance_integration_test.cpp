/**
 * @file dissonance_integration_test.cpp
 * @brief Integration tests for dissonance detection across all generation modes.
 *
 * These tests catch dissonance issues systematically before manual listening,
 * regardless of which tracks or generation order causes the problem.
 */

#include <gtest/gtest.h>
#include "core/generator.h"
#include "core/harmony_context.h"
#include "core/pitch_utils.h"
#include "core/timing_constants.h"
#include "core/types.h"
#include <map>
#include <string>
#include <vector>

namespace midisketch {
namespace {

// Maximum allowed register separation for clash detection (2 octaves)
constexpr int MAX_CLASH_SEPARATION = 24;

struct ClashInfo {
  std::string track_a;
  std::string track_b;
  uint8_t pitch_a;
  uint8_t pitch_b;
  Tick tick;
  int interval;
};

// Get track name for reporting
std::string getTrackName(const MidiTrack* track, const Song& song) {
  if (track == &song.vocal()) return "Vocal";
  if (track == &song.bass()) return "Bass";
  if (track == &song.chord()) return "Chord";
  if (track == &song.motif()) return "Motif";
  if (track == &song.aux()) return "Aux";
  return "Unknown";
}

// Find all dissonant clashes between two tracks using chord context
std::vector<ClashInfo> findClashes(const MidiTrack& track_a, const std::string& name_a,
                                    const MidiTrack& track_b, const std::string& name_b,
                                    const HarmonyContext& harmony) {
  std::vector<ClashInfo> clashes;

  for (const auto& note_a : track_a.notes()) {
    Tick start_a = note_a.start_tick;
    Tick end_a = start_a + note_a.duration;

    for (const auto& note_b : track_b.notes()) {
      Tick start_b = note_b.start_tick;
      Tick end_b = start_b + note_b.duration;

      // Check temporal overlap
      bool overlap = (start_a < end_b) && (start_b < end_a);
      if (!overlap) continue;

      // Calculate actual interval
      int actual_interval = std::abs(static_cast<int>(note_a.note) -
                                     static_cast<int>(note_b.note));

      // Skip wide separations (perceptually not clashing)
      if (actual_interval >= MAX_CLASH_SEPARATION) continue;

      // Check dissonance using unified logic from pitch_utils
      Tick overlap_tick = std::max(start_a, start_b);
      int8_t chord_degree = harmony.getChordDegreeAt(overlap_tick);

      if (isDissonantActualInterval(actual_interval, chord_degree)) {
        clashes.push_back({name_a, name_b, note_a.note, note_b.note,
                          overlap_tick, actual_interval});
      }
    }
  }

  return clashes;
}

// Analyze all track pairs in a song for dissonances using chord context
std::vector<ClashInfo> analyzeAllTrackPairs(const Song& song, const HarmonyContext& harmony) {
  std::vector<ClashInfo> all_clashes;

  // Get all melodic tracks (skip drums and SE)
  std::vector<std::pair<const MidiTrack*, std::string>> tracks;
  if (!song.vocal().empty()) tracks.push_back({&song.vocal(), "Vocal"});
  if (!song.bass().empty()) tracks.push_back({&song.bass(), "Bass"});
  if (!song.chord().empty()) tracks.push_back({&song.chord(), "Chord"});
  if (!song.motif().empty()) tracks.push_back({&song.motif(), "Motif"});
  if (!song.aux().empty()) tracks.push_back({&song.aux(), "Aux"});

  // Check all unique pairs
  for (size_t i = 0; i < tracks.size(); ++i) {
    for (size_t j = i + 1; j < tracks.size(); ++j) {
      auto clashes = findClashes(*tracks[i].first, tracks[i].second,
                                  *tracks[j].first, tracks[j].second, harmony);
      all_clashes.insert(all_clashes.end(), clashes.begin(), clashes.end());
    }
  }

  return all_clashes;
}

class TrackClashIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::FullPop;
    params_.mood = Mood::IdolPop;
    params_.chord_id = 0;
    params_.key = Key::C;
    params_.drums_enabled = true;
    params_.vocal_low = 57;
    params_.vocal_high = 79;
    params_.bpm = 120;
  }

  GeneratorParams params_;
};

// =============================================================================
// Comprehensive dissonance tests for each composition style
// =============================================================================

TEST_F(TrackClashIntegrationTest, MelodyLeadMode_NoDissonantClashes) {
  params_.composition_style = CompositionStyle::MelodyLead;

  std::vector<uint32_t> seeds = {12345, 67890, 4130447576, 99999, 2802138756};

  for (uint32_t seed : seeds) {
    params_.seed = seed;

    Generator gen;
    gen.generate(params_);

    auto clashes = analyzeAllTrackPairs(gen.getSong(), gen.getHarmonyContext());

    if (!clashes.empty()) {
      std::cerr << "\n=== Seed " << seed << " clashes ===\n";
      for (const auto& c : clashes) {
        std::cerr << c.track_a << "(" << (int)c.pitch_a << ") vs "
                  << c.track_b << "(" << (int)c.pitch_b << ") "
                  << "interval=" << c.interval << " tick=" << c.tick << "\n";
      }
    }

    EXPECT_EQ(clashes.size(), 0u)
        << "MelodyLead mode (seed " << seed << ") has " << clashes.size()
        << " dissonant clashes";
  }
}

TEST_F(TrackClashIntegrationTest, BackgroundMotifMode_NoDissonantClashes) {
  params_.composition_style = CompositionStyle::BackgroundMotif;

  std::vector<uint32_t> seeds = {12345, 67890, 2802138756, 3054356854, 99999};

  for (uint32_t seed : seeds) {
    params_.seed = seed;

    Generator gen;
    gen.generate(params_);

    auto clashes = analyzeAllTrackPairs(gen.getSong(), gen.getHarmonyContext());

    EXPECT_EQ(clashes.size(), 0u)
        << "BackgroundMotif mode (seed " << seed << ") has " << clashes.size()
        << " dissonant clashes";
  }
}

TEST_F(TrackClashIntegrationTest, SynthDrivenMode_NoDissonantClashes) {
  params_.composition_style = CompositionStyle::SynthDriven;
  params_.arpeggio_enabled = true;

  std::vector<uint32_t> seeds = {12345, 67890, 99999};

  for (uint32_t seed : seeds) {
    params_.seed = seed;

    Generator gen;
    gen.generate(params_);

    auto clashes = analyzeAllTrackPairs(gen.getSong(), gen.getHarmonyContext());

    EXPECT_EQ(clashes.size(), 0u)
        << "SynthDriven mode (seed " << seed << ") has " << clashes.size()
        << " dissonant clashes";
  }
}

// =============================================================================
// Cross-configuration tests
// =============================================================================

TEST_F(TrackClashIntegrationTest, AllChordProgressions_NoDissonantClashes) {
  params_.composition_style = CompositionStyle::BackgroundMotif;
  params_.seed = 12345;

  for (uint8_t chord_id = 0; chord_id < 10; ++chord_id) {
    params_.chord_id = chord_id;

    Generator gen;
    gen.generate(params_);

    auto clashes = analyzeAllTrackPairs(gen.getSong(), gen.getHarmonyContext());

    EXPECT_EQ(clashes.size(), 0u)
        << "Chord progression " << static_cast<int>(chord_id)
        << " has " << clashes.size() << " dissonant clashes";
  }
}

TEST_F(TrackClashIntegrationTest, AllKeys_NoDissonantClashes) {
  params_.composition_style = CompositionStyle::BackgroundMotif;
  params_.seed = 12345;

  for (int key = 0; key < 12; ++key) {
    params_.key = static_cast<Key>(key);

    Generator gen;
    gen.generate(params_);

    auto clashes = analyzeAllTrackPairs(gen.getSong(), gen.getHarmonyContext());

    EXPECT_EQ(clashes.size(), 0u)
        << "Key " << key << " has " << clashes.size() << " dissonant clashes";
  }
}

TEST_F(TrackClashIntegrationTest, AllMoods_NoDissonantClashes) {
  params_.composition_style = CompositionStyle::BackgroundMotif;
  params_.seed = 12345;

  std::vector<Mood> moods = {
    Mood::StraightPop, Mood::BrightUpbeat, Mood::EnergeticDance,
    Mood::LightRock, Mood::Ballad, Mood::CityPop, Mood::Yoasobi
  };

  for (Mood mood : moods) {
    params_.mood = mood;

    Generator gen;
    gen.generate(params_);

    auto clashes = analyzeAllTrackPairs(gen.getSong(), gen.getHarmonyContext());

    EXPECT_EQ(clashes.size(), 0u)
        << "Mood " << static_cast<int>(mood) << " has " << clashes.size()
        << " dissonant clashes";
  }
}

// =============================================================================
// Specific track pair tests (for detailed diagnosis)
// =============================================================================

TEST_F(TrackClashIntegrationTest, MotifBassClashes_BGMMode) {
  params_.composition_style = CompositionStyle::BackgroundMotif;

  std::vector<uint32_t> seeds = {12345, 2802138756, 3054356854};

  for (uint32_t seed : seeds) {
    params_.seed = seed;

    Generator gen;
    gen.generate(params_);

    const auto& motif = gen.getSong().motif();
    const auto& bass = gen.getSong().bass();

    if (motif.empty() || bass.empty()) continue;

    auto clashes = findClashes(motif, "Motif", bass, "Bass", gen.getHarmonyContext());

    EXPECT_EQ(clashes.size(), 0u)
        << "Motif-Bass clashes (seed " << seed << "): " << clashes.size();
  }
}

TEST_F(TrackClashIntegrationTest, VocalBassClashes_MelodyLeadMode) {
  params_.composition_style = CompositionStyle::MelodyLead;

  std::vector<uint32_t> seeds = {12345, 4130447576, 67890};

  for (uint32_t seed : seeds) {
    params_.seed = seed;

    Generator gen;
    gen.generate(params_);

    const auto& vocal = gen.getSong().vocal();
    const auto& bass = gen.getSong().bass();

    if (vocal.empty() || bass.empty()) continue;

    auto clashes = findClashes(vocal, "Vocal", bass, "Bass", gen.getHarmonyContext());

    EXPECT_EQ(clashes.size(), 0u)
        << "Vocal-Bass clashes (seed " << seed << "): " << clashes.size();
  }
}

// Regression test for anticipation tritone bug
// Bug: Bass anticipation to next chord didn't check for tritone clash with vocal
// Example: Vocal B4 vs Bass F3 (anticipating F chord) = 18 semitones = compound tritone
TEST_F(TrackClashIntegrationTest, AnticipationTritoneRegression_Seed464394633) {
  params_.composition_style = CompositionStyle::MelodyLead;
  params_.seed = 464394633;
  params_.target_duration_seconds = 150;

  Generator gen;
  gen.generate(params_);

  auto clashes = analyzeAllTrackPairs(gen.getSong(), gen.getHarmonyContext());

  // This seed previously caused F-B tritone clashes at bar 53
  // due to bass anticipation not checking for tritone interval
  EXPECT_EQ(clashes.size(), 0u)
      << "Anticipation tritone regression: " << clashes.size() << " clashes found";
}

// Regression test for chord-bass tritone clash
// Bug: Bass anticipation F clashed with Chord B on phrase boundaries
TEST_F(TrackClashIntegrationTest, ChordBassAnticipationRegression_Seed3263424241) {
  params_.composition_style = CompositionStyle::MelodyLead;
  params_.seed = 3263424241;
  params_.target_duration_seconds = 150;

  Generator gen;
  gen.generate(params_);

  auto clashes = analyzeAllTrackPairs(gen.getSong(), gen.getHarmonyContext());

  // This seed previously caused Chord(B) vs Bass(F) tritone clashes
  // at bars 17, 33, 41 due to phrase-end anticipation
  EXPECT_EQ(clashes.size(), 0u)
      << "Chord-Bass anticipation regression: " << clashes.size() << " clashes found";
}

// Diagnostic test to identify which track pairs are clashing
TEST_F(TrackClashIntegrationTest, DISABLED_DiagnoseClashSources) {
  params_.composition_style = CompositionStyle::MelodyLead;
  params_.seed = 67890;

  Generator gen;
  gen.generate(params_);
  const auto& song = gen.getSong();

  std::cout << "\n=== MelodyLead mode (seed 67890) clash analysis ===\n";
  std::cout << "Vocal notes: " << song.vocal().notes().size() << "\n";
  std::cout << "Bass notes: " << song.bass().notes().size() << "\n";
  std::cout << "Chord notes: " << song.chord().notes().size() << "\n";
  std::cout << "Aux notes: " << song.aux().notes().size() << "\n";
  std::cout << "Motif notes: " << song.motif().notes().size() << "\n\n";

  const auto& harmony = gen.getHarmonyContext();
  auto vb = findClashes(song.vocal(), "Vocal", song.bass(), "Bass", harmony);
  auto vc = findClashes(song.vocal(), "Vocal", song.chord(), "Chord", harmony);
  auto va = findClashes(song.vocal(), "Vocal", song.aux(), "Aux", harmony);
  auto bc = findClashes(song.bass(), "Bass", song.chord(), "Chord", harmony);
  auto ba = findClashes(song.bass(), "Bass", song.aux(), "Aux", harmony);
  auto ca = findClashes(song.chord(), "Chord", song.aux(), "Aux", harmony);

  std::cout << "Vocal-Bass: " << vb.size() << "\n";
  std::cout << "Vocal-Chord: " << vc.size() << "\n";
  std::cout << "Vocal-Aux: " << va.size() << "\n";
  std::cout << "Bass-Chord: " << bc.size() << "\n";
  std::cout << "Bass-Aux: " << ba.size() << "\n";
  std::cout << "Chord-Aux: " << ca.size() << "\n";
  std::cout << "Total: " << (vb.size() + vc.size() + va.size() + bc.size() + ba.size() + ca.size()) << "\n\n";

  // Print first few clashes for each pair
  auto printClashes = [](const std::vector<ClashInfo>& clashes, const std::string& name, int max = 3) {
    if (clashes.empty()) return;
    std::cout << name << " details:\n";
    int count = 0;
    for (const auto& c : clashes) {
      if (count >= max) break;
      int bar = static_cast<int>(c.tick / 1920) + 1;
      std::cout << "  Bar " << bar << ": " << c.track_a << " " << static_cast<int>(c.pitch_a)
                << " vs " << c.track_b << " " << static_cast<int>(c.pitch_b)
                << " (interval: " << c.interval << ")\n";
      ++count;
    }
  };

  printClashes(vb, "Vocal-Bass");
  printClashes(vc, "Vocal-Chord");
  printClashes(va, "Vocal-Aux");
  printClashes(bc, "Bass-Chord");
  printClashes(ba, "Bass-Aux");
  printClashes(ca, "Chord-Aux");

  // Test for other seeds too
  std::cout << "\n=== Other seeds summary ===\n";
  for (uint32_t seed : {99999u, 2802138756u}) {
    params_.seed = seed;
    gen.generate(params_);
    const auto& s = gen.getSong();
    auto all = analyzeAllTrackPairs(s, gen.getHarmonyContext());
    std::cout << "Seed " << seed << ": " << all.size() << " clashes\n";

    // Count by pair
    std::map<std::string, int> pair_counts;
    for (const auto& c : all) {
      std::string key = c.track_a + "-" + c.track_b;
      pair_counts[key]++;
    }
    for (const auto& [pair, count] : pair_counts) {
      std::cout << "  " << pair << ": " << count << "\n";
    }
  }
}

}  // namespace
}  // namespace midisketch
