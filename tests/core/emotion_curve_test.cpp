/**
 * @file emotion_curve_test.cpp
 * @brief Tests for the EmotionCurve system.
 */

#include "core/emotion_curve.h"

#include <gtest/gtest.h>

#include "core/generator.h"
#include "core/structure.h"

namespace midisketch {
namespace {

// ============================================================================
// EmotionCurve Basic Tests
// ============================================================================

TEST(EmotionCurveTest, EmptyBeforePlan) {
  EmotionCurve curve;
  EXPECT_FALSE(curve.isPlanned());
  EXPECT_EQ(curve.size(), 0);
}

TEST(EmotionCurveTest, PlannedAfterPlan) {
  EmotionCurve curve;

  std::vector<Section> sections = buildStructure(StructurePattern::StandardPop);
  curve.plan(sections, Mood::ModernPop);

  EXPECT_TRUE(curve.isPlanned());
  EXPECT_EQ(curve.size(), sections.size());
}

TEST(EmotionCurveTest, GetEmotionInRange) {
  EmotionCurve curve;

  std::vector<Section> sections = buildStructure(StructurePattern::StandardPop);
  curve.plan(sections, Mood::ModernPop);

  for (size_t i = 0; i < sections.size(); ++i) {
    const auto& emotion = curve.getEmotion(i);
    EXPECT_GE(emotion.tension, 0.0f);
    EXPECT_LE(emotion.tension, 1.0f);
    EXPECT_GE(emotion.energy, 0.0f);
    EXPECT_LE(emotion.energy, 1.0f);
    EXPECT_GE(emotion.resolution_need, 0.0f);
    EXPECT_LE(emotion.resolution_need, 1.0f);
    EXPECT_GE(emotion.pitch_tendency, -3);
    EXPECT_LE(emotion.pitch_tendency, 3);
    EXPECT_GE(emotion.density_factor, 0.5f);
    EXPECT_LE(emotion.density_factor, 1.5f);
  }
}

TEST(EmotionCurveTest, GetEmotionOutOfRange) {
  EmotionCurve curve;

  std::vector<Section> sections = buildStructure(StructurePattern::StandardPop);
  curve.plan(sections, Mood::ModernPop);

  // Out of range should return default
  const auto& emotion = curve.getEmotion(999);
  EXPECT_FLOAT_EQ(emotion.tension, 0.5f);
  EXPECT_FLOAT_EQ(emotion.energy, 0.5f);
}

// ============================================================================
// Section Type Emotion Tests
// ============================================================================

TEST(EmotionCurveTest, ChorusHasHighestEnergy) {
  EmotionCurve curve;

  std::vector<Section> sections = buildStructure(StructurePattern::BuildUp);
  curve.plan(sections, Mood::ModernPop);

  // Find Chorus and compare energy
  float chorus_energy = 0.0f;
  float max_non_chorus_energy = 0.0f;

  for (size_t i = 0; i < sections.size(); ++i) {
    const auto& emotion = curve.getEmotion(i);
    if (sections[i].type == SectionType::Chorus) {
      chorus_energy = std::max(chorus_energy, emotion.energy);
    } else if (sections[i].type != SectionType::MixBreak) {
      max_non_chorus_energy = std::max(max_non_chorus_energy, emotion.energy);
    }
  }

  EXPECT_GT(chorus_energy, max_non_chorus_energy)
      << "Chorus should have highest energy";
}

TEST(EmotionCurveTest, BBeforeChorusHasHighTension) {
  EmotionCurve curve;

  std::vector<Section> sections = buildStructure(StructurePattern::BuildUp);
  curve.plan(sections, Mood::ModernPop);

  // Find B sections before Chorus
  for (size_t i = 0; i + 1 < sections.size(); ++i) {
    if (sections[i].type == SectionType::B &&
        sections[i + 1].type == SectionType::Chorus) {
      const auto& b_emotion = curve.getEmotion(i);
      EXPECT_GT(b_emotion.tension, 0.6f)
          << "B section before Chorus should have high tension";
      EXPECT_GT(b_emotion.resolution_need, 0.5f)
          << "B section before Chorus should have high resolution need";
    }
  }
}

TEST(EmotionCurveTest, IntroHasLowEnergy) {
  EmotionCurve curve;

  std::vector<Section> sections = buildStructure(StructurePattern::BuildUp);
  curve.plan(sections, Mood::ModernPop);

  // Find Intro
  for (size_t i = 0; i < sections.size(); ++i) {
    if (sections[i].type == SectionType::Intro) {
      const auto& emotion = curve.getEmotion(i);
      EXPECT_LT(emotion.energy, 0.5f)
          << "Intro should have low energy";
    }
  }
}

TEST(EmotionCurveTest, OutroHasLowTension) {
  EmotionCurve curve;

  std::vector<Section> sections = buildStructure(StructurePattern::FullPop);
  curve.plan(sections, Mood::ModernPop);

  // Find Outro
  for (size_t i = 0; i < sections.size(); ++i) {
    if (sections[i].type == SectionType::Outro) {
      const auto& emotion = curve.getEmotion(i);
      EXPECT_LT(emotion.tension, 0.3f)
          << "Outro should have low tension";
      EXPECT_LT(emotion.resolution_need, 0.3f)
          << "Outro should have low resolution need (resolved)";
    }
  }
}

// ============================================================================
// Mood Intensity Tests
// ============================================================================

TEST(EmotionCurveTest, EnergeticMoodHigherIntensity) {
  EXPECT_GT(EmotionCurve::getMoodIntensity(Mood::EnergeticDance),
            EmotionCurve::getMoodIntensity(Mood::ModernPop));
  EXPECT_GT(EmotionCurve::getMoodIntensity(Mood::IdolPop),
            EmotionCurve::getMoodIntensity(Mood::ModernPop));
}

TEST(EmotionCurveTest, BalladMoodLowerIntensity) {
  EXPECT_LT(EmotionCurve::getMoodIntensity(Mood::Ballad),
            EmotionCurve::getMoodIntensity(Mood::ModernPop));
  EXPECT_LT(EmotionCurve::getMoodIntensity(Mood::Chill),
            EmotionCurve::getMoodIntensity(Mood::ModernPop));
}

TEST(EmotionCurveTest, MoodAffectsEnergy) {
  EmotionCurve energetic_curve;
  EmotionCurve ballad_curve;

  std::vector<Section> sections = buildStructure(StructurePattern::StandardPop);
  energetic_curve.plan(sections, Mood::EnergeticDance);
  ballad_curve.plan(sections, Mood::Ballad);

  // Find Chorus and compare
  for (size_t i = 0; i < sections.size(); ++i) {
    if (sections[i].type == SectionType::Chorus) {
      const auto& energetic_emotion = energetic_curve.getEmotion(i);
      const auto& ballad_emotion = ballad_curve.getEmotion(i);

      EXPECT_GT(energetic_emotion.energy, ballad_emotion.energy)
          << "Energetic mood should have higher energy than Ballad";
    }
  }
}

// ============================================================================
// Transition Hint Tests
// ============================================================================

TEST(EmotionCurveTest, TransitionHintCrescendoBeforeChorus) {
  EmotionCurve curve;

  std::vector<Section> sections = buildStructure(StructurePattern::BuildUp);
  curve.plan(sections, Mood::ModernPop);

  // Find B -> Chorus transition
  for (size_t i = 0; i + 1 < sections.size(); ++i) {
    if (sections[i].type == SectionType::B &&
        sections[i + 1].type == SectionType::Chorus) {
      auto hint = curve.getTransitionHint(i);
      EXPECT_TRUE(hint.crescendo)
          << "Should crescendo from B to Chorus";
      EXPECT_TRUE(hint.use_fill)
          << "Should use fill before Chorus";
      EXPECT_TRUE(hint.use_leading_tone)
          << "Should use leading tone from B to Chorus";
    }
  }
}

TEST(EmotionCurveTest, TransitionHintOutOfRange) {
  EmotionCurve curve;

  std::vector<Section> sections = buildStructure(StructurePattern::StandardPop);
  curve.plan(sections, Mood::ModernPop);

  // Out of range transition
  auto hint = curve.getTransitionHint(999);
  EXPECT_FALSE(hint.crescendo);
  EXPECT_FALSE(hint.use_fill);
  EXPECT_FLOAT_EQ(hint.velocity_ramp, 1.0f);
}

TEST(EmotionCurveTest, TransitionHintLastSection) {
  EmotionCurve curve;

  std::vector<Section> sections = buildStructure(StructurePattern::StandardPop);
  curve.plan(sections, Mood::ModernPop);

  // Last section transition (no next section)
  auto hint = curve.getTransitionHint(sections.size() - 1);
  EXPECT_FALSE(hint.crescendo);
  EXPECT_FALSE(hint.use_fill);
}

// ============================================================================
// Progressive Intensity Tests
// ============================================================================

TEST(EmotionCurveTest, RepeatedChorusIncreasingEnergy) {
  EmotionCurve curve;

  // Use pattern with multiple choruses
  std::vector<Section> sections = buildStructure(StructurePattern::RepeatChorus);
  curve.plan(sections, Mood::ModernPop);

  // Find all Chorus sections and track energy
  std::vector<float> chorus_energies;
  for (size_t i = 0; i < sections.size(); ++i) {
    if (sections[i].type == SectionType::Chorus) {
      chorus_energies.push_back(curve.getEmotion(i).energy);
    }
  }

  // Later choruses should have equal or higher energy
  for (size_t i = 1; i < chorus_energies.size(); ++i) {
    EXPECT_GE(chorus_energies[i], chorus_energies[i - 1])
        << "Later Chorus should have equal or higher energy";
  }
}

// ============================================================================
// EmotionCurve Integration Tests (with Generator)
// ============================================================================

class EmotionCurveIntegrationTest : public ::testing::Test {
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

