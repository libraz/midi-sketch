/**
 * @file post_processor_test.cpp
 * @brief Tests for post-processing functions (chorus drop, ritardando, final hit).
 */

#include "core/post_processor.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "core/midi_track.h"
#include "core/section_types.h"
#include "core/types.h"

namespace midisketch {
namespace {

// GM Drum Map constants
constexpr uint8_t KICK = 36;
constexpr uint8_t SNARE = 38;
constexpr uint8_t CRASH = 49;

// ============================================================================
// applyChorusDrop Tests
// ============================================================================

class ChorusDropTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create B section followed by Chorus
    Section b_section;
    b_section.type = SectionType::B;
    b_section.start_tick = 0;
    b_section.bars = 8;
    b_section.name = "B";

    Section chorus;
    chorus.type = SectionType::Chorus;
    chorus.start_tick = 8 * TICKS_PER_BAR;
    chorus.bars = 8;
    chorus.name = "Chorus";

    sections_ = {b_section, chorus};
  }

  std::vector<Section> sections_;
};

TEST_F(ChorusDropTest, TruncatesMelodicTracksInLastBeat) {
  // At B->Chorus transition, melodic tracks should have notes truncated
  // in the last beat (480 ticks) of the B section

  // Create chord track with notes extending through the drop zone
  MidiTrack chord_track;
  Tick drop_zone_start = 8 * TICKS_PER_BAR - TICKS_PER_BEAT;  // Last beat of B section
  // Note starting before drop zone, extending into it
  chord_track.addNote(drop_zone_start - TICKS_PER_BEAT, TICKS_PER_BEAT * 2, 60, 80);
  // Note starting in drop zone
  chord_track.addNote(drop_zone_start + TICKS_PER_BEAT / 2, TICKS_PER_BEAT / 2, 64, 80);

  MidiTrack drum_track;
  // Not processed by applyChorusDrop directly

  std::vector<MidiTrack*> tracks = {&chord_track};
  PostProcessor::applyChorusDrop(tracks, sections_, &drum_track);

  // Verify note starting before drop zone is truncated at drop zone boundary
  bool found_truncated = false;
  bool found_removed = true;
  for (const auto& note : chord_track.notes()) {
    if (note.start_tick < drop_zone_start) {
      // This note should be truncated
      EXPECT_LE(note.start_tick + note.duration, drop_zone_start)
          << "Note extending into drop zone should be truncated";
      found_truncated = true;
    }
    if (note.start_tick >= drop_zone_start && note.start_tick < 8 * TICKS_PER_BAR) {
      // Notes starting in drop zone should be removed
      found_removed = false;
    }
  }

  EXPECT_TRUE(found_truncated) << "Should have truncated notes";
  EXPECT_TRUE(found_removed) << "Notes starting in drop zone should be removed";
}

TEST_F(ChorusDropTest, PreservesVocalTrack) {
  // Vocal track should NOT be truncated (preserved for pre-chorus lift effect)
  // Note: The applyChorusDrop function does not know which track is vocal,
  // so if vocal is passed in the tracks vector, it WOULD be processed.
  // The caller must exclude vocal from the tracks vector.
  // This test verifies that only passed tracks are modified.

  MidiTrack melodic_track;
  Tick drop_zone_start = 8 * TICKS_PER_BAR - TICKS_PER_BEAT;
  melodic_track.addNote(drop_zone_start - TICKS_PER_BEAT, TICKS_PER_BEAT * 2, 60, 80);

  MidiTrack vocal_track;
  vocal_track.addNote(drop_zone_start - TICKS_PER_BEAT, TICKS_PER_BEAT * 2, 72, 100);

  // Only pass melodic track, not vocal
  std::vector<MidiTrack*> tracks = {&melodic_track};
  PostProcessor::applyChorusDrop(tracks, sections_, nullptr);

  // Melodic track should be truncated
  bool melodic_truncated = false;
  for (const auto& note : melodic_track.notes()) {
    if (note.start_tick < drop_zone_start) {
      if (note.start_tick + note.duration <= drop_zone_start) {
        melodic_truncated = true;
      }
    }
  }
  EXPECT_TRUE(melodic_truncated) << "Melodic track should be truncated";

  // Vocal track should be preserved (not passed to applyChorusDrop)
  EXPECT_EQ(vocal_track.notes().size(), 1u) << "Vocal track should be unchanged";
  EXPECT_GT(vocal_track.notes()[0].duration, TICKS_PER_BEAT)
      << "Vocal note duration should be unchanged";
}

