/**
 * @file post_processor_test.cpp
 * @brief Tests for post-processing functions (chorus drop, ritardando, final hit).
 */

#include "core/post_processor.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/midi_track.h"
#include "core/note_source.h"
#include "core/preset_data.h"
#include "core/section_types.h"
#include "core/types.h"
#include "test_support/stub_harmony_context.h"

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
  chord_track.addNote(NoteEventBuilder::create(drop_zone_start - TICKS_PER_BEAT, TICKS_PER_BEAT * 2, 60, 80));
  // Note starting in drop zone
  chord_track.addNote(NoteEventBuilder::create(drop_zone_start + TICKS_PER_BEAT / 2, TICKS_PER_BEAT / 2, 64, 80));

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
  melodic_track.addNote(NoteEventBuilder::create(drop_zone_start - TICKS_PER_BEAT, TICKS_PER_BEAT * 2, 60, 80));

  MidiTrack vocal_track;
  vocal_track.addNote(NoteEventBuilder::create(drop_zone_start - TICKS_PER_BEAT, TICKS_PER_BEAT * 2, 72, 100));

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
  drum_track.addNote(NoteEventBuilder::create(drop_zone_start, TICKS_PER_BEAT / 4, KICK, 100));
  drum_track.addNote(NoteEventBuilder::create(drop_zone_start + TICKS_PER_BEAT / 4, TICKS_PER_BEAT / 4, SNARE, 90));
  drum_track.addNote(NoteEventBuilder::create(drop_zone_start + TICKS_PER_BEAT / 2, TICKS_PER_BEAT / 4, SNARE, 95));

  MidiTrack chord_track;
  chord_track.addNote(NoteEventBuilder::create(drop_zone_start - TICKS_PER_BEAT, TICKS_PER_BEAT * 2, 60, 80));

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
  chord_track.addNote(NoteEventBuilder::create(b_last_beat - TICKS_PER_BEAT, TICKS_PER_BEAT * 2, 60, 80));
  chord_track.addNote(NoteEventBuilder::create(b_last_beat + TICKS_PER_BEAT / 2, TICKS_PER_BEAT / 2, 64, 80));

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
  track.addNote(NoteEventBuilder::create(rit_zone_start, original_duration, 60, 80));      // Start of rit zone
  track.addNote(NoteEventBuilder::create(rit_zone_start + 2 * TICKS_PER_BAR, original_duration, 64, 80));  // Middle
  track.addNote(NoteEventBuilder::create(8 * TICKS_PER_BAR - TICKS_PER_BAR, original_duration, 67, 80));   // Near end

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
  track.addNote(NoteEventBuilder::create(rit_zone_start, TICKS_PER_BEAT, 60, original_velocity));
  track.addNote(NoteEventBuilder::create(rit_zone_start + 2 * TICKS_PER_BAR, TICKS_PER_BEAT, 64, original_velocity));
  track.addNote(NoteEventBuilder::create(8 * TICKS_PER_BAR - TICKS_PER_BAR, TICKS_PER_BEAT, 67, original_velocity));

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
  track.addNote(NoteEventBuilder::create(section_end - TICKS_PER_BAR, original_duration, 60, 80));

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
  track.addNote(NoteEventBuilder::create(4 * TICKS_PER_BAR, original_duration, 60, original_velocity));
  track.addNote(NoteEventBuilder::create(6 * TICKS_PER_BAR, original_duration, 64, original_velocity));

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
  drum_track.addNote(NoteEventBuilder::create(0, TICKS_PER_BEAT / 2, KICK, 80));
  drum_track.addNote(NoteEventBuilder::create(TICKS_PER_BEAT, TICKS_PER_BEAT / 2, SNARE, 85));

  PostProcessor::applyEnhancedFinalHit(nullptr, &drum_track, nullptr, nullptr, section_);

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
  chord_track.addNote(NoteEventBuilder::create(final_beat_start, original_duration, 60, 80));  // C
  chord_track.addNote(NoteEventBuilder::create(final_beat_start, original_duration, 64, 80));  // E
  chord_track.addNote(NoteEventBuilder::create(final_beat_start, original_duration, 67, 80));  // G

  PostProcessor::applyEnhancedFinalHit(nullptr, nullptr, &chord_track, nullptr, section_);

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
  bass_track.addNote(NoteEventBuilder::create(final_beat_start, TICKS_PER_BEAT, 36, 80));

  PostProcessor::applyEnhancedFinalHit(&bass_track, nullptr, nullptr, nullptr, section_);

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

  PostProcessor::applyEnhancedFinalHit(nullptr, &drum_track, nullptr, nullptr, other_section);

  EXPECT_EQ(drum_track.notes().size(), original_count)
      << "Should not add notes when exit_pattern is not FinalHit";
}

