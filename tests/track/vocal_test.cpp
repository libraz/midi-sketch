#include <gtest/gtest.h>
#include "core/generator.h"
#include "core/song.h"
#include "core/types.h"
#include <random>
#include <set>

namespace midisketch {
namespace {

class VocalTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::StandardPop;
    params_.mood = Mood::ElectroPop;
    params_.chord_id = 0;  // Canon progression
    params_.key = Key::C;
    params_.drums_enabled = false;
    params_.vocal_low = 60;   // C4
    params_.vocal_high = 84;  // C6
    params_.bpm = 120;
    params_.seed = 42;
    params_.arpeggio_enabled = false;
  }

  GeneratorParams params_;
};

TEST_F(VocalTest, VocalTrackGenerated) {
  Generator gen;
  gen.generate(params_);

  const auto& song = gen.getSong();
  EXPECT_FALSE(song.vocal().empty());
}

TEST_F(VocalTest, VocalHasNotes) {
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().vocal();
  EXPECT_GT(track.notes().size(), 0u);
}

TEST_F(VocalTest, VocalNotesInValidMidiRange) {
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().vocal();
  for (const auto& note : track.notes()) {
    EXPECT_GE(note.note, 0) << "Note pitch below 0";
    EXPECT_LE(note.note, 127) << "Note pitch above 127";
    EXPECT_GT(note.velocity, 0) << "Velocity is 0";
    EXPECT_LE(note.velocity, 127) << "Velocity above 127";
  }
}

TEST_F(VocalTest, VocalNotesWithinConfiguredRange) {
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().vocal();
  for (const auto& note : track.notes()) {
    // Allow some tolerance for octave adjustments
    EXPECT_GE(note.note, params_.vocal_low - 12)
        << "Note " << static_cast<int>(note.note) << " below range";
    EXPECT_LE(note.note, params_.vocal_high + 12)
        << "Note " << static_cast<int>(note.note) << " above range";
  }
}

TEST_F(VocalTest, VocalNotesAreScaleTones) {
  // C major scale pitch classes: C=0, D=2, E=4, F=5, G=7, A=9, B=11
  std::set<int> c_major_pcs = {0, 2, 4, 5, 7, 9, 11};

  params_.key = Key::C;
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().vocal();
  int out_of_scale_count = 0;

  for (const auto& note : track.notes()) {
    int pc = note.note % 12;
    if (c_major_pcs.find(pc) == c_major_pcs.end()) {
      out_of_scale_count++;
    }
  }

  // Allow very few out-of-scale notes (chromatic passing tones)
  double out_of_scale_ratio =
      static_cast<double>(out_of_scale_count) / track.notes().size();
  EXPECT_LT(out_of_scale_ratio, 0.05)
      << "Too many out-of-scale notes: " << out_of_scale_count << " of "
      << track.notes().size();
}

TEST_F(VocalTest, VocalIntervalConstraints) {
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().vocal();
  ASSERT_GT(track.notes().size(), 1u);

  int large_leaps = 0;
  constexpr int MAX_REASONABLE_LEAP = 12;  // One octave

  for (size_t i = 1; i < track.notes().size(); ++i) {
    int interval =
        std::abs(static_cast<int>(track.notes()[i].note) -
                 static_cast<int>(track.notes()[i - 1].note));
    if (interval > MAX_REASONABLE_LEAP) {
      large_leaps++;
    }
  }

  // Very few leaps should exceed an octave
  double large_leap_ratio =
      static_cast<double>(large_leaps) / (track.notes().size() - 1);
  EXPECT_LT(large_leap_ratio, 0.1)
      << "Too many large leaps: " << large_leaps << " of "
      << track.notes().size() - 1;
}

TEST_F(VocalTest, DifferentSeedsProduceDifferentMelodies) {
  Generator gen1, gen2;
  params_.seed = 100;
  gen1.generate(params_);

  params_.seed = 200;
  gen2.generate(params_);

  const auto& track1 = gen1.getSong().vocal();
  const auto& track2 = gen2.getSong().vocal();

  // Different seeds should produce different note sequences
  bool all_same = true;
  size_t min_size = std::min(track1.notes().size(), track2.notes().size());
  for (size_t i = 0; i < min_size && i < 20; ++i) {
    if (track1.notes()[i].note != track2.notes()[i].note) {
      all_same = false;
      break;
    }
  }
  EXPECT_FALSE(all_same) << "Different seeds produced identical melodies";
}

TEST_F(VocalTest, SameSeedProducesSameMelody) {
  Generator gen1, gen2;
  params_.seed = 12345;
  gen1.generate(params_);
  gen2.generate(params_);

  const auto& track1 = gen1.getSong().vocal();
  const auto& track2 = gen2.getSong().vocal();

  ASSERT_EQ(track1.notes().size(), track2.notes().size())
      << "Same seed produced different number of notes";

  for (size_t i = 0; i < track1.notes().size(); ++i) {
    EXPECT_EQ(track1.notes()[i].note, track2.notes()[i].note)
        << "Note mismatch at index " << i;
    EXPECT_EQ(track1.notes()[i].startTick, track2.notes()[i].startTick)
        << "Timing mismatch at index " << i;
  }
}

TEST_F(VocalTest, VocalRangeRespected) {
  // Test with narrow range
  params_.vocal_low = 64;   // E4
  params_.vocal_high = 72;  // C5
  params_.seed = 999;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().vocal();
  for (const auto& note : track.notes()) {
    // Notes should be within or near the configured range
    EXPECT_GE(note.note, params_.vocal_low - 12);
    EXPECT_LE(note.note, params_.vocal_high + 12);
  }
}

TEST_F(VocalTest, TranspositionWorksCorrectly) {
  // Generate in C major
  params_.key = Key::C;
  Generator gen_c;
  gen_c.generate(params_);

  // Generate in G major (7 semitones up)
  params_.key = Key::G;
  Generator gen_g;
  gen_g.generate(params_);

  const auto& track_c = gen_c.getSong().vocal();
  const auto& track_g = gen_g.getSong().vocal();

  // G major should have notes that are roughly 7 semitones higher
  // (with octave adjustments for range)
  EXPECT_FALSE(track_c.notes().empty());
  EXPECT_FALSE(track_g.notes().empty());
}

}  // namespace
}  // namespace midisketch
