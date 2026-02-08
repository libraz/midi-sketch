/**
 * @file drums.cpp
 * @brief Implementation of DrumsGenerator.
 */

#include "track/generators/drums.h"

#include "core/song.h"
#include "track/drums.h"

namespace midisketch {

void DrumsGenerator::doGenerateFullTrack(MidiTrack& track, const FullTrackContext& ctx) {
  // Check for vocal-dependent generation modes
  if (ctx.vocal_analysis) {
    const VocalAnalysis* va = ctx.vocal_analysis;
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


}  // namespace midisketch