TEST_F(EnhancedFinalHitTest, AddsMissingKickOnFinalBeat) {
  // If no kick exists on final beat, one should be added

  MidiTrack drum_track;
  // Add notes but NOT on the final beat
  drum_track.addNote(NoteEventBuilder::create(0, TICKS_PER_BEAT / 2, KICK, 80));
  drum_track.addNote(NoteEventBuilder::create(TICKS_PER_BAR, TICKS_PER_BEAT / 2, SNARE, 85));

  PostProcessor::applyEnhancedFinalHit(nullptr, &drum_track, nullptr, nullptr, section_);

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
// SustainPatternTest Tests
// ============================================================================

class SustainPatternTest : public ::testing::Test {
 protected:
  void SetUp() override {
    section_.type = SectionType::B;
    section_.start_tick = 0;
    section_.bars = 4;
    section_.exit_pattern = ExitPattern::Sustain;
  }

  Section section_;
};

TEST_F(SustainPatternTest, ExtendsSingleNoteToSectionEnd) {
  // Single chord in last bar should extend to section end
  MidiTrack track;
  Tick section_end = 4 * TICKS_PER_BAR;
  Tick last_bar_start = section_end - TICKS_PER_BAR;

  // Add single chord at start of last bar
  track.addNote(NoteEventBuilder::create(last_bar_start, TICKS_PER_BEAT, 60, 80));  // C
  track.addNote(NoteEventBuilder::create(last_bar_start, TICKS_PER_BEAT, 64, 80));  // E
  track.addNote(NoteEventBuilder::create(last_bar_start, TICKS_PER_BEAT, 67, 80));  // G

  std::vector<MidiTrack*> tracks = {&track};
  std::vector<Section> sections = {section_};
  PostProcessor::applyAllExitPatterns(tracks, sections);

  // All notes should extend to section end
  for (const auto& note : track.notes()) {
    Tick note_end = note.start_tick + note.duration;
    EXPECT_EQ(note_end, section_end)
        << "Single chord notes should extend to section end";
  }
}

TEST_F(SustainPatternTest, PreventsSustainOverlapWithMultipleChords) {
  // Two chords per bar (subdivision=2): G at beats 1-2, Am at beats 3-4
  // G should NOT extend past Am's start
  MidiTrack track;
  Tick section_end = 4 * TICKS_PER_BAR;
  Tick last_bar_start = section_end - TICKS_PER_BAR;
  Tick half_bar = TICKS_PER_BAR / 2;

  // First chord (G) at beat 1 of last bar
  track.addNote(NoteEventBuilder::create(last_bar_start, half_bar, 67, 80));              // G
  track.addNote(NoteEventBuilder::create(last_bar_start, half_bar, 71, 80));              // B
  track.addNote(NoteEventBuilder::create(last_bar_start, half_bar, 74, 80));              // D

  // Second chord (Am) at beat 3 of last bar
  Tick second_chord_start = last_bar_start + half_bar;
  track.addNote(NoteEventBuilder::create(second_chord_start, half_bar, 69, 80));          // A
  track.addNote(NoteEventBuilder::create(second_chord_start, half_bar, 72, 80));          // C
  track.addNote(NoteEventBuilder::create(second_chord_start, half_bar, 76, 80));          // E

  std::vector<MidiTrack*> tracks = {&track};
  std::vector<Section> sections = {section_};
  PostProcessor::applyAllExitPatterns(tracks, sections);

  // Check that first chord notes end at or before second chord start
  // Check that second chord notes extend to section end
  for (const auto& note : track.notes()) {
    Tick note_end = note.start_tick + note.duration;
    if (note.start_tick == last_bar_start) {
      // First chord should NOT extend past second chord start
      EXPECT_LE(note_end, second_chord_start)
          << "First chord should not overlap with second chord";
    } else if (note.start_tick == second_chord_start) {
      // Second chord should extend to section end
      EXPECT_EQ(note_end, section_end)
          << "Second chord should extend to section end";
    }
  }
}

TEST_F(SustainPatternTest, HandlesNotesAlreadyExtendedBeyondNextNote) {
  // Edge case: note with duration that already extends past next note's start
  MidiTrack track;
  Tick section_end = 4 * TICKS_PER_BAR;
  Tick last_bar_start = section_end - TICKS_PER_BAR;

  // First note with very long duration (extends past next note)
  track.addNote(NoteEventBuilder::create(last_bar_start, TICKS_PER_BAR, 60, 80));

  // Second note at half bar
  Tick second_note_start = last_bar_start + TICKS_PER_BAR / 2;
  track.addNote(NoteEventBuilder::create(second_note_start, TICKS_PER_BEAT, 64, 80));

  std::vector<MidiTrack*> tracks = {&track};
  std::vector<Section> sections = {section_};
  PostProcessor::applyAllExitPatterns(tracks, sections);

  // First note should be truncated to second note's start
  // Second note should extend to section end
  for (const auto& note : track.notes()) {
    Tick note_end = note.start_tick + note.duration;
    if (note.start_tick == last_bar_start) {
      EXPECT_EQ(note_end, second_note_start)
          << "First note should be truncated to second note's start";
    } else if (note.start_tick == second_note_start) {
      EXPECT_EQ(note_end, section_end)
          << "Second note should extend to section end";
    }
  }
}

TEST_F(SustainPatternTest, HandlesNotesOutsideLastBar) {
  // Notes outside the last bar should not be affected
  MidiTrack track;
  Tick section_end = 4 * TICKS_PER_BAR;
  Tick last_bar_start = section_end - TICKS_PER_BAR;
  Tick original_duration = TICKS_PER_BEAT;

  // Note before last bar (should be unchanged)
  track.addNote(NoteEventBuilder::create(last_bar_start - TICKS_PER_BAR, original_duration, 60, 80));

  // Note in last bar (should be extended)
  track.addNote(NoteEventBuilder::create(last_bar_start, original_duration, 64, 80));

  std::vector<MidiTrack*> tracks = {&track};
  std::vector<Section> sections = {section_};
  PostProcessor::applyAllExitPatterns(tracks, sections);

  for (const auto& note : track.notes()) {
    if (note.start_tick < last_bar_start) {
      // Note before last bar should be unchanged
      EXPECT_EQ(note.duration, original_duration)
          << "Notes before last bar should not be modified";
    } else {
      // Note in last bar should be extended to section end
      Tick note_end = note.start_tick + note.duration;
      EXPECT_EQ(note_end, section_end)
          << "Notes in last bar should extend to section end";
    }
  }
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
  track.addNote(NoteEventBuilder::create(b_drop_zone - TICKS_PER_BEAT, TICKS_PER_BEAT * 2, 60, 80));

  // Add notes in Outro section (affected by ritardando)
  Tick outro_rit_zone = 20 * TICKS_PER_BAR - 4 * TICKS_PER_BAR;
  track.addNote(NoteEventBuilder::create(outro_rit_zone, TICKS_PER_BEAT, 72, 90));
  track.addNote(NoteEventBuilder::create(19 * TICKS_PER_BAR, TICKS_PER_BEAT, 72, 90));  // Final note

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

// ============================================================================
// Provenance Tests
// ============================================================================

#ifdef MIDISKETCH_NOTE_PROVENANCE

TEST_F(EnhancedFinalHitTest, AddedNotesHavePostProcessProvenance) {
  // Notes added by applyEnhancedFinalHit should have provenance set

  MidiTrack bass_track;
  MidiTrack drum_track;
  // Add a note so drum_track is not empty (required for applyEnhancedFinalHit)
  drum_track.addNote(NoteEventBuilder::create(0, TICKS_PER_BEAT / 2, KICK, 80));

  PostProcessor::applyEnhancedFinalHit(&bass_track, &drum_track, nullptr, nullptr, section_);

  Tick final_beat_start = 4 * TICKS_PER_BAR - TICKS_PER_BEAT;

  // Check bass note provenance
  for (const auto& note : bass_track.notes()) {
    if (note.start_tick >= final_beat_start) {
      EXPECT_EQ(note.prov_source, static_cast<uint8_t>(NoteSource::PostProcess))
          << "Added bass note should have PostProcess provenance";
      EXPECT_EQ(note.prov_lookup_tick, final_beat_start)
          << "prov_lookup_tick should match start tick";
      EXPECT_EQ(note.prov_original_pitch, note.note)
          << "prov_original_pitch should match note pitch";
      EXPECT_EQ(note.prov_chord_degree, -1)
          << "prov_chord_degree should be -1 for PostProcessor notes";
    }
  }

  // Check drum notes provenance (kick and crash)
  for (const auto& note : drum_track.notes()) {
    if (note.start_tick >= final_beat_start) {
      EXPECT_EQ(note.prov_source, static_cast<uint8_t>(NoteSource::PostProcess))
          << "Added drum note should have PostProcess provenance";
      EXPECT_EQ(note.prov_lookup_tick, final_beat_start)
          << "prov_lookup_tick should match start tick";
      EXPECT_EQ(note.prov_original_pitch, note.note)
          << "prov_original_pitch should match note pitch";
    }
  }
}

TEST_F(EnhancedFinalHitTest, BassPitchUsesCollisionCheckWhenHarmonyProvided) {
  // When harmony context reports collision for default C2 (36), the function
  // should find a safe alternative via getSafePitchCandidates.

  test::StubHarmonyContext harmony;
  // Mark all pitches as unsafe to force collision resolution
  harmony.setAllPitchesSafe(false);
  // Provide chord tones so getSafePitchCandidates can find alternatives
  harmony.setChordTones({0, 4, 7});  // C major triad

  MidiTrack bass_track;  // Empty - no existing bass note on final beat

  PostProcessor::applyEnhancedFinalHit(&bass_track, nullptr, nullptr, nullptr, section_,
                                        &harmony);

  // Bass note should still be added (collision check is best-effort)
  EXPECT_FALSE(bass_track.notes().empty())
      << "Should still add bass note even when collision detected";

  // Verify it was placed on the final beat
  Tick final_beat_start = 4 * TICKS_PER_BAR - TICKS_PER_BEAT;
  bool has_final_bass = false;
  for (const auto& note : bass_track.notes()) {
    if (note.start_tick == final_beat_start) {
      has_final_bass = true;
    }
  }
  EXPECT_TRUE(has_final_bass) << "Bass note should be on final beat";
}

TEST_F(EnhancedFinalHitTest, BassPitchUnchangedWhenSafe) {
  // When harmony context reports the default pitch as safe,
  // the bass note should keep the default C2 (36).

  test::StubHarmonyContext harmony;
  harmony.setAllPitchesSafe(true);
  harmony.setChordTones({0, 4, 7});

  MidiTrack bass_track;  // Empty - forces adding a new note

  PostProcessor::applyEnhancedFinalHit(&bass_track, nullptr, nullptr, nullptr, section_,
                                        &harmony);

  Tick final_beat_start = 4 * TICKS_PER_BAR - TICKS_PER_BEAT;
  for (const auto& note : bass_track.notes()) {
    if (note.start_tick == final_beat_start) {
      EXPECT_EQ(note.note, 36u)
          << "Bass note should remain C2 (36) when pitch is safe";
    }
  }
}

TEST_F(EnhancedFinalHitTest, BassPitchFallsBackToDefaultWithoutHarmony) {
  // When no harmony context is provided (nullptr), the default C2 should be used.

  MidiTrack bass_track;  // Empty

  PostProcessor::applyEnhancedFinalHit(&bass_track, nullptr, nullptr, nullptr, section_,
                                        nullptr);

  Tick final_beat_start = 4 * TICKS_PER_BAR - TICKS_PER_BEAT;
  for (const auto& note : bass_track.notes()) {
    if (note.start_tick == final_beat_start) {
      EXPECT_EQ(note.note, 36u)
          << "Bass note should be C2 (36) when no harmony context";
    }
  }
}

#ifdef MIDISKETCH_NOTE_PROVENANCE
TEST_F(EnhancedFinalHitTest, BassPitchProvenanceTracksOriginalWhenCollisionResolved) {
  // When collision resolution changes the pitch, prov_original_pitch should
  // still record the default C2 (36) for debugging.

  test::StubHarmonyContext harmony;
  harmony.setAllPitchesSafe(false);
  harmony.setChordTones({0, 4, 7});

  MidiTrack bass_track;

  PostProcessor::applyEnhancedFinalHit(&bass_track, nullptr, nullptr, nullptr, section_,
                                        &harmony);

  Tick final_beat_start = 4 * TICKS_PER_BAR - TICKS_PER_BEAT;
  for (const auto& note : bass_track.notes()) {
    if (note.start_tick == final_beat_start) {
      EXPECT_EQ(note.prov_original_pitch, 36u)
          << "prov_original_pitch should record original C2 before collision resolution";
      EXPECT_EQ(note.prov_source, static_cast<uint8_t>(NoteSource::PostProcess))
          << "Source should be PostProcess";
    }
  }
}
#endif  // MIDISKETCH_NOTE_PROVENANCE

TEST_F(ChorusDropTest, DrumHitCrashHasPostProcessProvenance) {
  // Crash cymbal added by DrumHit style should have provenance set

  MidiTrack track;
  // Add notes in B section
  track.addNote(NoteEventBuilder::create(0, TICKS_PER_BEAT, 60, 80));

  MidiTrack drum_track;
  // Add a note so drum_track is not empty
  drum_track.addNote(NoteEventBuilder::create(0, TICKS_PER_BEAT / 2, KICK, 80));

  std::vector<MidiTrack*> tracks = {&track};

  // Apply with DrumHit style to add crash at chorus entry
  PostProcessor::applyChorusDrop(tracks, sections_, &drum_track, ChorusDropStyle::DrumHit);

  Tick chorus_start = sections_[1].start_tick;

  bool found_crash = false;
  for (const auto& note : drum_track.notes()) {
    if (note.start_tick == chorus_start && note.note == CRASH) {
      found_crash = true;
      EXPECT_EQ(note.prov_source, static_cast<uint8_t>(NoteSource::PostProcess))
          << "Added crash should have PostProcess provenance";
      EXPECT_EQ(note.prov_lookup_tick, chorus_start)
          << "prov_lookup_tick should match chorus start";
      EXPECT_EQ(note.prov_original_pitch, CRASH)
          << "prov_original_pitch should be CRASH";
      EXPECT_EQ(note.prov_chord_degree, -1)
          << "prov_chord_degree should be -1 for PostProcessor notes";
    }
  }

  EXPECT_TRUE(found_crash) << "DrumHit style should add crash at chorus entry";
}

#endif  // MIDISKETCH_NOTE_PROVENANCE

// ============================================================================
// Phase 3: Micro-Timing Offset Tests
// ============================================================================

TEST(MicroTimingTest, VocalTimingVariesByPhrasePosition) {
  // Vocal timing offsets are capped to ±2 ticks for pitch accuracy.
  // Human body timing model still runs, but final offset is clamped.
  MidiTrack vocal, bass, drums;

  // Create 4-bar section
  std::vector<Section> sections;
  Section section;
  section.type = SectionType::A;
  section.start_tick = 0;
  section.bars = 4;
  sections.push_back(section);

  // Add notes at different phrase positions
  Tick phrase_start_tick = 0;
  vocal.addNote(NoteEventBuilder::create(phrase_start_tick, TICKS_PER_BEAT, 60, 80));
  Tick phrase_middle_tick = TICKS_PER_BAR * 2;
  vocal.addNote(NoteEventBuilder::create(phrase_middle_tick, TICKS_PER_BEAT, 62, 80));
  Tick phrase_end_tick = TICKS_PER_BAR * 3;
  vocal.addNote(NoteEventBuilder::create(phrase_end_tick, TICKS_PER_BEAT, 64, 80));

  Tick orig_start = vocal.notes()[0].start_tick;
  Tick orig_middle = vocal.notes()[1].start_tick;
  Tick orig_end = vocal.notes()[2].start_tick;

  PostProcessor::applyMicroTimingOffsets(vocal, bass, drums, &sections);

  // All vocal offsets capped to ±2 ticks
  Tick start_offset = vocal.notes()[0].start_tick - orig_start;
  Tick middle_offset = vocal.notes()[1].start_tick - orig_middle;
  Tick end_offset = vocal.notes()[2].start_tick - orig_end;

  EXPECT_GE(start_offset, 0) << "Vocal offset should be non-negative (forward lean)";
  EXPECT_LE(start_offset, 2) << "Vocal offset capped at +2";
  EXPECT_GE(middle_offset, 0);
  EXPECT_LE(middle_offset, 2);
  EXPECT_GE(end_offset, 0);
  EXPECT_LE(end_offset, 2);
}

TEST(MicroTimingTest, VocalTimingUniformWithoutSections) {
  // Without sections, vocal should get uniform +4 offset
  MidiTrack vocal, bass, drums;

  Tick start_tick = TICKS_PER_BAR;
  vocal.addNote(NoteEventBuilder::create(start_tick, TICKS_PER_BEAT, 60, 80));

  Tick orig = vocal.notes()[0].start_tick;

  // Apply without sections (nullptr)
  PostProcessor::applyMicroTimingOffsets(vocal, bass, drums, nullptr);

  // Should have uniform +4 offset
  EXPECT_EQ(vocal.notes()[0].start_tick, orig + 4) << "Without sections, vocal gets +4";
}

TEST(MicroTimingTest, BassAlwaysLaysBack) {
  // Bass should always get -4 offset regardless of sections
  MidiTrack vocal, bass, drums;

  std::vector<Section> sections;
  Section section;
  section.type = SectionType::A;
  section.start_tick = 0;
  section.bars = 4;
  sections.push_back(section);

  Tick start_tick = TICKS_PER_BAR;
  bass.addNote(NoteEventBuilder::create(start_tick, TICKS_PER_BEAT, 36, 80));

  Tick orig = bass.notes()[0].start_tick;

  PostProcessor::applyMicroTimingOffsets(vocal, bass, drums, &sections);

  // Bass should lay back (-4)
  EXPECT_EQ(bass.notes()[0].start_tick, orig - 4) << "Bass should lay back by 4 ticks";
}

TEST(MicroTimingTest, DrumTimingByInstrument) {
  // Hi-hat pushes ahead, snare lays back, kick is tight
  // Now with beat-position-aware timing for enhanced "pocket" feel
  MidiTrack vocal, bass, drums;

  constexpr uint8_t HH = 42;  // Closed hi-hat
  constexpr uint8_t SD = 38;  // Snare
  constexpr uint8_t BD = 36;  // Kick

  Tick start = TICKS_PER_BAR;  // Beat 0 (downbeat)
  drums.addNote(NoteEventBuilder::create(start, 60, HH, 80));
  drums.addNote(NoteEventBuilder::create(start, 60, SD, 80));
  drums.addNote(NoteEventBuilder::create(start, 60, BD, 80));

  PostProcessor::applyMicroTimingOffsets(vocal, bass, drums, nullptr);

  // Find each drum note
  // At beat 0 (downbeat), timing offsets are:
  // - Hi-hat: +8 (standard push)
  // - Snare: -4 (not on beat 1 or 3, so standard layback)
  // - Kick: -1 (tight on downbeat for anchor)
  for (const auto& note : drums.notes()) {
    if (note.note == HH) {
      EXPECT_EQ(note.start_tick, start + 8) << "Hi-hat should push ahead by 8";
    } else if (note.note == SD) {
      EXPECT_EQ(note.start_tick, start - 4) << "Snare should lay back by 4 on downbeat";
    } else if (note.note == BD) {
      EXPECT_EQ(note.start_tick, start - 1) << "Kick should be tight (-1) on downbeat";
    }
  }
}

// ============================================================================
// Drive Feel Integration Tests for Micro-Timing
// ============================================================================

TEST(MicroTimingTest, DriveFeelScalesTimingOffsets) {
  // Test that drive_feel properly scales timing offsets
  constexpr uint8_t HH = 42;

  Tick start = TICKS_PER_BAR;

  // Laid-back (drive=0): offsets should be halved (0.5x)
  MidiTrack vocal_laid, bass_laid, drums_laid;
  drums_laid.addNote(NoteEventBuilder::create(start, 60, HH, 80));
  bass_laid.addNote(NoteEventBuilder::create(start, 60, 36, 80));
  PostProcessor::applyMicroTimingOffsets(vocal_laid, bass_laid, drums_laid, nullptr, 0);

  // Neutral (drive=50): offsets should be 1.0x
  MidiTrack vocal_neutral, bass_neutral, drums_neutral;
  drums_neutral.addNote(NoteEventBuilder::create(start, 60, HH, 80));
  bass_neutral.addNote(NoteEventBuilder::create(start, 60, 36, 80));
  PostProcessor::applyMicroTimingOffsets(vocal_neutral, bass_neutral, drums_neutral, nullptr, 50);

  // Aggressive (drive=100): offsets should be 1.5x
  MidiTrack vocal_agg, bass_agg, drums_agg;
  drums_agg.addNote(NoteEventBuilder::create(start, 60, HH, 80));
  bass_agg.addNote(NoteEventBuilder::create(start, 60, 36, 80));
  PostProcessor::applyMicroTimingOffsets(vocal_agg, bass_agg, drums_agg, nullptr, 100);

  // Hi-hat offsets: base=8, so laid-back=4, neutral=8, aggressive=12
  EXPECT_EQ(drums_laid.notes()[0].start_tick, start + 4)
      << "Laid-back hi-hat should push ahead by 4 (0.5x of 8)";
  EXPECT_EQ(drums_neutral.notes()[0].start_tick, start + 8)
      << "Neutral hi-hat should push ahead by 8 (1.0x)";
  EXPECT_EQ(drums_agg.notes()[0].start_tick, start + 12)
      << "Aggressive hi-hat should push ahead by 12 (1.5x of 8)";

  // Bass offsets: base=-4, so laid-back=-2, neutral=-4, aggressive=-6
  EXPECT_EQ(bass_laid.notes()[0].start_tick, start - 2)
      << "Laid-back bass should lay back by 2 (0.5x of 4)";
  EXPECT_EQ(bass_neutral.notes()[0].start_tick, start - 4)
      << "Neutral bass should lay back by 4 (1.0x)";
  EXPECT_EQ(bass_agg.notes()[0].start_tick, start - 6)
      << "Aggressive bass should lay back by 6 (1.5x of 4)";
}

TEST(MicroTimingTest, DriveFeelAffectsVocalPhraseOffsets) {
  // Vocal offsets are capped to ±2 regardless of drive_feel.
  // Drive feel still affects drums/bass but vocal stays tight.
  MidiTrack vocal, bass, drums;

  std::vector<Section> sections;
  Section section;
  section.type = SectionType::A;
  section.start_tick = 0;
  section.bars = 4;
  sections.push_back(section);

  Tick phrase_start = 0;
  vocal.addNote(NoteEventBuilder::create(phrase_start, TICKS_PER_BEAT, 60, 80));

  Tick orig = vocal.notes()[0].start_tick;

  // With aggressive drive (100), vocal offset is still capped to ±2
  PostProcessor::applyMicroTimingOffsets(vocal, bass, drums, &sections, 100);

  Tick offset = vocal.notes()[0].start_tick - orig;
  EXPECT_GE(offset, 0) << "Vocal offset should be non-negative";
  EXPECT_LE(offset, 2) << "Vocal offset capped at +2 even with aggressive drive";
}

TEST(MicroTimingTest, DefaultDriveFeelMatchesNeutral) {
  constexpr uint8_t HH = 42;
  Tick start = TICKS_PER_BAR;

  // Default (no drive_feel specified)
  MidiTrack vocal_def, bass_def, drums_def;
  drums_def.addNote(NoteEventBuilder::create(start, 60, HH, 80));
  PostProcessor::applyMicroTimingOffsets(vocal_def, bass_def, drums_def, nullptr);

  // Explicit neutral (drive_feel = 50)
  MidiTrack vocal_neutral, bass_neutral, drums_neutral;
  drums_neutral.addNote(NoteEventBuilder::create(start, 60, HH, 80));
  PostProcessor::applyMicroTimingOffsets(vocal_neutral, bass_neutral, drums_neutral, nullptr, 50);

  // Both should have same offset
  EXPECT_EQ(drums_def.notes()[0].start_tick, drums_neutral.notes()[0].start_tick)
      << "Default drive_feel should match neutral (50)";
}

// ============================================================================
// Phase 1: Human Body Timing Model Tests
// ============================================================================

TEST(PostProcessorTest, HighPitchTimingDelay) {
  // Vocal offsets capped to ±2 ticks. High pitch delay is computed internally
  // but the final output is clamped, so both low and high notes stay within cap.
  MidiTrack vocal, bass, drums;

  std::vector<Section> sections;
  Section section;
  section.type = SectionType::A;
  section.start_tick = 0;
  section.bars = 4;
  sections.push_back(section);

  Tick start = TICKS_PER_BAR;
  vocal.addNote(NoteEventBuilder::create(start, TICKS_PER_BEAT, 60, 80));
  vocal.addNote(NoteEventBuilder::create(start + TICKS_PER_BEAT, TICKS_PER_BEAT, 80, 80));

  Tick orig_low = vocal.notes()[0].start_tick;
  Tick orig_high = vocal.notes()[1].start_tick;

  PostProcessor::applyMicroTimingOffsets(vocal, bass, drums, &sections);

  Tick low_offset = vocal.notes()[0].start_tick - orig_low;
  Tick high_offset = vocal.notes()[1].start_tick - orig_high;

  // Both offsets are within ±2 cap
  EXPECT_GE(low_offset, 0);
  EXPECT_LE(low_offset, 2);
  EXPECT_GE(high_offset, 0);
  EXPECT_LE(high_offset, 2);
}

TEST(PostProcessorTest, LeapLandingTimingDelay) {
  // Vocal offsets capped to ±2 ticks. Leap delay is computed internally
  // but clamped, so both step and leap notes stay within cap.
  MidiTrack vocal, bass, drums;

  std::vector<Section> sections;
  Section section;
  section.type = SectionType::A;
  section.start_tick = 0;
  section.bars = 4;
  sections.push_back(section);

  Tick start = TICKS_PER_BAR;
  vocal.addNote(NoteEventBuilder::create(start, TICKS_PER_BEAT, 60, 80));
  vocal.addNote(NoteEventBuilder::create(start + TICKS_PER_BEAT, TICKS_PER_BEAT, 62, 80));
  vocal.addNote(NoteEventBuilder::create(start + 2 * TICKS_PER_BEAT, TICKS_PER_BEAT, 74, 80));

  Tick orig_step = vocal.notes()[1].start_tick;
  Tick orig_leap = vocal.notes()[2].start_tick;

  PostProcessor::applyMicroTimingOffsets(vocal, bass, drums, &sections);

  Tick step_offset = vocal.notes()[1].start_tick - orig_step;
  Tick leap_offset = vocal.notes()[2].start_tick - orig_leap;

  // Both offsets within ±2 cap
  EXPECT_GE(step_offset, 0);
  EXPECT_LE(step_offset, 2);
  EXPECT_GE(leap_offset, 0);
  EXPECT_LE(leap_offset, 2);
}

TEST(PostProcessorTest, PostBreathSoftStart) {
  // Vocal offsets capped to ±2 ticks. Post-breath delay is computed internally
  // but clamped, so all notes stay within the cap.
  MidiTrack vocal, bass, drums;

  std::vector<Section> sections;
  Section section;
  section.type = SectionType::A;
  section.start_tick = 0;
  section.bars = 4;
  sections.push_back(section);

  Tick start = TICKS_PER_BAR;
  vocal.addNote(NoteEventBuilder::create(start, TICKS_PER_BEAT, 67, 80));
  vocal.addNote(NoteEventBuilder::create(start + TICKS_PER_BEAT, TICKS_PER_BEAT, 67, 80));
  vocal.addNote(NoteEventBuilder::create(start + 3 * TICKS_PER_BEAT, TICKS_PER_BEAT, 67, 80));

  Tick orig_first = vocal.notes()[0].start_tick;
  Tick orig_second = vocal.notes()[1].start_tick;
  Tick orig_third = vocal.notes()[2].start_tick;

  PostProcessor::applyMicroTimingOffsets(vocal, bass, drums, &sections);

  Tick first_offset = vocal.notes()[0].start_tick - orig_first;
  Tick second_offset = vocal.notes()[1].start_tick - orig_second;
  Tick third_offset = vocal.notes()[2].start_tick - orig_third;

  // All vocal offsets within ±2 cap
  EXPECT_GE(first_offset, 0);
  EXPECT_LE(first_offset, 2);
  EXPECT_GE(second_offset, 0);
  EXPECT_LE(second_offset, 2);
  EXPECT_GE(third_offset, 0);
  EXPECT_LE(third_offset, 2);
}

TEST(PostProcessorTest, HumanBodyTimingCombined) {
  // Vocal offsets capped to ±2 ticks. All three human body timing effects
  // (high pitch, leap, post-breath) still compute internally, but the final
  // offset is clamped to keep vocal on-grid.
  MidiTrack vocal, bass, drums;

  std::vector<Section> sections;
  Section section;
  section.type = SectionType::A;
  section.start_tick = 0;
  section.bars = 4;
  sections.push_back(section);

  Tick start = TICKS_PER_BAR;
  vocal.addNote(NoteEventBuilder::create(start, TICKS_PER_BEAT, 60, 80));
  vocal.addNote(NoteEventBuilder::create(start + 3 * TICKS_PER_BEAT, TICKS_PER_BEAT, 84, 80));

  Tick orig_high = vocal.notes()[1].start_tick;

  PostProcessor::applyMicroTimingOffsets(vocal, bass, drums, &sections);

  Tick offset = vocal.notes()[1].start_tick - orig_high;

  // Combined offset capped to ±2 ticks
  EXPECT_GE(offset, 0) << "Vocal offset should be non-negative";
  EXPECT_LE(offset, 2) << "Vocal offset capped at +2 even with combined delays";
}

// ============================================================================
// Motif-Vocal Clash Resolution Tests
// ============================================================================

TEST(PostProcessorTest, FixMotifVocalClashesResolveMinor2nd) {
  // Motif C4 (48) clashing with Vocal B3 (47) - minor 2nd below
  MidiTrack motif, vocal;
  motif.addNote(NoteEventBuilder::create(0, 480, 48, 80));  // C4
  vocal.addNote(NoteEventBuilder::create(0, 480, 47, 80));  // B3 (minor 2nd below)

  test::StubHarmonyContext harmony;
  harmony.setChordDegree(0);  // C major (chord tones: C, E, G -> pitch classes 0, 4, 7)

  PostProcessor::fixMotifVocalClashes(motif, vocal, harmony);

  // Motif should snap to nearest chord tone (C, E, or G)
  // From C4 (48), nearest chord tones are C4 (48), E3 (40 - far), G3 (43), E4 (52), G4 (55)
  // The algorithm should pick a chord tone that doesn't clash
  int pc = motif.notes()[0].note % 12;
  EXPECT_TRUE(pc == 0 || pc == 4 || pc == 7)
      << "Motif pitch class should be C(0), E(4), or G(7), got " << pc;
}

TEST(PostProcessorTest, FixMotifVocalClashesResolveMajor7th) {
  // Motif C4 (60) clashing with Vocal B4 (71) - major 7th above
  MidiTrack motif, vocal;
  motif.addNote(NoteEventBuilder::create(0, 480, 60, 80));  // C4
  vocal.addNote(NoteEventBuilder::create(0, 480, 71, 80));  // B4 (major 7th above)

  test::StubHarmonyContext harmony;
  harmony.setChordDegree(0);  // C major

  PostProcessor::fixMotifVocalClashes(motif, vocal, harmony);

  // Motif C4 clashes with Vocal B4 -> should snap to chord tone
  int pc = motif.notes()[0].note % 12;
  EXPECT_TRUE(pc == 0 || pc == 4 || pc == 7)
      << "Motif pitch class should be C(0), E(4), or G(7), got " << pc;
}

TEST(PostProcessorTest, FixMotifVocalClashesResolveMajor2ndClose) {
  // Motif D4 (62) clashing with Vocal C4 (60) - major 2nd in close voicing
  MidiTrack motif, vocal;
  motif.addNote(NoteEventBuilder::create(0, 480, 62, 80));  // D4
  vocal.addNote(NoteEventBuilder::create(0, 480, 60, 80));  // C4 (major 2nd below)

  test::StubHarmonyContext harmony;
  harmony.setChordDegree(0);  // C major (chord tones: C, E, G)

  PostProcessor::fixMotifVocalClashes(motif, vocal, harmony);

  // Motif D4 clashes with Vocal C4 -> should snap to chord tone
  int pc = motif.notes()[0].note % 12;
  EXPECT_TRUE(pc == 0 || pc == 4 || pc == 7)
      << "Motif pitch class should be C(0), E(4), or G(7), got " << pc;
}

TEST(PostProcessorTest, FixMotifVocalClashesIgnoresMajor9th) {
  // Motif D5 (74) vs Vocal C4 (60) - major 9th (14 semitones)
  // Major 2nd interval class (2), but actual interval >= 12, so OK
  MidiTrack motif, vocal;
  motif.addNote(NoteEventBuilder::create(0, 480, 74, 80));  // D5
  vocal.addNote(NoteEventBuilder::create(0, 480, 60, 80));  // C4 (major 9th = 14 semitones)

  test::StubHarmonyContext harmony;
  harmony.setChordDegree(0);

  PostProcessor::fixMotifVocalClashes(motif, vocal, harmony);

  // Major 9th is a tension, not a close-voicing clash - should not change
  EXPECT_EQ(motif.notes()[0].note, 74)
      << "Major 9th (wide interval) should not be modified";
}

TEST(PostProcessorTest, FixMotifVocalClashesIgnoresConsonant) {
  // Motif C4 clashing with Vocal G4 - perfect 5th (consonant, should NOT change)
  MidiTrack motif, vocal;
  motif.addNote(NoteEventBuilder::create(0, 480, 60, 80));  // C4
  vocal.addNote(NoteEventBuilder::create(0, 480, 67, 80));  // G4 (perfect 5th - consonant)

  test::StubHarmonyContext harmony;
  harmony.setChordDegree(0);

  PostProcessor::fixMotifVocalClashes(motif, vocal, harmony);

  // No change expected for consonant interval
  EXPECT_EQ(motif.notes()[0].note, 60)
      << "Consonant interval should not be modified";
}

TEST(PostProcessorTest, FixMotifVocalClashesHandlesNoOverlap) {
  // Motif and vocal don't overlap in time - no change expected
  MidiTrack motif, vocal;
  motif.addNote(NoteEventBuilder::create(0, 480, 60, 80));      // C4 at tick 0-480
  vocal.addNote(NoteEventBuilder::create(960, 480, 61, 80));    // C#4 at tick 960-1440 (no overlap)

  test::StubHarmonyContext harmony;
  harmony.setChordDegree(0);

  PostProcessor::fixMotifVocalClashes(motif, vocal, harmony);

  // No change expected - notes don't overlap
  EXPECT_EQ(motif.notes()[0].note, 60)
      << "Non-overlapping notes should not be modified";
}

TEST(PostProcessorTest, FixMotifVocalClashesUpdatesProvenance) {
  // Verify provenance is updated when fixing clashes
  MidiTrack motif, vocal;
  motif.addNote(NoteEventBuilder::create(0, 480, 48, 80));  // C4
  vocal.addNote(NoteEventBuilder::create(0, 480, 47, 80));  // B3 (minor 2nd clash)

  test::StubHarmonyContext harmony;
  harmony.setChordDegree(0);

  PostProcessor::fixMotifVocalClashes(motif, vocal, harmony);

  // Check provenance was updated
  const auto& note = motif.notes()[0];
  EXPECT_EQ(note.prov_source, static_cast<uint8_t>(NoteSource::CollisionAvoid))
      << "Provenance source should be CollisionAvoid";
  EXPECT_EQ(note.prov_original_pitch, 48)
      << "Original pitch should be preserved in provenance";
  EXPECT_EQ(note.prov_chord_degree, 0)
      << "Chord degree should be recorded";
}

// Core fix test: Motif is already a chord tone but clashes with vocal
// This was the root cause of the IdolHyper dissonance bug (seed 88888)
TEST(PostProcessorTest, FixMotifVocalClashesWhenMotifIsChordTone) {
  // G major chord (degree 4 = V): chord tones are G(7), B(11), D(2)
  // Motif B3 (59) is a chord tone, but clashes with Vocal C4 (60) - minor 2nd
  // The old code would snap B3 to nearest chord tone (B3), leaving clash unresolved
  // The fix should move to a different chord tone (G or D) at different octave
  MidiTrack motif, vocal;
  motif.addNote(NoteEventBuilder::create(0, 480, 59, 80));  // B3 - chord tone of G major
  vocal.addNote(NoteEventBuilder::create(0, 480, 60, 80));  // C4 - creates minor 2nd clash

  test::StubHarmonyContext harmony;
  harmony.setChordDegree(4);  // G major (V chord): G-B-D

  PostProcessor::fixMotifVocalClashes(motif, vocal, harmony);

  // Result should NOT clash with vocal (C4 = 60)
  // Must be chord tone (G, B, or D) and NOT create minor 2nd/major 7th/major 2nd with C4
  uint8_t result = motif.notes()[0].note;
  int interval = std::abs(static_cast<int>(result) - 60);
  int interval_class = interval % 12;

  // Check it's a chord tone
  int pc = result % 12;
  EXPECT_TRUE(pc == 7 || pc == 11 || pc == 2)
      << "Result should be chord tone (G=7, B=11, D=2), got pc=" << pc;

  // Check no dissonance with C4 (60)
  bool is_dissonant = (interval_class == 1) ||             // minor 2nd
                      (interval_class == 11) ||            // major 7th
                      (interval_class == 2 && interval < 12);  // major 2nd close
  EXPECT_FALSE(is_dissonant)
      << "Result pitch " << static_cast<int>(result)
      << " should not clash with vocal C4 (60), interval=" << interval;
}

// Test: When nearest chord tone would also clash, find alternative octave
TEST(PostProcessorTest, FixMotifVocalClashesAvoidsNearestWhenItClashes) {
  // C major chord (degree 0): chord tones are C(0), E(4), G(7)
  // Motif D4 (62) clashes with Vocal C4 (60) - major 2nd
  // Nearest chord tone to D4 is C4 or E4
  // But C4 would be unison with vocal, E4 (64) creates major 3rd (ok)
  // The fix should prefer E4 or G4 over C4 if C4 would create new issues
  MidiTrack motif, vocal;
  motif.addNote(NoteEventBuilder::create(0, 480, 62, 80));  // D4
  vocal.addNote(NoteEventBuilder::create(0, 480, 60, 80));  // C4 - major 2nd clash

  test::StubHarmonyContext harmony;
  harmony.setChordDegree(0);  // C major

  PostProcessor::fixMotifVocalClashes(motif, vocal, harmony);

  uint8_t result = motif.notes()[0].note;
  int pc = result % 12;

  // Should be chord tone
  EXPECT_TRUE(pc == 0 || pc == 4 || pc == 7)
      << "Result should be chord tone (C=0, E=4, G=7), got pc=" << pc;

  // Check no dissonance with C4
  int interval = std::abs(static_cast<int>(result) - 60);
  int interval_class = interval % 12;
  bool is_dissonant = (interval_class == 1) || (interval_class == 11) ||
                      (interval_class == 2 && interval < 12);
  EXPECT_FALSE(is_dissonant)
      << "Result should not create dissonance with vocal";
}

// Test: Octave displacement to avoid clash
TEST(PostProcessorTest, FixMotifVocalClashesUsesOctaveDisplacement) {
  // Am chord (degree 5 = vi): chord tones are A(9), C(0), E(4)
  // Motif B4 (71) clashes with Vocal C5 (72) - minor 2nd
  // The fix should find a chord tone that doesn't create dissonance
  // Note: Unison (same pitch) is musically acceptable, not dissonant
  MidiTrack motif, vocal;
  motif.addNote(NoteEventBuilder::create(0, 480, 71, 80));  // B4
  vocal.addNote(NoteEventBuilder::create(0, 480, 72, 80));  // C5 - minor 2nd clash

  test::StubHarmonyContext harmony;
  harmony.setChordDegree(5);  // Am (vi chord): A-C-E

  PostProcessor::fixMotifVocalClashes(motif, vocal, harmony);

  uint8_t result = motif.notes()[0].note;
  int pc = result % 12;

  // Should be chord tone (A, C, or E)
  EXPECT_TRUE(pc == 9 || pc == 0 || pc == 4)
      << "Result should be Am chord tone (A=9, C=0, E=4), got pc=" << pc;

  // Should not create dissonance with vocal (unison is OK)
  int interval = std::abs(static_cast<int>(result) - 72);
  int interval_class = interval % 12;
  bool is_dissonant = (interval_class == 1) || (interval_class == 11) ||
                      (interval_class == 2 && interval < 12);
  EXPECT_FALSE(is_dissonant)
      << "Result should not create dissonance with vocal C5";
}

// Test: Multiple motif notes with different clashes in same track
TEST(PostProcessorTest, FixMotifVocalClashesHandlesMultipleNotes) {
  MidiTrack motif, vocal;
  // Multiple motif notes at different times
  motif.addNote(NoteEventBuilder::create(0, 480, 59, 80));     // B3 - will clash with vocal C4
  motif.addNote(NoteEventBuilder::create(960, 480, 65, 80));   // F4 - will clash with vocal E4
  motif.addNote(NoteEventBuilder::create(1920, 480, 67, 80));  // G4 - consonant, no change needed

  vocal.addNote(NoteEventBuilder::create(0, 480, 60, 80));     // C4 - minor 2nd with B3
  vocal.addNote(NoteEventBuilder::create(960, 480, 64, 80));   // E4 - minor 2nd with F4
  vocal.addNote(NoteEventBuilder::create(1920, 480, 67, 80));  // G4 - unison with G4 (ok)

  test::StubHarmonyContext harmony;
  harmony.setChordDegree(0);  // C major throughout

  PostProcessor::fixMotifVocalClashes(motif, vocal, harmony);

  // First note (B3) should be resolved
  int interval1 = std::abs(static_cast<int>(motif.notes()[0].note) - 60);
  int ic1 = interval1 % 12;
  EXPECT_FALSE(ic1 == 1 || ic1 == 11 || (ic1 == 2 && interval1 < 12))
      << "First motif note should not clash with C4";

  // Second note (F4) should be resolved
  int interval2 = std::abs(static_cast<int>(motif.notes()[1].note) - 64);
  int ic2 = interval2 % 12;
  EXPECT_FALSE(ic2 == 1 || ic2 == 11 || (ic2 == 2 && interval2 < 12))
      << "Second motif note should not clash with E4";

  // Third note should remain G4 (unison is ok)
  EXPECT_EQ(motif.notes()[2].note, 67)
      << "Third note (G4 unison) should not change";
}

// Regression test: IdolHyper seed 88888 scenario
// This reproduces the actual bug where B3 (chord tone of G major) clashed with C4
// The old code would not change B3 because it was already a chord tone
TEST(PostProcessorTest, RegressionIdolHyperSeed88888) {
  // Reproduces the clash at tick 30720 from IdolHyper seed 88888:
  // - Chord changes from C major to G major at tick 30720
  // - Vocal C4 is sustained across the chord change
  // - Motif B3 is generated on G major (B is chord tone)
  // - B3 vs C4 = minor 2nd clash
  MidiTrack motif, vocal;

  // Simulate the overlapping notes at tick 30720
  motif.addNote(NoteEventBuilder::create(30720, 240, 59, 80));  // B3 - chord tone of G major
  vocal.addNote(NoteEventBuilder::create(30715, 480, 60, 80));  // C4 - sustained, overlaps with motif

  test::StubHarmonyContext harmony;
  harmony.setChordDegree(4);  // G major (V chord): G-B-D

  PostProcessor::fixMotifVocalClashes(motif, vocal, harmony);

  uint8_t result = motif.notes()[0].note;

  // Verify: result should be chord tone of G major
  int pc = result % 12;
  EXPECT_TRUE(pc == 7 || pc == 11 || pc == 2)
      << "Result should be G major chord tone (G=7, B=11, D=2), got pc=" << pc;

  // Verify: result should NOT clash with C4 (60)
  int interval = std::abs(static_cast<int>(result) - 60);
  int interval_class = interval % 12;
  bool is_dissonant = (interval_class == 1) ||              // minor 2nd
                      (interval_class == 11) ||             // major 7th
                      (interval_class == 2 && interval < 12);  // major 2nd close
  EXPECT_FALSE(is_dissonant)
      << "B3 (59) should be moved to avoid clash with C4 (60), result=" << static_cast<int>(result);

  // Verify: specifically should NOT remain B3 (59) which was the bug
  EXPECT_NE(result, 59)
      << "Should not remain B3 (59) which creates minor 2nd with C4 (60)";
}

// ============================================================================
// Per-Section ChorusDropStyle Tests
// ============================================================================

class PerSectionDropStyleTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create B section with explicit drop_style followed by Chorus
    b_section_.type = SectionType::B;
    b_section_.start_tick = 0;
    b_section_.bars = 8;
    b_section_.name = "B";
    b_section_.drop_style = ChorusDropStyle::None;  // Will be set per-test

    chorus_.type = SectionType::Chorus;
    chorus_.start_tick = 8 * TICKS_PER_BAR;
    chorus_.bars = 8;
    chorus_.name = "Chorus";
  }

  Section b_section_;
  Section chorus_;
};

