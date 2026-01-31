/**
 * @file track_base.h
 * @brief Base implementation for track generators.
 *
 * Provides common functionality for all track generators:
 * - Physical model constraint enforcement
 * - Safe note creation with collision checking
 * - Priority-based generation coordination
 */

#ifndef MIDISKETCH_CORE_TRACK_BASE_H
#define MIDISKETCH_CORE_TRACK_BASE_H

#include "core/i_track_base.h"
#include "core/note_factory.h"

namespace midisketch {

/// @brief Base implementation for track generators.
///
/// Provides common functionality that all track generators share:
/// - Physical model enforcement
/// - Safe note creation
/// - Priority coordination
///
/// Subclasses implement generateSection() with track-specific logic.
class TrackBase : public ITrackBase {
 public:
  ~TrackBase() override = default;

  // =========================================================================
  // ITrackBase interface
  // =========================================================================

  void configure(const TrackConfig& config) override {
    config_ = config;
  }

  // =========================================================================
  // Deferred registration API
  // =========================================================================

  /// @brief Enable or disable deferred registration mode.
  ///
  /// In deferred mode, notes created via createSafeNoteDeferred() are queued
  /// instead of being registered immediately. This allows post-processing
  /// (e.g., overlap removal) before final registration.
  ///
  /// @param deferred True to enable deferred mode, false to disable
  void setDeferredRegistration(bool deferred) { deferred_registration_ = deferred; }

  /// @brief Check if deferred registration mode is active.
  bool isDeferredRegistration() const { return deferred_registration_; }

  /// @brief Register all deferred notes with the harmony context.
  ///
  /// Call this after post-processing to register all queued notes.
  /// Clears the deferred notes queue after registration.
  ///
  /// @param ctx Track context with harmony coordinator
  void flushDeferredNotes(TrackContext& ctx);

  /// @brief Get the deferred notes queue (for post-processing).
  std::vector<NoteEvent>& getDeferredNotes() { return deferred_notes_; }

  /// @brief Get the deferred notes queue (const).
  const std::vector<NoteEvent>& getDeferredNotes() const { return deferred_notes_; }

  /// @brief Clear the deferred notes queue without registering.
  void clearDeferredNotes() { deferred_notes_.clear(); }

  SafeNoteOptions getSafeOptions(Tick start, Tick duration, uint8_t desired_pitch,
                                  const TrackContext& ctx) const override;

  std::optional<NoteEvent> createSafeNote(Tick start, Tick duration, uint8_t pitch,
                                           uint8_t velocity,
                                           const TrackContext& ctx) const override;

  /// @brief Default implementation: loop through sections and call generateSection().
  ///
  /// Override this for tracks that need section-spanning logic (phrases, pattern caching).
  void generateFullTrack(MidiTrack& track, const FullTrackContext& ctx) override;

 protected:
  TrackConfig config_;

  /// @brief Check if this track is the coordinate axis (no pitch adjustment).
  bool isCoordinateAxis(const TrackContext& ctx) const {
    if (!ctx.harmony) return false;
    return ctx.harmony->getTrackPriority(getRole()) == TrackPriority::Highest;
  }

  /// @brief Get the effective pitch range for this track.
  /// @param ctx Track context (unused, reserved for future expansion)
  /// @return Pair of (low, high) pitch bounds
  std::pair<uint8_t, uint8_t> getEffectivePitchRange(
      [[maybe_unused]] const TrackContext& ctx) const {
    PhysicalModel model = getPhysicalModel();
    uint8_t low = model.pitch_low;
    uint8_t high = model.pitch_high;

    // Apply vocal ceiling if applicable
    if (model.vocal_ceiling_offset != 0) {
      high = model.getEffectiveHigh(config_.vocal_high);
    }

    return {low, high};
  }

  /// @brief Create a note with provenance tracking.
  /// @param start Start tick
  /// @param duration Duration
  /// @param pitch Pitch
  /// @param velocity Velocity
  /// @param source Note source for provenance
  /// @param ctx Track context
  /// @return Created note event
  NoteEvent createNoteWithProvenance(Tick start, Tick duration, uint8_t pitch,
                                      uint8_t velocity, NoteSource source,
                                      const TrackContext& ctx) const;

  /// @brief Create and register a note with the harmony context.
  /// @param start Start tick
  /// @param duration Duration
  /// @param pitch Pitch
  /// @param velocity Velocity
  /// @param source Note source for provenance
  /// @param ctx Track context
  /// @return Created note event
  NoteEvent createAndRegister(Tick start, Tick duration, uint8_t pitch,
                               uint8_t velocity, NoteSource source,
                               TrackContext& ctx) const;

  /// @brief Create a safe note with deferred registration support.
  ///
  /// In deferred mode: queues the note for later registration.
  /// In normal mode: registers immediately with harmony context.
  ///
  /// @param start Start tick
  /// @param duration Duration
  /// @param pitch Desired pitch (may be adjusted for safety)
  /// @param velocity Velocity
  /// @param source Note source for provenance
  /// @param ctx Track context
  /// @return Created note if safe, nullopt otherwise
  std::optional<NoteEvent> createSafeNoteDeferred(Tick start, Tick duration,
                                                   uint8_t pitch, uint8_t velocity,
                                                   NoteSource source,
                                                   TrackContext& ctx);

 private:
  bool deferred_registration_ = false;
  std::vector<NoteEvent> deferred_notes_;
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_TRACK_BASE_H
