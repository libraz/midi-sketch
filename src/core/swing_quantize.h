/**
 * @file swing_quantize.h
 * @brief True triplet-grid swing quantization for authentic shuffle feel.
 *
 * Provides shared swing quantization used across all track generators (drums,
 * bass, arpeggio). Instead of adding a simple offset to straight-grid positions,
 * this module blends between straight 8th/16th grids and triplet grids for
 * authentic shuffle/swing feel.
 */

#ifndef MIDISKETCH_CORE_SWING_QUANTIZE_H_
#define MIDISKETCH_CORE_SWING_QUANTIZE_H_

#include <vector>

#include "core/basic_types.h"

namespace midisketch {

struct Section;

/**
 * @brief Quantize a tick position to a swing grid by blending straight and triplet grids.
 *
 * At swing_amount=0, the tick is returned unchanged (straight grid).
 * At swing_amount=1.0, off-beat 8th notes move to full triplet positions
 * (2/3 of a beat instead of 1/2).
 *
 * The function identifies whether the tick falls on an "off-beat" 8th-note position
 * (the second 8th within each beat) and applies swing interpolation only to those
 * positions. On-beat positions are never affected.
 *
 * Straight 8th grid per beat: 0, 240 (half-beat)
 * Triplet 8th grid per beat:  0, 320 (2/3 beat)
 * Blended off-beat position:  240 + (320 - 240) * swing_amount = 240 + 80 * swing_amount
 *
 * @param tick Absolute tick position to quantize
 * @param swing_amount Swing amount (0.0 = straight, 1.0 = full triplet)
 * @return Quantized tick position with swing applied
 */
Tick quantizeToSwingGrid(Tick tick, float swing_amount);

/**
 * @brief Quantize a tick position to a 16th-note swing grid.
 *
 * Similar to quantizeToSwingGrid but operates at 16th-note resolution.
 * Off-beat 16th notes (positions 1 and 3 within each beat's 4 subdivisions)
 * are shifted toward their triplet equivalents.
 *
 * Straight 16th grid per beat: 0, 120, 240, 360
 * With swing, positions 1 (120) and 3 (360) shift toward triplet positions:
 *   Position 1: 120 -> 160 (TICKS_PER_BEAT / 3)
 *   Position 3: 360 -> 400 (TICKS_PER_BEAT * 2 / 3 + TICKS_PER_BEAT / 3 = 320 + 80)
 *
 * @param tick Absolute tick position to quantize
 * @param swing_amount Swing amount (0.0 = straight, 1.0 = full triplet)
 * @return Quantized tick position with 16th-note swing applied
 */
Tick quantizeToSwingGrid16th(Tick tick, float swing_amount);

/**
 * @brief Calculate the swing offset for an off-beat position.
 *
 * Pure utility that returns the tick delta for a given swing amount at 8th-note
 * resolution. This is useful when you know a position is off-beat and just need
 * the offset value (e.g., for additive application in existing code paths).
 *
 * @param swing_amount Swing amount (0.0-1.0)
 * @return Tick offset (0 at swing_amount=0, 80 at swing_amount=1.0)
 */
Tick swingOffsetForEighth(float swing_amount);

/**
 * @brief Calculate the swing offset for a 16th-note off-beat position.
 *
 * @param swing_amount Swing amount (0.0-1.0)
 * @return Tick offset (0 at swing_amount=0, 40 at swing_amount=1.0)
 */
Tick swingOffsetFor16th(float swing_amount);

class MidiTrack;

/**
 * @brief Apply swing quantization to all notes in a MidiTrack.
 *
 * Post-processes a track by applying triplet-grid swing quantization to every
 * note that falls on an off-beat 8th-note position. On-beat notes are not affected.
 * This is useful for tracks like bass where swing is applied after generation.
 *
 * @param track MidiTrack to modify in-place
 * @param swing_amount Swing amount (0.0 = no change, 1.0 = full triplet swing)
 */
void applySwingToTrack(MidiTrack& track, float swing_amount);

/**
 * @brief Apply per-section swing quantization to a MidiTrack.
 *
 * For each note in the track, determines which section it belongs to and
 * applies the section's swing amount. This ensures different sections can
 * have different swing feels (e.g., straight intro, swung chorus).
 *
 * @param track MidiTrack to modify in-place
 * @param sections Song sections with swing_amount fields
 */
void applySwingToTrackBySections(MidiTrack& track, const std::vector<Section>& sections);

/**
 * @brief Get swing scaling factor for a track role.
 *
 * Different instruments feel more natural with different swing amounts:
 * - HiHat/Arpeggio patterns sound better with exaggerated swing (1.2x)
 * - Kick/Bass should stay tight to the grid (0.8x)
 * - Snare is the reference point (1.0x)
 * - Vocal benefits from slightly reduced swing (0.9x)
 *
 * @param role Track role
 * @return Swing multiplier (applied to the base swing amount)
 */
float getSwingScaleForRole(TrackRole role);

/**
 * @brief Apply per-section swing quantization with track-role scaling.
 *
 * Same as applySwingToTrackBySections but multiplies swing_amount by
 * a role-specific scaling factor for more natural feel.
 *
 * @param track MidiTrack to modify in-place
 * @param sections Song sections with swing_amount fields
 * @param role Track role for swing scaling
 */
void applySwingToTrackBySections(MidiTrack& track, const std::vector<Section>& sections,
                                 TrackRole role);

/**
 * @brief Apply per-section swing quantization with track-role scaling and humanize timing.
 *
 * Same as applySwingToTrackBySections(role) but additionally scales the effective swing
 * by humanize_timing. This allows unified control of all timing variations.
 *
 * @param track MidiTrack to modify in-place
 * @param sections Song sections with swing_amount fields
 * @param role Track role for swing scaling
 * @param humanize_timing Global humanization scaling (0.0-1.0, scales swing offset)
 */
void applySwingToTrackBySections(MidiTrack& track, const std::vector<Section>& sections,
                                 TrackRole role, float humanize_timing);

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_SWING_QUANTIZE_H_
