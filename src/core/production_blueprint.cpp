/**
 * @file production_blueprint.cpp
 * @brief Production blueprint presets and selection functions.
 */

#include "core/production_blueprint.h"

#include <algorithm>

#include "core/rng_util.h"
#include <cctype>
#include <cstring>

namespace midisketch {

// ============================================================================
// Section Flow Definitions
// ============================================================================

namespace {

// RhythmLock-style section flow: rhythm-synced, staggered intro build
// Uses Pushed time_feel for tight rhythm sync, Dramatic drops for EDM-like impact
// New field legend (appended after ChorusDropStyle when non-default):
// stagger_bars, custom_layer_schedule, layer_add_at_mid, layer_remove_at_end,
// guitar_style_hint(GT), phrase_tail_rest(PT), max_moving_voices(MV),
// motif_motion_hint(MM), guide_tone_rate(GR), vocal_range_span(VS)
constexpr SectionSlot RHYTHMLOCK_FLOW[] = {
    // Intro: all tracks with staggered entry, atmospheric drums
    {SectionType::Intro, 4, TrackMask::All, EntryPattern::Stagger, SectionEnergy::Low, 60, 50,
     PeakLevel::None, DrumRole::Ambient, -1.0f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::OnBeat, 1.0f, ChorusDropStyle::None},

    // A melody: PedalTone guitar, Ostinato motif, voice limit=2, guide tone 50%
    {SectionType::A, 8, TrackMask::Vocal | TrackMask::Drums | TrackMask::Bass | TrackMask::Motif,
     EntryPattern::GradualBuild, SectionEnergy::Medium, 70, 70, PeakLevel::None, DrumRole::Full,
     -1.0f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::Pushed, 1.0f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     4, true, 2, 6, 50, 0},

    // B melody: PedalTone, voice limit=3, guide tone 60%, phrase tail rest
    {SectionType::B, 8,
     TrackMask::Vocal | TrackMask::Drums | TrackMask::Bass | TrackMask::Chord | TrackMask::Motif,
     EntryPattern::Immediate, SectionEnergy::High, 80, 85, PeakLevel::None, DrumRole::Full,
     -1.0f, SectionModifier::None, 100,
     ExitPattern::CutOff, TimeFeel::Pushed, 0.5f, ChorusDropStyle::Dramatic,
     0, false, TrackMask::None, TrackMask::None,
     4, true, 3, 0, 60, 0},

    // Chorus: RhythmChord, voice limit=3, guide tone 55%
    {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, SectionEnergy::Peak, 90, 100,
     PeakLevel::None, DrumRole::Full, -1.0f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::Pushed, 0.5f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     5, false, 3, 0, 55, 0},

    // Interlude: drums solo (all defaults)
    {SectionType::Interlude, 4, TrackMask::Drums, EntryPattern::Immediate, SectionEnergy::Low, 65,
     60, PeakLevel::None, DrumRole::Ambient, -1.0f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::OnBeat, 1.0f, ChorusDropStyle::None},

    // 2nd A melody: PedalTone, Ostinato, voice limit=2, guide tone 50%
    {SectionType::A, 8, TrackMask::Vocal | TrackMask::Drums | TrackMask::Bass | TrackMask::Motif,
     EntryPattern::Immediate, SectionEnergy::Medium, 72, 75, PeakLevel::None, DrumRole::Full,
     -1.0f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::Pushed, 1.0f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     4, true, 2, 6, 50, 0},

    // 2nd B melody: PedalTone, voice limit=3, guide tone 60%, phrase tail rest
    {SectionType::B, 8,
     TrackMask::Vocal | TrackMask::Drums | TrackMask::Bass | TrackMask::Chord | TrackMask::Motif,
     EntryPattern::GradualBuild, SectionEnergy::High, 82, 90, PeakLevel::None, DrumRole::Full,
     -1.0f, SectionModifier::None, 100,
     ExitPattern::CutOff, TimeFeel::Pushed, 0.5f, ChorusDropStyle::Dramatic,
     0, false, TrackMask::None, TrackMask::None,
     4, true, 3, 0, 60, 0},

    // 2nd Chorus: RhythmChord, voice limit=3, guide tone 55%
    {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, SectionEnergy::Peak, 92, 100,
     PeakLevel::Medium, DrumRole::Full, -1.0f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::Pushed, 0.5f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     5, false, 3, 0, 55, 0},

    // Drop chorus: vocal solo (all defaults)
    {SectionType::Chorus, 4, TrackMask::Vocal, EntryPattern::Immediate, SectionEnergy::High, 85, 70,
     PeakLevel::None, DrumRole::Full, -1.0f, SectionModifier::None, 100,
     ExitPattern::CutOff, TimeFeel::OnBeat, 1.0f, ChorusDropStyle::DrumHit},

    // Last chorus: TremoloPick, voice limit=4 (relaxed), guide tone 55%
    {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, SectionEnergy::Peak, 95, 100,
     PeakLevel::Max, DrumRole::Full, -1.0f, SectionModifier::Climactic, 100,
     ExitPattern::FinalHit, TimeFeel::Pushed, 0.5f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     6, false, 4, 0, 55, 0},

    // Outro: fade out (all defaults)
    {SectionType::Outro, 4, TrackMask::Drums | TrackMask::Bass, EntryPattern::Immediate,
     SectionEnergy::Low, 60, 50, PeakLevel::None, DrumRole::Ambient, -1.0f, SectionModifier::None, 100,
     ExitPattern::Fadeout, TimeFeel::OnBeat, 2.0f, ChorusDropStyle::None},
};

// StoryPop-style section flow: melody-driven, full arrangement
// Uses OnBeat time_feel for precise pop timing, Subtle drops for smooth transitions
constexpr SectionSlot STORYPOP_FLOW[] = {
    // Intro: full arrangement (all defaults)
    {SectionType::Intro, 4, TrackMask::All, EntryPattern::Immediate, SectionEnergy::Medium, 70, 80,
     PeakLevel::None, DrumRole::Full, -1.0f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::OnBeat, 1.0f, ChorusDropStyle::None},

    // A melody: PedalTone, Ostinato motif, voice limit=2, guide tone 60%, vocal range 15st
    {SectionType::A, 8, TrackMask::All, EntryPattern::Immediate, SectionEnergy::Medium, 75, 85,
     PeakLevel::None, DrumRole::Full, -1.0f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::OnBeat, 1.0f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     4, true, 2, 6, 60, 15},

    // B melody: PedalTone, voice limit=3, guide tone 70%, vocal range 15st, phrase tail rest
    {SectionType::B, 8, TrackMask::All, EntryPattern::GradualBuild, SectionEnergy::High, 82, 90,
     PeakLevel::None, DrumRole::Full, -1.0f, SectionModifier::None, 100,
     ExitPattern::Sustain, TimeFeel::OnBeat, 0.5f, ChorusDropStyle::Subtle,
     0, false, TrackMask::None, TrackMask::None,
     4, true, 3, 0, 70, 15},

    // Chorus: RhythmChord, voice limit=3, guide tone 65%, vocal range 15st
    {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, SectionEnergy::Peak, 90, 100,
     PeakLevel::None, DrumRole::Full, -1.0f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::OnBeat, 0.5f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     5, false, 3, 0, 65, 15},

    // 2nd A melody: PedalTone, Ostinato, voice limit=2, guide tone 60%, vocal range 15st
    {SectionType::A, 8, TrackMask::All, EntryPattern::Immediate, SectionEnergy::Medium, 77, 85,
     PeakLevel::None, DrumRole::Full, -1.0f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::OnBeat, 1.0f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     4, true, 2, 6, 60, 15},

    // 2nd B melody: PedalTone, voice limit=3, guide tone 70%, vocal range 15st, phrase tail rest
    {SectionType::B, 8, TrackMask::All, EntryPattern::GradualBuild, SectionEnergy::High, 85, 92,
     PeakLevel::None, DrumRole::Full, -1.0f, SectionModifier::None, 100,
     ExitPattern::Sustain, TimeFeel::OnBeat, 0.5f, ChorusDropStyle::Subtle,
     0, false, TrackMask::None, TrackMask::None,
     4, true, 3, 0, 70, 15},

    // 2nd Chorus: RhythmChord, voice limit=3, guide tone 65%, vocal range 15st
    {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, SectionEnergy::Peak, 92, 100,
     PeakLevel::Medium, DrumRole::Full, -1.0f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::OnBeat, 0.5f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     5, false, 3, 0, 65, 15},

    // Bridge: sparse, guide tone 70%, vocal range 15st, phrase tail rest
    {SectionType::Bridge, 8, TrackMask::Vocal | TrackMask::Chord | TrackMask::Drums,
     EntryPattern::Immediate, SectionEnergy::High, 78, 75, PeakLevel::None, DrumRole::Minimal,
     -1.0f, SectionModifier::Transitional, 100,
     ExitPattern::Sustain, TimeFeel::LaidBack, 1.0f, ChorusDropStyle::Dramatic,
     0, false, TrackMask::None, TrackMask::None,
     0, true, 2, 0, 70, 15},

    // Last chorus: RhythmChord, voice limit=3, guide tone 65%, vocal range 15st
    {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, SectionEnergy::Peak, 95, 100,
     PeakLevel::Max, DrumRole::Full, -1.0f, SectionModifier::Climactic, 100,
     ExitPattern::FinalHit, TimeFeel::OnBeat, 0.5f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     5, false, 3, 0, 65, 15},

    // Outro (all defaults)
    {SectionType::Outro, 4, TrackMask::All, EntryPattern::Immediate, SectionEnergy::Low, 65, 70,
     PeakLevel::None, DrumRole::Full, -1.0f, SectionModifier::None, 100,
     ExitPattern::Fadeout, TimeFeel::LaidBack, 2.0f, ChorusDropStyle::None},
};

// Ballad-style section flow: gradual build, sparse intro
// Light swing (0.15f) throughout for gentle sway feel
// Uses LaidBack time_feel for relaxed timing, sparse harmonic_rhythm (2.0) in intro
constexpr SectionSlot BALLAD_FLOW[] = {
    // Intro: chord only (all defaults)
    {SectionType::Intro, 4, TrackMask::Chord, EntryPattern::Immediate, SectionEnergy::Low, 60, 60,
     PeakLevel::None, DrumRole::Full, 0.15f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::LaidBack, 2.0f, ChorusDropStyle::None},

    // A melody: Fingerpick, voice limit=2, guide tone 70%, vocal range 15st, phrase tail rest
    {SectionType::A, 8, TrackMask::Vocal | TrackMask::Chord, EntryPattern::Immediate,
     SectionEnergy::Low, 65, 70, PeakLevel::None, DrumRole::Full, 0.15f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::LaidBack, 1.0f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     1, true, 2, 0, 70, 15},

    // B melody: Fingerpick, voice limit=2, guide tone 70%, vocal range 15st, phrase tail rest
    {SectionType::B, 8, TrackMask::Vocal | TrackMask::Chord | TrackMask::Bass,
     EntryPattern::GradualBuild, SectionEnergy::Medium, 70, 75, PeakLevel::None, DrumRole::Full,
     0.15f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::LaidBack, 1.0f, ChorusDropStyle::Subtle,
     0, false, TrackMask::None, TrackMask::None,
     1, true, 2, 0, 70, 15},

    // Chorus: Strum, voice limit=2, guide tone 65%, vocal range 15st, phrase tail rest
    {SectionType::Chorus, 8, TrackMask::Basic, EntryPattern::GradualBuild, SectionEnergy::High, 78,
     80, PeakLevel::None, DrumRole::Minimal, 0.2f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::LaidBack, 0.5f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     2, true, 2, 0, 65, 15},

    // Interlude: chord only (all defaults)
    {SectionType::Interlude, 4, TrackMask::Chord, EntryPattern::Immediate, SectionEnergy::Low, 60,
     55, PeakLevel::None, DrumRole::Full, 0.15f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::LaidBack, 2.0f, ChorusDropStyle::None},

    // 2nd A melody: Fingerpick, voice limit=2, guide tone 70%, vocal range 15st, phrase tail rest
    {SectionType::A, 8, TrackMask::Vocal | TrackMask::Chord, EntryPattern::Immediate,
     SectionEnergy::Low, 67, 72, PeakLevel::None, DrumRole::Full, 0.15f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::LaidBack, 1.0f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     1, true, 2, 0, 70, 15},

    // 2nd B melody: Fingerpick, voice limit=2, guide tone 70%, vocal range 15st, phrase tail rest
    {SectionType::B, 8, TrackMask::Vocal | TrackMask::Chord | TrackMask::Bass,
     EntryPattern::GradualBuild, SectionEnergy::Medium, 73, 80, PeakLevel::None, DrumRole::Full,
     0.2f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::LaidBack, 1.0f, ChorusDropStyle::Subtle,
     0, false, TrackMask::None, TrackMask::None,
     1, true, 2, 0, 70, 15},

    // Ochisabi Chorus: Strum, voice limit=2, guide tone 65%, vocal range 15st
    {SectionType::Chorus, 8, TrackMask::All, EntryPattern::GradualBuild, SectionEnergy::High, 82,
     90, PeakLevel::Medium, DrumRole::Full, 0.25f, SectionModifier::Ochisabi, 100,
     ExitPattern::None, TimeFeel::LaidBack, 0.5f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     2, false, 2, 0, 65, 15},

    // Last chorus: Strum, voice limit=3 (relaxed), guide tone 60%, vocal range 18st (wider)
    {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, SectionEnergy::Peak, 90, 100,
     PeakLevel::Max, DrumRole::Full, 0.3f, SectionModifier::Climactic, 100,
     ExitPattern::FinalHit, TimeFeel::OnBeat, 0.5f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     2, false, 3, 0, 60, 18},

    // Outro: fade out (all defaults)
    {SectionType::Outro, 8, TrackMask::Chord, EntryPattern::Immediate, SectionEnergy::Low, 55, 50,
     PeakLevel::None, DrumRole::Full, 0.1f, SectionModifier::None, 100,
     ExitPattern::Fadeout, TimeFeel::LaidBack, 2.0f, ChorusDropStyle::None},
};

// IdolStandard: Classic idol pop - memorable melody, gradual energy build
// Structure: Intro(4) -> A(8) -> B(8) -> Chorus(8) -> A(8) -> B(8) -> Chorus(8)
//            -> Bridge(8) -> LastChorus(16) -> Outro(4) = 80 bars
// Uses OnBeat time_feel for clean idol pop timing, Subtle drops for smooth transitions
constexpr SectionSlot IDOL_STANDARD_FLOW[] = {
    // Intro: kick only (all defaults)
    {SectionType::Intro, 4, TrackMask::Drums, EntryPattern::Immediate, SectionEnergy::Low, 60, 50,
     PeakLevel::None, DrumRole::Minimal, -1.0f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::OnBeat, 1.0f, ChorusDropStyle::None},

    // A melody: Strum, voice limit=3, guide tone 55%, phrase tail rest
    {SectionType::A, 8, TrackMask::Vocal | TrackMask::Drums | TrackMask::Chord,
     EntryPattern::GradualBuild, SectionEnergy::Low, 65, 60, PeakLevel::None, DrumRole::Minimal,
     -1.0f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::OnBeat, 1.0f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     2, true, 3, 0, 55, 0},

    // B melody: Strum, voice limit=3, guide tone 65%, phrase tail rest
    {SectionType::B, 8, TrackMask::Vocal | TrackMask::Drums | TrackMask::Bass | TrackMask::Chord,
     EntryPattern::GradualBuild, SectionEnergy::Medium, 72, 75, PeakLevel::None, DrumRole::Full,
     -1.0f, SectionModifier::None, 100,
     ExitPattern::Sustain, TimeFeel::OnBeat, 0.5f, ChorusDropStyle::Subtle,
     0, false, TrackMask::None, TrackMask::None,
     2, true, 3, 0, 65, 0},

    // First Chorus: Strum, no voice limit, guide tone 60%
    {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, SectionEnergy::High, 82, 90,
     PeakLevel::None, DrumRole::Full, -1.0f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::OnBeat, 0.5f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     2, false, 0, 0, 60, 0},

    // 2nd A melody: Strum, voice limit=3, guide tone 55%, phrase tail rest
    {SectionType::A, 8, TrackMask::Vocal | TrackMask::Drums | TrackMask::Chord | TrackMask::Bass,
     EntryPattern::Immediate, SectionEnergy::Medium, 68, 65, PeakLevel::None, DrumRole::Full,
     -1.0f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::OnBeat, 1.0f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     2, true, 3, 0, 55, 0},

    // 2nd B melody: Strum, voice limit=3, guide tone 65%, phrase tail rest
    {SectionType::B, 8, TrackMask::Vocal | TrackMask::Drums | TrackMask::Bass | TrackMask::Chord,
     EntryPattern::GradualBuild, SectionEnergy::High, 75, 80, PeakLevel::None, DrumRole::Full,
     -1.0f, SectionModifier::None, 100,
     ExitPattern::Sustain, TimeFeel::OnBeat, 0.5f, ChorusDropStyle::Subtle,
     0, false, TrackMask::None, TrackMask::None,
     2, true, 3, 0, 65, 0},

    // 2nd Chorus: Strum, no voice limit, guide tone 60%, SlapPop bass
    {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, SectionEnergy::High, 85, 95,
     PeakLevel::Medium, DrumRole::Full, -1.0f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::OnBeat, 0.5f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     2, false, 0, 0, 60, 0, 16},

    // Bridge: sparse, voice limit=2, guide tone 70%, phrase tail rest
    {SectionType::Bridge, 8, TrackMask::Vocal | TrackMask::Chord | TrackMask::Drums,
     EntryPattern::Immediate, SectionEnergy::Medium, 70, 70, PeakLevel::None, DrumRole::Minimal,
     -1.0f, SectionModifier::Transitional, 100,
     ExitPattern::Sustain, TimeFeel::LaidBack, 1.0f, ChorusDropStyle::Dramatic,
     0, false, TrackMask::None, TrackMask::None,
     0, true, 2, 0, 70, 0},

    // Last Chorus: Strum, no voice limit, guide tone 60%
    {SectionType::Chorus, 16, TrackMask::All, EntryPattern::DropIn, SectionEnergy::Peak, 95, 100,
     PeakLevel::Max, DrumRole::Full, -1.0f, SectionModifier::Climactic, 100,
     ExitPattern::FinalHit, TimeFeel::OnBeat, 0.5f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     2, false, 0, 0, 60, 0},

    // Outro (all defaults)
    {SectionType::Outro, 4, TrackMask::Drums | TrackMask::Chord, EntryPattern::Immediate,
     SectionEnergy::Low, 60, 50, PeakLevel::None, DrumRole::Ambient, -1.0f, SectionModifier::None, 100,
     ExitPattern::Fadeout, TimeFeel::LaidBack, 2.0f, ChorusDropStyle::None},
};

// IdolHyper: High-energy idol pop - chorus-first, high BPM, dense arrangement
// Structure: Intro(2) -> Chorus(8) -> A(4) -> Chorus(8) -> B(4) -> Chorus(8)
//            -> Drop(4) -> LastChorus(16) = 54 bars
// Strong swing (0.5f) for high energy shuffle feel
// Uses Pushed time_feel for driving energy, Dramatic drop before chorus
constexpr SectionSlot IDOL_HYPER_FLOW[] = {
    // Intro: RhythmChord, immediate high energy
    {SectionType::Intro, 2, TrackMask::All, EntryPattern::DropIn, SectionEnergy::High, 85, 90,
     PeakLevel::None, DrumRole::Full, 0.5f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::Pushed, 0.5f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     5, false, 0, 0, 0, 0},

    // First Chorus: RhythmChord, guide tone 55%
    {SectionType::Chorus, 8, TrackMask::All, EntryPattern::Immediate, SectionEnergy::Peak, 90, 100,
     PeakLevel::None, DrumRole::Full, 0.5f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::Pushed, 0.5f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     5, false, 0, 0, 55, 0},

    // A melody: PedalTone, voice limit=3, guide tone 50%
    {SectionType::A, 4, TrackMask::All, EntryPattern::Immediate, SectionEnergy::High, 82, 85,
     PeakLevel::None, DrumRole::Full, 0.45f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::Pushed, 0.5f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     4, false, 3, 0, 50, 0},

    // 2nd Chorus: RhythmChord, guide tone 55%
    {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, SectionEnergy::Peak, 92, 100,
     PeakLevel::None, DrumRole::Full, 0.5f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::Pushed, 0.5f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     5, false, 0, 0, 55, 0},

    // B melody: PedalTone, voice limit=3, guide tone 60%, phrase tail rest
    {SectionType::B, 4, TrackMask::All, EntryPattern::GradualBuild, SectionEnergy::High, 85, 90,
     PeakLevel::None, DrumRole::Full, 0.45f, SectionModifier::None, 100,
     ExitPattern::CutOff, TimeFeel::Pushed, 0.5f, ChorusDropStyle::Dramatic,
     0, false, TrackMask::None, TrackMask::None,
     4, true, 3, 0, 60, 0},

    // 3rd Chorus: RhythmChord, guide tone 55%
    {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, SectionEnergy::Peak, 93, 100,
     PeakLevel::Medium, DrumRole::Full, 0.55f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::Pushed, 0.5f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     5, false, 0, 0, 55, 0},

    // MixBreak (all defaults)
    {SectionType::MixBreak, 4, TrackMask::Vocal | TrackMask::Drums, EntryPattern::Immediate,
     SectionEnergy::High, 80, 70, PeakLevel::None, DrumRole::Ambient, 0.3f, SectionModifier::None, 100,
     ExitPattern::CutOff, TimeFeel::OnBeat, 1.0f, ChorusDropStyle::DrumHit},

    // Last Chorus: TremoloPick, FastRun bass, guide tone 55%
    {SectionType::Chorus, 16, TrackMask::All, EntryPattern::DropIn, SectionEnergy::Peak, 95, 100,
     PeakLevel::Max, DrumRole::Full, 0.55f, SectionModifier::Climactic, 100,
     ExitPattern::FinalHit, TimeFeel::Pushed, 0.5f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     6, false, 0, 0, 55, 0, 17},
};

// IdolKawaii: Sweet, bouncy idol pop - restrained dynamics, cute vibe
// Structure: Intro(4) -> A(8) -> Chorus(8) -> A(8) -> Chorus(8) -> CuteBreak(4)
//            -> LastChorus(12) = 52 bars
// Uses OnBeat time_feel for bouncy feel, Subtle drops for gentle transitions
constexpr SectionSlot IDOL_KAWAII_FLOW[] = {
    // Intro: soft, cute (all defaults)
    {SectionType::Intro, 4, TrackMask::Chord | TrackMask::Drums, EntryPattern::Immediate,
     SectionEnergy::Low, 55, 50, PeakLevel::None, DrumRole::Minimal, -1.0f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::OnBeat, 1.0f, ChorusDropStyle::None},

    // A melody: Fingerpick, voice limit=2, guide tone 60%, vocal range 12st, phrase tail rest
    {SectionType::A, 8, TrackMask::Vocal | TrackMask::Drums | TrackMask::Chord,
     EntryPattern::Immediate, SectionEnergy::Low, 60, 60, PeakLevel::None, DrumRole::Minimal,
     -1.0f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::OnBeat, 1.0f, ChorusDropStyle::Subtle,
     0, false, TrackMask::None, TrackMask::None,
     1, true, 2, 0, 60, 12},

    // First Chorus: Strum, voice limit=3, guide tone 55%, vocal range 12st
    {SectionType::Chorus, 8,
     TrackMask::Vocal | TrackMask::Drums | TrackMask::Chord | TrackMask::Bass, EntryPattern::DropIn,
     SectionEnergy::Medium, 70, 75, PeakLevel::None, DrumRole::Minimal, -1.0f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::OnBeat, 0.5f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     2, false, 3, 0, 55, 12},

    // 2nd A melody: Fingerpick, voice limit=2, guide tone 60%, vocal range 12st, phrase tail rest
    {SectionType::A, 8, TrackMask::Vocal | TrackMask::Drums | TrackMask::Chord | TrackMask::Bass,
     EntryPattern::Immediate, SectionEnergy::Medium, 65, 65, PeakLevel::None, DrumRole::Minimal,
     -1.0f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::OnBeat, 1.0f, ChorusDropStyle::Subtle,
     0, false, TrackMask::None, TrackMask::None,
     1, true, 2, 0, 60, 12},

    // 2nd Chorus: Strum, voice limit=3, guide tone 55%, vocal range 12st
    {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, SectionEnergy::High, 75, 80,
     PeakLevel::None, DrumRole::Full, -1.0f, SectionModifier::Ochisabi, 100,
     ExitPattern::None, TimeFeel::OnBeat, 0.5f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     2, false, 3, 0, 55, 12},

    // Cute Break (all defaults)
    {SectionType::Interlude, 4, TrackMask::Chord | TrackMask::Vocal, EntryPattern::Immediate,
     SectionEnergy::Low, 55, 50, PeakLevel::None, DrumRole::Minimal, -1.0f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::LaidBack, 2.0f, ChorusDropStyle::Subtle},

    // Last Chorus: Strum, voice limit=3, guide tone 55%, vocal range 12st
    {SectionType::Chorus, 12, TrackMask::All, EntryPattern::DropIn, SectionEnergy::High, 80, 85,
     PeakLevel::Max, DrumRole::Full, -1.0f, SectionModifier::Climactic, 100,
     ExitPattern::FinalHit, TimeFeel::OnBeat, 0.5f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     2, false, 3, 0, 55, 12},
};

// IdolCoolPop: Cool, stylish idol pop - four-on-floor, uniform dynamics
// Structure: Intro(8) -> A(8) -> Chorus(8) -> B(8) -> Chorus(8) -> DanceBreak(8)
//            -> LastChorus(16) = 64 bars
// Straight timing (0.0f) for tight four-on-floor dance feel
// Uses Pushed time_feel for driving energy, Dramatic drop for tension
constexpr SectionSlot IDOL_COOLPOP_FLOW[] = {
    // Intro: PedalTone, voice limit=3
    {SectionType::Intro, 8, TrackMask::All, EntryPattern::Stagger, SectionEnergy::Medium, 75, 80,
     PeakLevel::None, DrumRole::Full, 0.0f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::Pushed, 0.5f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     4, false, 3, 0, 0, 0},

    // A melody: PedalTone, voice limit=3, guide tone 50%
    {SectionType::A, 8, TrackMask::All, EntryPattern::Immediate, SectionEnergy::Medium, 78, 85,
     PeakLevel::None, DrumRole::Full, 0.0f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::Pushed, 0.5f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     4, false, 3, 0, 50, 0},

    // First Chorus: RhythmChord, voice limit=3, guide tone 50%
    {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, SectionEnergy::High, 85, 90,
     PeakLevel::None, DrumRole::Full, 0.0f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::Pushed, 0.5f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     5, false, 3, 0, 50, 0},

    // B melody: PedalTone, voice limit=3, guide tone 55%, phrase tail rest
    {SectionType::B, 8, TrackMask::All, EntryPattern::Immediate, SectionEnergy::Medium, 80, 85,
     PeakLevel::None, DrumRole::Full, 0.0f, SectionModifier::None, 100,
     ExitPattern::CutOff, TimeFeel::Pushed, 0.5f, ChorusDropStyle::Dramatic,
     0, false, TrackMask::None, TrackMask::None,
     4, true, 3, 0, 55, 0},

    // 2nd Chorus: RhythmChord, voice limit=3, guide tone 50%
    {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, SectionEnergy::High, 88, 95,
     PeakLevel::Medium, DrumRole::Full, 0.0f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::Pushed, 0.5f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     5, false, 3, 0, 50, 0},

    // Dance Break (all defaults)
    {SectionType::Interlude, 8, TrackMask::Drums | TrackMask::Bass | TrackMask::Arpeggio,
     EntryPattern::Immediate, SectionEnergy::High, 85, 95, PeakLevel::None, DrumRole::Full, 0.0f,
     SectionModifier::None, 100,
     ExitPattern::CutOff, TimeFeel::Pushed, 0.5f, ChorusDropStyle::DrumHit},

    // Last Chorus: SweepArpeggio, no voice limit, guide tone 50%
    {SectionType::Chorus, 16, TrackMask::All, EntryPattern::DropIn, SectionEnergy::Peak, 92, 100,
     PeakLevel::Max, DrumRole::Full, 0.0f, SectionModifier::Climactic, 100,
     ExitPattern::FinalHit, TimeFeel::Pushed, 0.5f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     7, false, 0, 0, 50, 0},
};

// IdolEmo: Emotional idol pop - quiet start, explosive finish
// Structure: Intro(4) -> A(8) -> B(8) -> Chorus(8) -> QuietA(4) -> Build(8)
//            -> LastChorus(16) -> Outro(4) = 60 bars
// Uses LaidBack time_feel for intimate sections, Pushed for climax
constexpr SectionSlot IDOL_EMO_FLOW[] = {
    // Intro: chord only (all defaults)
    {SectionType::Intro, 4, TrackMask::Chord, EntryPattern::Immediate, SectionEnergy::Low, 55, 50,
     PeakLevel::None, DrumRole::Ambient, -1.0f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::LaidBack, 2.0f, ChorusDropStyle::None},

    // A melody: Fingerpick, voice limit=2, guide tone 70%, vocal range 12st, phrase tail rest
    {SectionType::A, 8, TrackMask::Vocal | TrackMask::Chord, EntryPattern::Immediate,
     SectionEnergy::Low, 58, 55, PeakLevel::None, DrumRole::Ambient, -1.0f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::LaidBack, 1.0f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     1, true, 2, 0, 70, 12},

    // B melody: Strum, voice limit=3, guide tone 65%, vocal range 15st, phrase tail rest
    {SectionType::B, 8, TrackMask::Vocal | TrackMask::Chord | TrackMask::Bass,
     EntryPattern::GradualBuild, SectionEnergy::Medium, 65, 65, PeakLevel::None, DrumRole::Minimal,
     -1.0f, SectionModifier::None, 100,
     ExitPattern::Sustain, TimeFeel::LaidBack, 1.0f, ChorusDropStyle::Subtle,
     0, false, TrackMask::None, TrackMask::None,
     2, true, 3, 0, 65, 15},

    // First Chorus: PowerChord, voice limit=3, guide tone 60%
    {SectionType::Chorus, 8,
     TrackMask::Vocal | TrackMask::Drums | TrackMask::Chord | TrackMask::Bass, EntryPattern::DropIn,
     SectionEnergy::High, 78, 80, PeakLevel::None, DrumRole::Full, -1.0f, SectionModifier::None, 100,
     ExitPattern::None, TimeFeel::OnBeat, 0.5f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     3, false, 3, 0, 60, 0},

    // Quiet A (Ochisabi): Fingerpick, voice limit=2, guide tone 70%, vocal range 12st, phrase tail rest
    {SectionType::A, 4, TrackMask::Vocal | TrackMask::Chord, EntryPattern::Immediate,
     SectionEnergy::Low, 55, 50, PeakLevel::None, DrumRole::Ambient, -1.0f,
     SectionModifier::Ochisabi, 100,
     ExitPattern::None, TimeFeel::LaidBack, 2.0f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     1, true, 2, 0, 70, 12},

    // Build: Strum, voice limit=3, guide tone 65%, phrase tail rest
    {SectionType::B, 8, TrackMask::All, EntryPattern::GradualBuild, SectionEnergy::High, 75, 85,
     PeakLevel::None, DrumRole::Full, -1.0f, SectionModifier::None, 100,
     ExitPattern::CutOff, TimeFeel::OnBeat, 0.5f, ChorusDropStyle::Dramatic,
     0, false, TrackMask::None, TrackMask::None,
     2, true, 3, 0, 65, 0},

    // Last Chorus: RhythmChord, voice limit=4 (relaxed), guide tone 55%
    {SectionType::Chorus, 16, TrackMask::All, EntryPattern::DropIn, SectionEnergy::Peak, 95, 100,
     PeakLevel::Max, DrumRole::Full, -1.0f, SectionModifier::Climactic, 100,
     ExitPattern::FinalHit, TimeFeel::Pushed, 0.5f, ChorusDropStyle::None,
     0, false, TrackMask::None, TrackMask::None,
     5, false, 4, 0, 55, 0},

    // Outro (all defaults)
    {SectionType::Outro, 4, TrackMask::Chord | TrackMask::Vocal, EntryPattern::Immediate,
     SectionEnergy::Low, 55, 50, PeakLevel::None, DrumRole::Ambient, -1.0f, SectionModifier::None, 100,
     ExitPattern::Fadeout, TimeFeel::LaidBack, 2.0f, ChorusDropStyle::None},
};

}  // namespace

// ============================================================================
// Blueprint Presets
// ============================================================================

namespace {

constexpr ProductionBlueprint BLUEPRINTS[] = {
    // 0: Traditional (backward compatible)
    {
        "Traditional",
        42,                                           // weight: 42%
        GenerationParadigm::Traditional, nullptr, 0,  // Use existing StructurePattern
        RiffPolicy::Free,
        false,  // drums_sync_vocal
        false,  // drums_required
        true,   // intro_kick
        true,   // intro_bass
        40,     // intro_stagger_percent
        30,     // euclidean_drums_percent
        PercussionPolicy::Standard,  // percussion_policy
        false,  // addictive_mode
        0,      // mood_mask: all moods allowed
        {127, 108, 12, false,  // max_velocity, max_pitch, max_leap, prefer_stepwise
         InstrumentSkillLevel::Intermediate, InstrumentSkillLevel::Intermediate,
         InstrumentSkillLevel::Intermediate,  // keys_skill
         InstrumentModelMode::ConstraintsOnly,
         false, false, false,  // enable_slap, enable_tapping, enable_harmonics
         false,                 // guitar_below_vocal
         0.3f},                 // ritardando_amount (Traditional: default)
        // aux_profile: Mood default, standard functions, default scaling
        {0xFF, AuxFunction::MelodicHook, AuxFunction::MotifCounter, AuxFunction::EmotionalPad,
         1.0f, 1.0f, -2},
    },

    // 1: RhythmLock (rhythm-synced, formerly Orangestar)
    {
        "RhythmLock",
        14,  // weight: 14%
        GenerationParadigm::RhythmSync, RHYTHMLOCK_FLOW,
        static_cast<uint8_t>(sizeof(RHYTHMLOCK_FLOW) / sizeof(RHYTHMLOCK_FLOW[0])),
        RiffPolicy::Locked,
        true,   // drums_sync_vocal
        true,   // drums_required (RhythmSync needs drums)
        false,  // intro_kick (no kick in intro)
        false,  // intro_bass (no bass in intro)
        70,     // intro_stagger_percent (high chance for staggered build)
        50,     // euclidean_drums_percent (rhythm-sync benefits from euclidean)
        PercussionPolicy::Full,  // percussion_policy (high energy, rhythm-driven)
        false,  // addictive_mode
        0,      // mood_mask: all moods allowed
        {127, 108, 9, false,  // max_velocity, max_pitch, max_leap, prefer_stepwise
         InstrumentSkillLevel::Advanced, InstrumentSkillLevel::Advanced,
         InstrumentSkillLevel::Advanced,  // keys_skill
         InstrumentModelMode::Full,
         true, false, false,  // enable_slap for punchy rhythm
         true,                // guitar_below_vocal
         0.15f},              // ritardando_amount (RhythmLock: tight rhythm, subtle)
        // aux_profile: Square Lead, PulseLoop/GrooveAccent, punchy rhythm focus
        {80, AuxFunction::PulseLoop, AuxFunction::PulseLoop, AuxFunction::GrooveAccent,
         0.8f, 0.85f, -4},
    },

    // 2: StoryPop (melody-driven, formerly YOASOBI)
    {
        "StoryPop",
        10,  // weight: 10%
        GenerationParadigm::MelodyDriven, STORYPOP_FLOW,
        static_cast<uint8_t>(sizeof(STORYPOP_FLOW) / sizeof(STORYPOP_FLOW[0])),
        RiffPolicy::Evolving,
        false,  // drums_sync_vocal
        false,  // drums_required
        true,   // intro_kick
        true,   // intro_bass
        50,     // intro_stagger_percent
        40,     // euclidean_drums_percent
        PercussionPolicy::Minimal,  // percussion_policy (story focus, minimal percussion)
        false,  // addictive_mode
        0,      // mood_mask: all moods allowed
        {127, 108, 12, false,  // max_velocity, max_pitch, max_leap, prefer_stepwise
         InstrumentSkillLevel::Intermediate, InstrumentSkillLevel::Intermediate,
         InstrumentSkillLevel::Intermediate,  // keys_skill
         InstrumentModelMode::ConstraintsOnly,
         false, false, false,  // techniques
         true,                 // guitar_below_vocal
         0.3f},                // ritardando_amount (StoryPop: narrative ending)
        // aux_profile: Mood default, PhraseTail for gap-filling, gentle EmotionalPad chorus
        {0xFF, AuxFunction::MelodicHook, AuxFunction::PhraseTail, AuxFunction::EmotionalPad,
         0.7f, 0.75f, -2},
    },

    // 3: Ballad (sparse, emotional)
    {
        "Ballad",
        4,  // weight: 4%
        GenerationParadigm::MelodyDriven, BALLAD_FLOW,
        static_cast<uint8_t>(sizeof(BALLAD_FLOW) / sizeof(BALLAD_FLOW[0])), RiffPolicy::Free,
        false,  // drums_sync_vocal
        false,  // drums_required
        false,  // intro_kick
        false,  // intro_bass
        60,     // intro_stagger_percent
        20,     // euclidean_drums_percent (keep simple patterns for ballad)
        PercussionPolicy::None,  // percussion_policy (ballad: no aux percussion)
        false,  // addictive_mode
        // mood_mask: EmotionalPop(5), Sentimental(6), Chill(7), Ballad(8), Nostalgic(11)
        (1u << 5) | (1u << 6) | (1u << 7) | (1u << 8) | (1u << 11),
        {100, 84, 9, true,  // max_vel=100, max_pitch=C6(84), prefer_stepwise for lyrical flow
         InstrumentSkillLevel::Beginner, InstrumentSkillLevel::Beginner,
         InstrumentSkillLevel::Beginner,  // keys_skill
         InstrumentModelMode::ConstraintsOnly,
         false, false, false,  // no techniques for ballad simplicity
         true,                 // guitar_below_vocal
         0.4f},                // ritardando_amount (Ballad: dramatic slowdown)
        // aux_profile: Choir Aahs, SustainPad throughout, very quiet and sparse
        {52, AuxFunction::SustainPad, AuxFunction::SustainPad, AuxFunction::SustainPad,
         0.5f, 0.5f, -7},
    },

    // 4: IdolStandard (classic idol pop: memorable melody, gradual build)
    {
        "IdolStandard",
        10,  // weight: 10%
        GenerationParadigm::MelodyDriven, IDOL_STANDARD_FLOW,
        static_cast<uint8_t>(sizeof(IDOL_STANDARD_FLOW) / sizeof(IDOL_STANDARD_FLOW[0])),
        RiffPolicy::Evolving,
        false,  // drums_sync_vocal
        false,  // drums_required
        true,   // intro_kick
        false,  // intro_bass
        70,     // intro_stagger_percent (gradual build concept)
        35,     // euclidean_drums_percent
        PercussionPolicy::Standard,  // percussion_policy (classic idol)
        false,  // addictive_mode
        0,      // mood_mask: all moods allowed
        {127, 108, 10, false,  // max_leap=10 for memorable melodies
         InstrumentSkillLevel::Intermediate, InstrumentSkillLevel::Intermediate,
         InstrumentSkillLevel::Intermediate,  // keys_skill
         InstrumentModelMode::ConstraintsOnly,
         false, false, false,  // techniques
         false,                // guitar_below_vocal
         0.25f},               // ritardando_amount (IdolStandard)
        // aux_profile: Mood default, PhraseTail verse, Unison chorus for idol power
        {0xFF, AuxFunction::MelodicHook, AuxFunction::PhraseTail, AuxFunction::Unison,
         0.75f, 0.8f, -2},
    },

    // 5: IdolHyper (high BPM, chorus-first, high density)
    {
        "IdolHyper",
        6,  // weight: 6%
        GenerationParadigm::RhythmSync, IDOL_HYPER_FLOW,
        static_cast<uint8_t>(sizeof(IDOL_HYPER_FLOW) / sizeof(IDOL_HYPER_FLOW[0])),
        RiffPolicy::Locked,
        true,  // drums_sync_vocal
        true,  // drums_required (RhythmSync needs drums)
        true,  // intro_kick
        true,  // intro_bass
        0,     // intro_stagger_percent (2-bar intro, too short)
        60,    // euclidean_drums_percent (high energy, synth-like patterns)
        PercussionPolicy::Full,  // percussion_policy (high energy, full percussion)
        false,  // addictive_mode
        // mood_mask: EnergeticDance(2), ElectroPop(13), IdolPop(14), FutureBass(18)
        (1u << 2) | (1u << 13) | (1u << 14) | (1u << 18),
        {110, 96, 12, false,  // max_vel=110, max_pitch=C7(96), max_leap=12
         InstrumentSkillLevel::Advanced, InstrumentSkillLevel::Advanced,
         InstrumentSkillLevel::Advanced,  // keys_skill
         InstrumentModelMode::Full,
         true, false, false,  // enable_slap for high-energy punch
         true,                // guitar_below_vocal
         0.1f},               // ritardando_amount (IdolHyper: minimal, high-energy)
        // aux_profile: Square Lead, PulseLoop/GrooveAccent, high energy punch
        {80, AuxFunction::GrooveAccent, AuxFunction::PulseLoop, AuxFunction::GrooveAccent,
         0.85f, 0.9f, -4},
    },

    // 6: IdolKawaii (sweet, bouncy, restrained)
    {
        "IdolKawaii",
        5,  // weight: 5%
        GenerationParadigm::MelodyDriven, IDOL_KAWAII_FLOW,
        static_cast<uint8_t>(sizeof(IDOL_KAWAII_FLOW) / sizeof(IDOL_KAWAII_FLOW[0])),
        RiffPolicy::Locked,
        false,  // drums_sync_vocal: false for MelodyDriven (drums follow vocal phrases)
        false,  // drums_required: MelodyDriven doesn't require drums
        false,  // intro_kick
        false,  // intro_bass
        40,     // intro_stagger_percent
        25,     // euclidean_drums_percent (simple bouncy patterns)
        PercussionPolicy::Minimal,  // percussion_policy (kawaii: clap only)
        false,  // addictive_mode
        // mood_mask: BrightUpbeat(1), IdolPop(14), Yoasobi(16)
        (1u << 1) | (1u << 14) | (1u << 16),
        {80, 79, 7, true,  // max_vel=80, max_pitch=G5(79), max_leap=7, prefer_stepwise
         InstrumentSkillLevel::Beginner, InstrumentSkillLevel::Beginner,
         InstrumentSkillLevel::Beginner,  // keys_skill
         InstrumentModelMode::ConstraintsOnly,
         false, false, false,  // simple patterns for cute vibe
         true,                 // guitar_below_vocal
         0.2f},                // ritardando_amount (IdolKawaii: soft ending)
        // aux_profile: Music Box, MelodicHook throughout for cute sparkle, low density
        {10, AuxFunction::MelodicHook, AuxFunction::MelodicHook, AuxFunction::MelodicHook,
         0.6f, 0.6f, -5},
    },

    // 7: IdolCoolPop (cool, four-on-floor, uniform)
    {
        "IdolCoolPop",
        5,  // weight: 5%
        GenerationParadigm::RhythmSync, IDOL_COOLPOP_FLOW,
        static_cast<uint8_t>(sizeof(IDOL_COOLPOP_FLOW) / sizeof(IDOL_COOLPOP_FLOW[0])),
        RiffPolicy::Locked,
        false,  // drums_sync_vocal
        true,   // drums_required (four-on-floor needs drums)
        true,   // intro_kick
        true,   // intro_bass
        80,     // intro_stagger_percent (8-bar intro, full effect)
        70,     // euclidean_drums_percent (four-on-floor + euclidean = great match)
        PercussionPolicy::Full,  // percussion_policy (funky, full percussion)
        false,  // addictive_mode
        0,      // mood_mask: all moods allowed
        {120, 108, 9, false,  // max_vel=120, max_leap=9 for controlled coolness
         InstrumentSkillLevel::Advanced, InstrumentSkillLevel::Advanced,
         InstrumentSkillLevel::Advanced,  // keys_skill
         InstrumentModelMode::Full,
         true, false, false,  // enable_slap for funky grooves
         true,                // guitar_below_vocal
         0.15f},              // ritardando_amount (IdolCoolPop: cool, subtle)
        // aux_profile: Square Lead, PulseLoop/GrooveAccent, cool driving energy
        {80, AuxFunction::PulseLoop, AuxFunction::PulseLoop, AuxFunction::GrooveAccent,
         0.8f, 0.85f, -4},
    },

    // 8: IdolEmo (quiet→explosive, emotional, late peak)
    {
        "IdolEmo",
        4,  // weight: 4%
        GenerationParadigm::MelodyDriven, IDOL_EMO_FLOW,
        static_cast<uint8_t>(sizeof(IDOL_EMO_FLOW) / sizeof(IDOL_EMO_FLOW[0])), RiffPolicy::Locked,
        false,  // drums_sync_vocal
        false,  // drums_required
        false,  // intro_kick
        false,  // intro_bass
        50,     // intro_stagger_percent
        20,     // euclidean_drums_percent (emotional, simple patterns)
        PercussionPolicy::None,  // percussion_policy (emotional: no aux percussion)
        false,  // addictive_mode
        // mood_mask: EmotionalPop(5), Sentimental(6), Ballad(8)
        (1u << 5) | (1u << 6) | (1u << 8),
        {127, 108, 12, false,  // default (emotional dynamics need full range)
         InstrumentSkillLevel::Intermediate, InstrumentSkillLevel::Intermediate,
         InstrumentSkillLevel::Intermediate,  // keys_skill
         InstrumentModelMode::ConstraintsOnly,
         false, false, false,  // techniques
         true,                 // guitar_below_vocal
         0.35f},               // ritardando_amount (IdolEmo: emotional slowdown)
        // aux_profile: Choir Aahs, SustainPad throughout, very quiet and sparse
        {52, AuxFunction::SustainPad, AuxFunction::SustainPad, AuxFunction::SustainPad,
         0.55f, 0.5f, -7},
    },

    // 9: BehavioralLoop (addictive, highly repetitive hooks)
    {
        "BehavioralLoop",
        0,  // weight: 0% (explicit selection only, not random)
        GenerationParadigm::Traditional, nullptr, 0,  // Use existing StructurePattern
        RiffPolicy::LockedPitch,  // Fixed riff patterns
        false,  // drums_sync_vocal
        false,  // drums_required
        true,   // intro_kick
        true,   // intro_bass
        40,     // intro_stagger_percent
        30,     // euclidean_drums_percent
        PercussionPolicy::Standard,  // percussion_policy
        true,   // addictive_mode - enables Behavioral Loop
        0,      // mood_mask: all moods allowed
        {127, 108, 12, false,  // default constraints
         InstrumentSkillLevel::Intermediate, InstrumentSkillLevel::Intermediate,
         InstrumentSkillLevel::Intermediate,  // keys_skill
         InstrumentModelMode::ConstraintsOnly,
         false, false, false,  // techniques
         false,                // guitar_below_vocal
         0.3f},                // ritardando_amount (BehavioralLoop: default)
        // aux_profile: Mood default, PulseLoop for addictive loop feel
        {0xFF, AuxFunction::MelodicHook, AuxFunction::PulseLoop, AuxFunction::PulseLoop,
         0.9f, 0.9f, -2},
    },
};

constexpr uint8_t BLUEPRINT_COUNT =
    static_cast<uint8_t>(sizeof(BLUEPRINTS) / sizeof(BLUEPRINTS[0]));

}  // namespace

// ============================================================================
// API Implementation
// ============================================================================

const ProductionBlueprint& getProductionBlueprint(uint8_t id) {
  if (id >= BLUEPRINT_COUNT) {
    return BLUEPRINTS[0];  // Fallback to Traditional
  }
  return BLUEPRINTS[id];
}

uint8_t getProductionBlueprintCount() { return BLUEPRINT_COUNT; }

uint8_t selectProductionBlueprint(std::mt19937& rng, uint8_t explicit_id) {
  // If explicit ID is specified and valid, use it
  if (explicit_id < BLUEPRINT_COUNT) {
    return explicit_id;
  }

  // Calculate total weight
  uint32_t total_weight = 0;
  for (uint8_t i = 0; i < BLUEPRINT_COUNT; ++i) {
    total_weight += BLUEPRINTS[i].weight;
  }

  if (total_weight == 0) {
    return 0;  // Fallback to Traditional
  }

  // Random selection based on weights
  uint32_t roll = static_cast<uint32_t>(rng_util::rollRange(rng, 0, static_cast<int>(total_weight - 1)));

  uint32_t cumulative = 0;
  for (uint8_t i = 0; i < BLUEPRINT_COUNT; ++i) {
    cumulative += BLUEPRINTS[i].weight;
    if (roll < cumulative) {
      return i;
    }
  }

  return 0;  // Fallback
}

const char* getProductionBlueprintName(uint8_t id) {
  if (id >= BLUEPRINT_COUNT) {
    return "Unknown";
  }
  return BLUEPRINTS[id].name;
}

uint8_t findProductionBlueprintByName(const char* name) {
  if (name == nullptr) {
    return 255;
  }

  // Case-insensitive comparison
  for (uint8_t i = 0; i < BLUEPRINT_COUNT; ++i) {
    const char* blueprint_name = BLUEPRINTS[i].name;
    size_t len = std::strlen(name);

    if (std::strlen(blueprint_name) != len) {
      continue;
    }

    bool match = true;
    for (size_t j = 0; j < len; ++j) {
      if (std::tolower(static_cast<unsigned char>(name[j])) !=
          std::tolower(static_cast<unsigned char>(blueprint_name[j]))) {
        match = false;
        break;
      }
    }

    if (match) {
      return i;
    }
  }

  return 255;  // Not found
}

bool isMoodCompatible(uint8_t blueprint_id, uint8_t mood) {
  if (blueprint_id >= BLUEPRINT_COUNT) {
    return true;  // Unknown blueprint allows all moods
  }
  uint32_t mask = BLUEPRINTS[blueprint_id].mood_mask;
  if (mask == 0) {
    return true;  // 0 = all moods valid
  }
  return (mask & (1u << mood)) != 0;
}

}  // namespace midisketch
