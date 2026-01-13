/**
 * @file composition_strategy.h
 * @brief Strategy pattern for different composition styles.
 *
 * Each CompositionStyle has a corresponding Strategy that defines
 * the track generation order and post-processing requirements.
 */

#ifndef MIDISKETCH_CORE_COMPOSITION_STRATEGY_H
#define MIDISKETCH_CORE_COMPOSITION_STRATEGY_H

#include "core/preset_types.h"
#include <memory>

namespace midisketch {

// Forward declarations
class Generator;

/**
 * @brief Abstract base class for composition strategies.
 *
 * Defines the interface for generating tracks based on composition style.
 * Each concrete strategy implements the specific track generation order
 * and post-processing steps for its style.
 */
class CompositionStrategy {
 public:
  virtual ~CompositionStrategy() = default;

  /**
   * @brief Generate melodic tracks (vocal, bass, aux, motif) in style-specific order.
   * @param gen Generator instance with access to track generation methods
   */
  virtual void generateMelodicTracks(Generator& gen) = 0;

  /**
   * @brief Generate chord track with style-specific voicing coordination.
   * @param gen Generator instance
   */
  virtual void generateChordTrack(Generator& gen) = 0;

  /**
   * @brief Check if arpeggio should be auto-enabled for this style.
   * @return true if arpeggio should be automatically enabled
   */
  virtual bool autoEnableArpeggio() const { return false; }

  /**
   * @brief Check if arpeggio-chord clash resolution is needed.
   * @return true if clashes should be resolved post-generation
   */
  virtual bool needsArpeggioClashResolution() const { return false; }

  /**
   * @brief Get the composition style this strategy handles.
   * @return CompositionStyle enum value
   */
  virtual CompositionStyle getStyle() const = 0;
};

/**
 * @brief Strategy for MelodyLead composition style.
 *
 * Vocal-first generation order for proper harmonic coordination.
 * Vocal -> Bass (with vocal analysis) -> Aux -> Chord
 */
class MelodyLeadStrategy : public CompositionStrategy {
 public:
  void generateMelodicTracks(Generator& gen) override;
  void generateChordTrack(Generator& gen) override;
  CompositionStyle getStyle() const override { return CompositionStyle::MelodyLead; }
};

/**
 * @brief Strategy for BackgroundMotif composition style.
 *
 * Motif-driven BGM mode (no vocal/aux).
 * Bass -> Motif -> Chord
 */
class BackgroundMotifStrategy : public CompositionStrategy {
 public:
  void generateMelodicTracks(Generator& gen) override;
  void generateChordTrack(Generator& gen) override;
  bool needsArpeggioClashResolution() const override { return true; }
  CompositionStyle getStyle() const override { return CompositionStyle::BackgroundMotif; }
};

/**
 * @brief Strategy for SynthDriven composition style.
 *
 * Arpeggio-driven BGM mode (no vocal/aux).
 * Bass -> Chord (arpeggio auto-enabled)
 */
class SynthDrivenStrategy : public CompositionStrategy {
 public:
  void generateMelodicTracks(Generator& gen) override;
  void generateChordTrack(Generator& gen) override;
  bool autoEnableArpeggio() const override { return true; }
  bool needsArpeggioClashResolution() const override { return true; }
  CompositionStyle getStyle() const override { return CompositionStyle::SynthDriven; }
};

/**
 * @brief Factory function to create appropriate strategy for a composition style.
 * @param style The composition style
 * @return Unique pointer to the corresponding strategy
 */
std::unique_ptr<CompositionStrategy> createCompositionStrategy(CompositionStyle style);

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_COMPOSITION_STRATEGY_H
