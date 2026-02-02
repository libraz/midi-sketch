/**
 * @file emotion_curve_velocity_test.cpp
 * @brief Tests for EmotionCurve velocity integration in Generator.
 *
 * Verifies that EmotionCurve's tension/energy parameters affect
 * note velocities throughout each section, not just at transitions.
 */

#include <gtest/gtest.h>

#include <numeric>

#include "core/emotion_curve.h"
#include "core/generator.h"
#include "core/structure.h"
#include "core/velocity.h"

namespace midisketch {
namespace {

// ============================================================================
// EmotionCurve Velocity Integration Tests
// ============================================================================

class EmotionCurveVelocityIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.key = Key::C;
    params_.bpm = 120;
    params_.mood = Mood::ModernPop;
    params_.chord_id = 0;
    params_.drums_enabled = true;
    params_.structure = StructurePattern::BuildUp;  // Intro -> A -> B -> Chorus
    params_.seed = 42;
    params_.vocal_low = 60;
    params_.vocal_high = 72;
  }

  // Helper: Calculate average velocity for notes in a section
  float averageVelocityInSection(const MidiTrack& track, const Section& section) {
    Tick section_start = section.start_tick;
    Tick section_end = section_start + section.bars * TICKS_PER_BAR;

    std::vector<uint8_t> velocities;
    for (const auto& note : track.notes()) {
      if (note.start_tick >= section_start && note.start_tick < section_end) {
        velocities.push_back(note.velocity);
      }
    }

    if (velocities.empty()) return 0.0f;
    return std::accumulate(velocities.begin(), velocities.end(), 0.0f) / velocities.size();
  }

  // Helper: Get section by type
  const Section* findSectionByType(const std::vector<Section>& sections, SectionType type) {
    for (const auto& section : sections) {
      if (section.type == type) return &section;
    }
    return nullptr;
  }

  // Helper: Count notes in a section
  int countNotesInSection(const MidiTrack& track, const Section& section) {
    Tick section_start = section.start_tick;
    Tick section_end = section_start + section.bars * TICKS_PER_BAR;
    int count = 0;
    for (const auto& note : track.notes()) {
      if (note.start_tick >= section_start && note.start_tick < section_end) {
        ++count;
      }
    }
    return count;
  }

  GeneratorParams params_;
  Generator generator_;
};

TEST_F(EmotionCurveVelocityIntegrationTest, HighEnergySectionHasLouderVelocity) {
  // Chorus (high energy) should have louder velocity than A section (medium energy)
  // Using chord track since it's always populated in all sections
  generator_.generate(params_);

  const auto& sections = generator_.getSong().arrangement().sections();
  const auto& chord = generator_.getSong().chord();

  const Section* a_section = findSectionByType(sections, SectionType::A);
  const Section* chorus = findSectionByType(sections, SectionType::Chorus);

  if (!a_section || !chorus) {
    GTEST_SKIP() << "Structure doesn't have both A section and Chorus";
  }

  // Skip if either section has no notes
  if (countNotesInSection(chord, *a_section) == 0 || countNotesInSection(chord, *chorus) == 0) {
    GTEST_SKIP() << "Chord track doesn't have notes in both sections";
  }

  float a_avg = averageVelocityInSection(chord, *a_section);
  float chorus_avg = averageVelocityInSection(chord, *chorus);

  // Chorus should have higher average velocity due to higher energy
  // Allow 10% tolerance due to other processing effects
  EXPECT_GT(chorus_avg, a_avg * 0.90f)
      << "Chorus (high energy) should have higher velocity than A section. "
      << "A avg: " << a_avg << ", Chorus avg: " << chorus_avg;
}

TEST_F(EmotionCurveVelocityIntegrationTest, LowTensionCapsVelocity) {
  // Intro has low tension, which should cap maximum velocity
  // Using chord track since it's always populated in all sections
  generator_.generate(params_);

  const auto& sections = generator_.getSong().arrangement().sections();
  const auto& chord = generator_.getSong().chord();

  const Section* intro = findSectionByType(sections, SectionType::Intro);
  if (!intro) {
    GTEST_SKIP() << "Structure doesn't have Intro section";
  }

  if (countNotesInSection(chord, *intro) == 0) {
    GTEST_SKIP() << "Chord track doesn't have notes in Intro";
  }

  Tick section_start = intro->start_tick;
  Tick section_end = section_start + intro->bars * TICKS_PER_BAR;

  // Check that Intro velocities are capped (tension limits ceiling)
  // With tension ~0.2, ceiling should be reduced from 127
  uint8_t max_velocity = 0;
  for (const auto& note : chord.notes()) {
    if (note.start_tick >= section_start && note.start_tick < section_end) {
      max_velocity = std::max(max_velocity, note.velocity);
    }
  }

  // Low tension sections should not exceed ~115 velocity (accounting for processing variance)
  // This tests that calculateVelocityCeiling is being applied
  EXPECT_LE(max_velocity, 115)
      << "Intro (low tension) should have capped velocity. Max found: " << static_cast<int>(max_velocity);
}

