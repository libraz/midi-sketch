/**
 * @file bass_theory_test.cpp
 * @brief Tests for bass track music theory, diatonic correctness, and chord-tone quality.
 *
 * Consolidates tests from:
 * - bass_diatonic_test.cpp: Diatonic scale membership tests
 * - bass_music_theory_test.cpp: Pure music theory tests (intervals, chord functions)
 * - bass_chord_tone_test.cpp: Diagnostic tests for chord-tone quality
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <vector>

#include "core/chord.h"
#include "core/chord_progression_tracker.h"
#include "core/chord_utils.h"
#include "core/generator.h"
#include "core/harmony_context.h"
#include "core/i_harmony_context.h"
#include "core/note_creator.h"
#include "core/pitch_utils.h"
#include "core/song.h"
#include "core/timing_constants.h"
#include "core/types.h"
#include "test_support/collision_test_helper.h"
#include "test_support/generator_test_fixture.h"
#include "track/bass/bass_density.h"
#include "track/bass/bass_motion.h"
#include "track/bass/bass_pattern_selection.h"
#include "track/generators/bass.h"

namespace midisketch {
namespace {

// ============================================================================
// Shared Helpers
// ============================================================================

// C major diatonic pitch classes
const std::set<int> C_MAJOR_DIATONIC = {0, 2, 4, 5, 7, 9, 11};

// Check if a pitch class is diatonic to C major
bool isDiatonic(int pitch) {
  int pitch_class = pitch % 12;
  return C_MAJOR_DIATONIC.count(pitch_class) > 0;
}

// Pitch class names for diagnostic output
const char* pitchClassName(int pitch_class) {
  static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  return names[((pitch_class % 12) + 12) % 12];
}

// Degree names for diagnostic output
const char* degreeName(int8_t degree) {
  static const char* names[] = {"I(C)", "ii(Dm)", "iii(Em)", "IV(F)", "V(G)", "vi(Am)", "vii(B)"};
  if (degree >= 0 && degree < 7) return names[degree];
  return "??";
}

// Format chord tones as a readable string
std::string formatChordTones(const std::vector<int>& tones) {
  std::string result = "{";
  for (size_t idx = 0; idx < tones.size(); ++idx) {
    if (idx > 0) result += ", ";
    result += pitchClassName(tones[idx]);
  }
  result += "}";
  return result;
}

TEST(BassAnalysisTest, EmptyBarReportsNoRootOnBeat1) {
  MidiTrack track;

  BassAnalysis analysis = BassAnalysis::analyzeBar(track, 0, 48);

  EXPECT_FALSE(analysis.has_root_on_beat1);
}

TEST(BassAnalysisTest, Beat1FifthDoesNotReportRootOnBeat1) {
  MidiTrack track;
  track.addNote(NoteEventBuilder::create(0, TICKS_PER_BEAT, 55, 100));  // G over C.

  BassAnalysis analysis = BassAnalysis::analyzeBar(track, 0, 48);

  EXPECT_FALSE(analysis.has_root_on_beat1);
  EXPECT_TRUE(analysis.has_fifth);
}

TEST(BassAnalysisTest, Beat1RootReportsRootOnBeat1) {
  MidiTrack track;
  track.addNote(NoteEventBuilder::create(0, TICKS_PER_BEAT, 48, 100));  // C root.

  BassAnalysis analysis = BassAnalysis::analyzeBar(track, 0, 48);

  EXPECT_TRUE(analysis.has_root_on_beat1);
}

TEST(BassDensityAdjustmentTest, PreservesHintedSyncopatedTresilloAndSlapPopOffbeats) {
  for (BassPattern pattern :
       {BassPattern::Syncopated, BassPattern::Tresillo, BassPattern::SlapPop}) {
    MidiTrack track;
    track.addNote(NoteEventBuilder::create(0, TICK_EIGHTH, 48, 100));
    track.addNote(NoteEventBuilder::create(TICK_EIGHTH, TICK_EIGHTH, 55, 80));
    track.addNote(NoteEventBuilder::create(TICK_QUARTER, TICK_EIGHTH, 48, 100));

    Section section;
    section.type = SectionType::A;
    section.start_tick = 0;
    section.bars = 1;
    section.density_percent = 60;
    section.bass_style_hint = static_cast<uint8_t>(pattern) + 1;

    applyDensityAdjustment(track, section);

    bool kept_offbeat = false;
    for (const auto& note : track.notes()) {
      if (note.start_tick == TICK_EIGHTH) {
        kept_offbeat = true;
        break;
      }
    }

    EXPECT_TRUE(kept_offbeat) << "Pattern " << static_cast<int>(pattern)
                              << " should preserve its offbeat identity";
  }
}

// A syncopated pattern reached by any route -- a genre table, a riff policy, a
// paradigm adjustment -- keeps its off-beats. Only a blueprint sets a hint, so
// a rule that reads the hint exempts none of the others.
TEST(BassDensityAdjustmentTest, PreservesOffbeatsOfAnUnhintedGeneratedPattern) {
  MidiTrack track;
  track.addNote(NoteEventBuilder::create(0, TICK_EIGHTH, 48, 100));
  track.addNote(NoteEventBuilder::create(TICK_EIGHTH, TICK_EIGHTH, 55, 80));

  Section section;
  section.type = SectionType::A;
  section.start_tick = 0;
  section.bars = 1;
  section.density_percent = 60;
  ASSERT_EQ(section.bass_style_hint, 0);

  std::vector<BassSectionPattern> section_patterns = {{0, TICKS_PER_BAR, BassPattern::Tresillo}};
  applyDensityAdjustment(track, section, section_patterns);

  ASSERT_EQ(track.notes().size(), 2u);
  EXPECT_EQ(track.notes()[1].start_tick, TICK_EIGHTH);
}

// The section played what the record says, whatever the hint asked for.
TEST(BassDensityAdjustmentTest, GeneratedPatternOverridesTheStyleHint) {
  MidiTrack track;
  track.addNote(NoteEventBuilder::create(0, TICK_EIGHTH, 48, 100));
  track.addNote(NoteEventBuilder::create(TICK_EIGHTH, TICK_EIGHTH, 55, 80));

  Section section;
  section.type = SectionType::A;
  section.start_tick = 0;
  section.bars = 1;
  section.density_percent = 60;
  section.bass_style_hint = static_cast<uint8_t>(BassPattern::Syncopated) + 1;

  std::vector<BassSectionPattern> section_patterns = {{0, TICKS_PER_BAR, BassPattern::RootFifth}};
  applyDensityAdjustment(track, section, section_patterns);

  ASSERT_EQ(track.notes().size(), 1u);
  EXPECT_EQ(track.notes()[0].start_tick, 0);
}

// A record for a different section says nothing about this one.
TEST(BassDensityAdjustmentTest, ReadsTheRecordCoveringThisSection) {
  MidiTrack track;
  track.addNote(NoteEventBuilder::create(TICKS_PER_BAR, TICK_EIGHTH, 48, 100));
  track.addNote(NoteEventBuilder::create(TICKS_PER_BAR + TICK_EIGHTH, TICK_EIGHTH, 55, 80));

  Section section;
  section.type = SectionType::A;
  section.start_tick = TICKS_PER_BAR;
  section.bars = 1;
  section.density_percent = 60;

  std::vector<BassSectionPattern> section_patterns = {
      {0, TICKS_PER_BAR, BassPattern::Tresillo},
      {TICKS_PER_BAR, 2 * TICKS_PER_BAR, BassPattern::Driving}};
  applyDensityAdjustment(track, section, section_patterns);

  ASSERT_EQ(track.notes().size(), 1u);
  EXPECT_EQ(track.notes()[0].start_tick, TICKS_PER_BAR);
}

TEST(BassDensityAdjustmentTest, LowDensityStillThinsUnhintedOffbeats) {
  MidiTrack track;
  track.addNote(NoteEventBuilder::create(0, TICK_EIGHTH, 48, 100));
  track.addNote(NoteEventBuilder::create(TICK_EIGHTH, TICK_EIGHTH, 55, 80));

  Section section;
  section.type = SectionType::A;
  section.start_tick = 0;
  section.bars = 1;
  section.density_percent = 60;

  applyDensityAdjustment(track, section);

  ASSERT_EQ(track.notes().size(), 1u);
  EXPECT_EQ(track.notes()[0].start_tick, 0);
}

// ============================================================================
// Part 1: Diatonic Tests (from bass_diatonic_test.cpp)
// ============================================================================

// Every bass note outside C major, whatever the reason.
std::vector<std::pair<Tick, uint8_t>> findNonDiatonicNotes(const MidiTrack& track) {
  std::vector<std::pair<Tick, uint8_t>> non_diatonic;
  for (const auto& note : track.notes()) {
    if (!isDiatonic(note.note)) {
      non_diatonic.push_back({note.start_tick, note.note});
    }
  }
  return non_diatonic;
}

// Bass notes outside C major that the chord sounding at that tick does not
// contain.
//
// The bass is not unconditionally diatonic: a secondary dominant or a borrowed
// chord on the shared timeline raises or lowers one of its tones, and the bass
// voicing that chord sounds that tone. The raised third of a secondary dominant
// is its leading tone, which is what makes it function, so a bass note on it is
// the chord rather than a defect. What must not happen is a non-diatonic bass
// note at a tick whose chord does not contain it - the accidental chromaticism
// these tests were written to catch.
std::vector<std::pair<Tick, uint8_t>> findUnexplainedNonDiatonicNotes(
    const MidiTrack& track, const IHarmonyContext& harmony) {
  std::vector<std::pair<Tick, uint8_t>> unexplained;
  for (const auto& [tick, pitch] : findNonDiatonicNotes(track)) {
    const ChordTones tones = harmony.getChordTonesAt(tick);
    if (std::find(tones.begin(), tones.end(), pitch % 12) == tones.end()) {
      unexplained.push_back({tick, pitch});
    }
  }
  return unexplained;
}

class BassDiatonicTest : public test::GeneratorTestFixture {
 protected:
  void SetUp() override {
    GeneratorTestFixture::SetUp();
    params_.drums_enabled = true;
    params_.vocal_high = 79;
  }
};

// Test: All bass notes must be diatonic to C major (strict)
TEST_F(BassDiatonicTest, AllBassNotesAreDiatonic) {
  std::vector<uint32_t> test_seeds = {42, 12345, 67890, 99999, 1670804638};

  for (uint32_t seed : test_seeds) {
    params_.seed = seed;
    Generator gen;
    gen.generate(params_);

    const auto& track = gen.getSong().bass();
    auto non_diatonic = findUnexplainedNonDiatonicNotes(track, gen.getHarmonyContext());

    EXPECT_TRUE(non_diatonic.empty())
        << "Seed " << seed << " produced " << non_diatonic.size()
        << " non-diatonic bass notes. First: tick="
        << (non_diatonic.empty() ? 0 : non_diatonic[0].first)
        << " pitch=" << (non_diatonic.empty() ? 0 : non_diatonic[0].second) << " ("
        << (non_diatonic.empty() ? "" : pitchClassName(non_diatonic[0].second)) << ")";
  }
}

// Test: vii chord (B) generates F (dim5), not F# (perfect 5th)
TEST_F(BassDiatonicTest, ViiChordUsesDiminishedFifth) {
  for (uint8_t chord_id = 0; chord_id < 20; ++chord_id) {
    const auto& prog = getChordProgression(chord_id);

    bool has_vii = false;
    for (int idx = 0; idx < prog.length; ++idx) {
      if (prog.degrees[idx] == 6) {
        has_vii = true;
        break;
      }
    }

    if (!has_vii) continue;

    params_.chord_id = chord_id;
    params_.seed = 42;

    Generator gen;
    gen.generate(params_);

    const auto& track = gen.getSong().bass();
    auto non_diatonic = findUnexplainedNonDiatonicNotes(track, gen.getHarmonyContext());

    int fsharp_count = 0;
    for (const auto& [tick, pitch] : non_diatonic) {
      if (pitch % 12 == 6) {
        fsharp_count++;
      }
    }

    EXPECT_EQ(fsharp_count, 0) << "Chord progression " << static_cast<int>(chord_id)
                               << " (contains vii) produced F# in bass. "
                               << "vii chord should use diminished 5th (F), not perfect 5th (F#)";
  }
}

// Test: Approach notes are diatonic across all moods
TEST_F(BassDiatonicTest, ApproachNotesAreDiatonicAllMoods) {
  std::vector<Mood> test_moods = {Mood::StraightPop, Mood::ElectroPop, Mood::Ballad,
                                  Mood::LightRock, Mood::EnergeticDance};
  params_.chord_id = 0;

  for (Mood mood : test_moods) {
    params_.mood = mood;
    params_.seed = 12345;

    Generator gen;
    gen.generate(params_);

    const auto& track = gen.getSong().bass();
    auto non_diatonic = findUnexplainedNonDiatonicNotes(track, gen.getHarmonyContext());

    EXPECT_TRUE(non_diatonic.empty())
        << "Mood " << static_cast<int>(mood) << " produced " << non_diatonic.size()
        << " non-diatonic bass notes. First: "
        << (non_diatonic.empty() ? "none" : pitchClassName(non_diatonic[0].second));
  }
}

// Test: Walking bass uses chromatic approach on beat 4 for small intervals.
TEST_F(BassDiatonicTest, WalkingBassPatternIsDiatonic) {
  params_.mood = Mood::CityPop;
  params_.chord_id = 19;
  params_.skip_vocal = true;

  constexpr Tick TICKS_PER_BAR_LOCAL = 1920;
  constexpr Tick BEAT4_OFFSET = 1440;

  for (uint32_t seed = 1; seed <= 10; ++seed) {
    params_.seed = seed;

    Generator gen;
    gen.generate(params_);

    const auto& track = gen.getSong().bass();
    size_t total_notes = track.notes().size();
    EXPECT_GT(total_notes, 0u) << "Walking bass should generate notes";

    size_t non_diatonic_other = 0;
    for (const auto& note : track.notes()) {
      if (!isDiatonic(note.note)) {
        Tick beat_offset = note.start_tick % TICKS_PER_BAR_LOCAL;
        if (beat_offset != BEAT4_OFFSET) {
          non_diatonic_other++;
        }
      }
    }

    if (total_notes > 0) {
      double chromatic_ratio =
          static_cast<double>(non_diatonic_other) / static_cast<double>(total_notes);
      EXPECT_LT(chromatic_ratio, 0.10)
          << "CityPop seed " << seed << ": too many non-diatonic notes on beats 1-3 ("
          << non_diatonic_other << "/" << total_notes << " = " << (chromatic_ratio * 100.0) << "%)";
    }
  }
}

// Test: Syncopated pattern with approach notes is diatonic
TEST_F(BassDiatonicTest, SyncopatedApproachNotesAreDiatonic) {
  params_.mood = Mood::EnergeticDance;

  for (uint32_t seed = 100; seed <= 110; ++seed) {
    params_.seed = seed;

    Generator gen;
    gen.generate(params_);

    const auto& track = gen.getSong().bass();
    auto non_diatonic = findUnexplainedNonDiatonicNotes(track, gen.getHarmonyContext());

    EXPECT_TRUE(non_diatonic.empty())
        << "EnergeticDance seed " << seed << " produced non-diatonic bass notes";
  }
}

// Test: Driving pattern is diatonic
TEST_F(BassDiatonicTest, DrivingPatternIsDiatonic) {
  params_.mood = Mood::LightRock;

  for (uint32_t seed = 200; seed <= 210; ++seed) {
    params_.seed = seed;

    Generator gen;
    gen.generate(params_);

    const auto& track = gen.getSong().bass();
    auto non_diatonic = findUnexplainedNonDiatonicNotes(track, gen.getHarmonyContext());

    EXPECT_TRUE(non_diatonic.empty())
        << "LightRock seed " << seed << " produced non-diatonic bass notes";
  }
}

// Test: Regression for original bug case (seed 1670804638)
TEST_F(BassDiatonicTest, RegressionOriginalBugCase) {
  params_.seed = 1670804638;
  params_.chord_id = 0;
  params_.mood = static_cast<Mood>(14);
  params_.structure = static_cast<StructurePattern>(5);
  params_.bpm = 150;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().bass();
  auto non_diatonic = findUnexplainedNonDiatonicNotes(track, gen.getHarmonyContext());

  EXPECT_TRUE(non_diatonic.empty()) << "Original bug case (seed 1670804638) still produces "
                                    << non_diatonic.size() << " non-diatonic bass notes";

  for (const auto& [tick, pitch] : non_diatonic) {
    EXPECT_NE(pitch % 12, 6) << "Found F# at tick " << tick << " - this was the original bug";
  }
}

// Test: Diatonic chord progressions produce diatonic bass
TEST_F(BassDiatonicTest, DiatonicChordProgressionsProduceDiatonicBass) {
  std::vector<uint8_t> diatonic_progressions = {0, 1,  2,  3,  4,  5,  6,  7, 8,
                                                9, 10, 13, 14, 16, 17, 18, 19};

  for (uint8_t chord_id : diatonic_progressions) {
    params_.chord_id = chord_id;
    params_.seed = 42;

    Generator gen;
    gen.generate(params_);

    const auto& track = gen.getSong().bass();
    auto non_diatonic = findUnexplainedNonDiatonicNotes(track, gen.getHarmonyContext());

    EXPECT_LE(non_diatonic.size(), 2u)
        << "Chord progression " << static_cast<int>(chord_id) << " produced " << non_diatonic.size()
        << " non-diatonic bass notes";
  }
}

// Test: Borrowed chord progressions correctly use non-diatonic roots
TEST_F(BassDiatonicTest, BorrowedChordProgressionsUseCorrectRoots) {
  const std::map<uint8_t, std::set<int>> borrowed_progressions = {
      {11, {10}},        // bVII
      {12, {10}},        // bVII
      {15, {3, 8, 10}},  // bVI, bVII chord tones
      {21, {1, 8}},      // bII root, iv minor third color
  };

  for (const auto& [chord_id, expected_pitch_classes] : borrowed_progressions) {
    params_.chord_id = chord_id;
    params_.seed = 42;

    Generator gen;
    gen.generate(params_);

    const auto& track = gen.getSong().bass();
    auto non_diatonic = findNonDiatonicNotes(track);

    EXPECT_FALSE(non_diatonic.empty()) << "Borrowed progression " << static_cast<int>(chord_id)
                                       << " should produce non-diatonic bass color";

    for (const auto& [tick, pitch] : non_diatonic) {
      EXPECT_TRUE(expected_pitch_classes.count(pitch % 12) > 0)
          << "Borrowed chord progression " << static_cast<int>(chord_id)
          << " produced unexpected non-diatonic pitch class " << (pitch % 12);
    }
  }
}

// Test: Bass notes on beat 1 must be chord tones
TEST_F(BassDiatonicTest, BassOnBeatOneMustBeChordTone) {
  const Tick BEAT_THRESHOLD = TICKS_PER_BEAT / 4;

  std::vector<Mood> test_moods = {Mood::StraightPop, Mood::ElectroPop, Mood::AnimeHighEnergy,
                                  Mood::IdolPop, Mood::CityPop};

  for (Mood mood : test_moods) {
    params_.mood = mood;
    params_.composition_style = CompositionStyle::MelodyLead;

    for (uint32_t seed = 1; seed <= 5; ++seed) {
      params_.seed = seed;

      Generator gen;
      gen.generate(params_);

      const auto& song = gen.getSong();
      const auto& bass_track = song.bass();
      const auto& progression = getChordProgression(params_.chord_id);

      ChordProgressionTracker tracker;
      tracker.initialize(song.arrangement(), progression, mood);

      int non_chord_tone_count = 0;
      std::vector<std::string> issues;

      for (const auto& note : bass_track.notes()) {
        Tick beat_position = note.start_tick % TICKS_PER_BAR;
        if (beat_position > BEAT_THRESHOLD) continue;

        uint32_t bar = note.start_tick / TICKS_PER_BAR;

        int8_t degree = tracker.getChordDegreeAt(note.start_tick);

        auto chord_tones = getChordTonePitchClasses(degree);
        int pitch_class = note.note % 12;

        bool is_chord_tone = false;
        for (int ct_pitch : chord_tones) {
          if (ct_pitch == pitch_class) {
            is_chord_tone = true;
            break;
          }
        }

        if (!is_chord_tone) {
          // Also accept chord tones from the generator's actual harmony context,
          // which may differ from the tracker at chord boundaries
          const auto& harmony = gen.getHarmonyContext();
          int8_t gen_degree = harmony.getChordDegreeAt(note.start_tick);
          if (gen_degree != degree) {
            auto gen_chord_tones = getChordTonePitchClasses(gen_degree);
            for (int ct_pitch : gen_chord_tones) {
              if (ct_pitch == pitch_class) {
                is_chord_tone = true;
                break;
              }
            }
          }

          if (!is_chord_tone) {
            non_chord_tone_count++;
            if (issues.size() < 3) {
              std::string issue = "Bar " + std::to_string(bar) +
                                  ": bass=" + pitchClassName(pitch_class) +
                                  " not in chord (degree " + std::to_string(degree) +
                                  ", gen_degree=" + std::to_string(gen_degree) + ")";
              issues.push_back(issue);
            }
          }
        }
      }

      EXPECT_EQ(non_chord_tone_count, 0)
          << "Mood " << static_cast<int>(mood) << " seed " << seed << ": " << non_chord_tone_count
          << " bass notes on beat 1 are non-chord tones. " << (issues.empty() ? "" : issues[0]);
    }
  }
}

// ============================================================================
// Part 2: Music Theory Tests (from bass_music_theory_test.cpp)
// ============================================================================

// --- Chord Function Approach Tests ---

class ChordFunctionApproachTest : public ::testing::Test {
 protected:
  bool isDiatonicPC(int pitch_class) {
    int pc = ((pitch_class % 12) + 12) % 12;
    return pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 11;
  }
};

TEST_F(ChordFunctionApproachTest, TonicChordFunctionClassification) {
  std::vector<int8_t> tonic_degrees = {0, 2, 5};
  for (int8_t deg : tonic_degrees) {
    EXPECT_TRUE(deg == 0 || deg == 2 || deg == 5)
        << "Degree " << (int)deg << " should be tonic function";
  }
}

TEST_F(ChordFunctionApproachTest, DominantChordFunctionClassification) {
  std::vector<int8_t> dominant_degrees = {4, 6};
  for (int8_t deg : dominant_degrees) {
    EXPECT_TRUE(deg == 4 || deg == 6) << "Degree " << (int)deg << " should be dominant function";
  }
}

TEST_F(ChordFunctionApproachTest, SubdominantChordFunctionClassification) {
  std::vector<int8_t> subdominant_degrees = {1, 3};
  for (int8_t deg : subdominant_degrees) {
    EXPECT_TRUE(deg == 1 || deg == 3) << "Degree " << (int)deg << " should be subdominant function";
  }
}

TEST_F(ChordFunctionApproachTest, ApproachCandidateSurvivesAllRootsAndDegrees) {
  for (int root_pc = 0; root_pc < 12; ++root_pc) {
    uint8_t next_root = static_cast<uint8_t>(40 + root_pc);
    uint8_t current_root = static_cast<uint8_t>(next_root + 7);
    if (current_root > BASS_HIGH) {
      current_root = static_cast<uint8_t>(current_root - 12);
    }

    for (int8_t degree = 0; degree < 7; ++degree) {
      uint8_t approach = selectBassApproachNote(current_root, next_root, degree);

      EXPECT_GE(approach, BASS_LOW) << "root_pc=" << root_pc << " degree=" << (int)degree;
      EXPECT_LE(approach, BASS_HIGH) << "root_pc=" << root_pc << " degree=" << (int)degree;
      EXPECT_NE(approach, next_root) << "Approach collapsed to target root for root_pc=" << root_pc
                                     << " degree=" << (int)degree;
      EXPECT_NE(approach, static_cast<uint8_t>(next_root - 12))
          << "Approach collapsed to octave-below fallback for root_pc=" << root_pc
          << " degree=" << (int)degree;
    }
  }
}

TEST_F(ChordFunctionApproachTest, OctaveDeviceUsesPlayableDisplacementAcrossBassRange) {
  for (uint8_t root = BASS_LOW; root <= BASS_HIGH; ++root) {
    uint8_t octave = selectBassOctaveNote(root);

    EXPECT_GE(octave, BASS_LOW) << "root=" << (int)root;
    EXPECT_LE(octave, BASS_HIGH) << "root=" << (int)root;
    EXPECT_EQ(octave % 12, root % 12) << "root=" << (int)root;
    EXPECT_NE(octave, root) << "Octave device collapsed to root=" << (int)root;
    EXPECT_EQ(std::abs(static_cast<int>(octave) - static_cast<int>(root)), 12)
        << "root=" << (int)root;
  }
}

TEST_F(ChordFunctionApproachTest, DiatonicStepWrapsInsteadOfClampingAtRangeEdges) {
  EXPECT_EQ(selectNextBassDiatonic(55, +1), 45)  // G3 -> A2
      << "Upper-range walking step should wrap down instead of clamping to G3";
  EXPECT_EQ(selectNextBassDiatonic(53, +1), 55)  // F3 -> G3
      << "In-range step should remain in the current octave";
  EXPECT_EQ(selectNextBassDiatonic(BASS_LOW, -1), 38)  // E1 -> D2
      << "Lower-range walking step should wrap up instead of clamping to E1";
}

TEST_F(ChordFunctionApproachTest, WalkingBassUpperRootsKeepStepwiseMotion) {
  std::vector<uint8_t> f_walk = {53, selectNextBassDiatonic(53, +1),
                                 selectNextBassDiatonic(selectNextBassDiatonic(53, +1), +1)};
  std::vector<uint8_t> g_walk = {55, selectNextBassDiatonic(55, +1),
                                 selectNextBassDiatonic(selectNextBassDiatonic(55, +1), +1)};

  EXPECT_EQ((std::set<uint8_t>(f_walk.begin(), f_walk.end()).size()), 3u)
      << "IV walking line should not collapse to a single clamped pitch";
  EXPECT_EQ((std::set<uint8_t>(g_walk.begin(), g_walk.end()).size()), 3u)
      << "V walking line should not collapse to a single clamped pitch";
}

TEST_F(ChordFunctionApproachTest, RnBDiatonicThirdWrapsInsteadOfClampingToRootOrHighLimit) {
  uint8_t f_third = selectBassDiatonicThird(53);  // F3 -> A2
  uint8_t g_third = selectBassDiatonicThird(55);  // G3 -> B2

  EXPECT_EQ(f_third, 45);
  EXPECT_EQ(g_third, 47);
  EXPECT_EQ(f_third % 12, 9);
  EXPECT_EQ(g_third % 12, 11);
  EXPECT_NE(f_third, 53);
  EXPECT_NE(g_third, 55);
}

TEST_F(ChordFunctionApproachTest, MotionAdjustmentMovesToNearestChordTone) {
  constexpr uint8_t kC3 = 48;
  constexpr uint8_t kVocalC5 = 72;
  constexpr int8_t kTonicDegree = 0;

  EXPECT_EQ(adjustPitchForMotion(kC3, MotionType::Similar, +1, kVocalC5, kTonicDegree), 52)
      << "Similar upward motion should move C3 to E3 instead of rejecting a semitone step";
  EXPECT_EQ(adjustPitchForMotion(kC3, MotionType::Contrary, +1, kVocalC5, kTonicDegree), 43)
      << "Contrary motion against upward vocal should move C3 down to G2";
  EXPECT_EQ(adjustPitchForMotion(kC3, MotionType::Similar, -1, kVocalC5, kTonicDegree), 43)
      << "Similar downward motion should move C3 to G2";
  EXPECT_EQ(adjustPitchForMotion(kC3, MotionType::Contrary, -1, kVocalC5, kTonicDegree), 52)
      << "Contrary motion against downward vocal should move C3 up to E3";
}

TEST_F(ChordFunctionApproachTest, HighVocalDensityStillSimplifiesNonPeakVerse) {
  GeneratorParams params;
  params.mood = Mood::EnergeticDance;

  Section verse;
  verse.type = SectionType::A;
  verse.peak_level = PeakLevel::None;
  verse.backing_density = BackingDensity::Normal;

  std::mt19937 rng(7);
  EXPECT_EQ(selectPatternForVocalDensity(0.9f, verse, params, rng), BassPattern::WholeNote);
}

TEST_F(ChordFunctionApproachTest, HighVocalDensityDoesNotCollapsePeakChorusToWholeNote) {
  GeneratorParams params;
  params.mood = Mood::EnergeticDance;

  Section chorus;
  chorus.type = SectionType::Chorus;
  chorus.peak_level = PeakLevel::Max;
  chorus.backing_density = BackingDensity::Thick;

  std::mt19937 rng(7);
  BassPattern pattern = selectPatternForVocalDensity(0.9f, chorus, params, rng);

  EXPECT_NE(pattern, BassPattern::WholeNote)
      << "Peak chorus should keep an active bass pattern despite dense vocal coverage";
}

TEST_F(ChordFunctionApproachTest, PeakLevelPromotionRaisesSparseBassPatterns) {
  EXPECT_EQ(promoteBassPatternForPeakLevel(BassPattern::WholeNote, PeakLevel::Medium),
            BassPattern::RootFifth);
  EXPECT_EQ(promoteBassPatternForPeakLevel(BassPattern::WholeNote, PeakLevel::Max),
            BassPattern::Syncopated);
}

// --- Chromatic Approach Tests ---

class ChromaticApproachTest : public ::testing::Test {};

TEST_F(ChromaticApproachTest, ChromaticApproachIsSemitoneBelow) {
  EXPECT_EQ((48 - 1) % 12, 11);  // C -> B
  EXPECT_EQ((43 - 1) % 12, 6);   // G -> F#
  EXPECT_EQ((50 - 1) % 12, 1);   // D -> C#
}

TEST_F(ChromaticApproachTest, ChromaticApproachPitchClasses) {
  struct TestCase {
    int target_pc;
    int expected_approach_pc;
  };

  std::vector<TestCase> cases = {
      {0, 11}, {2, 1}, {4, 3}, {5, 4}, {7, 6}, {9, 8}, {11, 10},
  };

  for (const auto& test_case : cases) {
    int approach = (test_case.target_pc - 1 + 12) % 12;
    EXPECT_EQ(approach, test_case.expected_approach_pc)
        << "Target PC " << test_case.target_pc << " should have approach PC "
        << test_case.expected_approach_pc;
  }
}

// --- Seventh Chord Extension Tests ---

class SeventhChordExtensionTest : public ::testing::Test {};

TEST_F(SeventhChordExtensionTest, MajorChordSeventhIsMajor7th) {
  EXPECT_EQ((0 + 11) % 12, 11);  // CMaj7 -> B
  EXPECT_EQ((5 + 11) % 12, 4);   // FMaj7 -> E
  EXPECT_EQ((7 + 11) % 12, 6);   // GMaj7 -> F#
}

TEST_F(SeventhChordExtensionTest, MinorChordSeventhIsMinor7th) {
  EXPECT_EQ((2 + 10) % 12, 0);  // Dm7 -> C
  EXPECT_EQ((4 + 10) % 12, 2);  // Em7 -> D
  EXPECT_EQ((9 + 10) % 12, 7);  // Am7 -> G
}

TEST_F(SeventhChordExtensionTest, SeventhNotesAreDiatonic) {
  auto isDiatonicCheck = [](int pc) {
    return pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 11;
  };

  EXPECT_TRUE(isDiatonicCheck(11));  // CMaj7: B
  EXPECT_TRUE(isDiatonicCheck(0));   // Dm7: C
  EXPECT_TRUE(isDiatonicCheck(2));   // Em7: D
  EXPECT_TRUE(isDiatonicCheck(4));   // FMaj7: E
  EXPECT_TRUE(isDiatonicCheck(5));   // G7: F
  EXPECT_TRUE(isDiatonicCheck(7));   // Am7: G
  EXPECT_TRUE(isDiatonicCheck(9));   // Bm7b5: A
}

// --- Voice Leading Tests ---

class VoiceLeadingTest : public ::testing::Test {};

TEST_F(VoiceLeadingTest, WeightedDistancePrinciple) {
  int bass_movement = 2;
  int tenor_movement = 2;
  int soprano_movement = 1;

  int unweighted = bass_movement + tenor_movement + soprano_movement;
  int weighted = bass_movement * 2 + tenor_movement * 1 + soprano_movement * 2;

  EXPECT_EQ(unweighted, 5);
  EXPECT_EQ(weighted, 8);
  EXPECT_GT(weighted, unweighted);
}

// --- Avoid Note Tests ---

class AvoidNoteTest : public ::testing::Test {};

TEST_F(AvoidNoteTest, Minor2ndWithAnyChordToneIsAvoid) {
  int f_pc = 5;
  int e_pc = 4;
  int interval = std::abs(f_pc - e_pc);
  if (interval > 6) interval = 12 - interval;
  EXPECT_EQ(interval, 1);
}

TEST_F(AvoidNoteTest, Minor2ndWithRootOnly) {
  int f_pc = 5;
  int c_pc = 0;
  int interval = std::abs(f_pc - c_pc);
  if (interval > 6) interval = 12 - interval;
  EXPECT_EQ(interval, 5);
}

TEST_F(AvoidNoteTest, TritoneWithRootIsAvoid) {
  int fsharp_pc = 6;
  int c_pc = 0;
  int interval = std::abs(fsharp_pc - c_pc);
  if (interval > 6) interval = 12 - interval;
  EXPECT_EQ(interval, 6);
}

// --- Walking Bass Approach Tests ---

class WalkingBassApproachTest : public ::testing::Test {};

TEST_F(WalkingBassApproachTest, ChromaticApproachPreferredForSmallIntervals) {
  struct TestCase {
    uint8_t current_root;
    uint8_t next_root;
    bool expect_chromatic;
  };

  std::vector<TestCase> cases = {
      {48, 50, true},   // C -> D: M2
      {50, 48, true},   // D -> C: M2
      {48, 51, true},   // C -> Eb: m3
      {45, 48, true},   // A -> C: m3
      {48, 53, false},  // C -> F: P4
      {48, 55, false},  // C -> G: P5
      {48, 48, false},  // C -> C: unison
      {48, 49, false},  // C -> C#: m2
  };

  for (const auto& test_case : cases) {
    int interval =
        std::abs(static_cast<int>(test_case.next_root) - static_cast<int>(test_case.current_root));
    interval = interval % 12;
    bool use_chromatic = (interval >= 2 && interval <= 3);
    EXPECT_EQ(use_chromatic, test_case.expect_chromatic)
        << "Current root=" << static_cast<int>(test_case.current_root)
        << " Next root=" << static_cast<int>(test_case.next_root) << " Interval=" << interval;
  }
}

TEST_F(WalkingBassApproachTest, ChromaticApproachIsSemitoneBelow) {
  uint8_t target_d = 50;
  int chromatic = static_cast<int>(target_d) - 1;
  EXPECT_EQ(chromatic, 49);

  uint8_t target_c = 48;
  chromatic = static_cast<int>(target_c) - 1;
  EXPECT_EQ(chromatic, 47);
}

TEST_F(WalkingBassApproachTest, OctaveNormalizationHandlesLargeIntervals) {
  int interval = std::abs(50 - 36);
  EXPECT_EQ(interval, 14);
  interval = interval % 12;
  EXPECT_EQ(interval, 2);
  EXPECT_TRUE(interval >= 2 && interval <= 3);
}

// ============================================================================
// Part 3: Chord-Tone Diagnostic Tests (from bass_chord_tone_test.cpp)
// ============================================================================

// Non-chord-tone detail for diagnostics
struct NonChordToneInfo {
  Tick tick;
  uint32_t bar;
  uint32_t beat;
  Tick beat_offset;
  uint8_t pitch;
  int pitch_class;
  int8_t chord_degree;
  std::vector<int> chord_tones;
  std::vector<uint8_t> motif_pitches_at_tick;
  std::vector<uint8_t> vocal_pitches_at_tick;
  bool is_approach_note;
  bool is_strong_beat;
};

class BassChordToneTest : public test::GeneratorTestFixture {
 protected:
  void SetUp() override {
    GeneratorTestFixture::SetUp();
    params_.seed = 42;
    params_.blueprint_id = 1;  // RhythmLock (RhythmSync paradigm)
    params_.mood = Mood::StraightPop;
    params_.drums_enabled = true;
    params_.vocal_high = 79;
    params_.bpm = 0;
  }

  /// The chord a scale degree names is the one the song was planned from, and
  /// a secondary dominant or a suspension registered on the timeline states a
  /// different one. Judging by the degree therefore counts the altered chord's
  /// own third as a note foreign to it, which is the opposite of the truth.
  bool isChordTone(int pitch_class, const ChordTones& sounding) {
    int normalized_pc = ((pitch_class % 12) + 12) % 12;
    for (uint8_t i = 0; i < sounding.count; ++i) {
      if (sounding.pitch_classes[i] == normalized_pc) return true;
    }
    return false;
  }

  std::vector<uint8_t> findSoundingNotes(const MidiTrack& track, Tick tick) {
    std::vector<uint8_t> pitches;
    for (const auto& note : track.notes()) {
      if (note.start_tick <= tick && note.start_tick + note.duration > tick) {
        pitches.push_back(note.note);
      }
    }
    return pitches;
  }

  std::vector<NonChordToneInfo> findNonChordToneNotes(const Song& song,
                                                      const IHarmonyContext& harmony) {
    std::vector<NonChordToneInfo> results;
    const auto& bass_track = song.bass();
    const auto& motif_track = song.motif();
    const auto& vocal_track = song.vocal();

    for (const auto& note : bass_track.notes()) {
      int8_t degree = harmony.getChordDegreeAt(note.start_tick);
      const ChordTones sounding = harmony.getChordTonesAt(note.start_tick);
      int pc = note.note % 12;

      if (!isChordTone(pc, sounding)) {
        NonChordToneInfo info;
        info.tick = note.start_tick;
        info.bar = note.start_tick / TICKS_PER_BAR;
        info.beat_offset = note.start_tick % TICKS_PER_BAR;
        info.beat = info.beat_offset / TICKS_PER_BEAT + 1;
        info.pitch = note.note;
        info.pitch_class = pc;
        info.chord_degree = degree;
        info.chord_tones.assign(sounding.begin(), sounding.end());
        info.motif_pitches_at_tick = findSoundingNotes(motif_track, note.start_tick);
        info.vocal_pitches_at_tick = findSoundingNotes(vocal_track, note.start_tick);
        info.is_approach_note = (info.beat_offset >= 3 * TICKS_PER_BEAT);
        info.is_strong_beat = (info.beat == 1 || info.beat == 3);
        results.push_back(info);
      }
    }
    return results;
  }

  std::string formatDiagnostics(const std::vector<NonChordToneInfo>& infos) {
    std::ostringstream oss;
    oss << "\n=== Non-chord-tone bass notes (" << infos.size() << " total) ===\n";
    for (const auto& info : infos) {
      oss << "  Bar " << info.bar << " beat " << info.beat
          << (info.is_approach_note ? " [APPROACH]" : "")
          << (info.is_strong_beat ? " [STRONG]" : "") << " | tick=" << info.tick
          << " | bass=" << pitchToNoteName(info.pitch)
          << " (pc=" << pitchClassName(info.pitch_class) << ")"
          << " | chord=" << degreeName(info.chord_degree)
          << " tones=" << formatChordTones(info.chord_tones);

      if (!info.motif_pitches_at_tick.empty()) {
        oss << " | motif={";
        for (size_t idx = 0; idx < info.motif_pitches_at_tick.size(); ++idx) {
          if (idx > 0) oss << ",";
          oss << pitchToNoteName(info.motif_pitches_at_tick[idx]);
        }
        oss << "}";
      }
      if (!info.vocal_pitches_at_tick.empty()) {
        oss << " | vocal={";
        for (size_t idx = 0; idx < info.vocal_pitches_at_tick.size(); ++idx) {
          if (idx > 0) oss << ",";
          oss << pitchToNoteName(info.vocal_pitches_at_tick[idx]);
        }
        oss << "}";
      }
      oss << "\n";
    }
    return oss.str();
  }
};

TEST_F(BassChordToneTest, DiagnoseRhythmLockSeed42NonChordTones) {
  Generator gen;
  gen.generate(params_);

  const auto& song = gen.getSong();
  const auto& harmony = gen.getHarmonyContext();
  const auto& bass_track = song.bass();

  ASSERT_FALSE(bass_track.empty()) << "Bass track should not be empty";

  auto non_chord_tones = findNonChordToneNotes(song, harmony);

  size_t total_notes = bass_track.notes().size();

  int approach_count = 0;
  int strong_beat_nct = 0;
  int weak_beat_nct = 0;
  for (const auto& info : non_chord_tones) {
    if (info.is_approach_note) {
      approach_count++;
    } else if (info.is_strong_beat) {
      strong_beat_nct++;
    } else {
      weak_beat_nct++;
    }
  }

  std::string diag = formatDiagnostics(non_chord_tones);
  std::cout << diag;
  std::cout << "\nTotal bass notes: " << total_notes << "\n";
  std::cout << "Non-chord-tone total: " << non_chord_tones.size() << "\n";
  std::cout << "  Approach notes (beat 4): " << approach_count << " (acceptable)\n";
  std::cout << "  Strong beat (1,3): " << strong_beat_nct << " (problematic)\n";
  std::cout << "  Weak beat (2,4 non-approach): " << weak_beat_nct << " (concerning)\n";

  std::map<int8_t, int> degree_counts;
  for (const auto& info : non_chord_tones) {
    if (!info.is_approach_note) {
      degree_counts[info.chord_degree]++;
    }
  }
  if (!degree_counts.empty()) {
    std::cout << "\nNon-approach non-chord-tone count by chord degree:\n";
    for (const auto& [degree, count] : degree_counts) {
      std::cout << "  " << degreeName(degree) << ": " << count << "\n";
    }
  }

  int non_approach_nct = strong_beat_nct + weak_beat_nct;
  double non_approach_ratio =
      total_notes > 0 ? static_cast<double>(non_approach_nct) / total_notes : 0.0;

  EXPECT_LT(non_approach_ratio, 0.06)
      << "Non-approach non-chord-tone bass notes exceed 5%: " << non_approach_nct << "/"
      << total_notes << " (" << std::fixed << std::setprecision(1) << (non_approach_ratio * 100.0)
      << "%)" << diag;
}

TEST_F(BassChordToneTest, DiagnoseCollisionCandidatesOnFChord) {
  Generator gen;
  gen.generate(params_);

  const auto& song = gen.getSong();
  const auto& harmony = gen.getHarmonyContext();

  auto non_chord_tones = findNonChordToneNotes(song, harmony);

  int f_chord_issues = 0;
  for (const auto& info : non_chord_tones) {
    if (info.chord_degree != 3) continue;
    if (info.is_approach_note) continue;
    f_chord_issues++;

    std::cout << "\n=== F chord non-chord-tone at bar " << info.bar << " beat " << info.beat
              << " ===\n";
    std::cout << "Bass pitch: " << pitchToNoteName(info.pitch) << " (MIDI "
              << static_cast<int>(info.pitch) << ")\n";
    std::cout << "F chord tones: " << formatChordTones(info.chord_tones) << "\n";

    if (!info.motif_pitches_at_tick.empty()) {
      std::cout << "Motif sounding: ";
      for (auto pitch : info.motif_pitches_at_tick) {
        std::cout << pitchToNoteName(pitch) << "(" << static_cast<int>(pitch) << ") ";
      }
      std::cout << "\n";
    }
    if (!info.vocal_pitches_at_tick.empty()) {
      std::cout << "Vocal sounding: ";
      for (auto pitch : info.vocal_pitches_at_tick) {
        std::cout << pitchToNoteName(pitch) << "(" << static_cast<int>(pitch) << ") ";
      }
      std::cout << "\n";
    }

    uint8_t desired_root = 53;
    auto candidates =
        getSafePitchCandidates(harmony, desired_root, info.tick, TICKS_PER_BEAT, TrackRole::Bass,
                               BASS_LOW, BASS_HIGH, PitchPreference::PreferRootFifth, 10);

    std::cout << "\nCandidates for desired " << pitchToNoteName(desired_root) << " (MIDI "
              << static_cast<int>(desired_root) << "):\n";
    for (size_t idx = 0; idx < candidates.size(); ++idx) {
      const auto& cand = candidates[idx];
      bool cand_is_ct = false;
      for (int tone : info.chord_tones) {
        if (tone == (cand.pitch % 12)) {
          cand_is_ct = true;
          break;
        }
      }
      std::cout << "  [" << idx << "] " << pitchToNoteName(cand.pitch) << " (MIDI "
                << static_cast<int>(cand.pitch) << ")"
                << " ct=" << (cand_is_ct ? "Y" : "N")
                << " r5=" << (cand.is_root_or_fifth ? "Y" : "N")
                << " strat=" << collisionAvoidStrategyToString(cand.strategy)
                << " interval=" << static_cast<int>(cand.interval_from_desired)
                << " collider=" << trackRoleToString(cand.colliding_track) << "("
                << static_cast<int>(cand.colliding_pitch) << ")"
                << "\n";
    }

    uint8_t desired_fifth = 48;
    auto fifth_candidates =
        getSafePitchCandidates(harmony, desired_fifth, info.tick, TICKS_PER_BEAT, TrackRole::Bass,
                               BASS_LOW, BASS_HIGH, PitchPreference::PreferRootFifth, 5);

    if (!fifth_candidates.empty()) {
      std::cout << "\nCandidates for C3(48) as 5th of F:\n";
      for (size_t idx = 0; idx < fifth_candidates.size(); ++idx) {
        const auto& cand = fifth_candidates[idx];
        std::cout << "  [" << idx << "] " << pitchToNoteName(cand.pitch)
                  << " strat=" << collisionAvoidStrategyToString(cand.strategy)
                  << " safe=" << (cand.strategy == CollisionAvoidStrategy::None ? "YES" : "no")
                  << "\n";
      }
    }

    test::CollisionTestHelper collision_helper(harmony);
    auto snapshot = collision_helper.snapshotAt(info.tick, TICKS_PER_BEAT);
    std::cout << "\n" << test::CollisionTestHelper::formatSnapshot(snapshot);
  }

  std::cout << "\nTotal F chord non-approach non-chord-tone issues: " << f_chord_issues << "\n";
}

TEST_F(BassChordToneTest, RhythmLockNonChordToneRatioAcrossSeeds) {
  constexpr int NUM_SEEDS = 20;
  int total_notes_all = 0;
  int non_chord_tone_non_approach_all = 0;
  int worst_seed = -1;
  double worst_ratio = 0.0;

  for (int seed = 1; seed <= NUM_SEEDS; ++seed) {
    params_.seed = seed;
    Generator gen;
    gen.generate(params_);

    const auto& song = gen.getSong();
    const auto& harmony = gen.getHarmonyContext();
    const auto& bass_track = song.bass();

    size_t total = bass_track.notes().size();
    auto non_chord = findNonChordToneNotes(song, harmony);

    int non_approach = 0;
    for (const auto& info : non_chord) {
      if (!info.is_approach_note) non_approach++;
    }

    total_notes_all += static_cast<int>(total);
    non_chord_tone_non_approach_all += non_approach;

    double ratio = total > 0 ? static_cast<double>(non_approach) / total : 0.0;
    if (ratio > worst_ratio) {
      worst_ratio = ratio;
      worst_seed = seed;
    }
  }

  double overall_ratio =
      total_notes_all > 0 ? static_cast<double>(non_chord_tone_non_approach_all) / total_notes_all
                          : 0.0;

  std::cout << "\n=== RhythmLock bass chord-tone analysis (excluding approach notes) ===\n";
  std::cout << "Seeds tested: " << NUM_SEEDS << "\n";
  std::cout << "Total bass notes: " << total_notes_all << "\n";
  std::cout << "Non-approach non-chord-tone: " << non_chord_tone_non_approach_all << "\n";
  std::cout << "Overall ratio: " << std::fixed << std::setprecision(1) << (overall_ratio * 100.0)
            << "%\n";
  std::cout << "Worst seed: " << worst_seed << " (" << std::fixed << std::setprecision(1)
            << (worst_ratio * 100.0) << "%)\n";

  EXPECT_LT(overall_ratio, 0.05) << "Non-approach non-chord-tone ratio exceeds 5% across "
                                 << NUM_SEEDS << " seeds: " << non_chord_tone_non_approach_all
                                 << "/" << total_notes_all << " (" << std::fixed
                                 << std::setprecision(1) << (overall_ratio * 100.0) << "%)";
}

TEST_F(BassChordToneTest, CollisionAvoidanceShouldPreferChordTones) {
  Generator gen;
  gen.generate(params_);

  const auto& song = gen.getSong();
  const auto& harmony = gen.getHarmonyContext();

  auto non_chord_tones = findNonChordToneNotes(song, harmony);

  int candidate_analysis_count = 0;
  int chord_tone_preferred_count = 0;
  int non_chord_tone_preferred_count = 0;

  for (const auto& info : non_chord_tones) {
    if (info.is_approach_note) continue;

    uint8_t root_pitch = static_cast<uint8_t>(degreeToRoot(info.chord_degree, Key::C));
    while (root_pitch > BASS_HIGH) root_pitch -= 12;
    while (root_pitch < BASS_LOW) root_pitch += 12;

    auto candidates =
        getSafePitchCandidates(harmony, root_pitch, info.tick, TICKS_PER_BEAT, TrackRole::Bass,
                               BASS_LOW, BASS_HIGH, PitchPreference::PreferRootFifth, 10);

    if (candidates.empty()) continue;
    candidate_analysis_count++;

    const auto& top = candidates[0];
    int top_pc = top.pitch % 12;

    bool top_is_chord_tone = false;
    for (int tone : info.chord_tones) {
      if (tone == top_pc) {
        top_is_chord_tone = true;
        break;
      }
    }

    if (top_is_chord_tone) {
      chord_tone_preferred_count++;
    } else {
      non_chord_tone_preferred_count++;
      std::cout << "  NON-CHORD-TONE preferred at bar " << info.bar << " beat " << info.beat
                << ": top=" << pitchToNoteName(top.pitch)
                << " strat=" << collisionAvoidStrategyToString(top.strategy) << "\n";
      for (size_t idx = 0; idx < candidates.size() && idx < 5; ++idx) {
        const auto& cand = candidates[idx];
        std::cout << "    [" << idx << "] " << pitchToNoteName(cand.pitch)
                  << " ct=" << (cand.is_chord_tone ? "Y" : "N")
                  << " r5=" << (cand.is_root_or_fifth ? "Y" : "N")
                  << " strat=" << collisionAvoidStrategyToString(cand.strategy) << "\n";
      }
    }
  }

  std::cout << "\n=== Candidate ranking analysis ===\n";
  std::cout << "Positions analyzed: " << candidate_analysis_count << "\n";
  std::cout << "Chord tone preferred: " << chord_tone_preferred_count << "\n";
  std::cout << "Non-chord-tone preferred: " << non_chord_tone_preferred_count << "\n";

  if (candidate_analysis_count > 0) {
    double ct_ratio = static_cast<double>(chord_tone_preferred_count) / candidate_analysis_count;
    EXPECT_GT(ct_ratio, 0.8)
        << "Bass collision avoidance should prefer chord tones in >80% of cases.";
  }
}

TEST_F(BassChordToneTest, GOnFChordBars) {
  Generator gen;
  gen.generate(params_);

  const auto& song = gen.getSong();
  const auto& harmony = gen.getHarmonyContext();
  const auto& bass_notes = song.bass().notes();

  int g_on_f_chord = 0;
  int g_on_f_chord_strong = 0;
  int total_f_chord_notes = 0;

  for (const auto& note : bass_notes) {
    int8_t degree = harmony.getChordDegreeAt(note.start_tick);
    if (degree != 3) continue;

    total_f_chord_notes++;
    int pc = note.note % 12;
    Tick beat_off = note.start_tick % TICKS_PER_BAR;
    uint32_t beat = beat_off / TICKS_PER_BEAT + 1;

    if (pc == 7) {
      g_on_f_chord++;
      bool is_strong = (beat == 1 || beat == 3);
      if (is_strong) g_on_f_chord_strong++;

      uint32_t bar = note.start_tick / TICKS_PER_BAR;
      std::cout << "  G on F chord: bar " << bar << " beat " << beat
                << " pitch=" << pitchToNoteName(note.note) << (is_strong ? " [STRONG]" : "")
                << "\n";
    }
  }

  std::cout << "\nG notes on F chord: " << g_on_f_chord << " / " << total_f_chord_notes
            << " F-chord bass notes"
            << " (strong beat: " << g_on_f_chord_strong << ")\n";

  if (total_f_chord_notes > 0) {
    double g_ratio = static_cast<double>(g_on_f_chord) / total_f_chord_notes;
    EXPECT_LT(g_ratio, 0.25) << "G notes on F chord exceed 25%: " << g_on_f_chord << "/"
                             << total_f_chord_notes;
  }
}

TEST_F(BassChordToneTest, CompareNonChordToneRatesByBlueprint) {
  constexpr uint8_t MAX_BLUEPRINT = 8;
  constexpr uint32_t TEST_SEED = 42;

  std::cout << "\n=== Non-chord-tone rate by blueprint (seed " << TEST_SEED
            << ", excluding approach notes) ===\n";

  for (uint8_t bp_id = 0; bp_id <= MAX_BLUEPRINT; ++bp_id) {
    params_.seed = TEST_SEED;
    params_.blueprint_id = bp_id;

    Generator gen;
    gen.generate(params_);

    const auto& song = gen.getSong();
    const auto& harmony = gen.getHarmonyContext();
    const auto& bass_track = song.bass();

    size_t total = bass_track.notes().size();
    auto non_chord = findNonChordToneNotes(song, harmony);

    int non_approach = 0;
    for (const auto& info : non_chord) {
      if (!info.is_approach_note) non_approach++;
    }

    double ratio = total > 0 ? static_cast<double>(non_approach) / total : 0.0;

    std::cout << "  Blueprint " << static_cast<int>(bp_id) << ": " << non_approach << "/" << total
              << " (" << std::fixed << std::setprecision(1) << (ratio * 100.0)
              << "% non-chord-tone, excluding approach)\n";

    EXPECT_LT(ratio, 0.15) << "Blueprint " << static_cast<int>(bp_id)
                           << " has too many non-approach non-chord-tone bass notes: "
                           << non_approach << "/" << total;
  }
}

TEST_F(BassChordToneTest, IdentifyNonChordToneSourcePath) {
  Generator gen;
  gen.generate(params_);

  const auto& song = gen.getSong();
  const auto& harmony = gen.getHarmonyContext();

  auto non_chord_tones = findNonChordToneNotes(song, harmony);

  int from_safe_path = 0;
  int from_collision_path = 0;
  int from_approach = 0;

  for (const auto& info : non_chord_tones) {
    if (info.is_approach_note) {
      from_approach++;
      continue;
    }

    bool is_safe =
        harmony.isConsonantWithOtherTracks(info.pitch, info.tick, TICKS_PER_BEAT, TrackRole::Bass);

    uint8_t correct_root = static_cast<uint8_t>(degreeToRoot(info.chord_degree, Key::C));
    while (correct_root > BASS_HIGH) correct_root -= 12;
    while (correct_root < BASS_LOW) correct_root += 12;

    bool root_is_safe = harmony.isConsonantWithOtherTracks(correct_root, info.tick, TICKS_PER_BEAT,
                                                           TrackRole::Bass);

    if (is_safe) {
      from_safe_path++;
      std::cout << "  SAFE-BUT-WRONG: bar " << info.bar << " beat " << info.beat
                << " bass=" << pitchToNoteName(info.pitch) << " on "
                << degreeName(info.chord_degree) << " (root " << pitchToNoteName(correct_root)
                << " safe=" << (root_is_safe ? "yes" : "no") << ")\n";
    } else {
      from_collision_path++;
      std::cout << "  COLLISION-RESULT: bar " << info.bar << " beat " << info.beat
                << " bass=" << pitchToNoteName(info.pitch) << " on "
                << degreeName(info.chord_degree) << " (root " << pitchToNoteName(correct_root)
                << " safe=" << (root_is_safe ? "yes" : "no") << ")\n";
    }
  }

  std::cout << "\n=== Source path analysis ===\n";
  std::cout << "Approach notes (expected): " << from_approach << "\n";
  std::cout << "Safe but wrong pitch (pattern bug): " << from_safe_path << "\n";
  std::cout << "Collision avoidance result: " << from_collision_path << "\n";

  if (from_safe_path + from_collision_path > 0) {
    std::cout << "\nConclusion: ";
    if (from_safe_path > from_collision_path) {
      std::cout << "Bug is primarily in bass PATTERN generation "
                << "(wrong desired pitch before collision check).\n";
    } else {
      std::cout << "Bug is primarily in collision AVOIDANCE "
                << "(correct desired pitch, wrong resolution).\n";
    }
  }
}

}  // namespace
}  // namespace midisketch
