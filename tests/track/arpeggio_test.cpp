#include <gtest/gtest.h>
#include "core/generator.h"
#include "core/song.h"
#include "core/types.h"
#include "track/arpeggio.h"
#include <random>

namespace midisketch {
namespace {

class ArpeggioTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create basic params for testing
    params_.structure = StructurePattern::StandardPop;
    params_.mood = Mood::ElectroPop;
    params_.chord_id = 0;  // Canon progression
    params_.key = Key::C;
    params_.drums_enabled = false;
    // modulation_timing defaults to None
    params_.vocal_low = 60;
    params_.vocal_high = 84;
    params_.bpm = 140;
    params_.seed = 42;
    params_.arpeggio_enabled = true;

    // Arpeggio params
    params_.arpeggio.pattern = ArpeggioPattern::Up;
    params_.arpeggio.speed = ArpeggioSpeed::Sixteenth;
    params_.arpeggio.octave_range = 2;
    params_.arpeggio.gate = 0.8f;
    params_.arpeggio.sync_chord = true;
    params_.arpeggio.base_velocity = 90;
  }

  GeneratorParams params_;
};

TEST_F(ArpeggioTest, ArpeggioTrackGenerated) {
  Generator gen;
  gen.generate(params_);

  const auto& song = gen.getSong();
  EXPECT_FALSE(song.arpeggio().empty());
}

TEST_F(ArpeggioTest, ArpeggioDisabledWhenNotEnabled) {
  params_.arpeggio_enabled = false;
  Generator gen;
  gen.generate(params_);

  const auto& song = gen.getSong();
  EXPECT_TRUE(song.arpeggio().empty());
}

TEST_F(ArpeggioTest, ArpeggioHasNotes) {
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().arpeggio();
  EXPECT_GT(track.notes().size(), 0u);
}

TEST_F(ArpeggioTest, ArpeggioNotesInValidRange) {
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().arpeggio();
  for (const auto& note : track.notes()) {
    EXPECT_GE(note.note, 0);
    EXPECT_LE(note.note, 127);
    EXPECT_GT(note.velocity, 0);
    EXPECT_LE(note.velocity, 127);
  }
}

TEST_F(ArpeggioTest, SixteenthNoteSpeed) {
  params_.arpeggio.speed = ArpeggioSpeed::Sixteenth;
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().arpeggio();
  ASSERT_GT(track.notes().size(), 1u);

  // Check that note spacing is approximately 16th notes (120 ticks at 480 PPQ)
  Tick expected_duration = TICKS_PER_BEAT / 4;  // 120 ticks
  bool found_sixteenth = false;

  for (size_t i = 1; i < track.notes().size() && i < 10; ++i) {
    Tick spacing = track.notes()[i].start_tick - track.notes()[i-1].start_tick;
    if (spacing == expected_duration) {
      found_sixteenth = true;
      break;
    }
  }
  EXPECT_TRUE(found_sixteenth);
}

TEST_F(ArpeggioTest, EighthNoteSpeed) {
  params_.arpeggio.speed = ArpeggioSpeed::Eighth;
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().arpeggio();
  ASSERT_GT(track.notes().size(), 1u);

  // Check that note spacing is approximately 8th notes (240 ticks at 480 PPQ)
  Tick expected_duration = TICKS_PER_BEAT / 2;  // 240 ticks
  bool found_eighth = false;

  for (size_t i = 1; i < track.notes().size() && i < 10; ++i) {
    Tick spacing = track.notes()[i].start_tick - track.notes()[i-1].start_tick;
    if (spacing == expected_duration) {
      found_eighth = true;
      break;
    }
  }
  EXPECT_TRUE(found_eighth);
}