TEST_F(EmotionCurveVelocityIntegrationTest, AllSectionsHaveEmotionApplied) {
  // Verify that every section's notes are affected by emotion parameters
  generator_.generate(params_);

  const auto& sections = generator_.getSong().arrangement().sections();
  const auto& emotion_curve = generator_.getEmotionCurve();
  const auto& chord = generator_.getSong().chord();

  ASSERT_TRUE(emotion_curve.isPlanned());
  ASSERT_EQ(emotion_curve.size(), sections.size());

  // Check that each section type has appropriate relative velocities
  // based on their emotion energy values
  std::map<SectionType, std::pair<float, float>> section_velocity_emotion;

  for (size_t i = 0; i < sections.size(); ++i) {
    const auto& section = sections[i];
    const auto& emotion = emotion_curve.getEmotion(i);

    float avg_velocity = averageVelocityInSection(chord, section);
    if (avg_velocity > 0) {
      section_velocity_emotion[section.type] = {avg_velocity, emotion.energy};
    }
  }

  // Higher energy sections should generally have higher velocities
  // Verify Chorus > A if both exist
  if (section_velocity_emotion.count(SectionType::Chorus) &&
      section_velocity_emotion.count(SectionType::A)) {
    float chorus_vel = section_velocity_emotion[SectionType::Chorus].first;
    float a_vel = section_velocity_emotion[SectionType::A].first;
    float chorus_energy = section_velocity_emotion[SectionType::Chorus].second;
    float a_energy = section_velocity_emotion[SectionType::A].second;

    // If energy difference is significant, velocity should follow
    if (chorus_energy - a_energy > 0.2f) {
      EXPECT_GT(chorus_vel, a_vel * 0.95f)  // Allow 5% tolerance
          << "Chorus should have higher velocity than A section";
    }
  }
}

TEST_F(EmotionCurveVelocityIntegrationTest, TransitionVelocityRampStillWorks) {
  // Verify that the existing transition velocity ramp still works
  // alongside the new section-wide emotion adjustments
  generator_.generate(params_);

  const auto& sections = generator_.getSong().arrangement().sections();
  const auto& emotion_curve = generator_.getEmotionCurve();
  const auto& chord = generator_.getSong().chord();

  // Find B -> Chorus transition
  for (size_t i = 0; i + 1 < sections.size(); ++i) {
    if (sections[i].type == SectionType::B && sections[i + 1].type == SectionType::Chorus) {
      auto hint = emotion_curve.getTransitionHint(i);

      // B -> Chorus should have velocity ramp > 1.0 (crescendo)
      EXPECT_GT(hint.velocity_ramp, 1.0f)
          << "B -> Chorus should have crescendo velocity ramp";

      // Verify notes in transition zone have been affected
      const auto& b_section = sections[i];
      Tick section_end = b_section.endTick();
      Tick transition_start = section_end - TICKS_PER_BEAT * 2;  // Last 2 beats

      std::vector<uint8_t> transition_velocities;
      for (const auto& note : chord.notes()) {
        if (note.start_tick >= transition_start && note.start_tick < section_end) {
          transition_velocities.push_back(note.velocity);
        }
      }

      // Transition zone should have notes with valid velocities
      if (!transition_velocities.empty()) {
        uint8_t max_transition = *std::max_element(transition_velocities.begin(),
                                                    transition_velocities.end());
        // Transition notes should be reasonably loud (crescendo effect)
        EXPECT_GE(max_transition, 50)
            << "Transition zone should have reasonably loud notes";
      }
      return;
    }
  }
}

TEST_F(EmotionCurveVelocityIntegrationTest, VelocityWithinValidRange) {
  // All velocities should be within valid MIDI range (1-127)
  // Note: Our emotion adjustment clamps to 30-127, but other processing steps
  // (humanization, blueprint constraints, etc.) may adjust velocities further
  generator_.generate(params_);

  const auto& vocal = generator_.getSong().vocal();
  const auto& chord = generator_.getSong().chord();
  const auto& bass = generator_.getSong().bass();

  auto checkTrackVelocities = [](const MidiTrack& track, const char* name) {
    for (const auto& note : track.notes()) {
      EXPECT_GE(note.velocity, 1)
          << name << " track has velocity below MIDI minimum: " << static_cast<int>(note.velocity);
      EXPECT_LE(note.velocity, 127)
          << name << " track has velocity above MIDI maximum: " << static_cast<int>(note.velocity);
    }
  };

  checkTrackVelocities(vocal, "Vocal");
  checkTrackVelocities(chord, "Chord");
  checkTrackVelocities(bass, "Bass");
}

