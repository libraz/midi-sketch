/**
 * @file drums.cpp
 * @brief Implementation of DrumsGenerator.
 */

#include "track/generators/drums.h"

#include "core/song.h"
#include "track/drums.h"

namespace midisketch {

void DrumsGenerator::generateSection(MidiTrack& /* track */, const Section& /* section */,
                                      TrackContext& /* ctx */) {
  // DrumsGenerator uses generateFullTrack() for fill coordination across sections
  // This method is kept for ITrackBase compliance but not used directly.
}

void DrumsGenerator::generateFullTrack(MidiTrack& track, const FullTrackContext& ctx) {
  if (!ctx.song || !ctx.params || !ctx.rng) {
    return;
  }
  // Check for vocal-dependent generation modes
  if (ctx.vocal_analysis) {
    const VocalAnalysis* va = static_cast<const VocalAnalysis*>(ctx.vocal_analysis);
    if (ctx.params->drums_sync_vocal) {
      generateDrumsTrackWithVocal(track, *ctx.song, *ctx.params, *ctx.rng, *va);
      return;
    }
    if (ctx.params->paradigm == GenerationParadigm::MelodyDriven) {
      generateDrumsTrackMelodyDriven(track, *ctx.song, *ctx.params, *ctx.rng, *va);
      return;
    }
  }
  // Traditional drum generation
  generateDrumsTrack(track, *ctx.song, *ctx.params, *ctx.rng);
}

void DrumsGenerator::generateWithVocal(MidiTrack& track, const Song& song,
                                        const GeneratorParams& params, std::mt19937& rng,
                                        const VocalAnalysis& vocal_analysis) {
  // Delegate to existing generateDrumsTrackWithVocal function
  generateDrumsTrackWithVocal(track, song, params, rng, vocal_analysis);
}

void DrumsGenerator::generateMelodyDriven(MidiTrack& track, const Song& song,
                                           const GeneratorParams& params, std::mt19937& rng,
                                           const VocalAnalysis& vocal_analysis) {
  // Delegate to existing generateDrumsTrackMelodyDriven function
  generateDrumsTrackMelodyDriven(track, song, params, rng, vocal_analysis);
}

}  // namespace midisketch
