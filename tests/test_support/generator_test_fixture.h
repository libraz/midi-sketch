/**
 * @file generator_test_fixture.h
 * @brief Base test fixture for generator-based tests.
 *
 * Provides standard SetUp with common parameters and convenience helpers
 * to eliminate boilerplate across track and generator test files.
 *
 * Usage:
 * @code
 * class MyTest : public GeneratorTestFixture {
 *  protected:
 *   void SetUp() override {
 *     GeneratorTestFixture::SetUp();
 *     // Customize params_ if needed
 *     params_.mood = Mood::Ballad;
 *   }
 * };
 *
 * TEST_F(MyTest, SomeTest) {
 *   generate();
 *   EXPECT_FALSE(song().bass().empty());
 * }
 * @endcode
 */

#ifndef MIDISKETCH_TEST_GENERATOR_TEST_FIXTURE_H
#define MIDISKETCH_TEST_GENERATOR_TEST_FIXTURE_H

#include <gtest/gtest.h>

#include "core/generator.h"
#include "core/song.h"
#include "core/types.h"

namespace midisketch {
namespace test {

class GeneratorTestFixture : public ::testing::Test {
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
    // Disable humanization for deterministic tests
    params_.humanize = false;
  }

  /// Generate a song using the current params_
  void generate() { gen_.generate(params_); }

  /// Get the generated song (call after generate())
  const Song& song() const { return gen_.getSong(); }

  GeneratorParams params_;
  Generator gen_;
};

}  // namespace test
}  // namespace midisketch

#endif  // MIDISKETCH_TEST_GENERATOR_TEST_FIXTURE_H
