#include "core/preset_data.h"
#include "core/chord.h"
#include <cmath>
#include <random>

namespace midisketch {

namespace {

// Mood default BPM values
constexpr uint16_t MOOD_BPM[20] = {
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
    // New synth-oriented moods
    148,  // Yoasobi - fast anime-style
    118,  // Synthwave - moderate retro
    145,  // FutureBass - fast EDM
    110,  // CityPop - moderate groove
};

// Mood note density values
constexpr float MOOD_DENSITY[20] = {
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
    // New synth-oriented moods
    0.75f,  // Yoasobi - high density melody
    0.55f,  // Synthwave - moderate
    0.70f,  // FutureBass - high density
    0.60f,  // CityPop - moderate groove
};

// Structure pattern names
const char* STRUCTURE_NAMES[11] = {
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
    "ExtendedFull",
};

// Mood names
const char* MOOD_NAMES[20] = {
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
    "yoasobi",
    "synthwave",
    "future_bass",
    "city_pop",
};

// Style preset definitions (17 presets)
const StylePreset STYLE_PRESETS[17] = {
    // ========== Pop/Dance (0-2) ==========
    // 0: Minimal Groove Pop
    {
        0,
        "minimal_groove_pop",
        "Minimal Groove Pop",
        "Repetitive 2-4 chord loops, simple melody",
        StructurePattern::StandardPop,
        118, 128, 122,
        VocalAttitude::Clean,
        ATTITUDE_CLEAN | ATTITUDE_EXPRESSIVE,
        {0, 1, 6, 13, 17, 19, 20, -1},
        {5, true, 0.9f, 0.1f, 0.65f, 8, 0.0f},  // Standard density
        {8, 0.7f, 0.2f},
        {true, 2, 1},
        1
    },
    // 1: Dance Pop Emotion
    {
        1,
        "dance_pop_emotion",
        "Dance Pop Emotion",
        "Classic structure, emotional chorus release",
        StructurePattern::FullPop,
        120, 135, 128,
        VocalAttitude::Expressive,
        ATTITUDE_CLEAN | ATTITUDE_EXPRESSIVE,
        {0, 1, 2, 4, 5, 14, 15, -1},
        {7, true, 0.8f, 0.3f, 0.75f, 8, 0.1f},  // Medium-high density for dance
        {8, 0.5f, 0.4f},
        {true, 2, 2},
        2
    },
    // 2: Bright Pop
    {
        2,
        "bright_pop",
        "Bright Pop",
        "Upbeat, memorable melodies with simple structure",
        StructurePattern::StandardPop,
        125, 145, 135,
        VocalAttitude::Clean,
        ATTITUDE_CLEAN,
        {0, 1, 3, 5, -1, -1, -1, -1},
        {4, false, 0.95f, 0.05f, 0.80f, 8, 0.15f},  // Higher density for upbeat
        {4, 0.85f, 0.1f},
        {true, 2, 1},
        1
    },
    // ========== Idol (3-6) ==========
    // 3: Idol Standard
    {
        3,
        "idol_standard",
        "Idol Standard",
        "Unison-friendly, memorable melodies",
        StructurePattern::StandardPop,
        130, 150, 140,
        VocalAttitude::Clean,
        ATTITUDE_CLEAN,
        {0, 1, 3, 4, 5, 6, 9, -1},
        {4, false, 0.95f, 0.05f, 0.85f, 8, 0.25f},  // High density idol
        {8, 0.8f, 0.1f},
        {true, 3, 0},
        2
    },
    // 4: Idol Emotion
    {
        4,
        "idol_emotion",
        "Idol Emotion",
        "Emotional idol songs with building pre-chorus",
        StructurePattern::FullPop,
        120, 140, 130,
        VocalAttitude::Expressive,
        ATTITUDE_CLEAN | ATTITUDE_EXPRESSIVE,
        {0, 1, 2, 5, 15, 16, 18, 21},
        {6, true, 0.85f, 0.25f, 0.80f, 8, 0.20f},  // Medium-high density
        {8, 0.6f, 0.3f},
        {true, 2, 1},
        2
    },
    // 5: Idol Energy
    {
        5,
        "idol_energy",
        "Idol Energy",
        "High-energy idol songs for live performances",
        StructurePattern::DriveUpbeat,
        140, 160, 150,
        VocalAttitude::Clean,
        ATTITUDE_CLEAN,
        {0, 2, 4, 6, 9, -1, -1, -1},
        {5, false, 0.9f, 0.1f, 0.90f, 8, 0.30f},  // Very high density for energy
        {4, 0.8f, 0.15f},
        {true, 3, 2},
        3
    },
    // 6: Idol Minimal
    {
        6,
        "idol_minimal",
        "Idol Minimal",
        "Minimal idol songs for short-form content",
        StructurePattern::ShortForm,
        125, 145, 135,
        VocalAttitude::Clean,
        ATTITUDE_CLEAN,
        {0, 6, -1, -1, -1, -1, -1, -1},
        {3, false, 0.95f, 0.02f, 0.75f, 8, 0.15f},  // Medium density
        {4, 0.9f, 0.05f},
        {true, 2, 0},
        1
    },
    // ========== Rock/Emo (7-9) ==========
    // 7: Rock Shout
    {
        7,
        "rock_shout",
        "Rock Shout",
        "Aggressive vocals with raw expression",
        StructurePattern::FullPop,
        115, 135, 125,
        VocalAttitude::Expressive,
        ATTITUDE_CLEAN | ATTITUDE_EXPRESSIVE | ATTITUDE_RAW,
        {4, 5, 6, 10, 11, 12, -1, -1},
        {9, true, 0.6f, 0.5f, 0.70f, 8, 0.10f},  // Medium density
        {8, 0.4f, 0.5f},
        {true, 3, 3},
        2
    },
    // 8: Pop Emotion
    {
        8,
        "pop_emotion",
        "Pop Emotion",
        "Word-driven emotional pop with lyrical focus",
        StructurePattern::FullPop,
        95, 120, 108,
        VocalAttitude::Expressive,
        ATTITUDE_EXPRESSIVE | ATTITUDE_RAW,
        {1, 5, 8, 15, 16, 18, -1, -1},
        {7, true, 0.7f, 0.4f, 0.60f, 8, 0.05f},  // Lower density for emotional
        {8, 0.4f, 0.5f},
        {true, 1, 1},
        1
    },
    // 9: Raw Emotional
    {
        9,
        "raw_emotional",
        "Raw Emotional",
        "Intense emotional expression with boundary-breaking phrases",
        StructurePattern::FullWithBridge,
        90, 115, 102,
        VocalAttitude::Expressive,
        ATTITUDE_EXPRESSIVE | ATTITUDE_RAW,
        {7, 8, 11, 15, 21, -1, -1, -1},
        {9, true, 0.5f, 0.6f, 0.55f, 8, 0.0f},  // Lower density, no 16th
        {8, 0.3f, 0.6f},
        {true, 2, 2},
        2
    },
    // ========== Special/Derived (10-12) ==========
    // 10: Acoustic Pop
    {
        10,
        "acoustic_pop",
        "Acoustic Pop",
        "Clear harmony, rhythm-light, vocal-centered",
        StructurePattern::Ballad,
        85, 110, 95,
        VocalAttitude::Expressive,
        ATTITUDE_CLEAN | ATTITUDE_EXPRESSIVE,
        {0, 1, 4, 6, 17, -1, -1, -1},
        {6, true, 0.85f, 0.2f, 0.50f, 4, 0.0f},  // Low density ballad
        {8, 0.5f, 0.3f},
        {false, 1, 0},
        0
    },
    // 11: Live Call & Response
    {
        11,
        "live_call_response",
        "Live Call & Response",
        "Concert-ready with call-response structure",
        StructurePattern::AnthemStyle,
        130, 150, 140,
        VocalAttitude::Clean,
        ATTITUDE_CLEAN,
        {0, 2, 4, -1, -1, -1, -1, -1},
        {4, false, 0.9f, 0.05f, 0.85f, 8, 0.20f},  // High density for live
        {4, 0.85f, 0.1f},
        {true, 3, 1},
        3
    },
    // 12: Background Motif
    {
        12,
        "background_motif",
        "Background Motif",
        "Motif-driven with subdued vocals, ambient feel",
        StructurePattern::StandardPop,
        110, 130, 120,
        VocalAttitude::Clean,
        ATTITUDE_CLEAN,
        {0, 6, 13, 19, -1, -1, -1, -1},
        {4, false, 0.95f, 0.05f, 0.50f, 8, 0.0f},  // Low density, subdued
        {8, 0.9f, 0.05f},
        {true, 2, 0},
        1
    },
    // ========== Genre-Specific (13-16) ==========
    // 13: City Pop
    {
        13,
        "city_pop",
        "City Pop",
        "Groovy 80s Japanese city pop with jazzy chords",
        StructurePattern::FullPop,
        95, 115, 105,
        VocalAttitude::Clean,
        ATTITUDE_CLEAN | ATTITUDE_EXPRESSIVE,
        {7, 8, 10, 1, 5, -1, -1, -1},
        {7, true, 0.75f, 0.35f, 0.65f, 8, 0.05f},  // Medium density groove
        {8, 0.5f, 0.4f},
        {true, 2, 1},
        2
    },
    // 14: Anime Opening
    {
        14,
        "anime_opening",
        "Anime Opening",
        "Epic, dramatic anime OP style with building energy",
        StructurePattern::BuildUp,
        130, 155, 142,
        VocalAttitude::Expressive,
        ATTITUDE_CLEAN | ATTITUDE_EXPRESSIVE,
        {5, 2, 6, 0, 4, -1, -1, -1},
        {6, true, 0.8f, 0.3f, 0.85f, 8, 0.25f},  // High density anime style
        {8, 0.6f, 0.3f},
        {true, 3, 2},
        2
    },
    // 15: EDM Synth Pop
    {
        15,
        "edm_synth",
        "EDM Synth Pop",
        "Modern electronic dance music with synth leads",
        StructurePattern::DirectChorus,
        125, 145, 138,
        VocalAttitude::Clean,
        ATTITUDE_CLEAN | ATTITUDE_EXPRESSIVE,
        {0, 4, 9, 1, -1, -1, -1, -1},
        {5, false, 0.85f, 0.15f, 0.75f, 16, 0.35f},  // High 16th ratio for EDM
        {4, 0.75f, 0.2f},
        {true, 2, 2},
        2
    },
    // 16: Emotional Ballad
    {
        16,
        "emotional_ballad",
        "Emotional Ballad",
        "Slow emotional ballad with expressive vocals",
        StructurePattern::Ballad,
        65, 85, 72,
        VocalAttitude::Expressive,
        ATTITUDE_CLEAN | ATTITUDE_EXPRESSIVE,
        {1, 3, 8, 11, 5, -1, -1, -1},
        {8, true, 0.7f, 0.4f, 0.45f, 4, 0.0f},  // Very low density ballad
        {8, 0.4f, 0.5f},
        {false, 1, 0},
        0
    },
};

// Form compatibility by style with weights (higher = more likely)
// Weight guidelines: Common=40-60, Frequent=20-35, Occasional=10-20, Rare=3-10
const FormWeight STYLE_FORMS_WEIGHTED[17][5] = {
    // 0: Minimal Groove Pop - shorter forms
    {{{StructurePattern::StandardPop}, 50}, {{StructurePattern::DirectChorus}, 25},
     {{StructurePattern::ShortForm}, 12}, {{StructurePattern::RepeatChorus}, 8},
     {{StructurePattern::BuildUp}, 5}},
    // 1: Dance Pop Emotion - full-length forms
    {{{StructurePattern::FullPop}, 45}, {{StructurePattern::FullWithBridge}, 25},
     {{StructurePattern::DriveUpbeat}, 15}, {{StructurePattern::Ballad}, 10},
     {{StructurePattern::AnthemStyle}, 5}},
    // 2: Bright Pop - standard forms
    {{{StructurePattern::StandardPop}, 45}, {{StructurePattern::DirectChorus}, 25},
     {{StructurePattern::RepeatChorus}, 15}, {{StructurePattern::BuildUp}, 10},
     {{StructurePattern::FullPop}, 5}},
    // 3: Idol Standard - standard and anthem forms
    {{{StructurePattern::StandardPop}, 40}, {{StructurePattern::BuildUp}, 25},
     {{StructurePattern::RepeatChorus}, 15}, {{StructurePattern::AnthemStyle}, 12},
     {{StructurePattern::FullPop}, 8}},
    // 4: Idol Emotion - full-length emotional forms
    {{{StructurePattern::FullPop}, 45}, {{StructurePattern::FullWithBridge}, 25},
     {{StructurePattern::Ballad}, 15}, {{StructurePattern::StandardPop}, 10},
     {{StructurePattern::BuildUp}, 5}},
    // 5: Idol Energy - driving high-energy forms
    {{{StructurePattern::DriveUpbeat}, 40}, {{StructurePattern::AnthemStyle}, 30},
     {{StructurePattern::FullPop}, 15}, {{StructurePattern::RepeatChorus}, 10},
     {{StructurePattern::DirectChorus}, 5}},
    // 6: Idol Minimal - short forms only
    {{{StructurePattern::ShortForm}, 45}, {{StructurePattern::DirectChorus}, 30},
     {{StructurePattern::StandardPop}, 15}, {{StructurePattern::RepeatChorus}, 7},
     {{StructurePattern::BuildUp}, 3}},
    // 7: Rock Shout - driving forms
    {{{StructurePattern::FullPop}, 40}, {{StructurePattern::DriveUpbeat}, 25},
     {{StructurePattern::AnthemStyle}, 18}, {{StructurePattern::BuildUp}, 12},
     {{StructurePattern::FullWithBridge}, 5}},
    // 8: Pop Emotion - emotional full forms
    {{{StructurePattern::FullPop}, 45}, {{StructurePattern::FullWithBridge}, 25},
     {{StructurePattern::Ballad}, 15}, {{StructurePattern::StandardPop}, 10},
     {{StructurePattern::BuildUp}, 5}},
    // 9: Raw Emotional - varied emotional forms
    {{{StructurePattern::FullWithBridge}, 40}, {{StructurePattern::FullPop}, 25},
     {{StructurePattern::Ballad}, 18}, {{StructurePattern::BuildUp}, 12},
     {{StructurePattern::DriveUpbeat}, 5}},
    // 10: Acoustic Pop - ballad and simple forms
    {{{StructurePattern::Ballad}, 50}, {{StructurePattern::StandardPop}, 22},
     {{StructurePattern::FullWithBridge}, 15}, {{StructurePattern::ShortForm}, 8},
     {{StructurePattern::BuildUp}, 5}},
    // 11: Live Call & Response - anthem and driving forms
    {{{StructurePattern::AnthemStyle}, 45}, {{StructurePattern::DriveUpbeat}, 25},
     {{StructurePattern::RepeatChorus}, 15}, {{StructurePattern::FullPop}, 10},
     {{StructurePattern::DirectChorus}, 5}},
    // 12: Background Motif - repetitive forms
    {{{StructurePattern::StandardPop}, 45}, {{StructurePattern::RepeatChorus}, 25},
     {{StructurePattern::ShortForm}, 15}, {{StructurePattern::DirectChorus}, 10},
     {{StructurePattern::BuildUp}, 5}},
    // 13: City Pop - groovy full forms
    {{{StructurePattern::FullPop}, 45}, {{StructurePattern::FullWithBridge}, 25},
     {{StructurePattern::StandardPop}, 15}, {{StructurePattern::BuildUp}, 10},
     {{StructurePattern::Ballad}, 5}},
    // 14: Anime Opening - build-up and driving forms
    {{{StructurePattern::BuildUp}, 45}, {{StructurePattern::DriveUpbeat}, 25},
     {{StructurePattern::FullPop}, 15}, {{StructurePattern::AnthemStyle}, 10},
     {{StructurePattern::FullWithBridge}, 5}},
    // 15: EDM Synth Pop - direct and driving forms
    {{{StructurePattern::DirectChorus}, 45}, {{StructurePattern::DriveUpbeat}, 25},
     {{StructurePattern::RepeatChorus}, 15}, {{StructurePattern::BuildUp}, 10},
     {{StructurePattern::FullPop}, 5}},
    // 16: Emotional Ballad - slow emotional forms
    {{{StructurePattern::Ballad}, 55}, {{StructurePattern::FullWithBridge}, 22},
     {{StructurePattern::StandardPop}, 12}, {{StructurePattern::FullPop}, 8},
     {{StructurePattern::BuildUp}, 3}},
};

constexpr size_t STYLE_FORM_COUNT = 5;

}  // namespace

uint16_t getMoodDefaultBpm(Mood mood) {
  uint8_t idx = static_cast<uint8_t>(mood);
  constexpr size_t count = sizeof(MOOD_BPM) / sizeof(MOOD_BPM[0]);
  if (idx < count) {
    return MOOD_BPM[idx];
  }
  return 120;  // fallback
}

float getMoodDensity(Mood mood) {
  uint8_t idx = static_cast<uint8_t>(mood);
  constexpr size_t count = sizeof(MOOD_DENSITY) / sizeof(MOOD_DENSITY[0]);
  if (idx < count) {
    return MOOD_DENSITY[idx];
  }
  return 0.5f;  // fallback
}

const char* getStructureName(StructurePattern pattern) {
  uint8_t idx = static_cast<uint8_t>(pattern);
  constexpr size_t count = sizeof(STRUCTURE_NAMES) / sizeof(STRUCTURE_NAMES[0]);
  if (idx < count) {
    return STRUCTURE_NAMES[idx];
  }
  return "Unknown";
}

const char* getMoodName(Mood mood) {
  uint8_t idx = static_cast<uint8_t>(mood);
  constexpr size_t count = sizeof(MOOD_NAMES) / sizeof(MOOD_NAMES[0]);
  if (idx < count) {
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
      return DrumStyle::FourOnFloor;

    // Upbeat - driving patterns
    case Mood::IdolPop:
    case Mood::BrightUpbeat:
    case Mood::ModernPop:
    case Mood::Anthem:
      return DrumStyle::Upbeat;

    // Rock - rock patterns
    case Mood::LightRock:
      return DrumStyle::Rock;

    // Synth - synth-oriented patterns (tight 16th HH)
    case Mood::Yoasobi:
    case Mood::Synthwave:
    case Mood::FutureBass:
    case Mood::CityPop:
      return DrumStyle::Synth;

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

// ============================================================================
// StylePreset API Implementation
// ============================================================================

const StylePreset& getStylePreset(uint8_t style_id) {
  if (style_id >= STYLE_PRESET_COUNT) {
    style_id = 0;  // fallback to first preset
  }
  return STYLE_PRESETS[style_id];
}

std::vector<StructurePattern> getFormsByStyle(uint8_t style_id) {
  std::vector<StructurePattern> result;
  if (style_id >= STYLE_PRESET_COUNT) {
    style_id = 0;
  }
  for (size_t i = 0; i < STYLE_FORM_COUNT; ++i) {
    result.push_back(STYLE_FORMS_WEIGHTED[style_id][i].form);
  }
  return result;
}

StructurePattern selectRandomForm(uint8_t style_id, uint32_t seed) {
  if (style_id >= STYLE_PRESET_COUNT) {
    style_id = 0;
  }

  // Calculate total weight
  uint32_t total_weight = 0;
  for (size_t i = 0; i < STYLE_FORM_COUNT; ++i) {
    total_weight += STYLE_FORMS_WEIGHTED[style_id][i].weight;
  }

  // Use seed to generate a random value
  std::mt19937 rng(seed);
  std::uniform_int_distribution<uint32_t> dist(0, total_weight - 1);
  uint32_t roll = dist(rng);

  // Select form based on weighted random roll
  uint32_t cumulative = 0;
  for (size_t i = 0; i < STYLE_FORM_COUNT; ++i) {
    cumulative += STYLE_FORMS_WEIGHTED[style_id][i].weight;
    if (roll < cumulative) {
      return STYLE_FORMS_WEIGHTED[style_id][i].form;
    }
  }

  // Fallback: return default form
  return STYLE_FORMS_WEIGHTED[style_id][0].form;
}

SongConfig createDefaultSongConfig(uint8_t style_id) {
  SongConfig config;
  if (style_id >= STYLE_PRESET_COUNT) {
    style_id = 0;
  }

  const StylePreset& preset = STYLE_PRESETS[style_id];
  config.style_preset_id = style_id;
  config.key = Key::C;
  config.bpm = preset.tempo_default;
  config.seed = 0;  // random
  config.form = preset.default_form;
  config.vocal_attitude = preset.default_vocal_attitude;
  config.drums_enabled = true;
  config.arpeggio_enabled = false;
  config.vocal_low = 60;   // C4
  config.vocal_high = 79;  // G5
  config.humanize = false;

  // Pick first recommended progression
  if (preset.recommended_progressions[0] >= 0) {
    config.chord_progression_id = static_cast<uint8_t>(preset.recommended_progressions[0]);
  } else {
    config.chord_progression_id = 0;
  }

  return config;
}

SongConfigError validateSongConfig(const SongConfig& config) {
  // Validate style preset ID
  if (config.style_preset_id >= STYLE_PRESET_COUNT) {
    return SongConfigError::InvalidStylePreset;
  }

  // Validate chord progression ID
  if (config.chord_progression_id >= CHORD_COUNT) {
    return SongConfigError::InvalidChordProgression;
  }

  // Validate form
  if (static_cast<uint8_t>(config.form) >= STRUCTURE_COUNT) {
    return SongConfigError::InvalidForm;
  }

  // Validate vocal attitude against allowed attitudes
  const StylePreset& preset = STYLE_PRESETS[config.style_preset_id];
  uint8_t attitude_flag = 1 << static_cast<uint8_t>(config.vocal_attitude);
  if ((preset.allowed_vocal_attitudes & attitude_flag) == 0) {
    return SongConfigError::InvalidVocalAttitude;
  }

  // Validate vocal range
  if (config.vocal_low > config.vocal_high) {
    return SongConfigError::InvalidVocalRange;
  }
  if (config.vocal_low < 36 || config.vocal_high > 96) {
    return SongConfigError::InvalidVocalRange;
  }

  // Validate BPM (0 = use default, otherwise 40-240)
  if (config.bpm != 0 && (config.bpm < 40 || config.bpm > 240)) {
    return SongConfigError::InvalidBpm;
  }

  // Validate modulation amount
  if (config.modulation_timing != ModulationTiming::None) {
    if (config.modulation_semitones < 1 || config.modulation_semitones > 4) {
      return SongConfigError::InvalidModulationAmount;
    }
  }

  // Validate call/duration compatibility
  if (config.call_enabled && config.target_duration_seconds > 0) {
    uint16_t resolved_bpm = (config.bpm != 0) ? config.bpm : preset.tempo_default;
    uint16_t min_seconds = getMinimumSecondsForCall(
        config.intro_chant, config.mix_pattern, resolved_bpm);

    if (config.target_duration_seconds < min_seconds) {
      return SongConfigError::DurationTooShortForCall;
    }
  }

  return SongConfigError::OK;
}

// ============================================================================
// Call System Functions Implementation
// ============================================================================

uint8_t calcIntroChantBars(IntroChant chant, uint16_t bpm) {
  // Required seconds for each pattern
  constexpr float REQUIRED_SECONDS[] = {
      0.0f,   // None
      18.0f,  // Gachikoi (~18 sec)
      4.0f    // Shouting (~4 sec)
  };

  uint8_t idx = static_cast<uint8_t>(chant);
  if (idx >= sizeof(REQUIRED_SECONDS) / sizeof(REQUIRED_SECONDS[0])) {
    return 0;
  }

  float seconds = REQUIRED_SECONDS[idx];
  if (seconds <= 0.0f) {
    return 0;
  }

  // bars = seconds * bpm / 240, rounded up
  uint8_t bars = static_cast<uint8_t>(std::ceil(seconds * bpm / 240.0f));
  // Clamp to [2, 16]
  if (bars < 2) bars = 2;
  if (bars > 16) bars = 16;
  return bars;
}

uint8_t calcMixPatternBars(MixPattern mix, uint16_t bpm) {
  // Required seconds for each pattern
  constexpr float REQUIRED_SECONDS[] = {
      0.0f,   // None
      8.0f,   // Standard (~8 sec)
      16.0f   // Tiger (~16 sec)
  };

  uint8_t idx = static_cast<uint8_t>(mix);
  if (idx >= sizeof(REQUIRED_SECONDS) / sizeof(REQUIRED_SECONDS[0])) {
    return 0;
  }

  float seconds = REQUIRED_SECONDS[idx];
  if (seconds <= 0.0f) {
    return 0;
  }

  // bars = seconds * bpm / 240, rounded up
  uint8_t bars = static_cast<uint8_t>(std::ceil(seconds * bpm / 240.0f));
  // Clamp to [2, 12]
  if (bars < 2) bars = 2;
  if (bars > 12) bars = 12;
  return bars;
}

uint16_t getMinimumBarsForCall(IntroChant intro_chant, MixPattern mix_pattern, uint16_t bpm) {
  uint16_t base_bars = 24;  // Basic structure (Intro + A + B + Chorus)

  if (intro_chant != IntroChant::None) {
    base_bars += calcIntroChantBars(intro_chant, bpm);
  }
  if (mix_pattern != MixPattern::None) {
    base_bars += calcMixPatternBars(mix_pattern, bpm);
  }

  return base_bars;
}

uint16_t getMinimumSecondsForCall(IntroChant intro_chant, MixPattern mix_pattern, uint16_t bpm) {
  uint16_t min_bars = getMinimumBarsForCall(intro_chant, mix_pattern, bpm);
  // seconds = bars * 240 / bpm
  return static_cast<uint16_t>(min_bars * 240 / bpm);
}

}  // namespace midisketch
