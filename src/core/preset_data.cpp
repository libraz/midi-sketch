#include "core/preset_data.h"

namespace midisketch {

namespace {

// Mood default BPM values
constexpr uint16_t MOOD_BPM[16] = {
    120,  // StraightPop
    130,  // BrightUpbeat
    140,  // EnergeticDance
    125,  // LightRock
    110,  // MidPop
    105,  // EmotionalPop
    95,   // Sentimental
    90,   // Chill
    75,   // Ballad
    115,  // DarkPop
    100,  // Dramatic
    105,  // Nostalgic
    125,  // ModernPop
    135,  // ElectroPop
    145,  // IdolPop
    130,  // Anthem
};

// Mood note density values
constexpr float MOOD_DENSITY[16] = {
    0.60f,  // StraightPop
    0.70f,  // BrightUpbeat
    0.80f,  // EnergeticDance
    0.65f,  // LightRock
    0.50f,  // MidPop
    0.55f,  // EmotionalPop
    0.40f,  // Sentimental
    0.35f,  // Chill
    0.30f,  // Ballad
    0.60f,  // DarkPop
    0.50f,  // Dramatic
    0.45f,  // Nostalgic
    0.65f,  // ModernPop
    0.75f,  // ElectroPop
    0.80f,  // IdolPop
    0.70f,  // Anthem
};

// Structure pattern names
const char* STRUCTURE_NAMES[10] = {
    "StandardPop",
    "BuildUp",
    "DirectChorus",
    "RepeatChorus",
    "ShortForm",
    "FullPop",
    "FullWithBridge",
    "DriveUpbeat",
    "Ballad",
    "AnthemStyle",
};

// Mood names
const char* MOOD_NAMES[16] = {
    "straight_pop",
    "bright_upbeat",
    "energetic_dance",
    "light_rock",
    "mid_pop",
    "emotional_pop",
    "sentimental",
    "chill",
    "ballad",
    "dark_pop",
    "dramatic",
    "nostalgic",
    "modern_pop",
    "electro_pop",
    "idol_pop",
    "anthem",
};

}  // namespace

uint16_t getMoodDefaultBpm(Mood mood) {
  uint8_t idx = static_cast<uint8_t>(mood);
  if (idx < 16) {
    return MOOD_BPM[idx];
  }
  return 120;  // fallback
}

float getMoodDensity(Mood mood) {
  uint8_t idx = static_cast<uint8_t>(mood);
  if (idx < 16) {
    return MOOD_DENSITY[idx];
  }
  return 0.5f;  // fallback
}

const char* getStructureName(StructurePattern pattern) {
  uint8_t idx = static_cast<uint8_t>(pattern);
  if (idx < 10) {
    return STRUCTURE_NAMES[idx];
  }
  return "Unknown";
}

const char* getMoodName(Mood mood) {
  uint8_t idx = static_cast<uint8_t>(mood);
  if (idx < 16) {
    return MOOD_NAMES[idx];
  }
  return "unknown";
}

DrumStyle getMoodDrumStyle(Mood mood) {
  switch (mood) {
    // Sparse - slow, minimal patterns
    case Mood::Ballad:
    case Mood::Sentimental:
    case Mood::Chill:
      return DrumStyle::Sparse;

    // FourOnFloor - dance patterns
    case Mood::EnergeticDance:
    case Mood::ElectroPop:
    case Mood::IdolPop:
      return DrumStyle::FourOnFloor;

    // Upbeat - driving patterns
    case Mood::BrightUpbeat:
    case Mood::ModernPop:
    case Mood::Anthem:
      return DrumStyle::Upbeat;

    // Rock - rock patterns
    case Mood::LightRock:
      return DrumStyle::Rock;

    // Standard - default pop patterns
    case Mood::StraightPop:
    case Mood::MidPop:
    case Mood::EmotionalPop:
    case Mood::DarkPop:
    case Mood::Dramatic:
    case Mood::Nostalgic:
    default:
      return DrumStyle::Standard;
  }
}

}  // namespace midisketch