TEST_F(PerSectionDropStyleTest, UsesSectionDropStyleWhenSet) {
  // When section has explicit drop_style, it should be used
  b_section_.drop_style = ChorusDropStyle::Dramatic;
  std::vector<Section> sections = {b_section_, chorus_};

  MidiTrack chord_track;
  Tick drop_zone_start = 8 * TICKS_PER_BAR - TICKS_PER_BEAT;
  chord_track.addNote(NoteEventBuilder::create(drop_zone_start + TICKS_PER_BEAT / 2, TICKS_PER_BEAT / 2, 60, 80));

  MidiTrack drum_track;
  drum_track.addNote(NoteEventBuilder::create(drop_zone_start, TICKS_PER_BEAT / 4, KICK, 100));

  std::vector<MidiTrack*> tracks = {&chord_track};

  // Call with default_style=Subtle, but section has Dramatic
  PostProcessor::applyChorusDrop(tracks, sections, &drum_track, ChorusDropStyle::Subtle);

  // Dramatic style should truncate drum track too
  bool drum_in_drop_zone = false;
  for (const auto& note : drum_track.notes()) {
    if (note.start_tick >= drop_zone_start && note.start_tick < 8 * TICKS_PER_BAR) {
      drum_in_drop_zone = true;
    }
  }
  EXPECT_FALSE(drum_in_drop_zone)
      << "Dramatic drop_style should truncate drum track in drop zone";
}