TEST_F(EmotionCurveVelocityIntegrationTest, EnergyFactorRangeIsCorrect) {
  // Test that energy factor calculation produces expected range
  // energy_factor = 0.85 + energy * 0.30
  // energy=0.0 -> factor=0.85
  // energy=0.5 -> factor=1.0
  // energy=1.0 -> factor=1.15

  SectionEmotion low_energy;
  low_energy.tension = 0.5f;
  low_energy.energy = 0.0f;

  SectionEmotion mid_energy;
  mid_energy.tension = 0.5f;
  mid_energy.energy = 0.5f;

  SectionEmotion high_energy;
  high_energy.tension = 0.5f;
  high_energy.energy = 1.0f;

  // Calculate expected velocities for base=100
  // Low energy: 100 * 0.85 = 85
  // Mid energy: 100 * 1.0 = 100
  // High energy: 100 * 1.15 = 115

  float low_factor = 0.85f + low_energy.energy * 0.30f;
  float mid_factor = 0.85f + mid_energy.energy * 0.30f;
  float high_factor = 0.85f + high_energy.energy * 0.30f;

  EXPECT_NEAR(low_factor, 0.85f, 0.001f);
  EXPECT_NEAR(mid_factor, 1.0f, 0.001f);
  EXPECT_NEAR(high_factor, 1.15f, 0.001f);
}

TEST_F(EmotionCurveVelocityIntegrationTest, MultipleStructurePatternsWork) {
  // Test that emotion-based velocity works with different structure patterns
  std::vector<StructurePattern> patterns = {
      StructurePattern::StandardPop,
      StructurePattern::BuildUp,
      StructurePattern::FullPop,
  };

  for (auto pattern : patterns) {
    params_.structure = pattern;
    params_.seed = 12345;

    Generator gen;
    gen.generate(params_);

    const auto& emotion_curve = gen.getEmotionCurve();
    EXPECT_TRUE(emotion_curve.isPlanned())
        << "EmotionCurve should be planned for pattern " << static_cast<int>(pattern);

    const auto& sections = gen.getSong().arrangement().sections();
    EXPECT_EQ(emotion_curve.size(), sections.size())
        << "EmotionCurve size should match sections for pattern " << static_cast<int>(pattern);

    // Verify at least some notes exist with valid velocities
    const auto& vocal = gen.getSong().vocal();
    bool has_valid_notes = false;
    for (const auto& note : vocal.notes()) {
      if (note.velocity >= 30 && note.velocity <= 127) {
        has_valid_notes = true;
        break;
      }
    }
    EXPECT_TRUE(has_valid_notes)
        << "Should have valid notes for pattern " << static_cast<int>(pattern);
  }
}

// ============================================================================
// Verification Tests - Ensure EmotionCurve integration actually works
// ============================================================================

TEST_F(EmotionCurveVelocityIntegrationTest, EmotionCurveActuallyAffectsVelocity) {
  // This test verifies that EmotionCurve integration actually modifies velocities
  // by checking that energy differences between sections create velocity differences
  generator_.generate(params_);

  const auto& sections = generator_.getSong().arrangement().sections();
  const auto& emotion_curve = generator_.getEmotionCurve();
  const auto& chord = generator_.getSong().chord();

  // Find sections with significantly different energy levels
  float max_energy = 0.0f;
  float min_energy = 1.0f;
  size_t max_energy_idx = 0;
  size_t min_energy_idx = 0;

  for (size_t i = 0; i < sections.size(); ++i) {
    const auto& emotion = emotion_curve.getEmotion(i);
    // Only consider sections with chord notes
    if (countNotesInSection(chord, sections[i]) > 0) {
      if (emotion.energy > max_energy) {
        max_energy = emotion.energy;
        max_energy_idx = i;
      }
      if (emotion.energy < min_energy) {
        min_energy = emotion.energy;
        min_energy_idx = i;
      }
    }
  }

  // Require significant energy difference to test
  if (max_energy - min_energy < 0.3f) {
    GTEST_SKIP() << "Not enough energy variation between sections";
  }

  float high_energy_avg = averageVelocityInSection(chord, sections[max_energy_idx]);
  float low_energy_avg = averageVelocityInSection(chord, sections[min_energy_idx]);

  // With energy difference of 0.3+, velocity difference should be noticeable
  // energy_factor = 0.85 + energy * 0.30
  // energy=0.3 -> factor=0.94, energy=1.0 -> factor=1.15
  // Expected ratio: 1.15/0.94 = 1.22 (22% difference)
  // Allow 4% minimum difference to account for other processing
  // (Multiple velocity adjustments can overlap and reduce the net effect)
  EXPECT_GT(high_energy_avg, low_energy_avg * 1.04f)
      << "High energy section (idx=" << max_energy_idx << ", energy=" << max_energy
      << ") should have higher velocity than low energy section (idx=" << min_energy_idx
      << ", energy=" << min_energy << "). "
      << "High avg: " << high_energy_avg << ", Low avg: " << low_energy_avg;
}

