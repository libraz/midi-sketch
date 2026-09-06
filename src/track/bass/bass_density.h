/**
 * @file bass_density.h
 * @brief Thinning a generated bass line to the weight its section asked for.
 *
 * A section's density is a property of the arrangement, not of the pattern:
 * the same figure carries a first verse and a last chorus, and what separates
 * them is how much of it is played. Rather than give every pattern a sparse
 * variant, the line is generated once at full weight and thinned here.
 *
 * Thinning is subtraction, so it can only take away notes the pattern does not
 * need in order to still be itself. A figure whose identity is its off-beat
 * placement has no such notes -- flattening one onto the quarters produces a
 * different pattern rather than a sparser one -- and those are left alone.
 */

#ifndef MIDISKETCH_TRACK_BASS_BASS_DENSITY_H
#define MIDISKETCH_TRACK_BASS_BASS_DENSITY_H

#include <vector>

#include "track/generators/bass.h"

namespace midisketch {

class MidiTrack;
class IHarmonyContext;
struct Section;

/// @brief Adjust bass density based on section density_percent.
///
/// - < 70%: simplify 8th patterns to quarter notes (thin out)
/// - > 90%: more active patterns (handled in generation)
///
/// The pattern the section actually generated decides the off-beat exemption,
/// not the hint that asked for one; `Section::bass_style_hint` is read only
/// where no record exists, since a hint states what was asked for and not what
/// the section played.
///
/// @param track Bass track to modify (in-place)
/// @param section Section with density_percent field
/// @param section_patterns Patterns the sections generated (may be empty)
void applyDensityAdjustment(MidiTrack& track, const Section& section,
                            const std::vector<BassSectionPattern>& section_patterns);

/// @brief Density adjustment with no generation record available.
void applyDensityAdjustment(MidiTrack& track, const Section& section);

/// @brief Density adjustment with collision checking.
///
/// Thinning lengthens the notes it keeps, and a longer note runs into whatever
/// was voiced against the original length, so the harmony context caps the
/// extension.
///
/// @param section_patterns Patterns the sections actually generated. The
///        low-density thinning exemption is decided from the record covering
///        this section; an empty list falls back to the section's style hint.
void applyDensityAdjustmentWithHarmony(MidiTrack& track, const Section& section,
                                       const std::vector<BassSectionPattern>& section_patterns,
                                       const IHarmonyContext* harmony);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_BASS_BASS_DENSITY_H