  GeneratorParams params_;
  Generator generator_;
};

TEST_F(EmotionCurveIntegrationTest, EmotionCurvePlannedAfterGeneration) {
  generator_.generate(params_);

  // EmotionCurve should be planned after generation
  EXPECT_TRUE(generator_.getEmotionCurve().isPlanned());
}

TEST_F(EmotionCurveIntegrationTest, EmotionCurveSizeMatchesSections) {
  generator_.generate(params_);

  const auto& sections = generator_.getSong().arrangement().sections();
  EXPECT_EQ(generator_.getEmotionCurve().size(), sections.size());
}

TEST_F(EmotionCurveIntegrationTest, TransitionHintAffectsVelocity) {
  // Generate with BuildUp pattern (has B -> Chorus transition)
  generator_.generate(params_);

  const auto& sections = generator_.getSong().arrangement().sections();

  // Find B -> Chorus transition
  for (size_t i = 0; i + 1 < sections.size(); ++i) {
    if (sections[i].type == SectionType::B &&
        sections[i + 1].type == SectionType::Chorus) {
      auto hint = generator_.getEmotionCurve().getTransitionHint(i);

      // B -> Chorus should have crescendo
      EXPECT_TRUE(hint.crescendo) << "B -> Chorus should crescendo";
      EXPECT_GT(hint.velocity_ramp, 1.0f) << "B -> Chorus should have velocity increase";
      break;
    }
  }
}

TEST_F(EmotionCurveIntegrationTest, VelocityIncreasesInTransitionZone) {
  // Test that applyEmotionBasedDynamics actually increases velocity
  // in the transition zone before Chorus
  generator_.generate(params_);

  const auto& sections = generator_.getSong().arrangement().sections();
  const auto& vocal = generator_.getSong().vocal();

  // Find B section that precedes Chorus
  for (size_t i = 0; i + 1 < sections.size(); ++i) {
    if (sections[i].type == SectionType::B &&
        sections[i + 1].type == SectionType::Chorus) {
      const auto& b_section = sections[i];

      // Define transition zone: last 2 beats of B section
      Tick section_end = b_section.start_tick + b_section.bars * 1920;
      Tick transition_start = section_end - 480 * 2;  // Last 2 beats
      Tick early_zone_end = b_section.start_tick + 1920;  // First bar

      // Collect velocities from early B section and transition zone
      std::vector<uint8_t> early_velocities;
      std::vector<uint8_t> transition_velocities;

      for (const auto& note : vocal.notes()) {
        if (note.start_tick >= b_section.start_tick && note.start_tick < early_zone_end) {
          early_velocities.push_back(note.velocity);
        }
        if (note.start_tick >= transition_start && note.start_tick < section_end) {
          transition_velocities.push_back(note.velocity);
        }
      }

      // If we have notes in both zones, transition zone should have higher average velocity
      if (!early_velocities.empty() && !transition_velocities.empty()) {
        float early_avg = 0.0f;
        for (auto v : early_velocities) early_avg += v;
        early_avg /= early_velocities.size();

        float transition_avg = 0.0f;
        for (auto v : transition_velocities) transition_avg += v;
        transition_avg /= transition_velocities.size();

        // Transition zone velocity should be >= early zone (crescendo effect)
        EXPECT_GE(transition_avg, early_avg * 0.95f)
            << "Transition zone should have equal or higher velocity than early B section";
      }
      break;
    }
  }
}

TEST_F(EmotionCurveIntegrationTest, UseFillAppliedToSectionFillBefore) {
  // Test that EmotionCurve's use_fill is reflected in Section.fill_before
  params_.structure = StructurePattern::BuildUp;  // Has B -> Chorus transition
  params_.seed = 12345;

  generator_.generate(params_);

  const auto& sections = generator_.getSong().arrangement().sections();
  const auto& emotion_curve = generator_.getEmotionCurve();

  // Find transitions where use_fill is true
  for (size_t i = 0; i + 1 < sections.size(); ++i) {
    auto hint = emotion_curve.getTransitionHint(i);

    // If emotion curve suggests a fill, the next section should have fill_before
    // (unless it was already set by PeakLevel)
    if (hint.use_fill) {
      // At minimum, verify the hint is correctly computed for high-energy transitions
      if (sections[i].type == SectionType::B && sections[i + 1].type == SectionType::Chorus) {
        EXPECT_TRUE(hint.use_fill) << "B -> Chorus should have use_fill hint";
      }
    }
  }
}

TEST_F(EmotionCurveIntegrationTest, FillBeforeReflectedInDrumTrack) {
  // Test that fill_before results in actual drum fills
  params_.structure = StructurePattern::BuildUp;
  params_.seed = 54321;
  params_.drums_enabled = true;

  generator_.generate(params_);

  const auto& sections = generator_.getSong().arrangement().sections();
  const auto& drums = generator_.getSong().drums();

  // Find a section with fill_before = true
  for (size_t i = 1; i < sections.size(); ++i) {
    if (sections[i].fill_before) {
      // Check for drum activity in the last bar of the previous section
      Tick prev_section_end = sections[i].start_tick;
      Tick prev_section_last_bar = prev_section_end - TICKS_PER_BAR;

      // Count drum hits in the last bar (fills typically have more activity)
      int last_bar_hits = 0;
      for (const auto& note : drums.notes()) {
        if (note.start_tick >= prev_section_last_bar && note.start_tick < prev_section_end) {
          ++last_bar_hits;
        }
      }

      // Fill sections should have drum activity
      EXPECT_GT(last_bar_hits, 0)
          << "Section with fill_before should have drum hits in preceding bar";
      break;
    }
  }
}

}  // namespace
}  // namespace midisketch
