/**
 * @file melody_motif_variants.cpp
 * @brief MelodyDesigner::prepareMotifVariants() and getMotifForSection() implementations.
 *
 * Extracted from melody_designer.cpp for modularity.
 */

#include "core/motif_transform.h"
#include "track/vocal/melody_designer.h"

namespace midisketch {

void MelodyDesigner::prepareMotifVariants(const GlobalMotif& source) {
  motif_variants_.clear();

  if (!source.isValid()) {
    return;
  }

  // Chorus: use original motif (strongest recognition)
  motif_variants_[SectionType::Chorus] = source;

  // A section: diminished rhythm (slightly faster feel for verses)
  motif_variants_[SectionType::A] = transformGlobalMotif(source, GlobalMotifTransform::Diminish);

  // B section: sequenced up (building tension toward chorus)
  motif_variants_[SectionType::B] = transformGlobalMotif(source, GlobalMotifTransform::Sequence, 2);

  // Bridge: inverted contour (maximum contrast)
  motif_variants_[SectionType::Bridge] = transformGlobalMotif(source, GlobalMotifTransform::Invert);

  // Outro: fragmented (winding down, partial recall)
  motif_variants_[SectionType::Outro] =
      transformGlobalMotif(source, GlobalMotifTransform::Fragment);

  // Intro/Interlude: retrograde (instrumental interest)
  motif_variants_[SectionType::Intro] =
      transformGlobalMotif(source, GlobalMotifTransform::Retrograde);
  motif_variants_[SectionType::Interlude] =
      transformGlobalMotif(source, GlobalMotifTransform::Retrograde);

  // Chant/MixBreak: augmented rhythm (emphasized, slower feel)
  motif_variants_[SectionType::Chant] = transformGlobalMotif(source, GlobalMotifTransform::Augment);
  motif_variants_[SectionType::MixBreak] =
      transformGlobalMotif(source, GlobalMotifTransform::Augment);
}

const GlobalMotif& MelodyDesigner::getMotifForSection(SectionType section_type) const {
  auto it = motif_variants_.find(section_type);
  if (it != motif_variants_.end()) {
    return it->second;
  }

  // Fallback to original motif if variant not found
  if (cached_global_motif_.has_value()) {
    return cached_global_motif_.value();
  }

  // Return a static empty motif if nothing available
  static const GlobalMotif empty_motif;
  return empty_motif;
}

}  // namespace midisketch
