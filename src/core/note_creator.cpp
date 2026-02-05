/**
 * @file note_creator.cpp
 * @brief Implementation of unified note creation API.
 */

#include "core/note_creator.h"

#include <algorithm>
#include <cmath>

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

// Rank candidates based on preference and monotony avoidance
void rankCandidates(std::vector<PitchCandidate>& candidates, PitchPreference preference,
                    bool consider_boundary = false,
                    uint8_t prev_pitch = 0, int consecutive_same_count = 0) {
  // Monotony threshold: if 3+ consecutive same pitches, strongly penalize repeating
  constexpr int kMonotonyThreshold = 3;
  bool avoid_same_as_prev = (prev_pitch > 0 && consecutive_same_count >= kMonotonyThreshold);

  std::stable_sort(candidates.begin(), candidates.end(),
    [preference, consider_boundary, prev_pitch, avoid_same_as_prev](
        const PitchCandidate& a, const PitchCandidate& b) {
      // Pre-primary: avoid consecutive same pitch when monotony threshold exceeded
      if (avoid_same_as_prev) {
        bool a_same = (a.pitch == prev_pitch);
        bool b_same = (b.pitch == prev_pitch);
        if (a_same != b_same) {
          return !a_same;  // Prefer the one that's different
        }
      }

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

  // Check if desired pitch is within range AND safe (with effective duration)
  bool in_range = (opts.desired_pitch >= opts.range_low && opts.desired_pitch <= opts.range_high);
  bool is_safe = in_range &&
      harmony.isConsonantWithOtherTracks(opts.desired_pitch, opts.start, effective_duration, opts.role);

  // Chord: try shortening duration before changing pitch.
  // Preserves correct voicing even when Motif enters mid-sustain.
  if (opts.role == TrackRole::Chord && in_range && !is_safe) {
    Tick safe_end = harmony.getMaxSafeEnd(
        opts.start, opts.desired_pitch, opts.role,
        opts.start + effective_duration);
    Tick safe_dur = safe_end - opts.start;
    constexpr Tick kMinChordDuration = 480;  // Quarter note minimum
    if (safe_dur >= kMinChordDuration && safe_dur < effective_duration) {
      effective_duration = safe_dur;
      is_safe = harmony.isConsonantWithOtherTracks(
          opts.desired_pitch, opts.start, effective_duration, opts.role);
    }
  }

  if (is_safe) {
    // PreserveContour: even if desired pitch is safe, check for severe monotony
    // and try to find an alternative pitch to break the repetition.
    if (opts.preference == PitchPreference::PreserveContour &&
        opts.prev_pitch > 0 && opts.consecutive_same_count >= 4 &&
        opts.desired_pitch == opts.prev_pitch) {
      // Desired pitch would continue monotony - try to find alternative
      is_safe = false;  // Force candidate generation
    }
    // PreserveContour: also check if desired pitch would cause a large leap from prev
    if (opts.preference == PitchPreference::PreserveContour && opts.prev_pitch > 0) {
      int leap = std::abs(static_cast<int>(opts.desired_pitch) - static_cast<int>(opts.prev_pitch));
      if (leap > 12) {
        // Large leap - try to find alternative
        is_safe = false;  // Force candidate generation
      }
    }
  }

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
    // Re-rank with boundary awareness and monotony avoidance
    rankCandidates(candidates, opts.preference, true,
                   opts.prev_pitch, opts.consecutive_same_count);
  } else if (!candidates.empty() && opts.prev_pitch > 0 && opts.consecutive_same_count >= 3) {
    // Re-rank with monotony avoidance only (no boundary consideration)
    rankCandidates(candidates, opts.preference, false,
                   opts.prev_pitch, opts.consecutive_same_count);
  }

  // PreserveContour: filter out candidates that would cause large leaps from prev_pitch
  // This prevents collision avoidance from creating jarring melodic discontinuities.
  constexpr int kMaxLeapFromPrev = 12;  // 1 octave maximum
  if (opts.preference == PitchPreference::PreserveContour &&
      opts.prev_pitch > 0 && !candidates.empty()) {
    std::vector<PitchCandidate> leap_safe_candidates;
    for (const auto& c : candidates) {
      int leap_from_prev = std::abs(static_cast<int>(c.pitch) - static_cast<int>(opts.prev_pitch));
      if (leap_from_prev <= kMaxLeapFromPrev) {
        leap_safe_candidates.push_back(c);
      }
    }
    if (!leap_safe_candidates.empty()) {
      candidates = std::move(leap_safe_candidates);
    }
    // If no candidates within leap limit, fall through to use original candidates
    // (some leap is better than no note in most cases)
  }

  // PreserveContour: if monotony is severe (4+ consecutive), filter out prev_pitch entirely
  // and skip the note if no alternatives exist. This prevents long runs of repeated pitches.
  // Note: prev_pitch is the actual output pitch from the previous note, not the desired pitch.
  if (opts.preference == PitchPreference::PreserveContour &&
      opts.prev_pitch > 0 && opts.consecutive_same_count >= 4 && !candidates.empty()) {
    std::vector<PitchCandidate> different_pitch_candidates;
    for (const auto& c : candidates) {
      if (c.pitch != opts.prev_pitch) {
        different_pitch_candidates.push_back(c);
      }
    }
    if (!different_pitch_candidates.empty()) {
      candidates = std::move(different_pitch_candidates);
    } else {
      // All candidates resolve to prev_pitch - skip this note to break monotony
      result.strategy_used = CollisionAvoidStrategy::Failed;
      return result;
    }
  }

  if (candidates.empty()) {
    // No safe pitch found - try octave-down if desired exceeds range_high,
    // then clamp to range_high as last resort.
    uint8_t fallback_pitch = opts.desired_pitch;
    if (fallback_pitch > opts.range_high) {
      int octave_down = static_cast<int>(fallback_pitch) - 12;
      if (octave_down >= static_cast<int>(opts.range_low) &&
          octave_down <= static_cast<int>(opts.range_high)) {
        fallback_pitch = static_cast<uint8_t>(octave_down);
      } else {
        fallback_pitch = opts.range_high;
      }
    }

    // Final safety check: if fallback still causes dissonance, skip the note
    // This prevents major 7th and other harsh intervals from slipping through
    if (!harmony.isConsonantWithOtherTracks(fallback_pitch, opts.start, effective_duration, opts.role)) {
      // Try one more time with octave adjustments
      bool found_safe = false;
      for (int oct_offset : {-1, 1, -2, 2}) {
        int adjusted = static_cast<int>(fallback_pitch) + oct_offset * 12;
        if (adjusted >= static_cast<int>(opts.range_low) &&
            adjusted <= static_cast<int>(opts.range_high) &&
            harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(adjusted), opts.start,
                                                effective_duration, opts.role)) {
          fallback_pitch = static_cast<uint8_t>(adjusted);
          found_safe = true;
          break;
        }
      }
      if (!found_safe) {
        // Still dissonant after all attempts - skip note to avoid clash
        result.strategy_used = CollisionAvoidStrategy::Failed;
        return result;
      }
    }

    // PreserveContour: skip if fallback would cause severe monotony
    if (opts.preference == PitchPreference::PreserveContour &&
        opts.prev_pitch > 0 && opts.consecutive_same_count >= 4 &&
        fallback_pitch == opts.prev_pitch) {
      result.strategy_used = CollisionAvoidStrategy::Failed;
      return result;
    }

    NoteEvent event = buildNoteEvent(harmony, opts.start, effective_duration,
                                      fallback_pitch, opts.velocity,
                                      opts.source, opts.record_provenance, true_original);

#ifdef MIDISKETCH_NOTE_PROVENANCE
    if (opts.record_provenance && true_original != fallback_pitch) {
      event.addTransformStep(TransformStepType::MotionAdjust, true_original,
                             fallback_pitch, 0, 0);
    }
    event.addTransformStep(TransformStepType::CollisionAvoid, opts.desired_pitch,
                           fallback_pitch, 0, 0);
    if (result.was_chord_clipped && opts.record_provenance) {
      event.addTransformStep(TransformStepType::ChordBoundaryClip,
                             static_cast<uint8_t>(opts.duration > 255 ? 255 : opts.duration),
                             static_cast<uint8_t>(effective_duration > 255 ? 255 : effective_duration),
                             boundary_info.next_degree, 0);
    }
#endif

    result.note = event;
    result.final_pitch = fallback_pitch;
    result.strategy_used = CollisionAvoidStrategy::ExhaustiveSearch;
    result.was_adjusted = (fallback_pitch != true_original);

    if (opts.register_to_harmony) {
      harmony.registerNote(opts.start, effective_duration, fallback_pitch, opts.role);
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
    if (!harmony.isConsonantWithOtherTracks(pitch, start, duration, role)) return;

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

    // Annotate cross-boundary safety for notes with meaningful duration
    if (duration >= 240) {  // TICK_EIGHTH
      auto binfo = harmony.analyzeChordBoundary(pitch, start, duration);
      candidate.cross_boundary_safety = binfo.safety;
      candidate.is_safe_across_boundary =
          (binfo.safety == CrossBoundarySafety::NoBoundary ||
           binfo.safety == CrossBoundarySafety::ChordTone ||
           binfo.safety == CrossBoundarySafety::Tension);
    }

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
  if (harmony.isConsonantWithOtherTracks(desired_pitch, start, duration, role)) {
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
      // Prefer pitches closer to desired
      // Vocal: allow up to 2 octaves for melodic flexibility (RhythmSync compatibility)
      // Other tracks: 1 octave limit for tighter voicing
      int dist = std::abs(static_cast<int>(sounding_pitch) - static_cast<int>(desired_pitch));
      int max_dist = (role == TrackRole::Vocal) ? 24 : 12;
      if (dist > max_dist) continue;
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

  // Strategy 5: Vocal diversity fallback
  // In RhythmSync, Vocal often gets stuck on same pitch because Motif occupies
  // nearby pitches. Add octave-separated chord tones without strict consonance check.
  // This ensures selectBestCandidate has alternatives to penalize same-pitch streaks.
  if (role == TrackRole::Vocal && candidates.size() <= 2) {
    // Check if all current candidates are the same pitch
    bool all_same_pitch = true;
    if (!candidates.empty()) {
      uint8_t first_pitch = candidates[0].pitch;
      for (const auto& c : candidates) {
        if (c.pitch != first_pitch) {
          all_same_pitch = false;
          break;
        }
      }
    }

    if (all_same_pitch || candidates.size() <= 1) {
      // Add chord tones at octave distance from desired_pitch
      // These are more likely to be safe even in RhythmSync
      for (int ct : chord_tones) {
        for (int oct_offset : {-2, 2, -1, 1}) {  // Try further octaves first
          int oct = octave + oct_offset;
          if (oct < 0 || oct > 10) continue;
          uint8_t candidate_pitch = static_cast<uint8_t>(oct * 12 + ct);
          if (candidate_pitch < range_low || candidate_pitch > range_high) continue;

          // Check that this pitch is at least an octave away from any sounding note
          auto sounding = harmony.getSoundingPitches(start, start + duration, role);
          bool octave_safe = true;
          for (uint8_t sp : sounding) {
            int dist = std::abs(static_cast<int>(candidate_pitch) - static_cast<int>(sp));
            if (dist > 0 && dist < 10) {  // Too close (less than minor 7th)
              octave_safe = false;
              break;
            }
          }

          if (octave_safe) {
            // Add without strict consonance check, but mark as fallback
            PitchCandidate candidate;
            candidate.pitch = candidate_pitch;
            candidate.strategy = CollisionAvoidStrategy::ExhaustiveSearch;
            candidate.interval_from_desired =
                static_cast<int8_t>(candidate_pitch) - static_cast<int8_t>(desired_pitch);
            candidate.max_safe_duration = duration;  // Assume safe for now
            int pc = candidate_pitch % 12;
            candidate.is_chord_tone = true;  // We know it's a chord tone
            candidate.is_scale_tone = isScaleTone(pc);
            candidate.is_root_or_fifth = isRootOrFifth(pc, chord_tones);
            candidates.push_back(candidate);
          }
        }
      }
    }
  }

  // PreferRootFifth: filter to chord tones only (Bass must always play chord tones)
  // This prevents collision avoidance from selecting non-chord tones like E for V chord.
  if (preference == PitchPreference::PreferRootFifth && !candidates.empty()) {
    std::vector<PitchCandidate> chord_tone_candidates;
    for (const auto& c : candidates) {
      if (c.is_chord_tone) {
        chord_tone_candidates.push_back(c);
      }
    }
    // Only use filtered list if we have chord tone candidates
    if (!chord_tone_candidates.empty()) {
      candidates = std::move(chord_tone_candidates);
    }
    // If no chord tone candidates, fall through to use original candidates
    // (this is a fallback; ideally bass should skip the note)
  }

  // PreserveContour: filter out candidates with large leaps (>12 semitones)
  // This prevents Motif from making extreme jumps that break melodic contour.
  if (preference == PitchPreference::PreserveContour && !candidates.empty()) {
    constexpr int kMaxContourLeap = 12;  // 1 octave maximum
    std::vector<PitchCandidate> contour_safe_candidates;
    for (const auto& c : candidates) {
      if (std::abs(c.interval_from_desired) <= kMaxContourLeap) {
        contour_safe_candidates.push_back(c);
      }
    }
    // Only use filtered list if we have candidates within the leap limit
    if (!contour_safe_candidates.empty()) {
      candidates = std::move(contour_safe_candidates);
    }
    // If all candidates exceed the leap limit, the caller should skip the note
    // by checking if the returned candidate has a large interval
  }

  // Rank and trim (consider boundary if notes are long enough)
  bool has_boundary = (duration >= 240);
  rankCandidates(candidates, preference, has_boundary);
  if (candidates.size() > max_candidates) {
    candidates.resize(max_candidates);
  }

  return candidates;
}

// ============================================================================
// Musical candidate selection
// ============================================================================

// Section-type weight multipliers for 5-dimensional scoring.
// Rows: SectionType enum (Intro=0, A=1, B=2, Chorus=3, Bridge=4, Interlude=5, Outro=6, ...)
// Columns: Melodic, Harmonic, Contour, Tessitura, Intent
struct SectionWeights {
  float melodic;    // Dimension 1
  float harmonic;   // Dimension 2
  float contour;    // Dimension 3
  float tessitura;  // Dimension 4
  float intent;     // Dimension 5
};

static const SectionWeights kSectionWeightTable[] = {
  // Intro:      balanced
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  // A (Verse):  baseline reference
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  // B (Pre-chorus): non-chord tones permitted, contour emphasized, wider range
  {1.0f, 0.8f, 1.2f, 0.8f, 1.0f},
  // Chorus:     harmonic stability, relax intent constraint
  {1.0f, 1.2f, 1.0f, 1.0f, 0.8f},
  // Bridge:     exploratory - relax all constraints
  {0.8f, 0.7f, 0.8f, 0.5f, 1.0f},
  // Interlude:  balanced
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  // Outro:      stable, converge range
  {1.0f, 1.1f, 1.0f, 1.2f, 1.0f},
  // Chant:      balanced
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  // MixBreak:   balanced
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
  // Drop:       balanced
  {1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
};

static const SectionWeights& getSectionWeights(int8_t section_type_int) {
  if (section_type_int < 0 ||
      section_type_int >= static_cast<int8_t>(sizeof(kSectionWeightTable) / sizeof(kSectionWeightTable[0]))) {
    return kSectionWeightTable[1];  // Default: A (verse) baseline
  }
  return kSectionWeightTable[section_type_int];
}

uint8_t selectBestCandidate(const std::vector<PitchCandidate>& candidates,
                             uint8_t fallback_pitch,
                             const PitchSelectionHints& hints) {
  if (candidates.empty()) {
    return fallback_pitch;
  }

  // No melodic context: return first candidate (already ranked by rankCandidates)
  if (hints.prev_pitch < 0) {
    return candidates[0].pitch;
  }

  // Get section-specific weight multipliers
  const SectionWeights& sw = getSectionWeights(hints.section_type);

  // 5-dimensional musical scoring
  int best_index = 0;
  float best_score = -1e9f;

  // Determine rhythm-interval category from note_duration
  // Short: < half beat (240), Long: >= 1 beat (480), Medium: in between
  enum class DurationCat { Short, Medium, Long };
  DurationCat dur_cat = DurationCat::Medium;
  if (hints.note_duration > 0) {
    if (hints.note_duration < 240) {
      dur_cat = DurationCat::Short;
    } else if (hints.note_duration >= 480) {
      dur_cat = DurationCat::Long;
    }
  }

  for (size_t i = 0; i < candidates.size(); ++i) {
    const auto& c = candidates[i];
    float score = 0.0f;

    int interval = static_cast<int>(c.pitch) - hints.prev_pitch;
    int abs_interval = std::abs(interval);

    // === Dimension 1: Melodic continuity (max 35) ===
    // Rhythm-interval coupling: short notes prefer steps, long notes allow leaps.
    float melodic_score = 0.0f;
    switch (dur_cat) {
      case DurationCat::Short:
        if (abs_interval == 0) melodic_score = 33.0f;
        else if (abs_interval <= 2) melodic_score = 35.0f;
        else if (abs_interval <= 4) melodic_score = 20.0f;
        else if (abs_interval <= 7) melodic_score = 5.0f;
        else melodic_score = -1.5f * abs_interval;
        break;
      case DurationCat::Long:
        if (abs_interval == 0) melodic_score = 15.0f;
        else if (abs_interval <= 2) melodic_score = 25.0f;
        else if (abs_interval <= 4) melodic_score = 30.0f;
        else if (abs_interval <= 7) melodic_score = 25.0f;
        else if (abs_interval <= 12) melodic_score = 15.0f;
        else melodic_score = -1.0f * abs_interval;
        break;
      case DurationCat::Medium:
        if (abs_interval == 0) melodic_score = 25.0f;
        else if (abs_interval <= 2) melodic_score = 30.0f;
        else if (abs_interval <= 4) melodic_score = 25.0f;
        else if (abs_interval <= 7) melodic_score = 15.0f;
        else melodic_score = -1.0f * abs_interval;
        break;
    }

    // === Consecutive same pitch penalty ===
    // Music theory: 1-2 consecutive same notes = OK (rhythmic figure)
    //               3 consecutive = moderate penalty
    //               4+ consecutive = strong penalty (monotonous, must avoid)
    if (abs_interval == 0 && hints.same_pitch_streak > 0) {
      if (hints.same_pitch_streak >= 3) {
        melodic_score -= 60.0f;  // 4th+ note: very strong penalty (force movement)
      } else if (hints.same_pitch_streak >= 2) {
        melodic_score -= 40.0f;  // 3rd note: strong penalty
      } else if (hints.same_pitch_streak >= 1) {
        melodic_score -= 15.0f;  // 2nd note: moderate penalty
      }
    }

    score += melodic_score * sw.melodic;

    // === Dimension 2: Harmonic stability (max 25) ===
    {
      float harmonic_score = 0.0f;
      if (c.is_chord_tone) {
        harmonic_score += 20.0f;
        if (c.is_root_or_fifth) harmonic_score += 5.0f;
      } else if (c.is_scale_tone) {
        harmonic_score += 10.0f;
      }
      score += harmonic_score * sw.harmonic;
    }

    // === Phrase position anchoring (max 8) ===
    // Pop music principle: phrase starts anchor on root/5th, phrase ends resolve.
    if (hints.phrase_position >= 0.0f) {
      if (hints.phrase_position < 0.15f && c.is_root_or_fifth) {
        score += 5.0f;  // Phrase start: anchor on root/5th
      }
      if (hints.phrase_position > 0.85f) {
        if (c.is_root_or_fifth) score += 8.0f;  // Phrase end: resolve to root/5th
        else if (c.is_chord_tone) score += 3.0f;  // Phrase end: chord tone OK
      }

      // Sub-phrase mid-point anchoring (development → climax boundary)
      if (hints.sub_phrase_index >= 0) {
        // Sub-phrase 1 (development): breathing point at 0.45-0.55
        if (hints.sub_phrase_index == 1 &&
            hints.phrase_position >= 0.45f && hints.phrase_position <= 0.55f) {
          if (c.is_chord_tone) score += 3.0f;  // Mid-phrase chord tone anchor
        }
      }
    }

    // === Dimension 3: Contour preservation (max 20) ===
    {
      float contour_score = 0.0f;
      if (hints.contour_direction != 0) {
        bool moving_in_preferred_direction =
            (hints.contour_direction > 0 && interval > 0) ||
            (hints.contour_direction < 0 && interval < 0);
        if (moving_in_preferred_direction) {
          contour_score = 20.0f;
        } else if (interval != 0) {
          contour_score = -10.0f;
        }
      }
      score += contour_score * sw.contour;
    }

    // === Dimension 4: Tessitura gravity (max 10) ===
    {
      int dist_from_center = std::abs(static_cast<int>(c.pitch) - hints.tessitura_center);
      float gravity = 10.0f - std::min(static_cast<float>(dist_from_center), 10.0f);
      score += gravity * sw.tessitura;
    }

    // === Dimension 5: Intent proximity (max ~15) ===
    score += (-std::abs(c.interval_from_desired) * 3.0f) * sw.intent;

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
