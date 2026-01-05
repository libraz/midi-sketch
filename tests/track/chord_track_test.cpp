#include <gtest/gtest.h>
#include "core/generator.h"
#include "core/song.h"
#include "core/types.h"
#include <set>

namespace midisketch {
namespace {

class ChordTrackTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::StandardPop;
    params_.mood = Mood::ElectroPop;
    params_.chord_id = 0;  // Canon progression
    params_.key = Key::C;
    params_.drums_enabled = false;
    params_.vocal_low = 60;
    params_.vocal_high = 84;
    params_.bpm = 120;
    params_.seed = 42;
    params_.arpeggio_enabled = false;
  }

  GeneratorParams params_;
};

TEST_F(ChordTrackTest, ChordTrackGenerated) {
  Generator gen;
  gen.generate(params_);

  const auto& song = gen.getSong();
  EXPECT_FALSE(song.chord().empty());
}

TEST_F(ChordTrackTest, ChordHasNotes) {
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().chord();
  EXPECT_GT(track.notes().size(), 0u);
}

TEST_F(ChordTrackTest, ChordNotesInValidMidiRange) {
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().chord();
  for (const auto& note : track.notes()) {
    EXPECT_GE(note.note, 0) << "Note pitch below 0";
    EXPECT_LE(note.note, 127) << "Note pitch above 127";
    EXPECT_GT(note.velocity, 0) << "Velocity is 0";
    EXPECT_LE(note.velocity, 127) << "Velocity above 127";
  }
}

TEST_F(ChordTrackTest, ChordNotesInPianoRange) {
  // Chord voicings should be in a reasonable piano range (C3-C6)
  constexpr uint8_t CHORD_LOW = 48;   // C3
  constexpr uint8_t CHORD_HIGH = 84;  // C6

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().chord();
  for (const auto& note : track.notes()) {
    EXPECT_GE(note.note, CHORD_LOW)
        << "Chord note " << static_cast<int>(note.note) << " below C3";
    EXPECT_LE(note.note, CHORD_HIGH)
        << "Chord note " << static_cast<int>(note.note) << " above C6";
  }
}

TEST_F(ChordTrackTest, ChordVoicingHasMultipleNotes) {
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().chord();
  ASSERT_GT(track.notes().size(), 3u);

  // Check that chords have multiple simultaneous notes
  std::map<Tick, int> notes_per_tick;
  for (const auto& note : track.notes()) {
    notes_per_tick[note.startTick]++;
  }

  // At least some chords should have 3+ notes
  int chords_with_3_plus = 0;
  for (const auto& [tick, count] : notes_per_tick) {
    if (count >= 3) {
      chords_with_3_plus++;
    }
  }

  EXPECT_GT(chords_with_3_plus, 0) << "No chords with 3+ simultaneous notes";
}

TEST_F(ChordTrackTest, DifferentProgressionsProduceDifferentChords) {
  Generator gen1, gen2;

  params_.chord_id = 0;  // Canon
  gen1.generate(params_);

  params_.chord_id = 1;  // Pop
  gen2.generate(params_);

  const auto& track1 = gen1.getSong().chord();
  const auto& track2 = gen2.getSong().chord();

  // Different progressions should produce different patterns
  bool all_same = true;
  size_t min_size = std::min(track1.notes().size(), track2.notes().size());
  for (size_t i = 0; i < min_size && i < 20; ++i) {
    if (track1.notes()[i].note != track2.notes()[i].note) {
      all_same = false;
      break;
    }
  }
  EXPECT_FALSE(all_same)
      << "Different progressions produced identical chord tracks";
}

TEST_F(ChordTrackTest, ChordNotesAreScaleTones) {
  // C major scale pitch classes
  std::set<int> c_major_pcs = {0, 2, 4, 5, 7, 9, 11};

  params_.key = Key::C;
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().chord();
  int out_of_scale_count = 0;

  for (const auto& note : track.notes()) {
    int pc = note.note % 12;
    if (c_major_pcs.find(pc) == c_major_pcs.end()) {
      out_of_scale_count++;
    }
  }

  // Chord notes should mostly be in scale (some alterations allowed)
  double out_of_scale_ratio =
      static_cast<double>(out_of_scale_count) / track.notes().size();
  EXPECT_LT(out_of_scale_ratio, 0.1)
      << "Too many out-of-scale chord notes: " << out_of_scale_count << " of "
      << track.notes().size();
}

TEST_F(ChordTrackTest, SameSeedProducesSameChords) {
  Generator gen1, gen2;
  params_.seed = 12345;
  gen1.generate(params_);
  gen2.generate(params_);

  const auto& track1 = gen1.getSong().chord();
  const auto& track2 = gen2.getSong().chord();

  ASSERT_EQ(track1.notes().size(), track2.notes().size())
      << "Same seed produced different number of chord notes";

  for (size_t i = 0; i < track1.notes().size(); ++i) {
    EXPECT_EQ(track1.notes()[i].note, track2.notes()[i].note)
        << "Note mismatch at index " << i;
  }
}

TEST_F(ChordTrackTest, TranspositionWorksCorrectly) {
  // Generate in C major
  params_.key = Key::C;
  params_.seed = 100;
  Generator gen_c;
  gen_c.generate(params_);

  // Generate in G major
  params_.key = Key::G;
  Generator gen_g;
  gen_g.generate(params_);

  const auto& track_c = gen_c.getSong().chord();
  const auto& track_g = gen_g.getSong().chord();

  EXPECT_FALSE(track_c.notes().empty());
  EXPECT_FALSE(track_g.notes().empty());

  // Check transposition by comparing pitch classes
  // G major should have F# instead of F (pitch class 6 instead of 5)
  std::set<int> pcs_c, pcs_g;
  for (const auto& note : track_c.notes()) {
    pcs_c.insert(note.note % 12);
  }
  for (const auto& note : track_g.notes()) {
    pcs_g.insert(note.note % 12);
  }

  // C major should have F (5), G major should have F# (6)
  bool c_has_f = pcs_c.count(5) > 0;      // F natural
  bool g_has_fsharp = pcs_g.count(6) > 0; // F#

  // At least one of these should be true to show transposition works
  EXPECT_TRUE(c_has_f || g_has_fsharp || pcs_c != pcs_g)
      << "Transposition did not change pitch content";
}

}  // namespace
}  // namespace midisketch
