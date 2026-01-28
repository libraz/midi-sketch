/**
 * @file composition_strategy.cpp
 * @brief Implementation of composition strategies.
 */

#include "core/composition_strategy.h"

#include "core/generator.h"
#include "core/track_registration_guard.h"
#include "track/bass.h"
#include "track/vocal_analysis.h"

namespace midisketch {

// ============================================================================
// MelodyLeadStrategy
// ============================================================================

void MelodyLeadStrategy::generateMelodicTracks(Generator& gen) {
  const auto& params = gen.getParams();

  if (!params.skip_vocal) {
    // For Orangestar style (RhythmSync + Locked), generate Motif first
    // as the rhythmic "coordinate axis" that Vocal will follow
    if (gen.shouldUseRhythmLock()) {
      gen.generateMotifAsAxis();
    }

    // Vocal-first for bass/chord to avoid vocal clashes
    gen.invokeGenerateVocal();
    gen.invokeGenerateAux();
  } else {
    // BGM-only mode: For RhythmSync paradigm, generate Motif as axis
    if (gen.shouldUseRhythmLock()) {
      gen.generateMotifAsAxis();
    }
  }
}

void MelodyLeadStrategy::generateChordTrack(Generator& gen) {
  // Generate Chord first so secondary dominants are registered for bass
  gen.invokeGenerateChord();

  // Generate Bass after chord so it sees secondary dominant registrations
  const auto& params = gen.getParams();
  if (!params.skip_vocal) {
    VocalAnalysis vocal_analysis = analyzeVocal(gen.getSong().vocal());
    {
      TrackRegistrationGuard guard(gen.getHarmonyContext(), gen.getSong().bass(), TrackRole::Bass);
      generateBassTrackWithVocal(gen.getSong().bass(), gen.getSong(), params, gen.getRng(),
                                 vocal_analysis, gen.getHarmonyContext());
    }
  } else {
    gen.invokeGenerateBass();
  }
}

// ============================================================================
// BackgroundMotifStrategy
// ============================================================================

void BackgroundMotifStrategy::generateMelodicTracks(Generator& gen) {
  // Generate Motif first as the background melodic element
  gen.invokeGenerateMotif();
}

void BackgroundMotifStrategy::generateChordTrack(Generator& gen) {
  // Generate Chord first so secondary dominants are registered for bass
  gen.invokeGenerateChord();

  // Generate Bass after chord so it sees secondary dominant registrations
  gen.invokeGenerateBass();
}

// ============================================================================
// SynthDrivenStrategy
// ============================================================================

void SynthDrivenStrategy::generateMelodicTracks([[maybe_unused]] Generator& gen) {
  // No melodic tracks in synth-driven mode
}

void SynthDrivenStrategy::generateChordTrack(Generator& gen) {
  // Generate Chord first so secondary dominants are registered for bass
  gen.invokeGenerateChord();

  // Generate Bass after chord so it sees secondary dominant registrations
  gen.invokeGenerateBass();
}

// ============================================================================
// Factory
// ============================================================================

std::unique_ptr<CompositionStrategy> createCompositionStrategy(CompositionStyle style) {
  switch (style) {
    case CompositionStyle::BackgroundMotif:
      return std::make_unique<BackgroundMotifStrategy>();
    case CompositionStyle::SynthDriven:
      return std::make_unique<SynthDrivenStrategy>();
    case CompositionStyle::MelodyLead:
    default:
      return std::make_unique<MelodyLeadStrategy>();
  }
}

}  // namespace midisketch
