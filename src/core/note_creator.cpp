/**
 * @file note_creator.cpp
 * @brief Implementation of unified note creation API.
 */

#include "core/note_creator.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/i_harmony_context.h"
#include "core/midi_track.h"

namespace midisketch {

namespace {

// Helper to check if a pitch class is a scale tone (C major scale)
bool isScaleTone(int pitch_class) {
  // C major scale: C, D, E, F, G, A, B (0, 2, 4, 5, 7, 9, 11)
  static const bool kScaleTones[12] = {
    true,   // C (0)
    false,  // C# (1)
    true,   // D (2)
    false,  // D# (3)
    true,   // E (4)
    true,   // F (5)
    false,  // F# (6)
    true,   // G (7)
    false,  // G# (8)
    true,   // A (9)
    false,  // A# (10)
    true    // B (11)
  };
  return kScaleTones[pitch_class % 12];
}

// Helper to check if a pitch class is root or 5th of the chord
bool isRootOrFifth(int pitch_class, const std::vector<int>& chord_tones) {
  if (chord_tones.empty()) return false;
  // Root is typically first chord tone, 5th is often third
  int root = chord_tones[0];
  int fifth = (root + 7) % 12;  // Perfect 5th
  return pitch_class == root || pitch_class == fifth;
}

// Create NoteEvent with provenance
NoteEvent buildNoteEvent(const IHarmonyContext& harmony, Tick start, Tick duration,
                          uint8_t pitch, uint8_t velocity, NoteSource source,
                          bool record_provenance, uint8_t original_pitch = 0) {
  NoteEvent event = NoteEventBuilder::create(start, duration, pitch, velocity);

#ifdef MIDISKETCH_NOTE_PROVENANCE
  if (record_provenance) {
    event.prov_chord_degree = harmony.getChordDegreeAt(start);
    event.prov_lookup_tick = start;
    event.prov_source = static_cast<uint8_t>(source);
    // Use original_pitch if provided, otherwise use final pitch
    event.prov_original_pitch = (original_pitch != 0) ? original_pitch : pitch;
  }
#else
  (void)harmony;
  (void)source;
  (void)record_provenance;
  (void)original_pitch;
#endif

  return event;
}

// Rank candidates based on preference
void rankCandidates(std::vector<PitchCandidate>& candidates, PitchPreference preference,
                    bool consider_boundary = false) {
  std::stable_sort(candidates.begin(), candidates.end(),
    [preference, consider_boundary](const PitchCandidate& a, const PitchCandidate& b) {
      // Primary: prefer pitches that didn't need resolution
      if (a.strategy != b.strategy) {
        if (a.strategy == CollisionAvoidStrategy::None) return true;
        if (b.strategy == CollisionAvoidStrategy::None) return false;
      }

      // Cross-boundary safety (when chord boundary policy is active)
      if (consider_boundary && a.is_safe_across_boundary != b.is_safe_across_boundary) {
        return a.is_safe_across_boundary;
      }

      // Secondary: preference-specific ranking
      switch (preference) {
        case PitchPreference::PreferRootFifth:
          // Prefer root/5th over other chord tones
          if (a.is_root_or_fifth != b.is_root_or_fifth) {
            return a.is_root_or_fifth;
          }
          break;

        case PitchPreference::PreferChordTones:
          // Prefer chord tones
          if (a.is_chord_tone != b.is_chord_tone) {
            return a.is_chord_tone;
          }
          break;

        case PitchPreference::PreserveContour:
          // Prefer smaller intervals to preserve melodic shape
          break;

        default:
          break;
      }

      // Tertiary: prefer smaller interval from desired
      return std::abs(a.interval_from_desired) < std::abs(b.interval_from_desired);
    });
}

// Overlap threshold below which crossing a boundary is treated as a passing tone
constexpr Tick kPassingToneThreshold = 240;  // 8th note

}  // namespace

// ============================================================================
// Main API
// ============================================================================

std::optional<NoteEvent> createNote(IHarmonyContext& harmony, const NoteOptions& opts) {
  return createNoteWithResult(harmony, opts).note;
}

std::optional<NoteEvent> createNoteAndAdd(MidiTrack& track, IHarmonyContext& harmony,
                                           const NoteOptions& opts) {
  auto result = createNoteWithResult(harmony, opts);
  if (result.note) {
    track.addNote(*result.note);
  }
  return result.note;
}

CreateNoteResult createNoteWithResult(IHarmonyContext& harmony, const NoteOptions& opts) {
  CreateNoteResult result;
  result.original_duration = opts.duration;

  // Determine the true original pitch (pre-adjustment)
  uint8_t true_original = (opts.original_pitch != 0) ? opts.original_pitch : opts.desired_pitch;

  // Working duration (may be shortened by chord boundary processing)
  Tick effective_duration = opts.duration;

  // Chord boundary processing (before collision check)
  ChordBoundaryInfo boundary_info;
  bool boundary_active = (opts.chord_boundary != ChordBoundaryPolicy::None);

  if (boundary_active) {
    boundary_info = harmony.analyzeChordBoundary(opts.desired_pitch, opts.start, opts.duration);

    // Only process if there's a boundary crossing with significant overlap
    if (boundary_info.boundary_tick > 0 && boundary_info.overlap_ticks >= kPassingToneThreshold) {
      switch (opts.chord_boundary) {
        case ChordBoundaryPolicy::ClipAtBoundary:
          // Always clip at boundary
          effective_duration = boundary_info.safe_duration;
          result.was_chord_clipped = true;
          break;

        case ChordBoundaryPolicy::ClipIfUnsafe:
          // Clip only for NonChordTone or AvoidNote
          if (boundary_info.safety == CrossBoundarySafety::NonChordTone ||
              boundary_info.safety == CrossBoundarySafety::AvoidNote) {
            effective_duration = boundary_info.safe_duration;
            result.was_chord_clipped = true;
          }
          break;

        case ChordBoundaryPolicy::PreferSafe:
          // Don't clip yet - will be used in candidate ranking
          // Only clip as fallback after candidate selection
          break;

        case ChordBoundaryPolicy::None:
          break;
      }
    }
  }

  // NoCollisionCheck: skip safety check, just create the note
  if (opts.preference == PitchPreference::NoCollisionCheck) {
    NoteEvent event = buildNoteEvent(harmony, opts.start, effective_duration,
                                      opts.desired_pitch, opts.velocity,
                                      opts.source, opts.record_provenance, true_original);

#ifdef MIDISKETCH_NOTE_PROVENANCE
    if (opts.record_provenance && true_original != opts.desired_pitch) {
      event.addTransformStep(TransformStepType::MotionAdjust, true_original,
                             opts.desired_pitch, 0, 0);
    }
    if (result.was_chord_clipped && opts.record_provenance) {
      event.addTransformStep(TransformStepType::ChordBoundaryClip,
                             static_cast<uint8_t>(opts.duration > 255 ? 255 : opts.duration),
                             static_cast<uint8_t>(effective_duration > 255 ? 255 : effective_duration),
                             boundary_info.next_degree, 0);
    }
#endif

    result.note = event;
    result.final_pitch = opts.desired_pitch;
    result.strategy_used = CollisionAvoidStrategy::None;
    result.was_adjusted = (true_original != opts.desired_pitch);

    if (opts.register_to_harmony) {
      harmony.registerNote(opts.start, effective_duration, opts.desired_pitch, opts.role);
      result.was_registered = true;
    }
    return result;
  }

  // Check if desired pitch is safe (with effective duration)
  bool is_safe = harmony.isPitchSafe(opts.desired_pitch, opts.start, effective_duration, opts.role);
  if (is_safe) {
    // For PreferSafe: check if this pitch needs boundary clip
    if (opts.chord_boundary == ChordBoundaryPolicy::PreferSafe &&
        boundary_info.boundary_tick > 0 && boundary_info.overlap_ticks >= kPassingToneThreshold &&
        (boundary_info.safety == CrossBoundarySafety::NonChordTone ||
         boundary_info.safety == CrossBoundarySafety::AvoidNote)) {
      // Pitch is collision-safe but not boundary-safe: clip as fallback
      effective_duration = boundary_info.safe_duration;
      result.was_chord_clipped = true;
    }

    NoteEvent event = buildNoteEvent(harmony, opts.start, effective_duration,
                                      opts.desired_pitch, opts.velocity,
                                      opts.source, opts.record_provenance, true_original);

#ifdef MIDISKETCH_NOTE_PROVENANCE
    if (opts.record_provenance && true_original != opts.desired_pitch) {
      event.addTransformStep(TransformStepType::MotionAdjust, true_original,
                             opts.desired_pitch, 0, 0);
    }
    if (result.was_chord_clipped && opts.record_provenance) {
      event.addTransformStep(TransformStepType::ChordBoundaryClip,
                             static_cast<uint8_t>(opts.duration > 255 ? 255 : opts.duration),
                             static_cast<uint8_t>(effective_duration > 255 ? 255 : effective_duration),
                             boundary_info.next_degree, 0);
    }
#endif

    result.note = event;
    result.final_pitch = opts.desired_pitch;
    result.strategy_used = CollisionAvoidStrategy::None;
    result.was_adjusted = (true_original != opts.desired_pitch);

    if (opts.register_to_harmony) {
      harmony.registerNote(opts.start, effective_duration, opts.desired_pitch, opts.role);
      result.was_registered = true;
    }
    return result;
  }

  // SkipIfUnsafe: don't try to resolve, just skip
  if (opts.preference == PitchPreference::SkipIfUnsafe) {
    result.strategy_used = CollisionAvoidStrategy::Failed;
    return result;
  }

  // Get candidates and select the best one
  bool consider_boundary = (opts.chord_boundary == ChordBoundaryPolicy::PreferSafe &&
                            boundary_info.boundary_tick > 0 &&
                            boundary_info.overlap_ticks >= kPassingToneThreshold);

  auto candidates = getSafePitchCandidates(harmony, opts.desired_pitch, opts.start,
                                            effective_duration, opts.role, opts.range_low,
                                            opts.range_high, opts.preference, 5);

  // Annotate candidates with cross-boundary safety for PreferSafe
  if (consider_boundary && !candidates.empty()) {
    for (auto& c : candidates) {
      auto c_boundary = harmony.analyzeChordBoundary(c.pitch, opts.start, opts.duration);
      c.cross_boundary_safety = c_boundary.safety;
      c.is_safe_across_boundary = (c_boundary.safety == CrossBoundarySafety::NoBoundary ||
                                    c_boundary.safety == CrossBoundarySafety::ChordTone ||
                                    c_boundary.safety == CrossBoundarySafety::Tension);
    }
    // Re-rank with boundary awareness
    rankCandidates(candidates, opts.preference, true);
  }

  if (candidates.empty()) {
    // No safe pitch found - use original as last resort
    NoteEvent event = buildNoteEvent(harmony, opts.start, effective_duration,
                                      opts.desired_pitch, opts.velocity,
                                      opts.source, opts.record_provenance, true_original);

#ifdef MIDISKETCH_NOTE_PROVENANCE
    if (opts.record_provenance && true_original != opts.desired_pitch) {
      event.addTransformStep(TransformStepType::MotionAdjust, true_original,
                             opts.desired_pitch, 0, 0);
    }
    event.addTransformStep(TransformStepType::CollisionAvoid, opts.desired_pitch,
                           opts.desired_pitch, 0, 0);
    if (result.was_chord_clipped && opts.record_provenance) {
      event.addTransformStep(TransformStepType::ChordBoundaryClip,
                             static_cast<uint8_t>(opts.duration > 255 ? 255 : opts.duration),
                             static_cast<uint8_t>(effective_duration > 255 ? 255 : effective_duration),
                             boundary_info.next_degree, 0);
    }
#endif

    result.note = event;
    result.final_pitch = opts.desired_pitch;
    result.strategy_used = CollisionAvoidStrategy::Failed;
    result.was_adjusted = (true_original != opts.desired_pitch);

    if (opts.register_to_harmony) {
      harmony.registerNote(opts.start, effective_duration, opts.desired_pitch, opts.role);
      result.was_registered = true;
    }
    return result;
  }

  // Use best candidate
  const PitchCandidate& best = candidates[0];

  // For PreferSafe: if best candidate is boundary-safe, use original duration
  Tick final_duration = effective_duration;
  if (opts.chord_boundary == ChordBoundaryPolicy::PreferSafe && best.is_safe_across_boundary) {
    final_duration = opts.duration;
    result.was_chord_clipped = false;  // Safe pitch found, no clip needed
  } else if (opts.chord_boundary == ChordBoundaryPolicy::PreferSafe && !best.is_safe_across_boundary) {
    // Fallback: clip duration for boundary-unsafe candidate
    final_duration = boundary_info.safe_duration;
    result.was_chord_clipped = true;
  }

  NoteEvent event = buildNoteEvent(harmony, opts.start, final_duration,
                                    best.pitch, opts.velocity,
                                    opts.source, opts.record_provenance, true_original);

#ifdef MIDISKETCH_NOTE_PROVENANCE
  if (opts.record_provenance) {
    if (true_original != opts.desired_pitch) {
      event.addTransformStep(TransformStepType::MotionAdjust, true_original,
                             opts.desired_pitch, 0, 0);
    }
    if (best.pitch != opts.desired_pitch) {
      event.addTransformStep(TransformStepType::CollisionAvoid, opts.desired_pitch,
                             best.pitch, static_cast<int8_t>(best.colliding_pitch), 0);
    }
    if (result.was_chord_clipped) {
      event.addTransformStep(TransformStepType::ChordBoundaryClip,
                             static_cast<uint8_t>(opts.duration > 255 ? 255 : opts.duration),
                             static_cast<uint8_t>(final_duration > 255 ? 255 : final_duration),
                             boundary_info.next_degree, 0);
    }
  }
#endif

  result.note = event;
  result.final_pitch = best.pitch;
  result.strategy_used = best.strategy;
  result.was_adjusted = (best.pitch != true_original);

  if (opts.register_to_harmony) {
    harmony.registerNote(opts.start, final_duration, best.pitch, opts.role);
    result.was_registered = true;
  }

  return result;
}

// ============================================================================
// Drums/SE API
// ============================================================================

NoteEvent createNoteWithoutHarmony(Tick start, Tick duration, uint8_t pitch, uint8_t velocity) {
  return NoteEventBuilder::create(start, duration, pitch, velocity);
}

NoteEvent createNoteWithoutHarmonyAndAdd(MidiTrack& track, Tick start, Tick duration,
                                          uint8_t pitch, uint8_t velocity) {
  NoteEvent event = createNoteWithoutHarmony(start, duration, pitch, velocity);
  track.addNote(event);
  return event;
}

// ============================================================================
// Candidate-based API
// ============================================================================

std::vector<PitchCandidate> getSafePitchCandidates(
    const IHarmonyContext& harmony,
    uint8_t desired_pitch,
    Tick start,
    Tick duration,
    TrackRole role,
    uint8_t range_low,
    uint8_t range_high,
    PitchPreference preference,
    size_t max_candidates) {

  std::vector<PitchCandidate> candidates;
  candidates.reserve(max_candidates * 2);  // May generate more, then trim

  auto chord_tones = harmony.getChordTonesAt(start);

  // Helper to add a candidate if safe
  auto tryAddCandidate = [&](uint8_t pitch, CollisionAvoidStrategy strategy) {
    if (pitch < range_low || pitch > range_high) return;
    if (!harmony.isPitchSafe(pitch, start, duration, role)) return;

    PitchCandidate candidate;
    candidate.pitch = pitch;
    candidate.strategy = strategy;
    candidate.interval_from_desired = static_cast<int8_t>(pitch) - static_cast<int8_t>(desired_pitch);

    // Calculate max_safe_duration
    candidate.max_safe_duration = harmony.getMaxSafeEnd(start, pitch, role, start + duration) - start;

    // Musical attributes
    int pc = pitch % 12;
    candidate.is_chord_tone = std::find(chord_tones.begin(), chord_tones.end(), pc) != chord_tones.end();
    candidate.is_scale_tone = isScaleTone(pc);
    candidate.is_root_or_fifth = isRootOrFifth(pc, chord_tones);

    // Get collision info if there was a collision
    if (pitch != desired_pitch) {
      auto collision_info = harmony.getCollisionInfo(desired_pitch, start, duration, role);
      if (collision_info.has_collision) {
        candidate.colliding_track = collision_info.colliding_track;
        candidate.colliding_pitch = collision_info.colliding_pitch;
      }
    }

    candidates.push_back(candidate);
  };

  // Strategy 1: Try desired pitch first (if safe, it's the best)
  if (harmony.isPitchSafe(desired_pitch, start, duration, role)) {
    tryAddCandidate(desired_pitch, CollisionAvoidStrategy::None);
    if (candidates.size() >= max_candidates) {
      rankCandidates(candidates, preference);
      candidates.resize(max_candidates);
      return candidates;
    }
  }

  // Strategy 1.5: Try sounding pitches (doubling)
  // If another track is playing a pitch in range, we can safely double it.
  // This is especially important for Chord track when Motif has already placed notes.
  {
    auto sounding = harmony.getSoundingPitches(start, start + duration, role);
    for (uint8_t sounding_pitch : sounding) {
      if (sounding_pitch < range_low || sounding_pitch > range_high) continue;
      // Prefer pitches closer to desired (skip if more than an octave away)
      int dist = std::abs(static_cast<int>(sounding_pitch) - static_cast<int>(desired_pitch));
      if (dist > 12) continue;
      tryAddCandidate(sounding_pitch, CollisionAvoidStrategy::ActualSounding);
    }
  }

  // Strategy 2: Based on preference, try specific pitches
  int octave = desired_pitch / 12;

  switch (preference) {
    case PitchPreference::PreferRootFifth:
      // Try root and 5th in nearby octaves
      if (!chord_tones.empty()) {
        int root = chord_tones[0];
        int fifth = (root + 7) % 12;
        for (int oct_offset = -1; oct_offset <= 1; ++oct_offset) {
          int oct = octave + oct_offset;
          if (oct >= 0 && oct <= 10) {
            tryAddCandidate(static_cast<uint8_t>(oct * 12 + root), CollisionAvoidStrategy::ChordTones);
            tryAddCandidate(static_cast<uint8_t>(oct * 12 + fifth), CollisionAvoidStrategy::ChordTones);
          }
        }
      }
      break;

    case PitchPreference::PreferChordTones:
      // Try all chord tones in nearby octaves
      for (int ct : chord_tones) {
        for (int oct_offset = -1; oct_offset <= 1; ++oct_offset) {
          int oct = octave + oct_offset;
          if (oct >= 0 && oct <= 10) {
            tryAddCandidate(static_cast<uint8_t>(oct * 12 + ct), CollisionAvoidStrategy::ChordTones);
          }
        }
      }
      break;

    case PitchPreference::PreserveContour:
      // Try octave shifts first (preserves pitch class)
      for (int oct_offset : {-1, 1, -2, 2}) {
        int pitch = desired_pitch + oct_offset * 12;
        if (pitch >= static_cast<int>(range_low) && pitch <= static_cast<int>(range_high)) {
          tryAddCandidate(static_cast<uint8_t>(pitch), CollisionAvoidStrategy::ActualSounding);
        }
      }
      // Then try chord tones
      for (int ct : chord_tones) {
        for (int oct_offset = -1; oct_offset <= 1; ++oct_offset) {
          int oct = octave + oct_offset;
          if (oct >= 0 && oct <= 10) {
            tryAddCandidate(static_cast<uint8_t>(oct * 12 + ct), CollisionAvoidStrategy::ChordTones);
          }
        }
      }
      break;

    default:
      // Default strategy: chord tones first
      for (int ct : chord_tones) {
        for (int oct_offset = -1; oct_offset <= 1; ++oct_offset) {
          int oct = octave + oct_offset;
          if (oct >= 0 && oct <= 10) {
            tryAddCandidate(static_cast<uint8_t>(oct * 12 + ct), CollisionAvoidStrategy::ChordTones);
          }
        }
      }
      break;
  }

  // Strategy 3: Consonant interval adjustments
  static const int kConsonantIntervals[] = {3, -3, 4, -4, 5, -5, 7, -7, 12, -12, 2, -2, 1, -1};
  for (int adj : kConsonantIntervals) {
    int pitch = static_cast<int>(desired_pitch) + adj;
    if (pitch >= static_cast<int>(range_low) && pitch <= static_cast<int>(range_high)) {
      tryAddCandidate(static_cast<uint8_t>(pitch), CollisionAvoidStrategy::ConsonantInterval);
    }
    if (candidates.size() >= max_candidates * 2) break;  // Enough candidates
  }

  // Strategy 4: Exhaustive search (only if still need candidates)
  if (candidates.size() < max_candidates) {
    for (int dist = 1; dist <= 24 && candidates.size() < max_candidates * 2; ++dist) {
      for (int sign : {-1, 1}) {
        int pitch = static_cast<int>(desired_pitch) + sign * dist;
        if (pitch >= static_cast<int>(range_low) && pitch <= static_cast<int>(range_high)) {
          tryAddCandidate(static_cast<uint8_t>(pitch), CollisionAvoidStrategy::ExhaustiveSearch);
        }
      }
    }
  }

  // Rank and trim
  rankCandidates(candidates, preference);
  if (candidates.size() > max_candidates) {
    candidates.resize(max_candidates);
  }

  return candidates;
}

// ============================================================================
// Musical candidate selection
// ============================================================================

uint8_t selectBestCandidate(const std::vector<PitchCandidate>& candidates,
                             uint8_t fallback_pitch,
                             const PitchSelectionHints& hints) {
  if (candidates.empty()) {
    return fallback_pitch;
  }

  // If no context provided, return the first candidate (already ranked by getSafePitchCandidates)
  if (hints.prev_pitch < 0 && hints.contour_direction == 0 && !hints.prefer_chord_tones) {
    return candidates[0].pitch;
  }

  // Score each candidate based on hints
  int best_index = 0;
  int best_score = std::numeric_limits<int>::min();

  for (size_t i = 0; i < candidates.size(); ++i) {
    const auto& c = candidates[i];
    int score = 0;

    // Base score: strategy (prefer None = original pitch was safe)
    if (c.strategy == CollisionAvoidStrategy::None) {
      score += 100;
    } else if (c.strategy == CollisionAvoidStrategy::ActualSounding) {
      score += 80;  // Doubling is harmonically safe
    } else if (c.strategy == CollisionAvoidStrategy::ChordTones) {
      score += 60;
    } else if (c.strategy == CollisionAvoidStrategy::ConsonantInterval) {
      score += 40;
    }

    // Chord tone preference
    if (hints.prefer_chord_tones && c.is_chord_tone) {
      score += 50;
    }

    // Boundary safety preference
    if (hints.prefer_boundary_safe && c.is_safe_across_boundary) {
      score += 40;
    }

    // Interval from previous pitch
    if (hints.prev_pitch >= 0) {
      int interval = static_cast<int>(c.pitch) - hints.prev_pitch;
      int abs_interval = std::abs(interval);

      // Small interval preference
      if (hints.prefer_small_intervals) {
        // Penalize large intervals (stepwise motion is preferred)
        if (abs_interval <= 2) {
          score += 30;  // Stepwise motion
        } else if (abs_interval <= 4) {
          score += 20;  // Small skip
        } else if (abs_interval <= 7) {
          score += 10;  // Moderate leap
        } else {
          score -= abs_interval;  // Penalize large leaps
        }
      }

      // Contour direction
      if (hints.contour_direction != 0) {
        bool moving_in_preferred_direction =
            (hints.contour_direction > 0 && interval > 0) ||
            (hints.contour_direction < 0 && interval < 0);

        if (moving_in_preferred_direction) {
          score += 25;
        } else if (interval != 0) {
          score -= 15;  // Penalize wrong direction
        }
      }
    }

    // Prefer pitch closer to desired (interval_from_desired)
    score -= std::abs(c.interval_from_desired) * 2;

    if (score > best_score) {
      best_score = score;
      best_index = static_cast<int>(i);
    }
  }

  return candidates[static_cast<size_t>(best_index)].pitch;
}

// ============================================================================
// Boundary safety annotation
// ============================================================================

void annotateBoundarySafety(std::vector<PitchCandidate>& candidates,
                            const IHarmonyContext& harmony,
                            Tick start, Tick duration) {
  for (auto& c : candidates) {
    auto info = harmony.analyzeChordBoundary(c.pitch, start, duration);
    c.cross_boundary_safety = info.safety;
    c.is_safe_across_boundary =
        (info.safety == CrossBoundarySafety::NoBoundary ||
         info.safety == CrossBoundarySafety::ChordTone ||
         info.safety == CrossBoundarySafety::Tension);
  }
}

}  // namespace midisketch