TEST_F(ChorusDropTest, DrumTrackRemainsUnaffected) {
  // Drum track notes should NOT be truncated (fill remains)

  MidiTrack drum_track;
  Tick drop_zone_start = 8 * TICKS_PER_BAR - TICKS_PER_BEAT;
  // Add drum notes in the drop zone
  drum_track.addNote(drop_zone_start, TICKS_PER_BEAT / 4, KICK, 100);
  drum_track.addNote(drop_zone_start + TICKS_PER_BEAT / 4, TICKS_PER_BEAT / 4, SNARE, 90);
  drum_track.addNote(drop_zone_start + TICKS_PER_BEAT / 2, TICKS_PER_BEAT / 4, SNARE, 95);

  MidiTrack chord_track;
  chord_track.addNote(drop_zone_start - TICKS_PER_BEAT, TICKS_PER_BEAT * 2, 60, 80);

  size_t original_drum_count = drum_track.notes().size();
  std::vector<MidiTrack*> tracks = {&chord_track};

  // drum_track is passed separately and should NOT be modified
  PostProcessor::applyChorusDrop(tracks, sections_, &drum_track);

  // Verify drum track is unchanged
  EXPECT_EQ(drum_track.notes().size(), original_drum_count)
      << "Drum notes should remain after chorus drop";

  // Verify drum notes in drop zone still exist
  int drums_in_drop_zone = 0;
  for (const auto& note : drum_track.notes()) {
    if (note.start_tick >= drop_zone_start) {
      drums_in_drop_zone++;
    }
  }
  EXPECT_GT(drums_in_drop_zone, 0) << "Drum fill should remain in drop zone";
}

TEST_F(ChorusDropTest, OnlyAffectsBToChorusTransition) {
  // Create A -> B section (no Chorus following)
  Section a_section;
  a_section.type = SectionType::A;
  a_section.start_tick = 0;
  a_section.bars = 8;

  Section b_section;
  b_section.type = SectionType::B;
  b_section.start_tick = 8 * TICKS_PER_BAR;
  b_section.bars = 8;

  std::vector<Section> no_chorus_sections = {a_section, b_section};

  MidiTrack chord_track;
  Tick b_last_beat = 16 * TICKS_PER_BAR - TICKS_PER_BEAT;
  chord_track.addNote(b_last_beat - TICKS_PER_BEAT, TICKS_PER_BEAT * 2, 60, 80);
  chord_track.addNote(b_last_beat + TICKS_PER_BEAT / 2, TICKS_PER_BEAT / 2, 64, 80);

  size_t original_count = chord_track.notes().size();
  Tick original_duration = chord_track.notes()[0].duration;

  std::vector<MidiTrack*> tracks = {&chord_track};
  PostProcessor::applyChorusDrop(tracks, no_chorus_sections, nullptr);

  // Notes should be unchanged since there's no Chorus following B
  EXPECT_EQ(chord_track.notes().size(), original_count)
      << "Notes should not be removed when no Chorus follows";
  EXPECT_EQ(chord_track.notes()[0].duration, original_duration)
      << "Note duration should be unchanged when no Chorus follows";
}

// ============================================================================
// applyRitardando Tests
// ============================================================================

class RitardandoTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create Outro section
    Section outro;
    outro.type = SectionType::Outro;
    outro.start_tick = 0;
    outro.bars = 8;
    outro.name = "Outro";

    sections_ = {outro};
  }

  std::vector<Section> sections_;
};

