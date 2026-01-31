/**
 * @file i_track_base.cpp
 * @brief Implementation of ITrackBase virtual destructor.
 *
 * Provides the out-of-line virtual destructor definition to anchor the vtable.
 */

#include "core/i_track_base.h"

namespace midisketch {

ITrackBase::~ITrackBase() = default;

}  // namespace midisketch
