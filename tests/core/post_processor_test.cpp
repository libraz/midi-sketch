/**
 * @file post_processor_test.cpp
 * @brief Tests for post-processing functions (chorus drop, ritardando, final hit).
 */

#include "core/post_processor.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "core/midi_track.h"
#include "core/note_factory.h"  // for NoteSource enum
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
  chord_track.addNote(final_beat_start, original_duration, 60, 80);  // C
  chord_track.addNote(final_beat_start, original_duration, 64, 80);  // E
  chord_track.addNote(final_beat_start, original_duration, 67, 80);  // G

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
  bass_track.addNote(final_beat_start, TICKS_PER_BEAT, 36, 80);

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
  drum_track.addNote(0, TICKS_PER_BEAT / 2, KICK, 80);
  drum_track.addNote(TICKS_PER_BAR, TICKS_PER_BEAT / 2, SNARE, 85);

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
  track.addNote(last_bar_start, TICKS_PER_BEAT, 60, 80);  // C
  track.addNote(last_bar_start, TICKS_PER_BEAT, 64, 80);  // E
  track.addNote(last_bar_start, TICKS_PER_BEAT, 67, 80);  // G

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
  track.addNote(last_bar_start, half_bar, 67, 80);              // G
  track.addNote(last_bar_start, half_bar, 71, 80);              // B
  track.addNote(last_bar_start, half_bar, 74, 80);              // D

  // Second chord (Am) at beat 3 of last bar
  Tick second_chord_start = last_bar_start + half_bar;
  track.addNote(second_chord_start, half_bar, 69, 80);          // A
  track.addNote(second_chord_start, half_bar, 72, 80);          // C
  track.addNote(second_chord_start, half_bar, 76, 80);          // E

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
  track.addNote(last_bar_start, TICKS_PER_BAR, 60, 80);

  // Second note at half bar
  Tick second_note_start = last_bar_start + TICKS_PER_BAR / 2;
  track.addNote(second_note_start, TICKS_PER_BEAT, 64, 80);

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
  track.addNote(last_bar_start - TICKS_PER_BAR, original_duration, 60, 80);

  // Note in last bar (should be extended)
  track.addNote(last_bar_start, original_duration, 64, 80);

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

// ============================================================================
// Provenance Tests
// ============================================================================

#ifdef MIDISKETCH_NOTE_PROVENANCE