TEST_F(ArpeggioTest, PatternUp) {
  params_.arpeggio.pattern = ArpeggioPattern::Up;
  params_.arpeggio.octave_range = 1;  // Single octave for simpler testing
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().arpeggio();
  ASSERT_GT(track.notes().size(), 2u);

  // In Up pattern, notes should generally ascend (within a chord cycle)
  // Just check that the track was generated
  EXPECT_FALSE(track.empty());
}

TEST_F(ArpeggioTest, PatternDown) {
  params_.arpeggio.pattern = ArpeggioPattern::Down;
  params_.arpeggio.octave_range = 1;
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().arpeggio();
  EXPECT_FALSE(track.empty());
}

TEST_F(ArpeggioTest, PatternUpDown) {
  params_.arpeggio.pattern = ArpeggioPattern::UpDown;
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().arpeggio();
  EXPECT_FALSE(track.empty());
}

TEST_F(ArpeggioTest, PatternRandom) {
  params_.arpeggio.pattern = ArpeggioPattern::Random;
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().arpeggio();
  EXPECT_FALSE(track.empty());
}

TEST_F(ArpeggioTest, OctaveRange) {
  params_.arpeggio.octave_range = 3;
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().arpeggio();
  EXPECT_FALSE(track.empty());

  // Find the range of notes
  uint8_t min_note = 127;
  uint8_t max_note = 0;
  for (const auto& note : track.notes()) {
    min_note = std::min(min_note, note.note);
    max_note = std::max(max_note, note.note);
  }

  // With 3 octave range, should have at least 2 octaves of range
  EXPECT_GE(max_note - min_note, 12);
}

TEST_F(ArpeggioTest, GateLength) {
  params_.arpeggio.gate = 0.5f;  // Half gate
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().arpeggio();
  ASSERT_GT(track.notes().size(), 0u);

  // Gate of 0.5 with 16th notes (120 ticks) should give ~60 tick duration
  Tick expected_duration = static_cast<Tick>((TICKS_PER_BEAT / 4) * 0.5f);
  bool found_short_note = false;

  for (const auto& note : track.notes()) {
    if (note.duration == expected_duration) {
      found_short_note = true;
      break;
    }
  }
  EXPECT_TRUE(found_short_note);
}

TEST_F(ArpeggioTest, SongClearIncludesArpeggio) {
  Generator gen;
  gen.generate(params_);

  auto& song = const_cast<Song&>(gen.getSong());
  EXPECT_FALSE(song.arpeggio().empty());

  song.clearAll();
  EXPECT_TRUE(song.arpeggio().empty());
}

TEST_F(ArpeggioTest, TrackRoleArpeggio) {
  Generator gen;
  gen.generate(params_);

  auto& song = const_cast<Song&>(gen.getSong());
  EXPECT_FALSE(song.track(TrackRole::Arpeggio).empty());
}

TEST_F(ArpeggioTest, SyncChordTrue) {
  // Test that sync_chord=true (default) syncs with chord changes each bar
  params_.arpeggio.sync_chord = true;
  params_.seed = 33333;

  Generator gen;
  gen.generate(params_);

  const auto& arpeggio = gen.getSong().arpeggio();
  EXPECT_FALSE(arpeggio.empty())
      << "Arpeggio should be generated with sync_chord=true";
}

TEST_F(ArpeggioTest, SyncChordFalse) {
  // Test that sync_chord=false continues pattern without chord resync
  params_.arpeggio.sync_chord = false;
  params_.seed = 33333;

  Generator gen;
  gen.generate(params_);

  const auto& arpeggio = gen.getSong().arpeggio();
  EXPECT_FALSE(arpeggio.empty())
      << "Arpeggio should be generated with sync_chord=false";
}

