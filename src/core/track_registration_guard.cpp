/**
 * @file track_registration_guard.cpp
 * @brief Implementation of TrackRegistrationGuard.
 */

#include "core/track_registration_guard.h"
#include "core/i_harmony_context.h"
#include "core/midi_track.h"

namespace midisketch {

TrackRegistrationGuard::TrackRegistrationGuard(IHarmonyContext& harmony,
                                               const MidiTrack& track,
                                               TrackRole role)
    : harmony_(&harmony), track_(&track), role_(role), active_(true) {}

TrackRegistrationGuard::~TrackRegistrationGuard() {
  if (active_ && harmony_ && track_) {
    harmony_->registerTrack(*track_, role_);
  }
}

TrackRegistrationGuard::TrackRegistrationGuard(TrackRegistrationGuard&& other) noexcept
    : harmony_(other.harmony_),
      track_(other.track_),
      role_(other.role_),
      active_(other.active_) {
  other.active_ = false;  // Prevent double registration
}

TrackRegistrationGuard& TrackRegistrationGuard::operator=(TrackRegistrationGuard&& other) noexcept {
  if (this != &other) {
    // Register current track if active
    if (active_ && harmony_ && track_) {
      harmony_->registerTrack(*track_, role_);
    }
    harmony_ = other.harmony_;
    track_ = other.track_;
    role_ = other.role_;
    active_ = other.active_;
    other.active_ = false;
  }
  return *this;
}

void TrackRegistrationGuard::cancel() {
  active_ = false;
}

void TrackRegistrationGuard::registerNow() {
  if (active_ && harmony_ && track_) {
    harmony_->registerTrack(*track_, role_);
    active_ = false;  // Prevent double registration in destructor
  }
}

}  // namespace midisketch
