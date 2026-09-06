/**
 * @file rhythm_sync_lead.h
 * @brief The RhythmSync lead treatment and the locked riff it is applied against.
 *
 * Under RhythmSync the motif is the coordinate axis and the vocal is stamped
 * with a shared pitch DNA, so the lead is not simply generated and left alone:
 * it is re-shaped section by section after the fact. That rewrite deliberately
 * skips the inter-track collision check -- the melody is king, and the
 * accompaniment is what gets fixed against the new lead -- so the caller owes
 * it a re-registration and the accompaniment-side clash passes afterwards.
 *
 * The riff reference exists because those clash passes are per-note. A locked
 * riff is an identity, and settling one of its notes against a neighbour
 * scatters that identity a note at a time. Capturing the intended realization
 * before the scatter, and pulling the riff back to it afterwards wherever the
 * final state allows, is what lets both run: the per-note fixes keep the song
 * consonant and the riff still arrives as the same riff.
 */

#ifndef MIDISKETCH_CORE_RHYTHM_SYNC_LEAD_H
#define MIDISKETCH_CORE_RHYTHM_SYNC_LEAD_H

#include <cstdint>
#include <map>
#include <vector>

#include "core/section_types.h"
#include "core/types.h"

namespace midisketch {

class MidiTrack;
class IHarmonyContext;
struct GeneratorParams;

/// @brief Whether this song is the RhythmSync lead configuration.
///
/// The treatment below is written for one setting, not for the paradigm as a
/// whole: the RhythmLock blueprint under the paradigm's high-energy mood. The
/// mood is part of the test because it is the closest exposed timbre and drum
/// preset for the arrangement the treatment assumes.
bool isRhythmSyncLeadSetting(const GeneratorParams& params, uint8_t resolved_blueprint_id);

/// @brief Whether the riff's identity is fixed and therefore worth restoring.
bool shouldRestoreLockedMotifRiff(const GeneratorParams& params);

/// @brief Stamp the shared lead DNA onto the vocal and motif, section by section.
///
/// Pitches are rewritten without an inter-track collision check. The caller has
/// to re-register both tracks afterwards and resolve the accompaniment against
/// the result; nothing here does either.
void applyRhythmSyncLeadDna(MidiTrack& vocal, MidiTrack& motif,
                            const std::vector<Section>& sections, const GeneratorParams& params,
                            const IHarmonyContext& harmony);

/// Intended riff realization per onset: start tick -> sorted pitch stack.
using MotifRiffReference = std::map<Tick, std::vector<uint8_t>>;

/// @brief Record what the riff was meant to be, before the per-note passes run.
///
/// Capture after any deliberate register shaping and before the clash fixers,
/// or the reference preserves the wrong thing in one direction or the other.
MotifRiffReference captureMotifRiffReference(const MidiTrack& motif);

/// @brief Pull the riff back toward @p reference wherever the final state allows.
///
/// A note is only taken back when the captured pitch is still consonant against
/// the tracks as they now stand, so this restores the riff's identity without
/// reintroducing the clashes the passes in between were resolving.
void restoreMotifRiffFromReference(MidiTrack& motif, const MidiTrack& vocal, const MidiTrack& aux,
                                   const MotifRiffReference& reference,
                                   const IHarmonyContext& harmony);

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_RHYTHM_SYNC_LEAD_H