TEST_F(PerSectionDropStyleTest, FallsBackToDefaultForBSectionWithNone) {
  // When B section has None drop_style, default_style should be used
  b_section_.drop_style = ChorusDropStyle::None;
  std::vector<Section> sections = {b_section_, chorus_};

  MidiTrack chord_track;
  Tick drop_zone_start = 8 * TICKS_PER_BAR - TICKS_PER_BEAT;
  chord_track.addNote(NoteEventBuilder::create(drop_zone_start + TICKS_PER_BEAT / 2, TICKS_PER_BEAT / 2, 60, 80));

  MidiTrack drum_track;
  drum_track.addNote(NoteEventBuilder::create(drop_zone_start, TICKS_PER_BEAT / 4, KICK, 100));
  size_t orig_drum_count = drum_track.notes().size();

  std::vector<MidiTrack*> tracks = {&chord_track};

  // Call with default_style=Subtle (doesn't truncate drums)
  PostProcessor::applyChorusDrop(tracks, sections, &drum_track, ChorusDropStyle::Subtle);

  // Subtle style should NOT truncate drum track
  EXPECT_EQ(drum_track.notes().size(), orig_drum_count)
      << "Subtle (default) drop_style should NOT truncate drum track";

  // But melodic tracks should still be truncated
  bool chord_in_drop_zone = false;
  for (const auto& note : chord_track.notes()) {
    if (note.start_tick >= drop_zone_start && note.start_tick < 8 * TICKS_PER_BAR) {
      chord_in_drop_zone = true;
    }
  }
  EXPECT_FALSE(chord_in_drop_zone)
      << "Chord track should be truncated in drop zone";
}