TEST_F(RitardandoTest, StretchesDurationInLast4Bars) {
  // In Outro's last 4 bars, note durations should be stretched
  // Ratio: 1.0 at start -> 1.3 at end

  MidiTrack track;
  Tick rit_zone_start = 8 * TICKS_PER_BAR - 4 * TICKS_PER_BAR;  // Last 4 bars
  Tick original_duration = TICKS_PER_BEAT;

  // Add notes throughout the ritardando zone
  track.addNote(rit_zone_start, original_duration, 60, 80);      // Start of rit zone
  track.addNote(rit_zone_start + 2 * TICKS_PER_BAR, original_duration, 64, 80);  // Middle
  track.addNote(8 * TICKS_PER_BAR - TICKS_PER_BAR, original_duration, 67, 80);   // Near end

  std::vector<MidiTrack*> tracks = {&track};
  PostProcessor::applyRitardando(tracks, sections_);

  // Check that durations increase progressively
  const auto& notes = track.notes();
  ASSERT_EQ(notes.size(), 3u);

  // First note: stretched minimally (progress ~0.0)
  EXPECT_GE(notes[0].duration, original_duration)
      << "First note should be stretched";

  // Middle note: stretched more (progress ~0.5)
  EXPECT_GT(notes[1].duration, notes[0].duration)
      << "Middle note should be stretched more than first";

  // Last note: stretched most (progress ~0.75)
  EXPECT_GT(notes[2].duration, notes[1].duration)
      << "Last note should be stretched most";
}

TEST_F(RitardandoTest, VelocityDecrescendo) {
  // Velocities should decrease in the ritardando zone (decrescendo)

  MidiTrack track;
  Tick rit_zone_start = 4 * TICKS_PER_BAR;  // Last 4 bars start
  uint8_t original_velocity = 100;

  // Add notes at different positions in the ritardando zone
  track.addNote(rit_zone_start, TICKS_PER_BEAT, 60, original_velocity);
  track.addNote(rit_zone_start + 2 * TICKS_PER_BAR, TICKS_PER_BEAT, 64, original_velocity);
  track.addNote(8 * TICKS_PER_BAR - TICKS_PER_BAR, TICKS_PER_BEAT, 67, original_velocity);

  std::vector<MidiTrack*> tracks = {&track};
  PostProcessor::applyRitardando(tracks, sections_);

  const auto& notes = track.notes();
  ASSERT_EQ(notes.size(), 3u);

  // First note: minimal reduction
  EXPECT_LE(notes[0].velocity, original_velocity)
      << "Velocity should not increase";

  // Middle note: more reduction
  EXPECT_LT(notes[1].velocity, notes[0].velocity)
      << "Middle note velocity should be lower";

  // Last note: most reduction (but still audible, minimum ~30)
  EXPECT_LT(notes[2].velocity, notes[1].velocity)
      << "Last note velocity should be lowest";
  EXPECT_GE(notes[2].velocity, 30u)
      << "Velocity should not go below minimum threshold";
}

TEST_F(RitardandoTest, FinalNoteExtendedToSectionEnd) {
  // The final note in the ritardando zone should be extended (fermata effect)

  MidiTrack track;
  Tick section_end = 8 * TICKS_PER_BAR;
  Tick original_duration = TICKS_PER_BEAT;

  // Add the final note in the section
  track.addNote(section_end - TICKS_PER_BAR, original_duration, 60, 80);

  std::vector<MidiTrack*> tracks = {&track};
  PostProcessor::applyRitardando(tracks, sections_);

  const auto& notes = track.notes();
  ASSERT_EQ(notes.size(), 1u);

  // Final note should be extended to near the section end
  Tick expected_end = section_end - TICKS_PER_BEAT / 8;  // Small release gap
  Tick actual_end = notes[0].start_tick + notes[0].duration;

  EXPECT_GT(notes[0].duration, original_duration)
      << "Final note should be extended (fermata)";
  EXPECT_GE(actual_end, expected_end - TICKS_PER_BEAT / 4)
      << "Final note should extend close to section end";
}

TEST_F(RitardandoTest, OnlyAffectsOutroSection) {
  // Ritardando should only apply to Outro sections

  Section a_section;
  a_section.type = SectionType::A;
  a_section.start_tick = 0;
  a_section.bars = 8;

  std::vector<Section> non_outro_sections = {a_section};

  MidiTrack track;
  Tick original_duration = TICKS_PER_BEAT;
  uint8_t original_velocity = 100;
  // Add notes in the last 4 bars
  track.addNote(4 * TICKS_PER_BAR, original_duration, 60, original_velocity);
  track.addNote(6 * TICKS_PER_BAR, original_duration, 64, original_velocity);

  std::vector<MidiTrack*> tracks = {&track};
  PostProcessor::applyRitardando(tracks, non_outro_sections);

  const auto& notes = track.notes();
  // Notes should be unchanged in non-Outro section
  for (const auto& note : notes) {
    EXPECT_EQ(note.duration, original_duration)
        << "Duration should be unchanged in non-Outro section";
    EXPECT_EQ(note.velocity, original_velocity)
        << "Velocity should be unchanged in non-Outro section";
  }
}

