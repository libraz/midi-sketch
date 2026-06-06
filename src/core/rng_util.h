/**
 * @file rng_util.h
 * @brief Random number generation utility helpers.
 *
 * Provides concise wrappers for common RNG patterns. All helpers consume the
 * raw mt19937 output directly instead of going through
 * std::uniform_*_distribution: the standard distributions are
 * implementation-defined, so libstdc++ (Linux) and libc++ (macOS / WASM)
 * produce different values from the same seeded engine. Deterministic
 * cross-platform output is a core requirement (seed reproducibility, WASM/CLI
 * parity, CI on Linux), so the mapping from engine output to values must be
 * fully specified here.
 *
 * The tiny modulo bias of `rng() % span` (span << 2^32) is musically
 * irrelevant and is accepted in exchange for portability.
 */

#ifndef MIDISKETCH_CORE_RNG_UTIL_H
#define MIDISKETCH_CORE_RNG_UTIL_H

#include <cstdint>
#include <random>
#include <utility>

namespace midisketch {
namespace rng_util {

/// @brief Generate a random float in [0.0, 1.0) with 24-bit resolution.
/// @param rng Random engine
/// @return Random float in [0.0, 1.0)
inline float rollUnit(std::mt19937& rng) {
  // Top 24 bits -> exactly representable in a float mantissa.
  return static_cast<float>(static_cast<uint32_t>(rng()) >> 8) * (1.0f / 16777216.0f);
}

/// @brief Roll a probability check: returns true with probability `threshold`.
/// @param rng Random engine
/// @param threshold Probability [0.0, 1.0]
/// @return true if random value < threshold
inline bool rollProbability(std::mt19937& rng, float threshold) {
  return rollUnit(rng) < threshold;
}

/// @brief Generate a random integer in [min, max] (inclusive).
/// @param rng Random engine
/// @param min Minimum value (inclusive)
/// @param max Maximum value (inclusive)
/// @return Random integer in [min, max]
inline int rollRange(std::mt19937& rng, int min, int max) {
  uint32_t span = static_cast<uint32_t>(max - min) + 1u;
  return min + static_cast<int>(static_cast<uint32_t>(rng()) % span);
}

/// @brief Generate a random float in [min, max).
/// @param rng Random engine
/// @param min Minimum value
/// @param max Maximum value
/// @return Random float in [min, max)
inline float rollFloat(std::mt19937& rng, float min, float max) {
  return min + rollUnit(rng) * (max - min);
}

/// @brief Select a random index in [0, size - 1].
/// @param rng Random engine
/// @param size Container size (must be > 0)
/// @return Random index in [0, size - 1]
inline size_t rollIndex(std::mt19937& rng, size_t size) {
  return static_cast<size_t>(static_cast<uint32_t>(rng()) % static_cast<uint32_t>(size));
}

/// @brief Select a random element from a container.
/// @param rng Random engine
/// @param container Non-empty container with random access
/// @return Reference to a randomly selected element
template <typename Container>
inline auto& selectRandom(std::mt19937& rng, Container& container) {
  return container[rollIndex(rng, container.size())];
}

/// @brief Select a random element from a const container.
/// @param rng Random engine
/// @param container Non-empty container with random access
/// @return Const reference to a randomly selected element
template <typename Container>
inline const auto& selectRandom(std::mt19937& rng, const Container& container) {
  return container[rollIndex(rng, container.size())];
}

/// @brief Select a random index from a container.
/// @param rng Random engine
/// @param container Non-empty container
/// @return Random index in [0, container.size() - 1]
template <typename Container>
inline size_t selectRandomIndex(std::mt19937& rng, const Container& container) {
  return rollIndex(rng, container.size());
}

/// @brief Deterministic Fisher-Yates shuffle.
/// @param first Iterator to the first element
/// @param last Iterator past the last element
/// @param rng Random engine
///
/// std::shuffle's use of the engine is implementation-defined; this spells
/// out the classic Fisher-Yates walk so the permutation is identical on
/// every platform.
template <typename RandomIt>
inline void shuffle(RandomIt first, RandomIt last, std::mt19937& rng) {
  auto n = last - first;
  for (auto i = n - 1; i > 0; --i) {
    auto j = static_cast<decltype(i)>(rollIndex(rng, static_cast<size_t>(i) + 1));
    std::swap(first[i], first[j]);
  }
}

}  // namespace rng_util
}  // namespace midisketch

#endif  // MIDISKETCH_CORE_RNG_UTIL_H