TEST_F(PerSectionDropStyleTest, DrumHitAddsCrashAtChorusEntry) {
  // DrumHit style should add crash cymbal at chorus entry
  b_section_.drop_style = ChorusDropStyle::DrumHit;
  std::vector<Section> sections = {b_section_, chorus_};

  MidiTrack chord_track;
  MidiTrack drum_track;
  drum_track.addNote(NoteEventBuilder::create(0, TICKS_PER_BEAT / 2, KICK, 80));  // Existing note

  std::vector<MidiTrack*> tracks = {&chord_track};
  PostProcessor::applyChorusDrop(tracks, sections, &drum_track, ChorusDropStyle::Subtle);

  Tick chorus_start = chorus_.start_tick;
  bool has_crash = false;
  for (const auto& note : drum_track.notes()) {
    if (note.start_tick == chorus_start && note.note == CRASH) {
      has_crash = true;
      EXPECT_GE(note.velocity, 100u)
          << "Crash at chorus entry should have strong velocity";
    }
  }
  EXPECT_TRUE(has_crash)
      << "DrumHit style should add crash cymbal at chorus entry";
}

TEST_F(PerSectionDropStyleTest, NoneDropStyleSkipsSection) {
  // Non-B section with explicit None drop_style should be skipped
  Section interlude;
  interlude.type = SectionType::Interlude;
  interlude.start_tick = 0;
  interlude.bars = 4;
  interlude.drop_style = ChorusDropStyle::None;  // Explicit None

  std::vector<Section> sections = {interlude, chorus_};

  MidiTrack chord_track;
  Tick section_end = 4 * TICKS_PER_BAR;
  chord_track.addNote(NoteEventBuilder::create(section_end - TICKS_PER_BEAT, TICKS_PER_BEAT, 60, 80));
  Tick orig_duration = chord_track.notes()[0].duration;

  std::vector<MidiTrack*> tracks = {&chord_track};
  PostProcessor::applyChorusDrop(tracks, sections, nullptr, ChorusDropStyle::Subtle);

  // Note should be unchanged since Interlude has explicit None
  EXPECT_EQ(chord_track.notes()[0].duration, orig_duration)
      << "Interlude with None drop_style should not be processed";
}