// ============================================================================
// applyEnhancedFinalHit Tests
// ============================================================================

class EnhancedFinalHitTest : public ::testing::Test {
 protected:
  void SetUp() override {
    section_.type = SectionType::Outro;
    section_.start_tick = 0;
    section_.bars = 4;
    section_.exit_pattern = ExitPattern::FinalHit;
  }

  Section section_;
};

TEST_F(EnhancedFinalHitTest, AddsKickAndCrashOnFinalBeat) {
  // On final beat, kick and crash should be present with velocity 110+

  MidiTrack drum_track;
  // Add some existing drum notes
  drum_track.addNote(0, TICKS_PER_BEAT / 2, KICK, 80);
  drum_track.addNote(TICKS_PER_BEAT, TICKS_PER_BEAT / 2, SNARE, 85);

  PostProcessor::applyEnhancedFinalHit(nullptr, &drum_track, nullptr, section_);

  Tick final_beat_start = 4 * TICKS_PER_BAR - TICKS_PER_BEAT;

  bool has_kick = false;
  bool has_crash = false;

  for (const auto& note : drum_track.notes()) {
    if (note.start_tick >= final_beat_start) {
      if (note.note == KICK) {
        has_kick = true;
        EXPECT_GE(note.velocity, 110u)
            << "Kick on final beat should have velocity 110+";
      }
      if (note.note == CRASH) {
        has_crash = true;
        EXPECT_GE(note.velocity, 110u)
            << "Crash on final beat should have velocity 110+";
      }
    }
  }

  EXPECT_TRUE(has_kick) << "Should have kick on final beat";
  EXPECT_TRUE(has_crash) << "Should have crash on final beat";
}

TEST_F(EnhancedFinalHitTest, ChordTrackSustainsFinalChord) {
  // Chord track notes on final beat should be sustained

  MidiTrack chord_track;
  Tick final_beat_start = 4 * TICKS_PER_BAR - TICKS_PER_BEAT;
  Tick original_duration = TICKS_PER_BEAT / 2;

  // Add chord notes on final beat
  chord_track.addNote(final_beat_start, original_duration, 60, 80);  // C
  chord_track.addNote(final_beat_start, original_duration, 64, 80);  // E
  chord_track.addNote(final_beat_start, original_duration, 67, 80);  // G

  PostProcessor::applyEnhancedFinalHit(nullptr, nullptr, &chord_track, section_);

  Tick section_end = 4 * TICKS_PER_BAR;

  for (const auto& note : chord_track.notes()) {
    if (note.start_tick >= final_beat_start) {
      // Notes should be extended to section end
      Tick note_end = note.start_tick + note.duration;
      EXPECT_EQ(note_end, section_end)
          << "Chord notes on final beat should be sustained to section end";
      EXPECT_GE(note.velocity, 110u)
          << "Chord notes on final beat should have velocity 110+";
    }
  }
}

TEST_F(EnhancedFinalHitTest, BoostsBassVelocity) {
  // Bass notes on final beat should have velocity 110+

  MidiTrack bass_track;
  Tick final_beat_start = 4 * TICKS_PER_BAR - TICKS_PER_BEAT;

  // Add bass note on final beat
  bass_track.addNote(final_beat_start, TICKS_PER_BEAT, 36, 80);

  PostProcessor::applyEnhancedFinalHit(&bass_track, nullptr, nullptr, section_);

  for (const auto& note : bass_track.notes()) {
    if (note.start_tick >= final_beat_start) {
      EXPECT_GE(note.velocity, 110u)
          << "Bass note on final beat should have velocity 110+";
    }
  }
}

