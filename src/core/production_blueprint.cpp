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

// Orangestar-style section flow: rhythm-synced, minimal intro
constexpr SectionSlot ORANGESTAR_FLOW[] = {
  // Intro: drums only, atmospheric
  {SectionType::Intro, 4, TrackMask::Drums, EntryPattern::Immediate, false},

  // A melody: vocal + minimal backing
  {SectionType::A, 8,
   TrackMask::Vocal | TrackMask::Drums | TrackMask::Bass,
   EntryPattern::GradualBuild, false},

  // B melody: add chord
  {SectionType::B, 8,
   TrackMask::Vocal | TrackMask::Drums | TrackMask::Bass | TrackMask::Chord,
   EntryPattern::Immediate, false},

  // Chorus: full arrangement, strong entry
  {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, true},

  // Interlude: drums solo
  {SectionType::Interlude, 4, TrackMask::Drums, EntryPattern::Immediate, false},

  // 2nd A melody
  {SectionType::A, 8,
   TrackMask::Vocal | TrackMask::Drums | TrackMask::Bass,
   EntryPattern::Immediate, false},

  // 2nd B melody
  {SectionType::B, 8,
   TrackMask::Vocal | TrackMask::Drums | TrackMask::Bass | TrackMask::Chord,
   EntryPattern::GradualBuild, false},

  // 2nd Chorus
  {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, true},

  // Drop chorus: vocal solo (dramatic pause)
  {SectionType::Chorus, 4, TrackMask::Vocal, EntryPattern::Immediate, false},

  // Last chorus: everyone drops in
  {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, true},

  // Outro: fade out with drums + bass
  {SectionType::Outro, 4,
   TrackMask::Drums | TrackMask::Bass,
   EntryPattern::Immediate, false},
};

// YOASOBI-style section flow: melody-driven, full arrangement
constexpr SectionSlot YOASOBI_FLOW[] = {
  // Intro: full arrangement
  {SectionType::Intro, 4, TrackMask::All, EntryPattern::Immediate, false},

  // A melody: full
  {SectionType::A, 8, TrackMask::All, EntryPattern::Immediate, false},

  // B melody: build up
  {SectionType::B, 8, TrackMask::All, EntryPattern::GradualBuild, false},

  // Chorus: strong entry
  {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, true},

  // 2nd A melody
  {SectionType::A, 8, TrackMask::All, EntryPattern::Immediate, false},

  // 2nd B melody
  {SectionType::B, 8, TrackMask::All, EntryPattern::GradualBuild, false},

  // 2nd Chorus
  {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, true},

  // Bridge: thinner arrangement
  {SectionType::Bridge, 8,
   TrackMask::Vocal | TrackMask::Chord | TrackMask::Drums,
   EntryPattern::Immediate, false},

  // Last chorus
  {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, true},

  // Outro
  {SectionType::Outro, 4, TrackMask::All, EntryPattern::Immediate, false},
};

// Ballad-style section flow: gradual build, sparse intro
constexpr SectionSlot BALLAD_FLOW[] = {
  // Intro: chord only (piano feel)
  {SectionType::Intro, 4, TrackMask::Chord, EntryPattern::Immediate, false},

  // A melody: vocal + chord
  {SectionType::A, 8,
   TrackMask::Vocal | TrackMask::Chord,
   EntryPattern::Immediate, false},

  // B melody: add bass
  {SectionType::B, 8,
   TrackMask::Vocal | TrackMask::Chord | TrackMask::Bass,
   EntryPattern::GradualBuild, false},

  // Chorus: add drums (gentle)
  {SectionType::Chorus, 8, TrackMask::Basic, EntryPattern::GradualBuild, false},

  // Interlude: chord only
  {SectionType::Interlude, 4, TrackMask::Chord, EntryPattern::Immediate, false},

  // 2nd A melody
  {SectionType::A, 8,
   TrackMask::Vocal | TrackMask::Chord,
   EntryPattern::Immediate, false},

  // 2nd B melody
  {SectionType::B, 8,
   TrackMask::Vocal | TrackMask::Chord | TrackMask::Bass,
   EntryPattern::GradualBuild, false},

  // 2nd Chorus: fuller
  {SectionType::Chorus, 8, TrackMask::All, EntryPattern::GradualBuild, false},

  // Last chorus: emotional peak
  {SectionType::Chorus, 8, TrackMask::All, EntryPattern::DropIn, true},

  // Outro: fade out with chord
  {SectionType::Outro, 8, TrackMask::Chord, EntryPattern::Immediate, false},
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
    60,  // weight: 60% - most common
    GenerationParadigm::Traditional,
    nullptr, 0,  // Use existing StructurePattern
    RiffPolicy::Free,
    false,  // drums_sync_vocal
    true,   // intro_kick
    true,   // intro_bass
  },

  // 1: Orangestar (rhythm-synced)
  {
    "Orangestar",
    20,  // weight: 20%
    GenerationParadigm::RhythmSync,
    ORANGESTAR_FLOW,
    static_cast<uint8_t>(sizeof(ORANGESTAR_FLOW) / sizeof(ORANGESTAR_FLOW[0])),
    RiffPolicy::Locked,
    true,   // drums_sync_vocal
    false,  // intro_kick (no kick in intro)
    false,  // intro_bass (no bass in intro)
  },

  // 2: YOASOBI (melody-driven)
  {
    "YOASOBI",
    15,  // weight: 15%
    GenerationParadigm::MelodyDriven,
    YOASOBI_FLOW,
    static_cast<uint8_t>(sizeof(YOASOBI_FLOW) / sizeof(YOASOBI_FLOW[0])),
    RiffPolicy::Evolving,
    false,  // drums_sync_vocal
    true,   // intro_kick
    true,   // intro_bass
  },

  // 3: Ballad (sparse, emotional)
  {
    "Ballad",
    5,  // weight: 5%
    GenerationParadigm::MelodyDriven,
    BALLAD_FLOW,
    static_cast<uint8_t>(sizeof(BALLAD_FLOW) / sizeof(BALLAD_FLOW[0])),
    RiffPolicy::Free,
    false,  // drums_sync_vocal
    false,  // intro_kick
    false,  // intro_bass
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

uint8_t getProductionBlueprintCount() {
  return BLUEPRINT_COUNT;
}

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