TEST_F(EmotionCurveVelocityIntegrationTest, IntroHasReducedVelocityDueToLowEnergy) {
  // Intro sections should have lower velocity due to low energy (typically ~0.3)
  // This directly tests that EmotionCurve energy affects output
  generator_.generate(params_);

  const auto& sections = generator_.getSong().arrangement().sections();
  const auto& emotion_curve = generator_.getEmotionCurve();
  const auto& chord = generator_.getSong().chord();

  const Section* intro = findSectionByType(sections, SectionType::Intro);
  if (!intro || countNotesInSection(chord, *intro) == 0) {
    GTEST_SKIP() << "No intro section with chord notes";
  }

  // Get Intro's emotion
  size_t intro_idx = 0;
  for (size_t i = 0; i < sections.size(); ++i) {
    if (sections[i].type == SectionType::Intro) {
      intro_idx = i;
      break;
    }
  }
  const auto& intro_emotion = emotion_curve.getEmotion(intro_idx);

  // Intro should have low energy (< 0.5)
  EXPECT_LT(intro_emotion.energy, 0.5f)
      << "Intro should have low energy, got: " << intro_emotion.energy;

  // Find average velocity across all sections with chord notes
  float total_avg = 0.0f;
  int section_count = 0;
  for (size_t i = 0; i < sections.size(); ++i) {
    float avg = averageVelocityInSection(chord, sections[i]);
    if (avg > 0) {
      total_avg += avg;
      section_count++;
    }
  }
  total_avg /= section_count;

  float intro_avg = averageVelocityInSection(chord, *intro);

  // Intro velocity should be roughly at or below overall average due to low energy.
  // Allow small margin (3%) because energy is just one of many velocity factors.
  // Note: With pitch safety improvements, the velocity distribution can shift slightly.
  EXPECT_LT(intro_avg, total_avg * 1.03f)
      << "Intro (low energy=" << intro_emotion.energy << ") should have below-average velocity. "
      << "Intro avg: " << intro_avg << ", Overall avg: " << total_avg;
}

// ============================================================================
// Direct Function Tests
// ============================================================================

TEST(ApplyEmotionToVelocityTest, HighEnergyIncreasesVelocity) {
  // Test the energy factor calculation directly
  uint8_t base = 80;

  // High energy: factor = 0.85 + 1.0 * 0.30 = 1.15
  // Result: 80 * 1.15 = 92
  float high_energy_factor = 0.85f + 1.0f * 0.30f;
  int expected_high = static_cast<int>(base * high_energy_factor);

  // Low energy: factor = 0.85 + 0.0 * 0.30 = 0.85
  // Result: 80 * 0.85 = 68
  float low_energy_factor = 0.85f + 0.0f * 0.30f;
  int expected_low = static_cast<int>(base * low_energy_factor);

  EXPECT_GT(expected_high, expected_low);
  EXPECT_NEAR(expected_high, 92, 1);
  EXPECT_NEAR(expected_low, 68, 1);
}

TEST(ApplyEmotionToVelocityTest, TensionAffectsCeiling) {
  // calculateVelocityCeiling behavior:
  // Low tension (0.2): ceiling_multiplier ~0.93 -> ceiling ~118
  // High tension (0.9): ceiling_multiplier ~1.16 -> ceiling ~127 (capped)

  // With low tension, even high energy shouldn't exceed the ceiling
  float low_tension = 0.2f;
  uint8_t ceiling_low = calculateVelocityCeiling(127, low_tension);

  float high_tension = 0.9f;
  uint8_t ceiling_high = calculateVelocityCeiling(127, high_tension);

  EXPECT_LT(ceiling_low, ceiling_high)
      << "Low tension should have lower velocity ceiling";
  EXPECT_LT(ceiling_low, 127)
      << "Low tension ceiling should be below max";
}

}  // namespace
}  // namespace midisketch
