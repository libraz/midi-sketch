/**
 * @file density_transformer.h
 * @brief Generic density transformation for pattern types.
 *
 * Provides a template-based approach to pattern density adjustments,
 * consolidating the common sparser/denser switch statements found in
 * bass.cpp and chord_track.cpp.
 *
 * Usage:
 * @code
 * // Define once
 * const auto kBassTransformer = DensityTransformer<BassPattern>::builder()
 *     .addTransition(BassPattern::Aggressive, BassPattern::Driving)
 *     .addTransition(BassPattern::Driving, BassPattern::Syncopated)
 *     .build();
 *
 * // Use anywhere
 * BassPattern sparser = kBassTransformer.sparser(BassPattern::Aggressive);
 * BassPattern denser = kBassTransformer.denser(BassPattern::Syncopated);
 * @endcode
 */

#ifndef MIDISKETCH_CORE_DENSITY_TRANSFORMER_H
#define MIDISKETCH_CORE_DENSITY_TRANSFORMER_H

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "core/types.h"

namespace midisketch {

/**
 * @brief Generic density transformer for pattern types.
 *
 * Stores bidirectional transitions between pattern values to enable
 * sparser() and denser() operations. Pattern values are treated as
 * integers for storage.
 *
 * @tparam PatternType Enum type for pattern values
 */
template <typename PatternType>
class DensityTransformer {
 public:
  /**
   * @brief Builder for constructing DensityTransformer instances.
   */
  class Builder {
   public:
    /**
     * @brief Add a transition from dense to sparse pattern.
     *
     * This implies: sparser(dense) = sparse, denser(sparse) = dense.
     *
     * @param dense The denser pattern
     * @param sparse The sparser pattern
     * @return Reference for chaining
     */
    Builder& addTransition(PatternType dense, PatternType sparse) {
      int d = static_cast<int>(dense);
      int s = static_cast<int>(sparse);
      sparse_map_[d] = s;
      dense_map_[s] = d;
      return *this;
    }

    /**
     * @brief Add a self-transition (pattern is already at limit).
     * @param pattern Pattern that can't go sparser or denser
     * @return Reference for chaining
     */
    Builder& addLimit(PatternType pattern) {
      int p = static_cast<int>(pattern);
      // Self-transitions indicate limits
      if (sparse_map_.find(p) == sparse_map_.end()) {
        sparse_map_[p] = p;
      }
      if (dense_map_.find(p) == dense_map_.end()) {
        dense_map_[p] = p;
      }
      return *this;
    }

    /**
     * @brief Build the transformer.
     * @return Constructed DensityTransformer
     */
    DensityTransformer build() const {
      return DensityTransformer(sparse_map_, dense_map_);
    }

   private:
    std::unordered_map<int, int> sparse_map_;
    std::unordered_map<int, int> dense_map_;
  };

  /**
   * @brief Create a builder for constructing transformers.
   * @return Builder instance
   */
  static Builder builder() { return Builder(); }

  /**
   * @brief Default constructor for empty transformer.
   */
  DensityTransformer() = default;

  /**
   * @brief Get the next sparser pattern.
   * @param pattern Current pattern
   * @return Sparser pattern, or same if at limit
   */
  PatternType sparser(PatternType pattern) const {
    int p = static_cast<int>(pattern);
    auto it = sparse_map_.find(p);
    if (it != sparse_map_.end()) {
      return static_cast<PatternType>(it->second);
    }
    return pattern;
  }

  /**
   * @brief Get the next denser pattern.
   * @param pattern Current pattern
   * @return Denser pattern, or same if at limit
   */
  PatternType denser(PatternType pattern) const {
    int p = static_cast<int>(pattern);
    auto it = dense_map_.find(p);
    if (it != dense_map_.end()) {
      return static_cast<PatternType>(it->second);
    }
    return pattern;
  }

  /**
   * @brief Adjust pattern based on backing density.
   * @param pattern Current pattern
   * @param density Target density
   * @return Adjusted pattern
   */
  PatternType adjust(PatternType pattern, BackingDensity density) const {
    switch (density) {
      case BackingDensity::Sparse:
        return sparser(pattern);
      case BackingDensity::Dense:
        return denser(pattern);
      case BackingDensity::Normal:
      default:
        return pattern;
    }
  }

 private:
  DensityTransformer(const std::unordered_map<int, int>& sparse_map,
                     const std::unordered_map<int, int>& dense_map)
      : sparse_map_(sparse_map), dense_map_(dense_map) {}

  std::unordered_map<int, int> sparse_map_;
  std::unordered_map<int, int> dense_map_;
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_DENSITY_TRANSFORMER_H