TEST_F(PerSectionDropStyleTest, ExplicitDropStyleOnInterludeIsApplied) {
  // Interlude with explicit Dramatic drop_style should be processed
  Section interlude;
  interlude.type = SectionType::Interlude;
  interlude.start_tick = 0;
  interlude.bars = 4;
  interlude.drop_style = ChorusDropStyle::Dramatic;  // Explicit Dramatic

  chorus_.start_tick = 4 * TICKS_PER_BAR;
  std::vector<Section> sections = {interlude, chorus_};

  MidiTrack chord_track;
  Tick drop_zone = 4 * TICKS_PER_BAR - TICKS_PER_BEAT;
  chord_track.addNote(NoteEventBuilder::create(drop_zone + TICKS_PER_BEAT / 2, TICKS_PER_BEAT / 2, 60, 80));

  std::vector<MidiTrack*> tracks = {&chord_track};
  PostProcessor::applyChorusDrop(tracks, sections, nullptr, ChorusDropStyle::Subtle);

  // Note in drop zone should be removed
  bool note_in_drop_zone = false;
  for (const auto& note : chord_track.notes()) {
    if (note.start_tick >= drop_zone && note.start_tick < 4 * TICKS_PER_BAR) {
      note_in_drop_zone = true;
    }
  }
  EXPECT_FALSE(note_in_drop_zone)
      << "Interlude with explicit Dramatic drop_style should process drop zone";
}

TEST_F(PerSectionDropStyleTest, MultipleSectionsWithDifferentDropStyles) {
  // Test multiple B sections with different drop styles
  Section b1;
  b1.type = SectionType::B;
  b1.start_tick = 0;
  b1.bars = 8;
  b1.drop_style = ChorusDropStyle::Subtle;

  Section chorus1;
  chorus1.type = SectionType::Chorus;
  chorus1.start_tick = 8 * TICKS_PER_BAR;
  chorus1.bars = 8;

  Section b2;
  b2.type = SectionType::B;
  b2.start_tick = 16 * TICKS_PER_BAR;
  b2.bars = 8;
  b2.drop_style = ChorusDropStyle::Dramatic;

  Section chorus2;
  chorus2.type = SectionType::Chorus;
  chorus2.start_tick = 24 * TICKS_PER_BAR;
  chorus2.bars = 8;

  std::vector<Section> sections = {b1, chorus1, b2, chorus2};

  MidiTrack drum_track;
  // Add drum notes in both drop zones
  Tick drop1 = 8 * TICKS_PER_BAR - TICKS_PER_BEAT;
  Tick drop2 = 24 * TICKS_PER_BAR - TICKS_PER_BEAT;
  drum_track.addNote(NoteEventBuilder::create(drop1, TICKS_PER_BEAT / 4, KICK, 100));
  drum_track.addNote(NoteEventBuilder::create(drop2, TICKS_PER_BEAT / 4, KICK, 100));

  MidiTrack chord_track;
  std::vector<MidiTrack*> tracks = {&chord_track};

  PostProcessor::applyChorusDrop(tracks, sections, &drum_track, ChorusDropStyle::None);

  // Count drum notes in each drop zone
  int drums_in_drop1 = 0;
  int drums_in_drop2 = 0;
  for (const auto& note : drum_track.notes()) {
    if (note.start_tick >= drop1 && note.start_tick < 8 * TICKS_PER_BAR) {
      drums_in_drop1++;
    }
    if (note.start_tick >= drop2 && note.start_tick < 24 * TICKS_PER_BAR) {
      drums_in_drop2++;
    }
  }

  // B1 has Subtle: drum notes should remain
  EXPECT_GT(drums_in_drop1, 0)
      << "Subtle drop_style should NOT truncate drum track";

  // B2 has Dramatic: drum notes should be removed
  EXPECT_EQ(drums_in_drop2, 0)
      << "Dramatic drop_style should truncate drum track";
}

// ============================================================================
// Phase 2 P2: DrumStyle-based Timing Profile Tests
// ============================================================================

TEST(DrumTimingProfileTest, StandardProfileMatchesOriginalHardcoded) {
  // The Standard profile must produce identical offsets to the original
  // hardcoded values to avoid behavioral regression.
  constexpr uint8_t HH = 42;
  constexpr uint8_t SD = 38;
  constexpr uint8_t BD = 36;

  Tick start = TICKS_PER_BAR;  // Beat 0 (downbeat)

  MidiTrack vocal, bass, drums;
  drums.addNote(NoteEventBuilder::create(start, 60, HH, 80));
  drums.addNote(NoteEventBuilder::create(start, 60, SD, 80));
  drums.addNote(NoteEventBuilder::create(start, 60, BD, 80));

  // Explicitly pass DrumStyle::Standard
  PostProcessor::applyMicroTimingOffsets(vocal, bass, drums, nullptr, 50,
                                         VocalStylePreset::Standard, DrumStyle::Standard);

  for (const auto& note : drums.notes()) {
    if (note.note == HH) {
      EXPECT_EQ(note.start_tick, start + 8)
          << "Standard profile: HH downbeat should be +8";
    } else if (note.note == SD) {
      EXPECT_EQ(note.start_tick, start - 4)
          << "Standard profile: snare on beat 0 should be -4";
    } else if (note.note == BD) {
      EXPECT_EQ(note.start_tick, start - 1)
          << "Standard profile: kick on downbeat should be -1";
    }
  }
}

TEST(DrumTimingProfileTest, SparseProducesSmallerOffsetsThanStandard) {
  // Sparse (Ballad) profile should have smaller absolute offsets for
  // a more subtle, relaxed groove feel.
  constexpr uint8_t HH = 42;
  constexpr uint8_t SD = 38;
  constexpr uint8_t BD = 36;

  // Use beat 1 (backbeat) for snare comparison
  Tick beat1 = TICKS_PER_BAR + TICKS_PER_BEAT;

  // Standard profile
  MidiTrack vocal_std, bass_std, drums_std;
  drums_std.addNote(NoteEventBuilder::create(beat1, 60, HH, 80));
  drums_std.addNote(NoteEventBuilder::create(beat1, 60, SD, 80));
  drums_std.addNote(NoteEventBuilder::create(beat1, 60, BD, 80));
  PostProcessor::applyMicroTimingOffsets(vocal_std, bass_std, drums_std, nullptr, 50,
                                         VocalStylePreset::Standard, DrumStyle::Standard);

  // Sparse profile
  MidiTrack vocal_sparse, bass_sparse, drums_sparse;
  drums_sparse.addNote(NoteEventBuilder::create(beat1, 60, HH, 80));
  drums_sparse.addNote(NoteEventBuilder::create(beat1, 60, SD, 80));
  drums_sparse.addNote(NoteEventBuilder::create(beat1, 60, BD, 80));
  PostProcessor::applyMicroTimingOffsets(vocal_sparse, bass_sparse, drums_sparse, nullptr, 50,
                                         VocalStylePreset::Standard, DrumStyle::Sparse);

  // Compare absolute offsets: Sparse should be smaller
  for (size_t idx = 0; idx < drums_std.notes().size(); ++idx) {
    int std_offset = static_cast<int>(drums_std.notes()[idx].start_tick) - static_cast<int>(beat1);
    int sparse_offset = static_cast<int>(drums_sparse.notes()[idx].start_tick) - static_cast<int>(beat1);
    EXPECT_LE(std::abs(sparse_offset), std::abs(std_offset))
        << "Sparse offset for note " << static_cast<int>(drums_sparse.notes()[idx].note)
        << " should be <= Standard offset in magnitude";
  }
}

TEST(DrumTimingProfileTest, SynthProducesNearZeroKickOffsets) {
  // Synth profile should have near-zero kick offsets for precision feel.
  constexpr uint8_t BD = 36;

  Tick downbeat = TICKS_PER_BAR;  // Beat 0

  MidiTrack vocal, bass, drums;
  drums.addNote(NoteEventBuilder::create(downbeat, 60, BD, 80));
  PostProcessor::applyMicroTimingOffsets(vocal, bass, drums, nullptr, 50,
                                         VocalStylePreset::Standard, DrumStyle::Synth);

  int kick_offset = static_cast<int>(drums.notes()[0].start_tick) - static_cast<int>(downbeat);
  EXPECT_EQ(kick_offset, 0)
      << "Synth profile: kick on downbeat should have zero offset";
}

TEST(DrumTimingProfileTest, UpbeatProducesLargerHiHatPush) {
  // Upbeat (Idol) profile should have larger hi-hat push for driving feel.
  constexpr uint8_t HH = 42;

  // Use offbeat position on beat 2 for strongest push comparison
  Tick offbeat = TICKS_PER_BAR + TICKS_PER_BEAT + TICKS_PER_BEAT / 2;

  // Standard profile
  MidiTrack vocal_std, bass_std, drums_std;
  drums_std.addNote(NoteEventBuilder::create(offbeat, 60, HH, 80));
  PostProcessor::applyMicroTimingOffsets(vocal_std, bass_std, drums_std, nullptr, 50,
                                         VocalStylePreset::Standard, DrumStyle::Standard);

  // Upbeat profile
  MidiTrack vocal_up, bass_up, drums_up;
  drums_up.addNote(NoteEventBuilder::create(offbeat, 60, HH, 80));
  PostProcessor::applyMicroTimingOffsets(vocal_up, bass_up, drums_up, nullptr, 50,
                                         VocalStylePreset::Standard, DrumStyle::Upbeat);

  int std_offset = static_cast<int>(drums_std.notes()[0].start_tick) - static_cast<int>(offbeat);
  int up_offset = static_cast<int>(drums_up.notes()[0].start_tick) - static_cast<int>(offbeat);

  EXPECT_GT(up_offset, std_offset)
      << "Upbeat profile should have larger hi-hat push than Standard";
}

