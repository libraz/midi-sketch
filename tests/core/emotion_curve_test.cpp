/**
 * @file emotion_curve_test.cpp
 * @brief Tests for the EmotionCurve system.
 */

#include "core/emotion_curve.h"

#include <gtest/gtest.h>

#include "core/generator.h"
#include "core/preset_data.h"
#include "core/structure.h"
#include "core/velocity.h"
#include "test_support/generator_test_fixture.h"

namespace midisketch {
namespace {

/// Structures wide enough to contain every section relation the curve reasons
/// about: a B before a Chorus, a repeated Chorus, and an Outro.
constexpr StructurePattern kCurvePatterns[] = {
    StructurePattern::StandardPop,
    StructurePattern::BuildUp,
    StructurePattern::FullPop,
    StructurePattern::RepeatChorus,
};

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

// The post-processing pipeline hands energy to calculateVelocityCeiling and to
// the section velocity factor, both of which are written for a 0.0-1.0 level.
// Mood scaling multiplies the whole curve by up to 1.2 before the clamp, so it
// is the clamp that keeps that assumption true -- and the default mood scales
// by 1.0, which asks nothing.
TEST(EmotionCurveTest, EnergyStaysInRangeForEveryMood) {
  for (uint8_t mood = 0; mood < MOOD_COUNT; ++mood) {
    for (StructurePattern pattern : kCurvePatterns) {
      std::vector<Section> sections = buildStructure(pattern);
      EmotionCurve curve;
      curve.plan(sections, static_cast<Mood>(mood));

      for (size_t i = 0; i < sections.size(); ++i) {
        float energy = curve.getEmotion(i).energy;
        EXPECT_GE(energy, 0.0f) << "mood=" << static_cast<int>(mood) << " section=" << i;
        EXPECT_LE(energy, 1.0f) << "mood=" << static_cast<int>(mood) << " section=" << i;
      }
    }
  }
}

TEST(EmotionCurveTest, GetEmotionOutOfRange) {
  EmotionCurve curve;

  std::vector<Section> sections = buildStructure(StructurePattern::StandardPop);
  curve.plan(sections, Mood::ModernPop);

  // An index past the planned curve returns the neutral midpoint rather than
  // reading past the end, and neutral means neutral on the wire that is read:
  // at 0.5 the ceiling stays at whatever base it was given.
  const auto& emotion = curve.getEmotion(999);
  EXPECT_FLOAT_EQ(emotion.energy, 0.5f);
  EXPECT_EQ(calculateVelocityCeiling(100, emotion.energy), 100);
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

  EXPECT_GT(chorus_energy, max_non_chorus_energy) << "Chorus should have highest energy";
}

// The run-up into a chorus is carried by energy, which sets both the section's
// velocity level and its transition ramp. Mood scaling multiplies every section
// by the same factor and then clamps at the top, so the question is whether the
// clamp can flatten the climb: a chorus levelled onto the B in front of it
// would stop being the section it is arranged to rise out of.
TEST(EmotionCurveTest, BStaysBelowTheChorusItRunsInto) {
  size_t pairs = 0;
  for (uint8_t mood = 0; mood < MOOD_COUNT; ++mood) {
    for (StructurePattern pattern : kCurvePatterns) {
      std::vector<Section> sections = buildStructure(pattern);
      EmotionCurve curve;
      curve.plan(sections, static_cast<Mood>(mood));

      for (size_t i = 0; i + 1 < sections.size(); ++i) {
        if (sections[i].type != SectionType::B || sections[i + 1].type != SectionType::Chorus) {
          continue;
        }
        ++pairs;
        EXPECT_LT(curve.getEmotion(i).energy, curve.getEmotion(i + 1).energy)
            << "mood=" << static_cast<int>(mood) << " B at section " << i
            << " is not below the Chorus it runs into";
      }
    }
  }
  EXPECT_GT(pairs, 0u) << "the patterns under test must contain a B before a Chorus";
}

TEST(EmotionCurveTest, IntroHasLowEnergy) {
  EmotionCurve curve;

  std::vector<Section> sections = buildStructure(StructurePattern::BuildUp);
  curve.plan(sections, Mood::ModernPop);

  // Find Intro
  for (size_t i = 0; i < sections.size(); ++i) {
    if (sections[i].type == SectionType::Intro) {
      const auto& emotion = curve.getEmotion(i);
      EXPECT_LT(emotion.energy, 0.5f) << "Intro should have low energy";
    }
  }
}

// An outro settles, and the field that carries "settled" into the song is the
// same one the run-up uses. Stated against the chorus rather than against a
// fixed number, the property survives a mood that scales the whole curve.
TEST(EmotionCurveTest, OutroSettlesBelowEveryChorus) {
  size_t outros = 0;
  for (uint8_t mood = 0; mood < MOOD_COUNT; ++mood) {
    for (StructurePattern pattern : kCurvePatterns) {
      std::vector<Section> sections = buildStructure(pattern);
      EmotionCurve curve;
      curve.plan(sections, static_cast<Mood>(mood));

      float peak_chorus_energy = -1.0f;
      for (size_t i = 0; i < sections.size(); ++i) {
        if (sections[i].type == SectionType::Chorus) {
          peak_chorus_energy = std::max(peak_chorus_energy, curve.getEmotion(i).energy);
        }
      }
      if (peak_chorus_energy < 0.0f) continue;

      for (size_t i = 0; i < sections.size(); ++i) {
        if (sections[i].type != SectionType::Outro) continue;
        ++outros;
        EXPECT_LT(curve.getEmotion(i).energy, peak_chorus_energy)
            << "mood=" << static_cast<int>(mood) << " Outro at section " << i
            << " is not below the loudest Chorus";
      }
    }
  }
  EXPECT_GT(outros, 0u) << "the patterns under test must contain an Outro";
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
    if (sections[i].type == SectionType::B && sections[i + 1].type == SectionType::Chorus) {
      auto hint = curve.getTransitionHint(i);
      EXPECT_TRUE(hint.crescendo) << "Should crescendo from B to Chorus";
      EXPECT_TRUE(hint.use_fill) << "Should use fill before Chorus";
      // crescendo only reaches the song through the ramp it sets.
      EXPECT_GT(hint.velocity_ramp, 1.0f) << "Crescendo should raise the transition ramp";
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

class EmotionCurveIntegrationTest : public test::GeneratorTestFixture {
 protected:
  void SetUp() override {
    GeneratorTestFixture::SetUp();
    params_.mood = Mood::ModernPop;
    params_.drums_enabled = true;
    params_.structure = StructurePattern::BuildUp;  // Intro -> A -> B -> Chorus
    params_.vocal_high = 72;
  }
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
    if (sections[i].type == SectionType::B && sections[i + 1].type == SectionType::Chorus) {
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
    if (sections[i].type == SectionType::B && sections[i + 1].type == SectionType::Chorus) {
      const auto& b_section = sections[i];

      // Define transition zone: last 2 beats of B section
      Tick section_end = b_section.endTick();
      Tick transition_start = section_end - 480 * 2;      // Last 2 beats
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

// use_fill is the only transition hint read before generation: it marks the
// next section for a drum fill while the arrangement is still being built. The
// arrangement is rebuilt from the marked sections, so the mark has to survive
// into the sections the song is generated from.
TEST_F(EmotionCurveIntegrationTest, UseFillAppliedToSectionFillBefore) {
  params_.structure = StructurePattern::BuildUp;  // Has B -> Chorus transition
  params_.seed = 12345;

  generator_.generate(params_);

  const auto& sections = generator_.getSong().arrangement().sections();
  const auto& emotion_curve = generator_.getEmotionCurve();

  size_t marked = 0;
  for (size_t i = 0; i + 1 < sections.size(); ++i) {
    if (!emotion_curve.getTransitionHint(i).use_fill) continue;
    ++marked;
    EXPECT_TRUE(sections[i + 1].fill_before)
        << "section " << (i + 1) << " follows a use_fill transition but carries no fill mark";
  }
  EXPECT_GT(marked, 0u) << "BuildUp must produce at least one use_fill transition";
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