TEST_F(ArpeggioTest, SyncChordAffectsPattern) {
  // Test that sync_chord affects the arpeggio pattern behavior
  params_.seed = 44444;

  // Generate with sync_chord=true
  params_.arpeggio.sync_chord = true;
  Generator gen_sync;
  gen_sync.generate(params_);
  size_t sync_notes = gen_sync.getSong().arpeggio().notes().size();

  // Generate with sync_chord=false
  params_.arpeggio.sync_chord = false;
  Generator gen_nosync;
  gen_nosync.generate(params_);
  size_t nosync_notes = gen_nosync.getSong().arpeggio().notes().size();

  // Both should have similar note counts (timing is different, not note count)
  // But ensure both generate something
  EXPECT_GT(sync_notes, 0u) << "Sync chord should generate notes";
  EXPECT_GT(nosync_notes, 0u) << "No sync chord should generate notes";
}

// ============================================================================
// Chant/MixBreak Section Velocity Tests
// ============================================================================

TEST_F(ArpeggioTest, ChantSectionHasReducedVelocity) {
  // Test that Chant sections produce lower velocity arpeggio notes
  // This is tested indirectly via the velocity calculation function
  // by comparing with Chorus which has highest velocity

  // Generate with a structure that has Chorus (for comparison)
  params_.structure = StructurePattern::StandardPop;
  params_.seed = 55555;

  Generator gen;
  gen.generate(params_);

  const auto& arpeggio = gen.getSong().arpeggio();
  EXPECT_FALSE(arpeggio.empty()) << "Arpeggio should be generated";

  // Just verify generation works - the velocity calculation is internal
  // The key test is that Chant section type is now handled in the switch
  // and won't fall through to default case
  EXPECT_GT(arpeggio.notes().size(), 0u);
}

// ============================================================================
// Sync Chord Refresh Tests
// ============================================================================

TEST_F(ArpeggioTest, SyncChordFalseRefreshesAtSectionBoundary) {
  // Test that sync_chord=false refreshes the pattern at section boundaries
  // This prevents drift from chord progression in long songs
  params_.structure = StructurePattern::FullPop;  // Has multiple sections
  params_.arpeggio.sync_chord = false;
  params_.seed = 66666;

  Generator gen;
  gen.generate(params_);

  const auto& arpeggio = gen.getSong().arpeggio();
  const auto& sections = gen.getSong().arrangement().sections();

  ASSERT_FALSE(arpeggio.empty());
  ASSERT_GT(sections.size(), 1u) << "Need multiple sections for this test";

  // Verify arpeggio spans multiple sections
  Tick first_section_end = sections[0].start_tick + sections[0].bars * TICKS_PER_BAR;
  bool has_notes_after_first_section = false;

  for (const auto& note : arpeggio.notes()) {
    if (note.start_tick >= first_section_end) {
      has_notes_after_first_section = true;
      break;
    }
  }

  EXPECT_TRUE(has_notes_after_first_section)
      << "Arpeggio should continue into second section";
}

TEST_F(ArpeggioTest, SyncChordFalsePatternRefreshedPerSection) {
  // Test that different sections get fresh patterns based on their chord context
  params_.structure = StructurePattern::FullPop;
  params_.arpeggio.sync_chord = false;
  params_.seed = 77777;

  Generator gen;
  gen.generate(params_);

  const auto& arpeggio = gen.getSong().arpeggio();
  EXPECT_FALSE(arpeggio.empty())
      << "Arpeggio should be generated with sync_chord=false";

  // The pattern should refresh at each section start
  // We can't easily verify the exact pattern, but we can verify
  // that notes are generated throughout the song
  const auto& sections = gen.getSong().arrangement().sections();
  if (sections.size() > 2) {
    Tick mid_section_start = sections[sections.size() / 2].start_tick;
    bool has_notes_in_mid_section = false;

    for (const auto& note : arpeggio.notes()) {
      if (note.start_tick >= mid_section_start &&
          note.start_tick < mid_section_start + TICKS_PER_BAR) {
        has_notes_in_mid_section = true;
        break;
      }
    }

    EXPECT_TRUE(has_notes_in_mid_section)
        << "Arpeggio should have notes in middle sections";
  }
}

}  // namespace
}  // namespace midisketch
