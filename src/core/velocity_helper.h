/**
 * @file velocity_helper.h
 * @brief Velocity calculation utilities.
 *
 * Provides concise wrappers for common velocity operations to avoid
 * repeated static_cast + std::clamp boilerplate.
 */

#ifndef MIDISKETCH_CORE_VELOCITY_HELPER_H
#define MIDISKETCH_CORE_VELOCITY_HELPER_H

#include <algorithm>
#include <cstdint>

namespace midisketch {
namespace vel {

/// @brief Clamp a velocity value to valid MIDI range [1, 127].
/// @param raw Raw velocity value (may be out of range)
/// @return Clamped uint8_t velocity
inline uint8_t clamp(int raw) {
  return static_cast<uint8_t>(std::clamp(raw, 1, 127));
}

/// @brief Clamp a float velocity value to valid MIDI range [1, 127].
/// @param raw Raw velocity value (may be out of range)
/// @return Clamped uint8_t velocity
inline uint8_t clamp(float raw) {
  return static_cast<uint8_t>(std::clamp(raw, 1.0f, 127.0f));
}

/// @brief Clamp a velocity value to a custom range.
/// @param raw Raw velocity value
/// @param min_vel Minimum velocity
/// @param max_vel Maximum velocity
/// @return Clamped uint8_t velocity
inline uint8_t clamp(int raw, int min_vel, int max_vel) {
  return static_cast<uint8_t>(std::clamp(raw, min_vel, max_vel));
}

/// @brief Clamp a float velocity value to a custom range.
inline uint8_t clamp(float raw, float min_vel, float max_vel) {
  return static_cast<uint8_t>(std::clamp(raw, min_vel, max_vel));
}

/// @brief Scale a base velocity by a ratio, clamped to [1, 127].
/// @param base Base velocity
/// @param ratio Scale factor (e.g. 0.8f for 80%)
/// @return Scaled and clamped uint8_t velocity
inline uint8_t scale(uint8_t base, float ratio) {
  return static_cast<uint8_t>(std::clamp(
      static_cast<int>(static_cast<float>(base) * ratio), 1, 127));
}

/// @brief Apply a signed delta to a velocity, clamped to [1, 127].
/// @param base Base velocity
/// @param delta Signed adjustment (positive = louder, negative = softer)
/// @return Adjusted and clamped uint8_t velocity
inline uint8_t withDelta(uint8_t base, int delta) {
  return static_cast<uint8_t>(std::clamp(static_cast<int>(base) + delta, 1, 127));
}

}  // namespace vel
}  // namespace midisketch

#endif  // MIDISKETCH_CORE_VELOCITY_HELPER_H
