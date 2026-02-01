/**
 * @file fretted_note_factory.cpp
 * @brief Implementation of FrettedNoteFactory.
 */

#include "instrument/fretted/fretted_note_factory.h"

#include <algorithm>
#include <cmath>

#include "core/i_harmony_context.h"
#include "core/note_creator.h"
#include "core/timing_constants.h"

namespace midisketch {

FrettedNoteFactory::FrettedNoteFactory(const IHarmonyContext& harmony,
                                       const IFrettedInstrument& instrument)
    : FrettedNoteFactory(harmony, instrument, 120) {}

FrettedNoteFactory::FrettedNoteFactory(const IHarmonyContext& harmony,
                                       const IFrettedInstrument& instrument, uint16_t bpm)
    : harmony_(harmony),
      instrument_(instrument),
      state_(instrument.getStringCount()),
      max_playability_cost_(0.6f),
      bpm_(bpm) {
  state_.hand_position = 3;  // Start in a comfortable position (around fret 3)
}

std::optional<NoteEvent> FrettedNoteFactory::create(Tick start, Tick duration, uint8_t pitch,
                                                     uint8_t velocity,
                                                     PlayingTechnique technique,
                                                     NoteSource source) {
  // Check if pitch is playable at all
  if (!instrument_.isPitchPlayable(pitch)) {
    // Try to find a playable alternative
    pitch = ensurePlayable(pitch, start, duration);
    if (!instrument_.isPitchPlayable(pitch)) {
      return std::nullopt;
    }
  }

  // Check technique constraints
  if (!instrument_.supportsTechnique(technique)) {
    technique = PlayingTechnique::Normal;
  }

  // Find best fingering
  Fingering fingering = instrument_.findBestFingering(pitch, state_, technique);
  if (!fingering.isValid()) {
    return std::nullopt;
  }

  // Check playability cost
  if (last_fingering_.isValid()) {
    PlayabilityCost transition_cost =
        instrument_.calculateTransitionCost(last_fingering_, fingering, duration, bpm_);

    // Check if transition is even possible
    if (!instrument_.isTransitionPossible(last_fingering_, fingering, duration, bpm_)) {
      // Try to find alternative position
      auto positions = instrument_.getPositionsForPitch(pitch);
      bool found_alternative = false;

      for (const auto& pos : positions) {
        FretboardState test_state = state_;
        test_state.hand_position =
            pos.fret > 0 ? (pos.fret > 1 ? pos.fret - 1 : 1) : state_.hand_position;

        Fingering alt_fingering = instrument_.findBestFingering(pitch, test_state, technique);
        if (alt_fingering.isValid() &&
            instrument_.isTransitionPossible(last_fingering_, alt_fingering, duration, bpm_)) {
          fingering = alt_fingering;
          found_alternative = true;
          break;
        }
      }

      if (!found_alternative) {
        return std::nullopt;
      }
    }

    fingering.playability_cost += transition_cost.total();
  }

  // Check against threshold
  if (fingering.playability_cost > max_playability_cost_ * 100.0f) {
    // Cost too high, try alternative pitch
    uint8_t alt_pitch = findPlayablePitch(pitch, start, duration, max_playability_cost_);
    if (alt_pitch != pitch) {
      pitch = alt_pitch;
      fingering = instrument_.findBestFingering(pitch, state_, technique);
      if (!fingering.isValid()) {
        return std::nullopt;
      }
    }
  }

  // Create the note using createNoteWithoutHarmony (no collision check here,
  // FrettedNoteFactory handles its own safety via instrument constraints)
  auto note = createNoteWithoutHarmony(start, duration, pitch, velocity);
  note.prov_source = static_cast<uint8_t>(source);

  // Apply fingering information
  applyFingeringProvenance(note, fingering, technique);

  // Update state
  instrument_.updateState(state_, fingering, start, duration);
  last_fingering_ = fingering;

  return note;
}

std::optional<NoteEvent> FrettedNoteFactory::create(Tick start, Tick duration, uint8_t pitch,
                                                     uint8_t velocity, NoteSource source) {
  return create(start, duration, pitch, velocity, PlayingTechnique::Normal, source);
}

std::optional<NoteEvent> FrettedNoteFactory::createIfNoDissonance(Tick start, Tick duration,
                                                         uint8_t pitch, uint8_t velocity,
                                                         TrackRole track,
                                                         PlayingTechnique technique,
                                                         NoteSource source) {
  // First check harmony safety - get candidates and use best one if available
  auto candidates = getSafePitchCandidates(harmony_, pitch, start, duration, track,
                                            instrument_.getLowestPitch(), instrument_.getHighestPitch());
  if (!candidates.empty() && candidates[0].pitch != pitch) {
    // Found a different safe pitch - verify it's playable
    uint8_t safe_pitch = candidates[0].pitch;
    if (!instrument_.isPitchPlayable(safe_pitch)) {
      return std::nullopt;
    }
    pitch = safe_pitch;
  }
  // else: either original pitch is safe, or no better alternative found - proceed with original

  // Now create with physical constraints
  return create(start, duration, pitch, velocity, technique, source);
}

uint8_t FrettedNoteFactory::findPlayablePitch(uint8_t desired, Tick start, Tick duration,
                                               float max_cost) {
  // If already playable and low cost, return as-is
  if (instrument_.isPitchPlayable(desired)) {
    Fingering test = instrument_.findBestFingering(desired, state_, PlayingTechnique::Normal);
    if (test.isValid() && test.playability_cost <= max_cost * 100.0f) {
      return desired;
    }
  }

  // Get chord tones at this position for harmonic alternatives
  auto chord_tones = harmony_.getChordTonesAt(start);

  // Try octave transpositions
  std::vector<uint8_t> candidates;
  candidates.push_back(desired);

  if (desired >= 12) candidates.push_back(desired - 12);
  if (desired < 115) candidates.push_back(desired + 12);
  if (desired >= 24) candidates.push_back(desired - 24);
  if (desired < 103) candidates.push_back(desired + 24);

  // Add chord tones in a similar range
  for (int tone : chord_tones) {
    // Find the chord tone closest to desired
    for (int octave = -2; octave <= 2; ++octave) {
      int candidate = tone + (desired / 12 + octave) * 12;
      if (candidate >= 0 && candidate <= 127) {
        candidates.push_back(static_cast<uint8_t>(candidate));
      }
    }
  }

  // Score each candidate
  float best_score = std::numeric_limits<float>::max();
  uint8_t best_pitch = desired;

  for (uint8_t candidate : candidates) {
    if (!instrument_.isPitchPlayable(candidate)) continue;

    Fingering fingering =
        instrument_.findBestFingering(candidate, state_, PlayingTechnique::Normal);
    if (!fingering.isValid()) continue;

    // Calculate score: playability + distance from desired
    float score = fingering.playability_cost;
    score += static_cast<float>(std::abs(static_cast<int>(candidate) -
                                          static_cast<int>(desired))) *
             2.0f;

    // Bonus if it's a chord tone
    int pitch_class = candidate % 12;
    if (std::find(chord_tones.begin(), chord_tones.end(), pitch_class) != chord_tones.end()) {
      score -= 5.0f;  // Prefer chord tones
    }

    if (score < best_score) {
      best_score = score;
      best_pitch = candidate;
    }
  }

  return best_pitch;
}

uint8_t FrettedNoteFactory::ensurePlayable(uint8_t pitch, Tick /* start */,
                                            Tick /* duration */) {
  if (instrument_.isPitchPlayable(pitch)) {
    return pitch;
  }

  uint8_t low = instrument_.getLowestPitch();
  uint8_t high = instrument_.getHighestPitch();

  // Clamp to instrument range
  if (pitch < low) {
    // Try octave up
    while (pitch < low && pitch < 127) {
      pitch += 12;
    }
  } else if (pitch > high) {
    // Try octave down
    while (pitch > high && pitch >= 12) {
      pitch -= 12;
    }
  }

  // Final check
  if (pitch < low) return low;
  if (pitch > high) return high;
  return pitch;
}

std::vector<Fingering> FrettedNoteFactory::planSequence(const std::vector<uint8_t>& pitches,
                                                         const std::vector<Tick>& durations,
                                                         PlayingTechnique technique) {
  return instrument_.findBestFingeringSequence(pitches, durations, state_, technique);
}

void FrettedNoteFactory::resetState() {
  state_ = FretboardState(instrument_.getStringCount());
  state_.hand_position = 3;
  last_fingering_ = Fingering();
  last_provenance_ = FingeringProvenance();
}

void FrettedNoteFactory::applyFingeringProvenance(NoteEvent& note, const Fingering& fingering,
                                                   PlayingTechnique technique) {
  if (fingering.assignments.empty()) return;

  const auto& assign = fingering.assignments[0];

  last_provenance_.string = assign.position.string;
  last_provenance_.fret = assign.position.fret;
  last_provenance_.finger = assign.finger;
  last_provenance_.is_barre = assign.is_barre;
  last_provenance_.barre_fret = fingering.barre.fret;
  last_provenance_.barre_span = fingering.barre.getStringCount();
  last_provenance_.technique = technique;
  last_provenance_.pick_dir = PickDirection::Alternate;

  // Note: NoteEvent doesn't have a fingering field in the base structure.
  // The provenance is stored in last_provenance_ for debugging/analysis.
  // To add fingering to NoteEvent would require extending the structure,
  // which should be done carefully to avoid bloating note events in WASM builds.
}

}  // namespace midisketch
