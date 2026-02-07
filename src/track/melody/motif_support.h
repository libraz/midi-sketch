/**
 * @file motif_support.h
 * @brief GlobalMotif extraction and evaluation for melodic coherence.
 */

#ifndef MIDISKETCH_TRACK_MELODY_MOTIF_SUPPORT_H
#define MIDISKETCH_TRACK_MELODY_MOTIF_SUPPORT_H

#include <vector>

#include "core/motif.h"
#include "core/types.h"

namespace midisketch {
namespace melody {

/// @brief Extract GlobalMotif from notes.
///
/// Analyzes the note sequence to extract:
/// - Interval signature (relative pitch changes)
/// - Rhythm signature (relative durations)
/// - Contour type (ascending, descending, peak, valley, plateau)
///
/// @param notes Notes to analyze
/// @return Extracted GlobalMotif structure
GlobalMotif extractGlobalMotif(const std::vector<NoteEvent>& notes);

/// @brief Evaluate candidate similarity to GlobalMotif.
///
/// Returns a bonus score for candidates that share similar
/// contour or interval patterns with the global motif.
///
/// Scoring components:
/// - Contour similarity (0.0-0.10)
/// - Interval pattern similarity (0.0-0.05)
/// - Direction matching (0.0-0.05)
/// - Interval consistency (0.0-0.05)
///
/// @param candidate Candidate melody notes
/// @param global_motif Reference motif from chorus
/// @return Bonus score (0.0-0.25)
float evaluateWithGlobalMotif(const std::vector<NoteEvent>& candidate,
                                  const GlobalMotif& global_motif);

}  // namespace melody
}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_MELODY_MOTIF_SUPPORT_H
