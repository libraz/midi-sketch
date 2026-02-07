/**
 * @file test_constants.h
 * @brief Shared constants for tests to eliminate duplication.
 *
 * Common musical constants used across multiple test files.
 */

#ifndef MIDISKETCH_TEST_TEST_CONSTANTS_H
#define MIDISKETCH_TEST_TEST_CONSTANTS_H

#include <cstdint>
#include <set>

namespace midisketch {
namespace test {

/// C major scale pitch classes: C, D, E, F, G, A, B
inline const std::set<int> kCMajorPitchClasses = {0, 2, 4, 5, 7, 9, 11};

/// Bass range limits (electric bass: C1 to C4)
constexpr uint8_t kBassLow = 24;   // C1
constexpr uint8_t kBassHigh = 60;  // C4

}  // namespace test
}  // namespace midisketch

#endif  // MIDISKETCH_TEST_TEST_CONSTANTS_H
