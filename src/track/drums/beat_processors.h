/**
 * @file beat_processors.h
 * @brief Per-beat drum generation processors.
 *
 * These functions generate individual drum elements (kick, snare, ghost notes,
 * hi-hat) for a single beat. They are called from the main drum generation loop.
 */

#ifndef MIDISKETCH_TRACK_DRUMS_BEAT_PROCESSORS_H
#define MIDISKETCH_TRACK_DRUMS_BEAT_PROCESSORS_H

#include <random>

#include "core/midi_track.h"
#include "core/preset_data.h"
#include "core/section_types.h"
#include "core/types.h"
#include "track/drums/drum_track_generator.h"
#include "track/drums/hihat_control.h"
#include "track/drums/kick_patterns.h"

namespace midisketch {
namespace drums {

/// @brief Generate kick drum for a single beat.
/// @param track Target MIDI track
/// @param beat_tick Tick position of the beat
/// @param adjusted_beat_tick Time-feel adjusted tick position
/// @param kick Kick pattern flags
/// @param beat Beat number (0-3)
/// @param velocity Base velocity
/// @param kick_prob DrumRole-based kick probability
/// @param in_prechorus_lift Whether in pre-chorus buildup zone
/// @param rng Random number generator
/// @param humanize_timing Global humanization scaling (0.0-1.0)
void generateKickForBeat(MidiTrack& track, Tick beat_tick, Tick adjusted_beat_tick,
                         const KickPattern& kick, uint8_t beat, uint8_t velocity,
                         float kick_prob, bool in_prechorus_lift, std::mt19937& rng,
                         float humanize_timing = 1.0f);

/// @brief Generate snare drum for a single beat.
/// @param track Target MIDI track
/// @param beat_tick Tick position of the beat
/// @param beat Beat number (0-3)
/// @param velocity Base velocity
/// @param section_type Current section type
/// @param style Drum style
/// @param role Drum role
/// @param snare_prob DrumRole-based snare probability
/// @param use_groove_snare Whether to use groove template snare pattern
/// @param groove_snare_pattern Groove template snare bitmask
/// @param is_intro_first Whether this is first bar of intro
/// @param in_prechorus_lift Whether in pre-chorus buildup zone
void generateSnareForBeat(MidiTrack& track, Tick beat_tick, uint8_t beat, uint8_t velocity,
                          SectionType section_type, DrumStyle style, DrumRole role,
                          float snare_prob, bool use_groove_snare, uint16_t groove_snare_pattern,
                          bool is_intro_first, bool in_prechorus_lift);

/// @brief Generate ghost notes for a single beat.
/// @param track Target MIDI track
/// @param beat_tick Tick position of the beat
/// @param beat Beat number (0-3)
/// @param velocity Base velocity
/// @param section_type Current section type
/// @param mood Current mood
/// @param backing_density Backing density setting
/// @param bpm Tempo in BPM
/// @param use_euclidean Whether using Euclidean rhythms
/// @param groove_ghost_density Ghost density from groove template
/// @param rng Random number generator
void generateGhostNotesForBeat(MidiTrack& track, Tick beat_tick, uint8_t beat, uint8_t velocity,
                               SectionType section_type, Mood mood, BackingDensity backing_density,
                               uint16_t bpm, bool use_euclidean, float groove_ghost_density,
                               std::mt19937& rng);

/// @brief Generate pre-chorus buildup pattern for a beat.
/// @param track Target MIDI track
/// @param beat_tick Tick position of the beat
/// @param beat Beat number (0-3)
/// @param velocity Base velocity
/// @param bar Current bar in section
/// @param section_bars Total bars in section
/// @param is_section_last_bar Whether this is the last bar
/// @return true if buildup was generated
bool generatePreChorusBuildup(MidiTrack& track, Tick beat_tick, uint8_t beat, uint8_t velocity,
                              uint8_t bar, uint8_t section_bars, bool is_section_last_bar);

/// @brief Generate hi-hat for a single beat.
/// @param track Target MIDI track
/// @param beat_tick Tick position of the beat
/// @param beat Beat number (0-3)
/// @param velocity Base velocity
/// @param ctx Section context
/// @param section_type Current section type
/// @param role Drum role
/// @param density_mult Density multiplier
/// @param bar_has_open_hh Whether this bar has open hi-hat accent
/// @param open_hh_beat Beat for open hi-hat (if applicable)
/// @param peak_open_hh_24 Whether peak level forces open HH on 2/4
/// @param bar Current bar number
/// @param section_bars Total bars in section
/// @param swing_amount Current swing amount
/// @param groove Groove feel
/// @param mood Current mood
/// @param bpm Tempo in BPM
/// @param rng Random number generator
void generateHiHatForBeat(MidiTrack& track, Tick beat_tick, uint8_t beat, uint8_t velocity,
                          const DrumSectionContext& ctx, SectionType section_type,
                          DrumRole role, float density_mult, bool bar_has_open_hh,
                          uint8_t open_hh_beat, bool peak_open_hh_24, uint8_t bar,
                          uint8_t section_bars, float swing_amount, DrumGrooveFeel groove,
                          Mood mood, uint16_t bpm, std::mt19937& rng);

/// @brief Get hi-hat swing factor based on mood.
/// @param mood Current mood
/// @return Swing factor (0.0-1.0)
float getHiHatSwingFactor(Mood mood);

/// @brief Apply time feel offset to tick position.
/// @param base_tick Original tick position
/// @param feel Time feel setting
/// @param bpm Tempo in BPM
/// @return Adjusted tick position
Tick applyTimeFeel(Tick base_tick, TimeFeel feel, uint16_t bpm);

/// @brief Get default time feel for a mood.
/// @param mood Current mood
/// @return Time feel setting
TimeFeel getMoodTimeFeel(Mood mood);

}  // namespace drums
}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_DRUMS_BEAT_PROCESSORS_H
