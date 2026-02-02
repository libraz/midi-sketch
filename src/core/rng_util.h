/**
 * @file rng_util.h
 * @brief Random number generation utility helpers.
 *
 * Provides concise wrappers for common RNG patterns to avoid
 * repeated std::uniform_*_distribution boilerplate.
 */

#ifndef MIDISKETCH_CORE_RNG_UTIL_H
#define MIDISKETCH_CORE_RNG_UTIL_H

#include <random>

namespace midisketch {
namespace rng_util {

/// @brief Roll a probability check: returns true with probability `threshold`.
/// @param rng Random engine
/// @param threshold Probability [0.0, 1.0]
/// @return true if random value < threshold
inline bool rollProbability(std::mt19937& rng, float threshold) {
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  return dist(rng) < threshold;
}

/// @brief Generate a random integer in [min, max] (inclusive).
/// @param rng Random engine
/// @param min Minimum value (inclusive)
/// @param max Maximum value (inclusive)
/// @return Random integer in [min, max]
inline int rollRange(std::mt19937& rng, int min, int max) {
  std::uniform_int_distribution<int> dist(min, max);
  return dist(rng);
}

/// @brief Generate a random float in [min, max].
/// @param rng Random engine
/// @param min Minimum value
/// @param max Maximum value
/// @return Random float in [min, max]
inline float rollFloat(std::mt19937& rng, float min, float max) {
  std::uniform_real_distribution<float> dist(min, max);
  return dist(rng);
}

/// @brief Select a random element from a container.
/// @param rng Random engine
/// @param container Non-empty container with random access
/// @return Reference to a randomly selected element
template <typename Container>
inline auto& selectRandom(std::mt19937& rng, Container& container) {
  std::uniform_int_distribution<size_t> dist(0, container.size() - 1);
  return container[dist(rng)];
}

/// @brief Select a random element from a const container.
/// @param rng Random engine
/// @param container Non-empty container with random access
/// @return Const reference to a randomly selected element
template <typename Container>
inline const auto& selectRandom(std::mt19937& rng, const Container& container) {
  std::uniform_int_distribution<size_t> dist(0, container.size() - 1);
  return container[dist(rng)];
}

/// @brief Select a random index from a container.
/// @param rng Random engine
/// @param container Non-empty container
/// @return Random index in [0, container.size() - 1]
template <typename Container>
inline size_t selectRandomIndex(std::mt19937& rng, const Container& container) {
  std::uniform_int_distribution<size_t> dist(0, container.size() - 1);
  return dist(rng);
}

}  // namespace rng_util
}  // namespace midisketch

#endif  // MIDISKETCH_CORE_RNG_UTIL_H