TEST(DrumTimingProfileTest, AllProfilesReturnValidProfiles) {
  // Verify that all 8 DrumStyle values produce valid profiles without crash.
  constexpr DrumStyle all_styles[] = {
      DrumStyle::Sparse, DrumStyle::Standard, DrumStyle::FourOnFloor, DrumStyle::Upbeat,
      DrumStyle::Rock,   DrumStyle::Synth,    DrumStyle::Trap,        DrumStyle::Latin,
  };

  constexpr uint8_t HH = 42;
  constexpr uint8_t SD = 38;
  constexpr uint8_t BD = 36;

  for (auto style : all_styles) {
    MidiTrack vocal, bass, drums;
    Tick start = TICKS_PER_BAR;
    drums.addNote(NoteEventBuilder::create(start, 60, HH, 80));
    drums.addNote(NoteEventBuilder::create(start, 60, SD, 80));
    drums.addNote(NoteEventBuilder::create(start, 60, BD, 80));

    // Should not crash
    PostProcessor::applyMicroTimingOffsets(vocal, bass, drums, nullptr, 50,
                                           VocalStylePreset::Standard, style);

    // Verify notes still exist
    EXPECT_EQ(drums.notes().size(), 3u)
        << "All 3 drum notes should remain for style " << static_cast<int>(style);

    // Verify tick values are reasonable (within +/-50 of original)
    for (const auto& note : drums.notes()) {
      int offset = static_cast<int>(note.start_tick) - static_cast<int>(start);
      EXPECT_GE(offset, -50)
          << "Offset too negative for style " << static_cast<int>(style);
      EXPECT_LE(offset, 50)
          << "Offset too positive for style " << static_cast<int>(style);
    }
  }
}

TEST(DrumTimingProfileTest, DriveFeelAppliesOnTopOfProfile) {
  // Verify that drive_feel multiplier is applied on top of profile values.
  constexpr uint8_t HH = 42;
  Tick start = TICKS_PER_BAR;

  // Sparse profile with aggressive drive (1.5x)
  MidiTrack vocal_agg, bass_agg, drums_agg;
  drums_agg.addNote(NoteEventBuilder::create(start, 60, HH, 80));
  PostProcessor::applyMicroTimingOffsets(vocal_agg, bass_agg, drums_agg, nullptr, 100,
                                         VocalStylePreset::Standard, DrumStyle::Sparse);

  // Sparse profile with laid-back drive (0.5x)
  MidiTrack vocal_laid, bass_laid, drums_laid;
  drums_laid.addNote(NoteEventBuilder::create(start, 60, HH, 80));
  PostProcessor::applyMicroTimingOffsets(vocal_laid, bass_laid, drums_laid, nullptr, 0,
                                         VocalStylePreset::Standard, DrumStyle::Sparse);

  int agg_offset = static_cast<int>(drums_agg.notes()[0].start_tick) - static_cast<int>(start);
  int laid_offset = static_cast<int>(drums_laid.notes()[0].start_tick) - static_cast<int>(start);

  // Aggressive drive should produce larger absolute offsets than laid-back
  EXPECT_GT(agg_offset, laid_offset)
      << "Aggressive drive should amplify Sparse hi-hat push more than laid-back";
}

// ============================================================================
// synchronizeBassKick Tests
// ============================================================================

class BassKickSyncTest : public ::testing::Test {
 protected:
  MidiTrack bass_;
  MidiTrack drums_;
};

TEST_F(BassKickSyncTest, SnapsNearbyBassNoteToKick) {
  // Kick at tick 480, bass note at tick 500 (20 ticks away)
  drums_.addNote(NoteEventBuilder::create(480, 60, KICK, 100));
  bass_.addNote(NoteEventBuilder::create(500, 240, 36, 80));

  PostProcessor::synchronizeBassKick(bass_, drums_, DrumStyle::Standard);

  // Standard tolerance is 48, so 20 ticks should snap
  EXPECT_EQ(bass_.notes()[0].start_tick, 480u)
      << "Bass note within tolerance should snap to kick";
}

TEST_F(BassKickSyncTest, DoesNotSnapBeyondTolerance) {
  // Kick at tick 480, bass note at tick 580 (100 ticks away)
  drums_.addNote(NoteEventBuilder::create(480, 60, KICK, 100));
  bass_.addNote(NoteEventBuilder::create(580, 240, 36, 80));

  PostProcessor::synchronizeBassKick(bass_, drums_, DrumStyle::Standard);

  // 100 ticks exceeds Standard tolerance of 48
  EXPECT_EQ(bass_.notes()[0].start_tick, 580u)
      << "Bass note beyond tolerance should not be moved";
}

TEST_F(BassKickSyncTest, AlreadyAlignedNotMoved) {
  // Both at tick 960
  drums_.addNote(NoteEventBuilder::create(960, 60, KICK, 100));
  bass_.addNote(NoteEventBuilder::create(960, 240, 36, 80));

  PostProcessor::synchronizeBassKick(bass_, drums_, DrumStyle::Standard);

  EXPECT_EQ(bass_.notes()[0].start_tick, 960u)
      << "Already-aligned bass note should stay put";
}

TEST_F(BassKickSyncTest, SnapsToNearestKick) {
  // Two kicks at 480 and 960, bass note at 930 (closer to 960)
  drums_.addNote(NoteEventBuilder::create(480, 60, KICK, 100));
  drums_.addNote(NoteEventBuilder::create(960, 60, KICK, 100));
  bass_.addNote(NoteEventBuilder::create(930, 240, 36, 80));

  PostProcessor::synchronizeBassKick(bass_, drums_, DrumStyle::Standard);

  EXPECT_EQ(bass_.notes()[0].start_tick, 960u)
      << "Bass note should snap to the nearest kick";
}

TEST_F(BassKickSyncTest, IgnoresNonKickDrumNotes) {
  // Only snare at tick 480, no kick
  drums_.addNote(NoteEventBuilder::create(480, 60, SNARE, 100));
  bass_.addNote(NoteEventBuilder::create(500, 240, 36, 80));

  PostProcessor::synchronizeBassKick(bass_, drums_, DrumStyle::Standard);

  EXPECT_EQ(bass_.notes()[0].start_tick, 500u)
      << "Bass note should not snap to non-kick drum notes";
}

TEST_F(BassKickSyncTest, EmptyDrumsDoesNothing) {
  bass_.addNote(NoteEventBuilder::create(500, 240, 36, 80));

  PostProcessor::synchronizeBassKick(bass_, drums_, DrumStyle::Standard);

  EXPECT_EQ(bass_.notes()[0].start_tick, 500u)
      << "Empty drums should leave bass unchanged";
}

TEST_F(BassKickSyncTest, SparseStyleHasLooseTolerance) {
  // Kick at 480, bass at 540 (60 ticks away)
  // Standard tolerance=48 would reject, but Sparse tolerance=72 should accept
  drums_.addNote(NoteEventBuilder::create(480, 60, KICK, 100));
  bass_.addNote(NoteEventBuilder::create(540, 240, 36, 80));

  PostProcessor::synchronizeBassKick(bass_, drums_, DrumStyle::Sparse);

  EXPECT_EQ(bass_.notes()[0].start_tick, 480u)
      << "Sparse style should use looser tolerance (72 ticks)";
}

TEST_F(BassKickSyncTest, ElectronicStyleHasTightTolerance) {
  // Kick at 480, bass at 510 (30 ticks away)
  // FourOnFloor tolerance=24 should reject 30 ticks
  drums_.addNote(NoteEventBuilder::create(480, 60, KICK, 100));
  bass_.addNote(NoteEventBuilder::create(510, 240, 36, 80));

  PostProcessor::synchronizeBassKick(bass_, drums_, DrumStyle::FourOnFloor);

  EXPECT_EQ(bass_.notes()[0].start_tick, 510u)
      << "FourOnFloor style should use tight tolerance (24 ticks)";
}

TEST_F(BassKickSyncTest, ElectronicStyleSnapsWithinTightTolerance) {
  // Kick at 480, bass at 500 (20 ticks away, within 24)
  drums_.addNote(NoteEventBuilder::create(480, 60, KICK, 100));
  bass_.addNote(NoteEventBuilder::create(500, 240, 36, 80));

  PostProcessor::synchronizeBassKick(bass_, drums_, DrumStyle::FourOnFloor);

  EXPECT_EQ(bass_.notes()[0].start_tick, 480u)
      << "FourOnFloor style should snap within tight tolerance";
}

TEST_F(BassKickSyncTest, MultipleBassNotesProcessedIndependently) {
  // Two kicks at 480 and 1920
  drums_.addNote(NoteEventBuilder::create(480, 60, KICK, 100));
  drums_.addNote(NoteEventBuilder::create(1920, 60, KICK, 100));

  // Bass note near first kick, another far from any kick
  bass_.addNote(NoteEventBuilder::create(500, 240, 36, 80));
  bass_.addNote(NoteEventBuilder::create(1200, 240, 36, 80));

  PostProcessor::synchronizeBassKick(bass_, drums_, DrumStyle::Standard);

  EXPECT_EQ(bass_.notes()[0].start_tick, 480u)
      << "First bass note should snap to nearby kick";
  EXPECT_EQ(bass_.notes()[1].start_tick, 1200u)
      << "Second bass note should stay (too far from any kick)";
}

TEST_F(BassKickSyncTest, BassBeforeKickSnapsForward) {
  // Kick at 480, bass at 460 (bass is 20 ticks before kick)
  drums_.addNote(NoteEventBuilder::create(480, 60, KICK, 100));
  bass_.addNote(NoteEventBuilder::create(460, 240, 36, 80));

  PostProcessor::synchronizeBassKick(bass_, drums_, DrumStyle::Standard);

  EXPECT_EQ(bass_.notes()[0].start_tick, 480u)
      << "Bass note slightly before kick should snap forward to kick";
}

// ============================================================================
// Arrangement Holes Tests (Phase 3-1)
// ============================================================================

class ArrangementHolesTest : public ::testing::Test {
 protected:
  MidiTrack motif_;
  MidiTrack arpeggio_;
  MidiTrack aux_;
  MidiTrack chord_;
  MidiTrack bass_;
  MidiTrack guitar_;
};

TEST_F(ArrangementHolesTest, ChorusMaxPeakMutesBackgroundFinalTwoBeats) {
  // 8-bar chorus with Max peak level
  Section chorus;
  chorus.type = SectionType::Chorus;
  chorus.start_tick = 0;
  chorus.bars = 8;
  chorus.peak_level = PeakLevel::Max;
  std::vector<Section> sections = {chorus};

  Tick section_end = chorus.endTick();  // 8 * 1920 = 15360
  Tick hole_start = section_end - TICKS_PER_BEAT * 2;  // last 2 beats

  // Notes in the hole zone
  motif_.addNote(NoteEventBuilder::create(hole_start, 240, 60, 80));
  arpeggio_.addNote(NoteEventBuilder::create(hole_start + 240, 240, 64, 80));
  aux_.addNote(NoteEventBuilder::create(hole_start + 480, 240, 67, 80));

  // Notes before the hole zone (should survive)
  motif_.addNote(NoteEventBuilder::create(0, 480, 60, 80));
  chord_.addNote(NoteEventBuilder::create(0, 480, 64, 80));

  PostProcessor::applyArrangementHoles(motif_, arpeggio_, aux_, chord_, bass_, guitar_, sections);

  // Notes in hole zone should be removed
  EXPECT_EQ(motif_.notes().size(), 1u) << "Motif should keep note before hole, remove note in hole";
  EXPECT_EQ(arpeggio_.notes().size(), 0u) << "Arpeggio note in hole should be removed";
  EXPECT_EQ(aux_.notes().size(), 0u) << "Aux note in hole should be removed";
  // Chord should be unaffected (chorus holes only mute motif/arpeggio/aux)
  EXPECT_EQ(chord_.notes().size(), 1u) << "Chord should be unaffected by chorus hole";
}

