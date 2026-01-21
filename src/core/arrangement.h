/**
 * @file arrangement.h
 * @brief Section arrangement and bar-based utilities.
 */

#ifndef MIDISKETCH_CORE_ARRANGEMENT_H
#define MIDISKETCH_CORE_ARRANGEMENT_H

#include <functional>
#include <vector>

#include "core/types.h"

namespace midisketch {

/// @brief Section arrangement and bar-based operation utilities.
class Arrangement {
 public:
  Arrangement() = default;
  explicit Arrangement(const std::vector<Section>& sections);

  /// @name Time Conversion
  /// @{
  Tick barToTick(uint32_t bar) const;
  std::pair<Tick, Tick> sectionToTickRange(const Section& section) const;
  /// @}

  /// @name Section Iteration
  /// @{
  void iterateSections(std::function<void(const Section&)> callback) const;
  /// @}

  /// @name Accessors
  /// @{
  const std::vector<Section>& sections() const { return sections_; }
  uint32_t totalBars() const;
  Tick totalTicks() const;
  size_t sectionCount() const { return sections_.size(); }
  const Section* sectionAtBar(uint32_t bar) const;
  /// @}

  /// @name Time Info
  /// @{
  Tick ticksPerBeat() const { return TICKS_PER_BEAT; }
  uint8_t beatsPerBar() const { return BEATS_PER_BAR; }
  Tick ticksPerBar() const { return TICKS_PER_BAR; }
  /// @}

 private:
  std::vector<Section> sections_;
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_ARRANGEMENT_H
