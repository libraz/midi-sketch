/**
 * @file timing_offset_calculator.h
 * @brief Timing offset calculation for micro-timing humanization.
 *
 * Extracted from PostProcessor::applyMicroTimingOffsets() to improve
 * traceability and reduce call chain depth. The original implementation
 * used inline lambdas that were difficult to trace and debug.
 *
 * This class provides:
 * - Clear separation of drum, bass, and vocal timing logic
 * - Named methods instead of anonymous lambdas
 * - Better testability for groove feel adjustments
 */

#ifndef MIDISKETCH_CORE_TIMING_OFFSET_CALCULATOR_H
#define MIDISKETCH_CORE_TIMING_OFFSET_CALCULATOR_H

#include <cstdint>
#include <vector>

#include "core/melody_types.h"
#include "core/section_types.h"
#include "core/types.h"
#include "core/velocity.h"  // for VocalPhysicsParams

namespace midisketch {

// Forward declarations
class MidiTrack;
struct NoteEvent;

/// @brief Position within a 4-bar phrase for timing adjustments.
enum class PhrasePosition;

/// @brief Calculator for micro-timing offsets to create "pocket" feel.
///
/// Provides per-instrument timing adjustments:
/// - Drums: beat-position-aware offsets (kick tight, snare layback, hi-hat push)
/// - Bass: consistent layback (-4 ticks base)
/// - Vocal: phrase-position-aware with human body model
///
/// All offsets are scaled by drive_feel (0-100):
/// - 0: 0.5x offsets (laid-back feel)
/// - 50: 1.0x offsets (neutral)
/// - 100: 1.5x offsets (driving feel)
class TimingOffsetCalculator {
 public:
  // GM drum note numbers
  static constexpr uint8_t kBassNote = 36;
  static constexpr uint8_t kSnareNote = 38;
  static constexpr uint8_t kHiHatClosed = 42;
  static constexpr uint8_t kHiHatOpen = 46;
  static constexpr uint8_t kHiHatFoot = 44;
  static constexpr int kBassBaseOffset = -4;

  /// @brief Construct with drive feel and vocal style.
  /// @param drive_feel Drive intensity (0-100)
  /// @param vocal_style Vocal style for physics parameters
  TimingOffsetCalculator(uint8_t drive_feel, VocalStylePreset vocal_style);

  // ============================================================================
  // Drum Timing
  // ============================================================================

  /// @brief Calculate timing offset for a drum note.
  ///
  /// Beat-position-aware timing:
  /// - Kick: -5~+3, tighter on downbeats, slightly ahead on offbeats
  /// - Snare: -8~0, maximum layback on beat 4 for anticipation
  /// - Hi-hat: +8~+15, stronger push on offbeats for drive
  ///
  /// @param note_number GM drum note number
  /// @param tick Note start tick
  /// @return Timing offset in ticks (negative = behind, positive = ahead)
  int getDrumTimingOffset(uint8_t note_number, Tick tick) const;

  /// @brief Apply timing offsets to all notes in a drum track.
  /// @param drum_track Drum track to modify (in-place)
  void applyDrumOffsets(MidiTrack& drum_track) const;

  // ============================================================================
  // Bass Timing
  // ============================================================================

  /// @brief Get bass timing offset (constant layback).
  /// @return Timing offset in ticks (always negative for layback)
  int getBassTimingOffset() const;

  /// @brief Apply timing offset to all notes in a bass track.
  /// @param bass_track Bass track to modify (in-place)
  void applyBassOffset(MidiTrack& bass_track) const;

  // ============================================================================
  // Vocal Timing (Human Body Model)
  // ============================================================================

  /// @brief Calculate timing offset for a vocal note.
  ///
  /// Combines multiple timing factors:
  /// - Phrase position: push ahead at phrase start, lay back at end
  /// - High pitch delay: notes above tessitura need preparation time
  /// - Leap landing delay: large intervals require stabilization
  /// - Post-breath delay: notes after breath gaps start late
  ///
  /// @param note The note to calculate offset for
  /// @param note_idx Index in the vocal notes vector
  /// @param vocal_notes Full vector of vocal notes (for context)
  /// @param sections Song sections for phrase position lookup
  /// @param tessitura_center Center of the vocal range
  /// @return Timing offset in ticks
  int getVocalTimingOffset(const NoteEvent& note, size_t note_idx,
                           const std::vector<NoteEvent>& vocal_notes,
                           const std::vector<Section>& sections,
                           uint8_t tessitura_center) const;

  /// @brief Apply timing offsets to all notes in a vocal track.
  ///
  /// Uses two-pass approach:
  /// 1. Calculate all offsets using original positions
  /// 2. Apply offsets to notes
  ///
  /// This ensures breath gap detection uses unmodified timing.
  ///
  /// @param vocal_track Vocal track to modify (in-place)
  /// @param sections Song sections for phrase position lookup
  void applyVocalOffsets(MidiTrack& vocal_track,
                         const std::vector<Section>& sections) const;

  // ============================================================================
  // Utility
  // ============================================================================

  /// @brief Get timing multiplier from drive feel.
  /// @return Multiplier (0.5 to 1.5)
  float getTimingMultiplier() const { return timing_mult_; }

  /// @brief Apply uniform offset to all notes in a track.
  /// @param track Track to modify (in-place)
  /// @param offset Offset in ticks
  static void applyUniformOffset(MidiTrack& track, int offset);

 private:
  float timing_mult_;                   ///< Timing multiplier from drive feel
  VocalPhysicsParams physics_;          ///< Vocal physics parameters

  /// @brief Get phrase position for a tick within sections.
  static PhrasePosition getPhrasePosition(Tick tick, const std::vector<Section>& sections);

  /// @brief Get base vocal timing offset for phrase position.
  static int getBaseVocalTimingOffset(PhrasePosition pos, float timing_mult);

  /// @brief Calculate tessitura center from vocal notes.
  static uint8_t calculateTessituraCenter(const std::vector<NoteEvent>& notes);

  /// @brief Check if a note is after a breath gap.
  bool isPostBreath(size_t note_idx, const std::vector<NoteEvent>& vocal_notes) const;
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_TIMING_OFFSET_CALCULATOR_H