TEST_F(ArrangementHolesTest, ChorusNonMaxPeakNotAffected) {
  Section chorus;
  chorus.type = SectionType::Chorus;
  chorus.start_tick = 0;
  chorus.bars = 8;
  chorus.peak_level = PeakLevel::None;  // Not Max
  std::vector<Section> sections = {chorus};

  Tick section_end = chorus.endTick();
  Tick hole_start = section_end - TICKS_PER_BEAT * 2;

  motif_.addNote(NoteEventBuilder::create(hole_start, 240, 60, 80));

  PostProcessor::applyArrangementHoles(motif_, arpeggio_, aux_, chord_, bass_, guitar_, sections);

  EXPECT_EQ(motif_.notes().size(), 1u) << "Non-Max chorus should not mute notes";
}

TEST_F(ArrangementHolesTest, BridgeMutesFirstTwoBeats) {
  Section bridge;
  bridge.type = SectionType::Bridge;
  bridge.start_tick = 0;
  bridge.bars = 4;
  std::vector<Section> sections = {bridge};

  Tick hole_end = TICKS_PER_BEAT * 2;

  // Notes in hole zone (bridge first 2 beats)
  motif_.addNote(NoteEventBuilder::create(0, 480, 60, 80));
  arpeggio_.addNote(NoteEventBuilder::create(240, 240, 64, 80));
  chord_.addNote(NoteEventBuilder::create(0, 960, 48, 80));
  bass_.addNote(NoteEventBuilder::create(0, 480, 36, 80));

  // Notes after hole zone (should survive)
  motif_.addNote(NoteEventBuilder::create(hole_end, 480, 60, 80));
  bass_.addNote(NoteEventBuilder::create(hole_end, 480, 36, 80));

  PostProcessor::applyArrangementHoles(motif_, arpeggio_, aux_, chord_, bass_, guitar_, sections);

  EXPECT_EQ(motif_.notes().size(), 1u) << "Motif: keep note after hole, remove note in hole";
  EXPECT_EQ(arpeggio_.notes().size(), 0u) << "Arpeggio in hole should be removed";
  EXPECT_EQ(chord_.notes().size(), 0u) << "Chord in hole should be removed (bridge)";
  EXPECT_EQ(bass_.notes().size(), 1u) << "Bass: keep note after hole, remove note in hole";
}

TEST_F(ArrangementHolesTest, EmptySectionsDoNothing) {
  motif_.addNote(NoteEventBuilder::create(0, 480, 60, 80));
  std::vector<Section> sections;

  PostProcessor::applyArrangementHoles(motif_, arpeggio_, aux_, chord_, bass_, guitar_, sections);
  EXPECT_EQ(motif_.notes().size(), 1u);
}

// ============================================================================
// smoothLargeLeaps Tests
// ============================================================================

class SmoothLargeLeapsTest : public ::testing::Test {
 protected:
  MidiTrack track_;
};

TEST_F(SmoothLargeLeapsTest, NoNotesDoesNothing) {
  PostProcessor::smoothLargeLeaps(track_);
  EXPECT_TRUE(track_.notes().empty());
}

TEST_F(SmoothLargeLeapsTest, SingleNoteDoesNothing) {
  track_.addNote(NoteEventBuilder::create(0, 480, 60, 80));
  PostProcessor::smoothLargeLeaps(track_);
  EXPECT_EQ(track_.notes().size(), 1u);
}

TEST_F(SmoothLargeLeapsTest, SmallLeapsPreserved) {
  // C4 -> E4 -> G4 (4st, 3st) - all within 12 semitones
  track_.addNote(NoteEventBuilder::create(0, 480, 60, 80));
  track_.addNote(NoteEventBuilder::create(480, 480, 64, 80));
  track_.addNote(NoteEventBuilder::create(960, 480, 67, 80));
  PostProcessor::smoothLargeLeaps(track_);
  EXPECT_EQ(track_.notes().size(), 3u);
}

TEST_F(SmoothLargeLeapsTest, ExactOctavePreserved) {
  // C4 -> C5 (12 semitones exactly) - at the boundary, preserved
  track_.addNote(NoteEventBuilder::create(0, 480, 60, 80));
  track_.addNote(NoteEventBuilder::create(480, 480, 72, 80));
  PostProcessor::smoothLargeLeaps(track_);
  EXPECT_EQ(track_.notes().size(), 2u);
}

TEST_F(SmoothLargeLeapsTest, LargeLeapNoteRemoved) {
  // C4 -> C4+2oct (24 semitones) - too large, second note removed
  track_.addNote(NoteEventBuilder::create(0, 480, 60, 80));
  track_.addNote(NoteEventBuilder::create(480, 480, 84, 80));
  PostProcessor::smoothLargeLeaps(track_);
  EXPECT_EQ(track_.notes().size(), 1u);
  EXPECT_EQ(track_.notes()[0].note, 60);
}

TEST_F(SmoothLargeLeapsTest, DownwardLargeLeapRemoved) {
  // G5 -> G3 (-24 semitones) - too large
  track_.addNote(NoteEventBuilder::create(0, 480, 79, 80));
  track_.addNote(NoteEventBuilder::create(480, 480, 55, 80));
  PostProcessor::smoothLargeLeaps(track_);
  EXPECT_EQ(track_.notes().size(), 1u);
  EXPECT_EQ(track_.notes()[0].note, 79);
}

TEST_F(SmoothLargeLeapsTest, CascadingRemovalHandled) {
  // A(60) -> B(62) -> C(86) -> D(64)
  // First pass: B->C (24st) removes C, creating B(62)->D(64) which is fine
  track_.addNote(NoteEventBuilder::create(0, 480, 60, 80));
  track_.addNote(NoteEventBuilder::create(480, 480, 62, 80));
  track_.addNote(NoteEventBuilder::create(960, 480, 86, 80));
  track_.addNote(NoteEventBuilder::create(1440, 480, 64, 80));
  PostProcessor::smoothLargeLeaps(track_);
  EXPECT_EQ(track_.notes().size(), 3u);
  EXPECT_EQ(track_.notes()[0].note, 60);
  EXPECT_EQ(track_.notes()[1].note, 62);
  EXPECT_EQ(track_.notes()[2].note, 64);
}

TEST_F(SmoothLargeLeapsTest, CascadingRemovalWithNewLeap) {
  // A(60) -> B(80) -> C(55) - removing B creates A(60)->C(55) which is 5st (ok)
  // But if A(60) -> B(80) -> C(40), removing B creates A(60)->C(40) = 20st -> C removed too
  track_.addNote(NoteEventBuilder::create(0, 480, 60, 80));
  track_.addNote(NoteEventBuilder::create(480, 480, 80, 80));
  track_.addNote(NoteEventBuilder::create(960, 480, 40, 80));
  PostProcessor::smoothLargeLeaps(track_);
  EXPECT_EQ(track_.notes().size(), 1u);  // Only A remains
  EXPECT_EQ(track_.notes()[0].note, 60);
}

TEST_F(SmoothLargeLeapsTest, UnsortedNotesGetSorted) {
  // Notes added out of order should be sorted first
  track_.addNote(NoteEventBuilder::create(960, 480, 67, 80));  // G4
  track_.addNote(NoteEventBuilder::create(0, 480, 60, 80));    // C4
  track_.addNote(NoteEventBuilder::create(480, 480, 64, 80));  // E4
  PostProcessor::smoothLargeLeaps(track_);
  EXPECT_EQ(track_.notes().size(), 3u);
  // Should be sorted by tick
  EXPECT_EQ(track_.notes()[0].start_tick, 0u);
  EXPECT_EQ(track_.notes()[1].start_tick, 480u);
  EXPECT_EQ(track_.notes()[2].start_tick, 960u);
}

TEST_F(SmoothLargeLeapsTest, CustomMaxSemitones) {
  // With max_semitones=6, even a 7st leap should be removed
  track_.addNote(NoteEventBuilder::create(0, 480, 60, 80));     // C4
  track_.addNote(NoteEventBuilder::create(480, 480, 67, 80));   // G4 (7st)
  PostProcessor::smoothLargeLeaps(track_, 6);
  EXPECT_EQ(track_.notes().size(), 1u);
}

// ============================================================================
// Regression: Aux Large Leap at Section Boundaries (seed=17, bp=5)
// ============================================================================

TEST_F(SmoothLargeLeapsTest, SectionBoundaryLeapRegression) {
  // Simulates the case where post-processing removes notes between sections,
  // creating new adjacencies with large leaps.
  // Before fix: pitch 76 -> 57 (19 semitones) at bar 3 (Intro->Chorus boundary)
  // After fix: the 57 note should be removed

  // Intro notes (high register ~72-76)
  track_.addNote(NoteEventBuilder::create(0, 240, 72, 70));
  track_.addNote(NoteEventBuilder::create(240, 240, 72, 70));
  track_.addNote(NoteEventBuilder::create(480, 240, 76, 70));
  track_.addNote(NoteEventBuilder::create(960, 240, 72, 70));
  track_.addNote(NoteEventBuilder::create(1440, 240, 76, 70));

  // Gap (simulating removed notes from fixAuxVocalClashes)

  // Chorus notes (low register ~55-57)
  track_.addNote(NoteEventBuilder::create(3840, 1920, 57, 60));
  track_.addNote(NoteEventBuilder::create(5760, 1920, 55, 60));
  track_.addNote(NoteEventBuilder::create(7680, 1920, 57, 60));

  PostProcessor::smoothLargeLeaps(track_);

  // The 57 at tick 3840 creates a leap of |57-76| = 19 > 12, so removed.
  // Then 55 at tick 5760 creates a leap of |55-76| = 21 > 12, so also removed.
  // Then 57 at tick 7680 creates a leap of |57-76| = 19 > 12, so also removed.
  // Only intro notes remain.
  EXPECT_EQ(track_.notes().size(), 5u);

  // Verify all remaining notes have no large leaps
  for (size_t idx = 1; idx < track_.notes().size(); ++idx) {
    int leap = std::abs(static_cast<int>(track_.notes()[idx].note) -
                        static_cast<int>(track_.notes()[idx - 1].note));
    EXPECT_LE(leap, 12) << "Large leap at index " << idx
                         << ": " << static_cast<int>(track_.notes()[idx - 1].note)
                         << " -> " << static_cast<int>(track_.notes()[idx].note);
  }
}

}  // namespace
}  // namespace midisketch