TEST_F(EnhancedFinalHitTest, OnlyAppliesWhenExitPatternIsFinalHit) {
  // Should not modify tracks if exit_pattern is not FinalHit

  Section other_section;
  other_section.type = SectionType::Outro;
  other_section.start_tick = 0;
  other_section.bars = 4;
  other_section.exit_pattern = ExitPattern::None;

  MidiTrack drum_track;
  size_t original_count = drum_track.notes().size();

  PostProcessor::applyEnhancedFinalHit(nullptr, &drum_track, nullptr, other_section);

  EXPECT_EQ(drum_track.notes().size(), original_count)
      << "Should not add notes when exit_pattern is not FinalHit";
}

TEST_F(EnhancedFinalHitTest, AddsMissingKickOnFinalBeat) {
  // If no kick exists on final beat, one should be added

  MidiTrack drum_track;
  // Add notes but NOT on the final beat
  drum_track.addNote(0, TICKS_PER_BEAT / 2, KICK, 80);
  drum_track.addNote(TICKS_PER_BAR, TICKS_PER_BEAT / 2, SNARE, 85);

  PostProcessor::applyEnhancedFinalHit(nullptr, &drum_track, nullptr, section_);

  Tick final_beat_start = 4 * TICKS_PER_BAR - TICKS_PER_BEAT;
  Tick section_end = 4 * TICKS_PER_BAR;

  bool has_kick_on_final = false;
  for (const auto& note : drum_track.notes()) {
    if (note.note == KICK &&
        note.start_tick >= final_beat_start &&
        note.start_tick < section_end) {
      has_kick_on_final = true;
      EXPECT_GE(note.velocity, 110u)
          << "Added kick should have velocity 110+";
      break;
    }
  }

  EXPECT_TRUE(has_kick_on_final) << "Should add kick on final beat if missing";
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST(PostProcessorIntegrationTest, ChorusDropAndRitardandoDoNotInterfere) {
  // Test that both effects can be applied to different sections without conflict

  Section b_section;
  b_section.type = SectionType::B;
  b_section.start_tick = 0;
  b_section.bars = 8;

  Section chorus;
  chorus.type = SectionType::Chorus;
  chorus.start_tick = 8 * TICKS_PER_BAR;
  chorus.bars = 8;

  Section outro;
  outro.type = SectionType::Outro;
  outro.start_tick = 16 * TICKS_PER_BAR;
  outro.bars = 4;

  std::vector<Section> sections = {b_section, chorus, outro};

  MidiTrack track;
  // Add notes in B section (affected by chorus drop)
  Tick b_drop_zone = 8 * TICKS_PER_BAR - TICKS_PER_BEAT;
  track.addNote(b_drop_zone - TICKS_PER_BEAT, TICKS_PER_BEAT * 2, 60, 80);

  // Add notes in Outro section (affected by ritardando)
  Tick outro_rit_zone = 20 * TICKS_PER_BAR - 4 * TICKS_PER_BAR;
  track.addNote(outro_rit_zone, TICKS_PER_BEAT, 72, 90);
  track.addNote(19 * TICKS_PER_BAR, TICKS_PER_BEAT, 72, 90);  // Final note

  std::vector<MidiTrack*> tracks = {&track};

  // Apply both effects
  PostProcessor::applyChorusDrop(tracks, sections, nullptr);
  PostProcessor::applyRitardando(tracks, sections);

  // Verify both effects were applied appropriately
  bool found_truncated_b = false;
  bool found_stretched_outro = false;

  for (const auto& note : track.notes()) {
    // B section note should be truncated
    if (note.start_tick < 8 * TICKS_PER_BAR) {
      Tick note_end = note.start_tick + note.duration;
      if (note_end <= b_drop_zone) {
        found_truncated_b = true;
      }
    }
    // Outro notes should have stretched duration
    if (note.start_tick >= outro_rit_zone) {
      if (note.duration > TICKS_PER_BEAT) {
        found_stretched_outro = true;
      }
    }
  }

  EXPECT_TRUE(found_truncated_b) << "B section note should be truncated by chorus drop";
  EXPECT_TRUE(found_stretched_outro) << "Outro note should be stretched by ritardando";
}

}  // namespace
}  // namespace midisketch