TEST_F(EnhancedFinalHitTest, AddedNotesHavePostProcessProvenance) {
  // Notes added by applyEnhancedFinalHit should have provenance set

  MidiTrack bass_track;
  MidiTrack drum_track;
  // Add a note so drum_track is not empty (required for applyEnhancedFinalHit)
  drum_track.addNote(0, TICKS_PER_BEAT / 2, KICK, 80);

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

TEST_F(ChorusDropTest, DrumHitCrashHasPostProcessProvenance) {
  // Crash cymbal added by DrumHit style should have provenance set

  MidiTrack track;
  // Add notes in B section
  track.addNote(0, TICKS_PER_BEAT, 60, 80);

  MidiTrack drum_track;
  // Add a note so drum_track is not empty
  drum_track.addNote(0, TICKS_PER_BEAT / 2, KICK, 80);

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
  // Test that vocal timing offset varies by phrase position when sections provided
  MidiTrack vocal, bass, drums;

  // Create 4-bar section
  std::vector<Section> sections;
  Section section;
  section.type = SectionType::A;
  section.start_tick = 0;
  section.bars = 4;
  sections.push_back(section);

  // Add notes at different phrase positions
  // Bar 0 (phrase start) - should get +8 offset
  Tick phrase_start_tick = 0;
  vocal.addNote(phrase_start_tick, TICKS_PER_BEAT, 60, 80);

  // Bar 1-2 (phrase middle) - should get +4 offset
  Tick phrase_middle_tick = TICKS_PER_BAR * 2;
  vocal.addNote(phrase_middle_tick, TICKS_PER_BEAT, 62, 80);

  // Bar 3 (phrase end) - should get 0 offset
  Tick phrase_end_tick = TICKS_PER_BAR * 3;
  vocal.addNote(phrase_end_tick, TICKS_PER_BEAT, 64, 80);

  // Record original positions
  Tick orig_start = vocal.notes()[0].start_tick;
  Tick orig_middle = vocal.notes()[1].start_tick;
  Tick orig_end = vocal.notes()[2].start_tick;

  // Apply micro-timing with sections
  PostProcessor::applyMicroTimingOffsets(vocal, bass, drums, &sections);

  // Phrase start should have largest forward offset (+8)
  Tick new_start = vocal.notes()[0].start_tick;
  EXPECT_EQ(new_start, orig_start + 8) << "Phrase start should have +8 offset";

  // Phrase middle should have medium offset (+4)
  Tick new_middle = vocal.notes()[1].start_tick;
  EXPECT_EQ(new_middle, orig_middle + 4) << "Phrase middle should have +4 offset";

  // Phrase end should have no offset (0)
  Tick new_end = vocal.notes()[2].start_tick;
  EXPECT_EQ(new_end, orig_end) << "Phrase end should have 0 offset";
}

TEST(MicroTimingTest, VocalTimingUniformWithoutSections) {
  // Without sections, vocal should get uniform +4 offset
  MidiTrack vocal, bass, drums;

  Tick start_tick = TICKS_PER_BAR;
  vocal.addNote(start_tick, TICKS_PER_BEAT, 60, 80);

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
  bass.addNote(start_tick, TICKS_PER_BEAT, 36, 80);

  Tick orig = bass.notes()[0].start_tick;

  PostProcessor::applyMicroTimingOffsets(vocal, bass, drums, &sections);

  // Bass should lay back (-4)
  EXPECT_EQ(bass.notes()[0].start_tick, orig - 4) << "Bass should lay back by 4 ticks";
}

TEST(MicroTimingTest, DrumTimingByInstrument) {
  // Hi-hat pushes ahead, snare lays back, kick stays on grid
  MidiTrack vocal, bass, drums;

  constexpr uint8_t HH = 42;  // Closed hi-hat
  constexpr uint8_t SD = 38;  // Snare
  constexpr uint8_t BD = 36;  // Kick

  Tick start = TICKS_PER_BAR;
  drums.addNote(start, 60, HH, 80);
  drums.addNote(start, 60, SD, 80);
  drums.addNote(start, 60, BD, 80);

  PostProcessor::applyMicroTimingOffsets(vocal, bass, drums, nullptr);

  // Find each drum note
  for (const auto& note : drums.notes()) {
    if (note.note == HH) {
      EXPECT_EQ(note.start_tick, start + 8) << "Hi-hat should push ahead by 8";
    } else if (note.note == SD) {
      EXPECT_EQ(note.start_tick, start - 8) << "Snare should lay back by 8";
    } else if (note.note == BD) {
      EXPECT_EQ(note.start_tick, start) << "Kick should stay on grid";
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
  drums_laid.addNote(start, 60, HH, 80);
  bass_laid.addNote(start, 60, 36, 80);
  PostProcessor::applyMicroTimingOffsets(vocal_laid, bass_laid, drums_laid, nullptr, 0);

  // Neutral (drive=50): offsets should be 1.0x
  MidiTrack vocal_neutral, bass_neutral, drums_neutral;
  drums_neutral.addNote(start, 60, HH, 80);
  bass_neutral.addNote(start, 60, 36, 80);
  PostProcessor::applyMicroTimingOffsets(vocal_neutral, bass_neutral, drums_neutral, nullptr, 50);

  // Aggressive (drive=100): offsets should be 1.5x
  MidiTrack vocal_agg, bass_agg, drums_agg;
  drums_agg.addNote(start, 60, HH, 80);
  bass_agg.addNote(start, 60, 36, 80);
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
  // Test that drive_feel scales vocal phrase-position offsets
  MidiTrack vocal, bass, drums;

  std::vector<Section> sections;
  Section section;
  section.type = SectionType::A;
  section.start_tick = 0;
  section.bars = 4;
  sections.push_back(section);

  // Add note at phrase start (bar 0)
  Tick phrase_start = 0;
  vocal.addNote(phrase_start, TICKS_PER_BEAT, 60, 80);

  Tick orig = vocal.notes()[0].start_tick;

  // With aggressive drive (100), offset should be 1.5x: base 8 * 1.5 = 12
  PostProcessor::applyMicroTimingOffsets(vocal, bass, drums, &sections, 100);

  EXPECT_EQ(vocal.notes()[0].start_tick, orig + 12)
      << "Aggressive drive should push phrase start ahead by 12 (1.5x of 8)";
}

TEST(MicroTimingTest, DefaultDriveFeelMatchesNeutral) {
  constexpr uint8_t HH = 42;
  Tick start = TICKS_PER_BAR;

  // Default (no drive_feel specified)
  MidiTrack vocal_def, bass_def, drums_def;
  drums_def.addNote(start, 60, HH, 80);
  PostProcessor::applyMicroTimingOffsets(vocal_def, bass_def, drums_def, nullptr);

  // Explicit neutral (drive_feel = 50)
  MidiTrack vocal_neutral, bass_neutral, drums_neutral;
  drums_neutral.addNote(start, 60, HH, 80);
  PostProcessor::applyMicroTimingOffsets(vocal_neutral, bass_neutral, drums_neutral, nullptr, 50);

  // Both should have same offset
  EXPECT_EQ(drums_def.notes()[0].start_tick, drums_neutral.notes()[0].start_tick)
      << "Default drive_feel should match neutral (50)";
}

}  // namespace
}  // namespace midisketch
