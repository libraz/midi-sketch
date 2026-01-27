/**
 * @file production_blueprint.cpp
 * @brief Production blueprint presets and selection functions.
 */

#include "core/production_blueprint.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace midisketch {

// ============================================================================
// Section Flow Definitions
// ============================================================================

namespace {

// RhythmLock-style section flow: rhythm-synced, staggered intro build
constexpr SectionSlot RHYTHMLOCK_FLOW[] = {
    // Intro: all tracks with staggered entry, atmospheric drums
    {SectionType::Intro, 4, TrackMask::All, EntryPattern::Stagger, SectionEnergy::Low, 60, 50,
     PeakLevel::None, DrumRole::Ambient},

    // A melody: vocal + minimal backing + motif for riff repetition
    {SectionType::A, 8, TrackMask::Vocal | TrackMask::Drums | TrackMask::Bass | TrackMask::Motif,
     EntryPattern::GradualBuild, SectionEnergy::Medium, 70, 70, PeakLevel::None, DrumRole::Full},

    // B melody: add chord, higher energy + motif
    {SectionType::B, 8,
     TrackMask::Vocal | TrackMask::Drums | TrackMask::Bass | TrackMask::Chord | TrackMask::Motif,
     EntryPattern::Immediate, SectionEnergy::High, 80, 85, PeakLevel::None, DrumRole::Full},

    // Chorus: full arrangement, strong entry
    {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, SectionEnergy::Peak, 90, 100,
     PeakLevel::None, DrumRole::Full},

    // Interlude: drums solo
    {SectionType::Interlude, 4, TrackMask::Drums, EntryPattern::Immediate, SectionEnergy::Low, 65,
     60, PeakLevel::None, DrumRole::Ambient},

    // 2nd A melody + motif
    {SectionType::A, 8, TrackMask::Vocal | TrackMask::Drums | TrackMask::Bass | TrackMask::Motif,
     EntryPattern::Immediate, SectionEnergy::Medium, 72, 75, PeakLevel::None, DrumRole::Full},

    // 2nd B melody + motif
    {SectionType::B, 8,
     TrackMask::Vocal | TrackMask::Drums | TrackMask::Bass | TrackMask::Chord | TrackMask::Motif,
     EntryPattern::GradualBuild, SectionEnergy::High, 82, 90, PeakLevel::None, DrumRole::Full},

    // 2nd Chorus (Medium peak)
    {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, SectionEnergy::Peak, 92, 100,
     PeakLevel::Medium, DrumRole::Full},

    // Drop chorus: vocal solo (dramatic pause)
    {SectionType::Chorus, 4, TrackMask::Vocal, EntryPattern::Immediate, SectionEnergy::High, 85, 70,
     PeakLevel::None, DrumRole::Full},

    // Last chorus: everyone drops in (Maximum peak)
    {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, SectionEnergy::Peak, 95, 100,
     PeakLevel::Max, DrumRole::Full},

    // Outro: fade out with drums + bass
    {SectionType::Outro, 4, TrackMask::Drums | TrackMask::Bass, EntryPattern::Immediate,
     SectionEnergy::Low, 60, 50, PeakLevel::None, DrumRole::Ambient},
};

// StoryPop-style section flow: melody-driven, full arrangement
constexpr SectionSlot STORYPOP_FLOW[] = {
    // Intro: full arrangement
    {SectionType::Intro, 4, TrackMask::All, EntryPattern::Immediate, SectionEnergy::Medium, 70, 80,
     PeakLevel::None, DrumRole::Full},

    // A melody: full
    {SectionType::A, 8, TrackMask::All, EntryPattern::Immediate, SectionEnergy::Medium, 75, 85,
     PeakLevel::None, DrumRole::Full},

    // B melody: build up, higher energy
    {SectionType::B, 8, TrackMask::All, EntryPattern::GradualBuild, SectionEnergy::High, 82, 90,
     PeakLevel::None, DrumRole::Full},

    // Chorus: strong entry
    {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, SectionEnergy::Peak, 90, 100,
     PeakLevel::None, DrumRole::Full},

    // 2nd A melody
    {SectionType::A, 8, TrackMask::All, EntryPattern::Immediate, SectionEnergy::Medium, 77, 85,
     PeakLevel::None, DrumRole::Full},

    // 2nd B melody
    {SectionType::B, 8, TrackMask::All, EntryPattern::GradualBuild, SectionEnergy::High, 85, 92,
     PeakLevel::None, DrumRole::Full},

    // 2nd Chorus (Medium peak)
    {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, SectionEnergy::Peak, 92, 100,
     PeakLevel::Medium, DrumRole::Full},

    // Bridge: thinner arrangement (Transitional modifier for preparation)
    {SectionType::Bridge, 8, TrackMask::Vocal | TrackMask::Chord | TrackMask::Drums,
     EntryPattern::Immediate, SectionEnergy::High, 78, 75, PeakLevel::None, DrumRole::Minimal,
     -1.0f, SectionModifier::Transitional, 100},

    // Last chorus (Maximum peak, Climactic modifier)
    {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, SectionEnergy::Peak, 95, 100,
     PeakLevel::Max, DrumRole::Full, -1.0f, SectionModifier::Climactic, 100},

    // Outro
    {SectionType::Outro, 4, TrackMask::All, EntryPattern::Immediate, SectionEnergy::Low, 65, 70,
     PeakLevel::None, DrumRole::Full},
};

// Ballad-style section flow: gradual build, sparse intro
// Light swing (0.15f) throughout for gentle sway feel
constexpr SectionSlot BALLAD_FLOW[] = {
    // Intro: chord only (piano feel)
    {SectionType::Intro, 4, TrackMask::Chord, EntryPattern::Immediate, SectionEnergy::Low, 60, 60,
     PeakLevel::None, DrumRole::Full, 0.15f},

    // A melody: vocal + chord
    {SectionType::A, 8, TrackMask::Vocal | TrackMask::Chord, EntryPattern::Immediate,
     SectionEnergy::Low, 65, 70, PeakLevel::None, DrumRole::Full, 0.15f},

    // B melody: add bass
    {SectionType::B, 8, TrackMask::Vocal | TrackMask::Chord | TrackMask::Bass,
     EntryPattern::GradualBuild, SectionEnergy::Medium, 70, 75, PeakLevel::None, DrumRole::Full,
     0.15f},

    // Chorus: add drums (gentle, Minimal DrumRole)
    {SectionType::Chorus, 8, TrackMask::Basic, EntryPattern::GradualBuild, SectionEnergy::High, 78,
     80, PeakLevel::None, DrumRole::Minimal, 0.2f},

    // Interlude: chord only
    {SectionType::Interlude, 4, TrackMask::Chord, EntryPattern::Immediate, SectionEnergy::Low, 60,
     55, PeakLevel::None, DrumRole::Full, 0.15f},

    // 2nd A melody
    {SectionType::A, 8, TrackMask::Vocal | TrackMask::Chord, EntryPattern::Immediate,
     SectionEnergy::Low, 67, 72, PeakLevel::None, DrumRole::Full, 0.15f},

    // 2nd B melody
    {SectionType::B, 8, TrackMask::Vocal | TrackMask::Chord | TrackMask::Bass,
     EntryPattern::GradualBuild, SectionEnergy::Medium, 73, 80, PeakLevel::None, DrumRole::Full,
     0.2f},

    // 2nd Chorus (Ochisabi - "falling sabi"): quiet, intimate before final climax
    {SectionType::Chorus, 8, TrackMask::All, EntryPattern::GradualBuild, SectionEnergy::High, 82,
     90, PeakLevel::Medium, DrumRole::Full, 0.25f, SectionModifier::Ochisabi, 100},

    // Last chorus: emotional peak (Maximum peak, Climactic modifier)
    {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, SectionEnergy::Peak, 90, 100,
     PeakLevel::Max, DrumRole::Full, 0.3f, SectionModifier::Climactic, 100},

    // Outro: fade out with chord
    {SectionType::Outro, 8, TrackMask::Chord, EntryPattern::Immediate, SectionEnergy::Low, 55, 50,
     PeakLevel::None, DrumRole::Full, 0.1f},
};

// IdolStandard: Classic idol pop - memorable melody, gradual energy build
// Structure: Intro(4) -> A(8) -> B(8) -> Chorus(8) -> A(8) -> B(8) -> Chorus(8)
//            -> Bridge(8) -> LastChorus(16) -> Outro(4) = 80 bars
constexpr SectionSlot IDOL_STANDARD_FLOW[] = {
    // Intro: kick only, building anticipation
    {SectionType::Intro, 4, TrackMask::Drums, EntryPattern::Immediate, SectionEnergy::Low, 60, 50,
     PeakLevel::None, DrumRole::Minimal},

    // A melody: vocal with light backing
    {SectionType::A, 8, TrackMask::Vocal | TrackMask::Drums | TrackMask::Chord,
     EntryPattern::GradualBuild, SectionEnergy::Low, 65, 60, PeakLevel::None, DrumRole::Minimal},

    // B melody: add bass, rising energy
    {SectionType::B, 8, TrackMask::Vocal | TrackMask::Drums | TrackMask::Bass | TrackMask::Chord,
     EntryPattern::GradualBuild, SectionEnergy::Medium, 72, 75, PeakLevel::None, DrumRole::Full},

    // First Chorus: full arrangement
    {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, SectionEnergy::High, 82, 90,
     PeakLevel::None, DrumRole::Full},

    // 2nd A melody: slightly fuller
    {SectionType::A, 8, TrackMask::Vocal | TrackMask::Drums | TrackMask::Chord | TrackMask::Bass,
     EntryPattern::Immediate, SectionEnergy::Medium, 68, 65, PeakLevel::None, DrumRole::Full},

    // 2nd B melody: build up
    {SectionType::B, 8, TrackMask::Vocal | TrackMask::Drums | TrackMask::Bass | TrackMask::Chord,
     EntryPattern::GradualBuild, SectionEnergy::High, 75, 80, PeakLevel::None, DrumRole::Full},

    // 2nd Chorus: fuller, medium peak
    {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, SectionEnergy::High, 85, 95,
     PeakLevel::Medium, DrumRole::Full},

    // Bridge: emotional pause (Transitional modifier for preparation)
    {SectionType::Bridge, 8, TrackMask::Vocal | TrackMask::Chord | TrackMask::Drums,
     EntryPattern::Immediate, SectionEnergy::Medium, 70, 70, PeakLevel::None, DrumRole::Minimal,
     -1.0f, SectionModifier::Transitional, 100},

    // Last Chorus: maximum peak, extended (Climactic modifier)
    {SectionType::Chorus, 16, TrackMask::All, EntryPattern::DropIn, SectionEnergy::Peak, 95, 100,
     PeakLevel::Max, DrumRole::Full, -1.0f, SectionModifier::Climactic, 100},

    // Outro: fade out
    {SectionType::Outro, 4, TrackMask::Drums | TrackMask::Chord, EntryPattern::Immediate,
     SectionEnergy::Low, 60, 50, PeakLevel::None, DrumRole::Ambient},
};

// IdolHyper: High-energy idol pop - chorus-first, high BPM, dense arrangement
// Structure: Intro(2) -> Chorus(8) -> A(4) -> Chorus(8) -> B(4) -> Chorus(8)
//            -> Drop(4) -> LastChorus(16) = 54 bars
// Strong swing (0.5f) for high energy shuffle feel
constexpr SectionSlot IDOL_HYPER_FLOW[] = {
    // Intro: immediate high energy, very short
    {SectionType::Intro, 2, TrackMask::All, EntryPattern::DropIn, SectionEnergy::High, 85, 90,
     PeakLevel::None, DrumRole::Full, 0.5f},

    // First Chorus: hook immediately (chorus-first structure)
    {SectionType::Chorus, 8, TrackMask::All, EntryPattern::Immediate, SectionEnergy::Peak, 90, 100,
     PeakLevel::None, DrumRole::Full, 0.5f},

    // A melody: brief verse, high density maintained
    {SectionType::A, 4, TrackMask::All, EntryPattern::Immediate, SectionEnergy::High, 82, 85,
     PeakLevel::None, DrumRole::Full, 0.45f},

    // 2nd Chorus: keep momentum
    {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, SectionEnergy::Peak, 92, 100,
     PeakLevel::None, DrumRole::Full, 0.5f},

    // B melody: brief, building tension
    {SectionType::B, 4, TrackMask::All, EntryPattern::GradualBuild, SectionEnergy::High, 85, 90,
     PeakLevel::None, DrumRole::Full, 0.45f},

    // 3rd Chorus: pre-drop energy
    {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, SectionEnergy::Peak, 93, 100,
     PeakLevel::Medium, DrumRole::Full, 0.55f},

    // Drop: tension release (MixBreak = tension pause)
    {SectionType::MixBreak, 4, TrackMask::Vocal | TrackMask::Drums, EntryPattern::Immediate,
     SectionEnergy::High, 80, 70, PeakLevel::None, DrumRole::Ambient, 0.3f},

    // Last Chorus: explosive finale
    {SectionType::Chorus, 16, TrackMask::All, EntryPattern::DropIn, SectionEnergy::Peak, 95, 100,
     PeakLevel::Max, DrumRole::Full, 0.55f},
};

// IdolKawaii: Sweet, bouncy idol pop - restrained dynamics, cute vibe
// Structure: Intro(4) -> A(8) -> Chorus(8) -> A(8) -> Chorus(8) -> CuteBreak(4)
//            -> LastChorus(12) = 52 bars
constexpr SectionSlot IDOL_KAWAII_FLOW[] = {
    // Intro: soft, cute opening
    {SectionType::Intro, 4, TrackMask::Chord | TrackMask::Drums, EntryPattern::Immediate,
     SectionEnergy::Low, 55, 50, PeakLevel::None, DrumRole::Minimal},

    // A melody: sweet vocals with light accompaniment
    {SectionType::A, 8, TrackMask::Vocal | TrackMask::Drums | TrackMask::Chord,
     EntryPattern::Immediate, SectionEnergy::Low, 60, 60, PeakLevel::None, DrumRole::Minimal},

    // First Chorus: bouncy but restrained
    {SectionType::Chorus, 8,
     TrackMask::Vocal | TrackMask::Drums | TrackMask::Chord | TrackMask::Bass, EntryPattern::DropIn,
     SectionEnergy::Medium, 70, 75, PeakLevel::None, DrumRole::Minimal},

    // 2nd A melody: slightly fuller
    {SectionType::A, 8, TrackMask::Vocal | TrackMask::Drums | TrackMask::Chord | TrackMask::Bass,
     EntryPattern::Immediate, SectionEnergy::Medium, 65, 65, PeakLevel::None, DrumRole::Minimal},

    // 2nd Chorus: more energy but still cute
    {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, SectionEnergy::High, 75, 80,
     PeakLevel::None, DrumRole::Full},

    // Cute Break: very soft interlude (using Interlude for CuteBreak)
    {SectionType::Interlude, 4, TrackMask::Chord | TrackMask::Vocal, EntryPattern::Immediate,
     SectionEnergy::Low, 55, 50, PeakLevel::None, DrumRole::Minimal},

    // Last Chorus: peak moment but still kawaii
    {SectionType::Chorus, 12, TrackMask::All, EntryPattern::DropIn, SectionEnergy::High, 80, 85,
     PeakLevel::Max, DrumRole::Full},
};

// IdolCoolPop: Cool, stylish idol pop - four-on-floor, uniform dynamics
// Structure: Intro(8) -> A(8) -> Chorus(8) -> B(8) -> Chorus(8) -> DanceBreak(8)
//            -> LastChorus(16) = 64 bars
// Straight timing (0.0f) for tight four-on-floor dance feel
constexpr SectionSlot IDOL_COOLPOP_FLOW[] = {
    // Intro: driving four-on-floor beat with staggered entry
    {SectionType::Intro, 8, TrackMask::All, EntryPattern::Stagger, SectionEnergy::Medium, 75, 80,
     PeakLevel::None, DrumRole::Full, 0.0f},

    // A melody: cool, steady groove
    {SectionType::A, 8, TrackMask::All, EntryPattern::Immediate, SectionEnergy::Medium, 78, 85,
     PeakLevel::None, DrumRole::Full, 0.0f},

    // First Chorus: powerful but controlled
    {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, SectionEnergy::High, 85, 90,
     PeakLevel::None, DrumRole::Full, 0.0f},

    // B melody: variation while maintaining groove
    {SectionType::B, 8, TrackMask::All, EntryPattern::Immediate, SectionEnergy::Medium, 80, 85,
     PeakLevel::None, DrumRole::Full, 0.0f},

    // 2nd Chorus: stronger, medium peak
    {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, SectionEnergy::High, 88, 95,
     PeakLevel::Medium, DrumRole::Full, 0.0f},

    // Dance Break: high energy instrumental (using Interlude)
    {SectionType::Interlude, 8, TrackMask::Drums | TrackMask::Bass | TrackMask::Arpeggio,
     EntryPattern::Immediate, SectionEnergy::High, 85, 95, PeakLevel::None, DrumRole::Full, 0.0f},

    // Last Chorus: climactic finish
    {SectionType::Chorus, 16, TrackMask::All, EntryPattern::DropIn, SectionEnergy::Peak, 92, 100,
     PeakLevel::Max, DrumRole::Full, 0.0f},
};

// IdolEmo: Emotional idol pop - quiet start, explosive finish
// Structure: Intro(4) -> A(8) -> B(8) -> Chorus(8) -> QuietA(4) -> Build(8)
//            -> LastChorus(16) -> Outro(4) = 60 bars
constexpr SectionSlot IDOL_EMO_FLOW[] = {
    // Intro: soft, contemplative
    {SectionType::Intro, 4, TrackMask::Chord, EntryPattern::Immediate, SectionEnergy::Low, 55, 50,
     PeakLevel::None, DrumRole::Ambient},

    // A melody: quiet, intimate vocals
    {SectionType::A, 8, TrackMask::Vocal | TrackMask::Chord, EntryPattern::Immediate,
     SectionEnergy::Low, 58, 55, PeakLevel::None, DrumRole::Ambient},

    // B melody: slowly building emotion
    {SectionType::B, 8, TrackMask::Vocal | TrackMask::Chord | TrackMask::Bass,
     EntryPattern::GradualBuild, SectionEnergy::Medium, 65, 65, PeakLevel::None, DrumRole::Minimal},

    // First Chorus: breakthrough, but still building
    {SectionType::Chorus, 8,
     TrackMask::Vocal | TrackMask::Drums | TrackMask::Chord | TrackMask::Bass, EntryPattern::DropIn,
     SectionEnergy::High, 78, 80, PeakLevel::None, DrumRole::Full},

    // Quiet A (Ochisabi): return to intimacy - "falling sabi" moment
    {SectionType::A, 4, TrackMask::Vocal | TrackMask::Chord, EntryPattern::Immediate,
     SectionEnergy::Low, 55, 50, PeakLevel::None, DrumRole::Ambient, -1.0f,
     SectionModifier::Ochisabi, 100},

    // Build: tension rising (using B for GradualBuild)
    {SectionType::B, 8, TrackMask::All, EntryPattern::GradualBuild, SectionEnergy::High, 75, 85,
     PeakLevel::None, DrumRole::Full},

    // Last Chorus: emotional explosion (Climactic modifier)
    {SectionType::Chorus, 16, TrackMask::All, EntryPattern::DropIn, SectionEnergy::Peak, 95, 100,
     PeakLevel::Max, DrumRole::Full, -1.0f, SectionModifier::Climactic, 100},

    // Outro: lingering emotion
    {SectionType::Outro, 4, TrackMask::Chord | TrackMask::Vocal, EntryPattern::Immediate,
     SectionEnergy::Low, 55, 50, PeakLevel::None, DrumRole::Ambient},
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
        false,  // addictive_mode
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
        false,  // addictive_mode
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
        false,  // addictive_mode
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
        false,  // addictive_mode
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
        false,  // addictive_mode
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
        false,  // addictive_mode
    },

    // 6: IdolKawaii (sweet, bouncy, restrained)
    {
        "IdolKawaii",
        5,  // weight: 5%
        GenerationParadigm::MelodyDriven, IDOL_KAWAII_FLOW,
        static_cast<uint8_t>(sizeof(IDOL_KAWAII_FLOW) / sizeof(IDOL_KAWAII_FLOW[0])),
        RiffPolicy::Locked,
        true,   // drums_sync_vocal
        true,   // drums_required (drums_sync_vocal needs drums)
        false,  // intro_kick
        false,  // intro_bass
        40,     // intro_stagger_percent
        25,     // euclidean_drums_percent (simple bouncy patterns)
        false,  // addictive_mode
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
        false,  // addictive_mode
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
        false,  // addictive_mode
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
        true,   // addictive_mode - enables Behavioral Loop
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
  std::uniform_int_distribution<uint32_t> dist(0, total_weight - 1);
  uint32_t roll = dist(rng);

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

}  // namespace midisketch
