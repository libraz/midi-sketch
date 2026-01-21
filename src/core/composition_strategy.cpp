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

    // Vocal-first for bass to avoid vocal clashes
    gen.invokeGenerateVocal();
    VocalAnalysis vocal_analysis = analyzeVocal(gen.getSong().vocal());
    {
      // Use guard for standalone function that doesn't auto-register
      TrackRegistrationGuard guard(gen.getHarmonyContext(), gen.getSong().bass(), TrackRole::Bass);
      generateBassTrackWithVocal(gen.getSong().bass(), gen.getSong(), params, gen.getRng(),
                                 vocal_analysis, gen.getHarmonyContext());
    }
    gen.invokeGenerateAux();
  } else {
    gen.invokeGenerateBass();  // No vocal to avoid
  }
}

void MelodyLeadStrategy::generateChordTrack(Generator& gen) { gen.invokeGenerateChord(); }

// ============================================================================
// BackgroundMotifStrategy
// ============================================================================

void BackgroundMotifStrategy::generateMelodicTracks(Generator& gen) {
  // Generate Bass first so Motif can avoid clashing with bass notes
  // Note: invokeGenerateBass() -> generateBass() auto-registers via RAII guard
  gen.invokeGenerateBass();

  // Now generate Motif with Bass registered for collision avoidance
  // Note: invokeGenerateMotif() -> generateMotif() auto-registers via RAII guard
  gen.invokeGenerateMotif();
}

void BackgroundMotifStrategy::generateChordTrack(Generator& gen) {
  // Generate Chord avoiding both Bass and Motif
  gen.invokeGenerateChord();
}

// ============================================================================
// SynthDrivenStrategy
// ============================================================================

void SynthDrivenStrategy::generateMelodicTracks(Generator& gen) { gen.invokeGenerateBass(); }

void SynthDrivenStrategy::generateChordTrack(Generator& gen) { gen.invokeGenerateChord(); }

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
