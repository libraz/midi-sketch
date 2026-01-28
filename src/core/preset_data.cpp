/**
 * @file preset_data.cpp
 * @brief Mood and style preset data definitions.
 */

#include "core/preset_data.h"

#include <cmath>
#include <random>

#include "core/chord.h"

namespace midisketch {

namespace {

// Mood default BPM values
constexpr uint16_t MOOD_BPM[24] = {
    120,  // StraightPop
    130,  // BrightUpbeat
    140,  // EnergeticDance
    125,  // LightRock
    110,  // MidPop
    105,  // EmotionalPop
    100,  // Sentimental (more movement than Chill)
    85,   // Chill (slower, ambient feel)
    75,   // Ballad
    115,  // DarkPop
    100,  // Dramatic
    105,  // Nostalgic
    125,  // ModernPop
    135,  // ElectroPop
    145,  // IdolPop
    130,  // Anthem
    // Synth-oriented moods
    148,  // Yoasobi - fast anime-style
    118,  // Synthwave - moderate retro
    145,  // FutureBass - fast EDM
    110,  // CityPop - moderate groove
    // Genre expansion moods
    92,   // RnBNeoSoul - slow groove (85-100 range)
    95,   // LatinPop - moderate dembow rhythm
    70,   // Trap - half-time feel (140 BPM double-time = 70 half-time)
    80,   // Lofi - slow, relaxed
};

// Mood note density values
constexpr float MOOD_DENSITY[24] = {
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
    // Synth-oriented moods
    0.75f,  // Yoasobi - high density melody
    0.55f,  // Synthwave - moderate
    0.70f,  // FutureBass - high density
    0.60f,  // CityPop - moderate groove
    // Genre expansion moods
    0.50f,  // RnBNeoSoul - moderate with space for embellishment
    0.65f,  // LatinPop - moderate-high for rhythmic drive
    0.45f,  // Trap - sparse with space for 808 and hi-hat rolls
    0.35f,  // Lofi - sparse, laid-back
};

// Structure pattern names
const char* STRUCTURE_NAMES[18] = {
    "StandardPop",      "BuildUp",         "DirectChorus",   "RepeatChorus",
    "ShortForm",        "FullPop",         "FullWithBridge", "DriveUpbeat",
    "Ballad",           "AnthemStyle",     "ExtendedFull",   "ChorusFirst",
    "ChorusFirstShort", "ChorusFirstFull", "ImmediateVocal", "ImmediateVocalFull",
    "AChorusB",         "DoubleVerse",
};

// Mood names
const char* MOOD_NAMES[24] = {
    "straight_pop",  "bright_upbeat", "energetic_dance", "light_rock",  "mid_pop",
    "emotional_pop", "sentimental",   "chill",           "ballad",      "dark_pop",
    "dramatic",      "nostalgic",     "modern_pop",      "electro_pop", "idol_pop",
    "anthem",        "yoasobi",       "synthwave",       "future_bass", "city_pop",
    "rnb_neosoul",   "latin_pop",     "trap",            "lofi",
};

// Style preset definitions (17 presets)
const StylePreset STYLE_PRESETS[17] = {
    // ========== Pop/Dance (0-2) ==========
    // 0: Minimal Groove Pop
    {0,
     "minimal_groove_pop",
     "Minimal Groove Pop",
     "Repetitive 2-4 chord loops, simple melody",
     StructurePattern::StandardPop,
     118,
     128,
     122,
     VocalAttitude::Clean,
     ATTITUDE_CLEAN | ATTITUDE_EXPRESSIVE,
     {0, 1, 6, 13, 17, 19, 20, -1},
     {5, true, 0.9f, 0.1f, 0.65f, 8, 0.0f},  // Standard density
     {8, 0.7f, 0.2f},
     {true, 2, 1},
     1},
    // 1: Dance Pop Emotion
    {1,
     "dance_pop_emotion",
     "Dance Pop Emotion",
     "Classic structure, emotional chorus release",
     StructurePattern::FullPop,
     120,
     135,
     128,
     VocalAttitude::Expressive,
     ATTITUDE_CLEAN | ATTITUDE_EXPRESSIVE,
     {0, 1, 2, 4, 5, 14, 15, -1},
     {7, true, 0.8f, 0.3f, 0.75f, 8, 0.1f},  // Medium-high density for dance
     {8, 0.5f, 0.4f},
     {true, 2, 2},
     2},
    // 2: Bright Pop
    {2,
     "bright_pop",
     "Bright Pop",
     "Upbeat, memorable melodies with simple structure",
     StructurePattern::StandardPop,
     125,
     145,
     135,
     VocalAttitude::Clean,
     ATTITUDE_CLEAN,
     {0, 1, 3, 5, -1, -1, -1, -1},
     {4, false, 0.95f, 0.05f, 0.80f, 8, 0.15f},  // Higher density for upbeat
     {4, 0.85f, 0.1f},
     {true, 2, 1},
     1},
    // ========== Idol (3-6) ==========
    // 3: Idol Standard
    {3,
     "idol_standard",
     "Idol Standard",
     "Unison-friendly, memorable melodies",
     StructurePattern::StandardPop,
     130,
     150,
     140,
     VocalAttitude::Clean,
     ATTITUDE_CLEAN,
     {0, 1, 3, 4, 5, 6, 9, -1},
     {4, false, 0.95f, 0.05f, 0.85f, 8, 0.25f},  // High density idol
     {8, 0.8f, 0.1f},
     {true, 3, 0},
     2},
    // 4: Idol Emotion
    {4,
     "idol_emotion",
     "Idol Emotion",
     "Emotional idol songs with building pre-chorus",
     StructurePattern::FullPop,
     120,
     140,
     130,
     VocalAttitude::Expressive,
     ATTITUDE_CLEAN | ATTITUDE_EXPRESSIVE,
     {0, 1, 2, 5, 15, 16, 18, 21},
     {6, true, 0.85f, 0.25f, 0.80f, 8, 0.20f},  // Medium-high density
     {8, 0.6f, 0.3f},
     {true, 2, 1},
     2},
    // 5: Idol Energy
    {5,
     "idol_energy",
     "Idol Energy",
     "High-energy idol songs for live performances",
     StructurePattern::DriveUpbeat,
     140,
     160,
     150,
     VocalAttitude::Clean,
     ATTITUDE_CLEAN,
     {0, 2, 4, 6, 9, -1, -1, -1},
     {5, false, 0.9f, 0.1f, 0.90f, 8, 0.30f},  // Very high density for energy
     {4, 0.8f, 0.15f},
     {true, 3, 2},
     3},
    // 6: Idol Minimal
    {6,
     "idol_minimal",
     "Idol Minimal",
     "Minimal idol songs for short-form content",
     StructurePattern::ShortForm,
     125,
     145,
     135,
     VocalAttitude::Clean,
     ATTITUDE_CLEAN,
     {0, 6, -1, -1, -1, -1, -1, -1},
     {3, false, 0.95f, 0.02f, 0.75f, 8, 0.15f},  // Medium density
     {4, 0.9f, 0.05f},
     {true, 2, 0},
     1},
    // ========== Rock/Emo (7-9) ==========
    // 7: Rock Shout
    {7,
     "rock_shout",
     "Rock Shout",
     "Aggressive vocals with raw expression",
     StructurePattern::FullPop,
     115,
     135,
     125,
     VocalAttitude::Expressive,
     ATTITUDE_CLEAN | ATTITUDE_EXPRESSIVE | ATTITUDE_RAW,
     {4, 5, 6, 10, 11, 12, -1, -1},
     {9, true, 0.6f, 0.5f, 0.70f, 8, 0.10f},  // Medium density
     {8, 0.4f, 0.5f},
     {true, 3, 3},
     2},
    // 8: Pop Emotion
    {8,
     "pop_emotion",
     "Pop Emotion",
     "Word-driven emotional pop with lyrical focus",
     StructurePattern::FullPop,
     95,
     120,
     108,
     VocalAttitude::Expressive,
     ATTITUDE_EXPRESSIVE | ATTITUDE_RAW,
     {1, 5, 8, 15, 16, 18, -1, -1},
     {7, true, 0.7f, 0.4f, 0.60f, 8, 0.05f},  // Lower density for emotional
     {8, 0.4f, 0.5f},
     {true, 1, 1},
     1},
    // 9: Raw Emotional
    {9,
     "raw_emotional",
     "Raw Emotional",
     "Intense emotional expression with boundary-breaking phrases",
     StructurePattern::FullWithBridge,
     90,
     115,
     102,
     VocalAttitude::Expressive,
     ATTITUDE_EXPRESSIVE | ATTITUDE_RAW,
     {7, 8, 11, 15, 21, -1, -1, -1},
     {9, true, 0.5f, 0.6f, 0.55f, 8, 0.0f},  // Lower density, no 16th
     {8, 0.3f, 0.6f},
     {true, 2, 2},
     2},
    // ========== Special/Derived (10-12) ==========
    // 10: Acoustic Pop
    {10,
     "acoustic_pop",
     "Acoustic Pop",
     "Clear harmony, rhythm-light, vocal-centered",
     StructurePattern::Ballad,
     85,
     110,
     95,
     VocalAttitude::Expressive,
     ATTITUDE_CLEAN | ATTITUDE_EXPRESSIVE,
     {0, 1, 4, 6, 17, -1, -1, -1},
     {6, true, 0.85f, 0.2f, 0.50f, 4, 0.0f},  // Low density ballad
     {8, 0.5f, 0.3f},
     {false, 1, 0},
     0},
    // 11: Live Call & Response
    {11,
     "live_call_response",
     "Live Call & Response",
     "Concert-ready with call-response structure",
     StructurePattern::AnthemStyle,
     130,
     150,
     140,
     VocalAttitude::Clean,
     ATTITUDE_CLEAN,
     {0, 2, 4, -1, -1, -1, -1, -1},
     {4, false, 0.9f, 0.05f, 0.85f, 8, 0.20f},  // High density for live
     {4, 0.85f, 0.1f},
     {true, 3, 1},
     3},
    // 12: Background Motif
    {12,
     "background_motif",
     "Background Motif",
     "Motif-driven with subdued vocals, ambient feel",
     StructurePattern::StandardPop,
     110,
     130,
     120,
     VocalAttitude::Clean,
     ATTITUDE_CLEAN,
     {0, 6, 13, 19, -1, -1, -1, -1},
     {4, false, 0.95f, 0.05f, 0.50f, 8, 0.0f},  // Low density, subdued
     {8, 0.9f, 0.05f},
     {true, 2, 0},
     1},
    // ========== Genre-Specific (13-16) ==========
    // 13: City Pop
    {13,
     "city_pop",
     "City Pop",
     "Groovy 80s Japanese city pop with jazzy chords",
     StructurePattern::FullPop,
     95,
     115,
     105,
     VocalAttitude::Clean,
     ATTITUDE_CLEAN | ATTITUDE_EXPRESSIVE,
     {7, 8, 10, 1, 5, -1, -1, -1},
     {7, true, 0.75f, 0.35f, 0.65f, 8, 0.05f},  // Medium density groove
     {8, 0.5f, 0.4f},
     {true, 2, 1},
     2},
    // 14: Anime Opening
    {14,
     "anime_opening",
     "Anime Opening",
     "Epic, dramatic anime OP style with building energy",
     StructurePattern::BuildUp,
     130,
     155,
     142,
     VocalAttitude::Expressive,
     ATTITUDE_CLEAN | ATTITUDE_EXPRESSIVE,
     {5, 2, 6, 0, 4, -1, -1, -1},
     {6, true, 0.8f, 0.3f, 0.85f, 8, 0.25f},  // High density anime style
     {8, 0.6f, 0.3f},
     {true, 3, 2},
     2},
    // 15: EDM Synth Pop
    {15,
     "edm_synth",
     "EDM Synth Pop",
     "Modern electronic dance music with synth leads",
     StructurePattern::DirectChorus,
     125,
     145,
     138,
     VocalAttitude::Clean,
     ATTITUDE_CLEAN | ATTITUDE_EXPRESSIVE,
     {0, 4, 9, 1, -1, -1, -1, -1},
     {5, false, 0.85f, 0.15f, 0.75f, 16, 0.35f},  // High 16th ratio for EDM
     {4, 0.75f, 0.2f},
     {true, 2, 2},
     2},
    // 16: Emotional Ballad
    {16,
     "emotional_ballad",
     "Emotional Ballad",
     "Slow emotional ballad with expressive vocals",
     StructurePattern::Ballad,
     65,
     85,
     72,
     VocalAttitude::Expressive,
     ATTITUDE_CLEAN | ATTITUDE_EXPRESSIVE,
     {1, 3, 8, 11, 5, -1, -1, -1},
     {8, true, 0.7f, 0.4f, 0.45f, 4, 0.0f},  // Very low density ballad
     {8, 0.4f, 0.5f},
     {false, 1, 0},
     0},
};

// Form compatibility by style with weights (higher = more likely)
// Weight guidelines: Common=40-60, Frequent=20-35, Occasional=10-20, Rare=3-10
const FormWeight STYLE_FORMS_WEIGHTED[17][8] = {
    // 0: Minimal Groove Pop - shorter forms
    {{StructurePattern::StandardPop, 40},
     {StructurePattern::DirectChorus, 20},
     {StructurePattern::ShortForm, 10},
     {StructurePattern::RepeatChorus, 8},
     {StructurePattern::BuildUp, 5},
     {StructurePattern::ChorusFirstShort, 10},
     {StructurePattern::ImmediateVocal, 7},
     {StructurePattern::ChorusFirst, 0}},
    // 1: Dance Pop Emotion - full-length forms + chorus-first
    {{StructurePattern::FullPop, 35},
     {StructurePattern::FullWithBridge, 20},
     {StructurePattern::DriveUpbeat, 12},
     {StructurePattern::Ballad, 8},
     {StructurePattern::AnthemStyle, 5},
     {StructurePattern::ChorusFirst, 10},
     {StructurePattern::ChorusFirstFull, 10},
     {StructurePattern::ImmediateVocalFull, 0}},
    // 2: Bright Pop - standard forms
    {{StructurePattern::StandardPop, 35},
     {StructurePattern::DirectChorus, 20},
     {StructurePattern::RepeatChorus, 12},
     {StructurePattern::BuildUp, 8},
     {StructurePattern::FullPop, 5},
     {StructurePattern::ChorusFirst, 10},
     {StructurePattern::ImmediateVocal, 10},
     {StructurePattern::AChorusB, 0}},
    // 3: Idol Standard - standard and anthem + chorus-first
    {{StructurePattern::StandardPop, 30},
     {StructurePattern::BuildUp, 18},
     {StructurePattern::RepeatChorus, 12},
     {StructurePattern::AnthemStyle, 10},
     {StructurePattern::FullPop, 8},
     {StructurePattern::ChorusFirst, 12},
     {StructurePattern::ChorusFirstShort, 10},
     {StructurePattern::AChorusB, 0}},
    // 4: Idol Emotion - full-length emotional forms
    {{StructurePattern::FullPop, 35},
     {StructurePattern::FullWithBridge, 20},
     {StructurePattern::Ballad, 12},
     {StructurePattern::StandardPop, 8},
     {StructurePattern::BuildUp, 5},
     {StructurePattern::DoubleVerse, 10},
     {StructurePattern::ChorusFirstFull, 10},
     {StructurePattern::ImmediateVocalFull, 0}},
    // 5: Idol Energy - driving high-energy + chorus-first
    {{StructurePattern::DriveUpbeat, 30},
     {StructurePattern::AnthemStyle, 22},
     {StructurePattern::FullPop, 12},
     {StructurePattern::RepeatChorus, 8},
     {StructurePattern::DirectChorus, 5},
     {StructurePattern::ChorusFirst, 13},
     {StructurePattern::ChorusFirstShort, 10},
     {StructurePattern::AChorusB, 0}},
    // 6: Idol Minimal - short forms + chorus-first-short
    {{StructurePattern::ShortForm, 30},
     {StructurePattern::DirectChorus, 22},
     {StructurePattern::StandardPop, 12},
     {StructurePattern::RepeatChorus, 5},
     {StructurePattern::BuildUp, 3},
     {StructurePattern::ChorusFirstShort, 18},
     {StructurePattern::ImmediateVocal, 10},
     {StructurePattern::ChorusFirst, 0}},
    // 7: Rock Shout - driving forms
    {{StructurePattern::FullPop, 32},
     {StructurePattern::DriveUpbeat, 20},
     {StructurePattern::AnthemStyle, 15},
     {StructurePattern::BuildUp, 10},
     {StructurePattern::FullWithBridge, 5},
     {StructurePattern::ImmediateVocalFull, 10},
     {StructurePattern::AChorusB, 8},
     {StructurePattern::ChorusFirst, 0}},
    // 8: Pop Emotion - emotional full forms
    {{StructurePattern::FullPop, 35},
     {StructurePattern::FullWithBridge, 20},
     {StructurePattern::Ballad, 12},
     {StructurePattern::StandardPop, 8},
     {StructurePattern::BuildUp, 5},
     {StructurePattern::DoubleVerse, 10},
     {StructurePattern::ImmediateVocalFull, 10},
     {StructurePattern::ChorusFirstFull, 0}},
    // 9: Raw Emotional - varied emotional forms
    {{StructurePattern::FullWithBridge, 32},
     {StructurePattern::FullPop, 20},
     {StructurePattern::Ballad, 15},
     {StructurePattern::BuildUp, 10},
     {StructurePattern::DriveUpbeat, 5},
     {StructurePattern::DoubleVerse, 10},
     {StructurePattern::ImmediateVocalFull, 8},
     {StructurePattern::ChorusFirstFull, 0}},
    // 10: Acoustic Pop - ballad and simple forms
    {{StructurePattern::Ballad, 40},
     {StructurePattern::StandardPop, 18},
     {StructurePattern::FullWithBridge, 12},
     {StructurePattern::ShortForm, 6},
     {StructurePattern::BuildUp, 4},
     {StructurePattern::DoubleVerse, 12},
     {StructurePattern::ImmediateVocal, 8},
     {StructurePattern::AChorusB, 0}},
    // 11: Live Call & Response - anthem and driving + chorus-first
    {{StructurePattern::AnthemStyle, 35},
     {StructurePattern::DriveUpbeat, 20},
     {StructurePattern::RepeatChorus, 12},
     {StructurePattern::FullPop, 8},
     {StructurePattern::DirectChorus, 5},
     {StructurePattern::ChorusFirst, 12},
     {StructurePattern::AChorusB, 8},
     {StructurePattern::ChorusFirstShort, 0}},
    // 12: Background Motif - repetitive forms
    {{StructurePattern::StandardPop, 35},
     {StructurePattern::RepeatChorus, 20},
     {StructurePattern::ShortForm, 12},
     {StructurePattern::DirectChorus, 8},
     {StructurePattern::BuildUp, 5},
     {StructurePattern::ChorusFirstShort, 10},
     {StructurePattern::ImmediateVocal, 10},
     {StructurePattern::ChorusFirst, 0}},
    // 13: City Pop - groovy full forms
    {{StructurePattern::FullPop, 35},
     {StructurePattern::FullWithBridge, 20},
     {StructurePattern::StandardPop, 12},
     {StructurePattern::BuildUp, 8},
     {StructurePattern::Ballad, 5},
     {StructurePattern::ImmediateVocalFull, 10},
     {StructurePattern::DoubleVerse, 10},
     {StructurePattern::AChorusB, 0}},
    // 14: Anime Opening - build-up + immediate vocal (YOASOBI style)
    {{StructurePattern::BuildUp, 30},
     {StructurePattern::DriveUpbeat, 18},
     {StructurePattern::FullPop, 12},
     {StructurePattern::AnthemStyle, 8},
     {StructurePattern::FullWithBridge, 4},
     {StructurePattern::ImmediateVocal, 15},
     {StructurePattern::ImmediateVocalFull, 13},
     {StructurePattern::ChorusFirst, 0}},
    // 15: EDM Synth Pop - direct + chorus-first
    {{StructurePattern::DirectChorus, 32},
     {StructurePattern::DriveUpbeat, 18},
     {StructurePattern::RepeatChorus, 12},
     {StructurePattern::BuildUp, 8},
     {StructurePattern::FullPop, 5},
     {StructurePattern::ChorusFirst, 13},
     {StructurePattern::ChorusFirstShort, 12},
     {StructurePattern::ImmediateVocal, 0}},
    // 16: Emotional Ballad - slow emotional forms
    {{StructurePattern::Ballad, 42},
     {StructurePattern::FullWithBridge, 18},
     {StructurePattern::StandardPop, 10},
     {StructurePattern::FullPop, 6},
     {StructurePattern::BuildUp, 3},
     {StructurePattern::DoubleVerse, 12},
     {StructurePattern::ImmediateVocalFull, 9},
     {StructurePattern::AChorusB, 0}},
};

constexpr size_t STYLE_FORM_COUNT = 8;

// Vocal style compatibility by StylePreset with weights (higher = more likely)
// UltraVocaloid is excluded from auto-selection (use explicit setting only)
// Weight 0 means unused slot
const VocalStyleWeight STYLE_VOCAL_STYLES[17][4] = {
    // 0: Minimal Groove Pop - simple styles
    {{VocalStylePreset::Standard, 50}, {VocalStylePreset::CoolSynth, 50}, {}, {}},
    // 1: Dance Pop Emotion - emotional dance styles
    {{VocalStylePreset::Standard, 34},
     {VocalStylePreset::Anime, 33},
     {VocalStylePreset::BrightKira, 33},
     {}},
    // 2: Bright Pop - bright upbeat styles
    {{VocalStylePreset::Standard, 34},
     {VocalStylePreset::BrightKira, 33},
     {VocalStylePreset::CuteAffected, 33},
     {}},
    // 3: Idol Standard - idol styles
    {{VocalStylePreset::Idol, 34},
     {VocalStylePreset::BrightKira, 33},
     {VocalStylePreset::CuteAffected, 33},
     {}},
    // 4: Idol Emotion - emotional idol styles
    {{VocalStylePreset::Idol, 34},
     {VocalStylePreset::Anime, 33},
     {VocalStylePreset::BrightKira, 33},
     {}},
    // 5: Idol Energy - high-energy idol styles
    {{VocalStylePreset::Idol, 34},
     {VocalStylePreset::BrightKira, 33},
     {VocalStylePreset::PowerfulShout, 33},
     {}},
    // 6: Idol Minimal - simple idol styles
    {{VocalStylePreset::Idol, 50}, {VocalStylePreset::Standard, 50}, {}, {}},
    // 7: Rock Shout - rock styles
    {{VocalStylePreset::Rock, 50}, {VocalStylePreset::PowerfulShout, 50}, {}, {}},
    // 8: Pop Emotion - emotional pop styles
    {{VocalStylePreset::Standard, 34},
     {VocalStylePreset::Anime, 33},
     {VocalStylePreset::CuteAffected, 33},
     {}},
    // 9: Raw Emotional - intense emotional styles
    {{VocalStylePreset::Rock, 50}, {VocalStylePreset::PowerfulShout, 50}, {}, {}},
    // 10: Acoustic Pop - acoustic/ballad styles
    {{VocalStylePreset::Ballad, 50}, {VocalStylePreset::Standard, 50}, {}, {}},
    // 11: Live Call & Response - live performance styles
    {{VocalStylePreset::Idol, 34},
     {VocalStylePreset::BrightKira, 33},
     {VocalStylePreset::PowerfulShout, 33},
     {}},
    // 12: Background Motif - subdued styles
    {{VocalStylePreset::Standard, 50}, {VocalStylePreset::CoolSynth, 50}, {}, {}},
    // 13: City Pop - city pop styles
    {{VocalStylePreset::CityPop, 50}, {VocalStylePreset::Standard, 50}, {}, {}},
    // 14: Anime Opening - anime styles
    {{VocalStylePreset::Anime, 34},
     {VocalStylePreset::Vocaloid, 33},
     {VocalStylePreset::BrightKira, 33},
     {}},
    // 15: EDM Synth Pop - synth/EDM styles
    {{VocalStylePreset::CoolSynth, 50}, {VocalStylePreset::Vocaloid, 50}, {}, {}},
    // 16: Emotional Ballad - ballad styles
    {{VocalStylePreset::Ballad, 50}, {VocalStylePreset::Standard, 50}, {}, {}},
};

constexpr size_t STYLE_VOCAL_COUNT = 4;

// ============================================================================
// VocalStylePreset Data Table
// ============================================================================
//
// Column definitions (18 fields per entry):
//
// [1] id                          - VocalStylePreset enum value
// [2] max_leap_interval           - Max melodic leap in semitones (5=4th, 7=5th, 12=octave, 14=9th)
// [3] syncopation_prob            - Syncopation probability (0.0-0.5, higher=more off-beat)
// [4] allow_bar_crossing          - Allow notes to cross bar lines (true=more flowing phrases)
//
// Section density modifiers (multiplied with base density):
// [5] verse_density_modifier      - Verse (A) density (0.3=sparse ballad, 1.0=normal)
// [6] prechorus_density_modifier  - Pre-chorus (B) density
// [7] chorus_density_modifier     - Chorus density (1.0=normal, 1.6=ultra-dense)
// [8] bridge_density_modifier     - Bridge density
//
// Section 32nd note ratios (for fast passages):
// [9]  verse_thirtysecond_ratio   - Verse 32nd note ratio (0.0=none, 1.0=all 32nd)
// [10] prechorus_thirtysecond_ratio
// [11] chorus_thirtysecond_ratio
// [12] bridge_thirtysecond_ratio
//
// Additional parameters:
// [13] consecutive_same_note_prob - Same-note repeat probability (0.1=rare, 1.0=allow all)
// [14] disable_vowel_constraints  - Disable human singing limits (true for Vocaloid styles)
// [15] hook_repetition            - Enable hook phrase repetition in chorus
// [16] chorus_long_tones          - Use sustained notes in chorus
// [17] chorus_register_shift      - Chorus pitch shift in semitones (5=normal, 7=higher)
// [18] tension_usage              - Tension note probability (0.2=low, 0.4=jazzy)
//
const VocalStylePresetData VOCAL_STYLE_PRESET_DATA[] = {
    // -------------------------------------------------------------------------
    // Auto (0) - Default values, no style-specific changes applied
    // -------------------------------------------------------------------------
    {VocalStylePreset::Auto, 7, 0.15f, false,  // leap=5th, low synco, no bar cross
     1.0f, 1.0f, 0.9f, 1.0f,                   // chorus=90% (avoid 8th note saturation)
     0.0f, 0.0f, 0.0f, 0.0f,                   // no 32nd notes
     1.0f, false, false, false, 5, 0.2f},      // all same-note OK, no special flags

    // -------------------------------------------------------------------------
    // Standard (1) - General purpose pop melody
    // -------------------------------------------------------------------------
    {VocalStylePreset::Standard, 7, 0.15f, false,  // leap=5th, low synco, no bar cross
     1.0f, 1.0f, 0.85f, 1.0f,                      // chorus=85% (room for long notes)
     0.0f, 0.0f, 0.0f, 0.0f,                       // no 32nd notes
     1.0f, false, false, false, 5, 0.2f},          // standard settings

    // -------------------------------------------------------------------------
    // Vocaloid (2) - YOASOBI style: energetic, wide leaps, singable
    // -------------------------------------------------------------------------
    {VocalStylePreset::Vocaloid, 12, 0.35f, true,  // leap=octave, high synco, bar cross OK
     0.8f, 0.9f, 1.0f, 0.85f,                      // verse sparse, chorus=100% (singable pace)
     0.0f, 0.0f, 0.0f, 0.0f,                       // no 32nd notes (still singable)
     1.0f, true, false, false, 5, 0.2f},           // disable vowel limits

    // -------------------------------------------------------------------------
    // UltraVocaloid (3) - Miku Disappearance style: ballad verse + barrage chorus
    // Extreme contrast between sparse intro and machine-gun chorus
    // -------------------------------------------------------------------------
    {VocalStylePreset::UltraVocaloid, 14, 0.4f, true,  // leap=9th(!), high synco, bar cross OK
     0.3f, 0.5f, 1.4f, 0.35f,                          // verse=ballad(30%), chorus=140%
     0.0f, 0.0f, 1.0f, 0.0f,                           // 32nd: verse=0%, chorus=100% (contrast!)
     0.1f, true, false, false, 5, 0.2f},               // same-note=10% only, disable vowel

    // -------------------------------------------------------------------------
    // Idol (4) - Love Live/Idolmaster style: catchy, unison-friendly
    // -------------------------------------------------------------------------
    {VocalStylePreset::Idol, 7, 0.15f, false,  // leap=5th, low synco (easy to dance)
     1.0f, 1.0f, 0.85f, 1.0f,                  // chorus slightly sparser (long tones)
     0.0f, 0.0f, 0.0f, 0.0f,                   // no 32nd notes
     1.0f, false, true, true, 5, 0.2f},        // hook repeat + long tones in chorus

    // -------------------------------------------------------------------------
    // Ballad (5) - Slow emotional ballad: small leaps, sustained notes
    // -------------------------------------------------------------------------
    {VocalStylePreset::Ballad, 5, 0.15f, false,  // leap=4th only, gentle movement
     1.0f, 1.0f, 0.55f, 1.0f,                    // chorus=55% (long tones, breathing room)
     0.0f, 0.0f, 0.0f, 0.0f,                     // no 32nd notes
     1.0f, false, false, true, 5, 0.2f},         // long tones in chorus

    // -------------------------------------------------------------------------
    // Rock (6) - Rock style: powerful, driving, wide chorus register
    // -------------------------------------------------------------------------
    {VocalStylePreset::Rock, 9, 0.25f, true,  // leap=6th, medium synco, bar cross OK
     1.0f, 1.0f, 0.75f, 1.0f,                 // chorus=75% (power needs sustain)
     0.0f, 0.0f, 0.0f, 0.0f,                  // no 32nd notes
     1.0f, false, true, true, 7, 0.2f},       // hook + long tones, chorus +7 semitones

    // -------------------------------------------------------------------------
    // CityPop (7) - 80s Japanese city pop: groovy, jazzy tensions
    // -------------------------------------------------------------------------
    {VocalStylePreset::CityPop, 7, 0.35f, true,  // leap=5th, high synco (groovy)
     1.0f, 1.0f, 0.75f, 1.0f,                    // chorus=75% (relaxed groove)
     0.0f, 0.0f, 0.0f, 0.0f,                     // no 32nd notes
     1.0f, false, false, false, 5, 0.4f},        // tension=0.4 (jazzy chords)

    // -------------------------------------------------------------------------
    // Anime (8) - Anime OP/ED style: dramatic, wide leaps, building energy
    // -------------------------------------------------------------------------
    {VocalStylePreset::Anime, 10, 0.25f, true,  // leap=minor 7th, medium synco
     1.0f, 1.0f, 1.0f, 1.0f,                    // chorus=100% (balanced for drama)
     0.0f, 0.0f, 0.0f, 0.0f,                    // no 32nd notes
     1.0f, false, true, true, 5, 0.2f},         // hook repeat + long tones

    // -------------------------------------------------------------------------
    // BrightKira (9) - Bright sparkly idol style: energetic, high register
    // -------------------------------------------------------------------------
    {VocalStylePreset::BrightKira, 10, 0.15f, false,  // leap=minor 7th, low synco
     1.0f, 1.0f, 1.0f, 1.0f,                          // chorus=100% (bright but not rushed)
     0.0f, 0.0f, 0.0f, 0.0f,                          // no 32nd notes
     1.0f, false, true, true, 7, 0.2f},               // hook + long, chorus +7 semitones

    // -------------------------------------------------------------------------
    // CoolSynth (10) - Cool synthetic style: mechanical, flowing phrases
    // -------------------------------------------------------------------------
    {VocalStylePreset::CoolSynth, 7, 0.15f, true,  // leap=5th, low synco, bar cross OK
     1.0f, 1.0f, 0.75f, 1.0f,                      // chorus=75% (cool and relaxed)
     0.0f, 0.0f, 0.0f, 0.0f,                       // no 32nd notes
     1.0f, false, true, false, 5, 0.2f},           // hook repeat, no long tones

    // -------------------------------------------------------------------------
    // CuteAffected (11) - Cute affected style: slightly wider leaps
    // -------------------------------------------------------------------------
    {VocalStylePreset::CuteAffected, 8, 0.15f, false,  // leap=minor 6th, low synco
     1.0f, 1.0f, 0.9f, 1.0f,                           // chorus=90% (cute, not frantic)
     0.0f, 0.0f, 0.0f, 0.0f,                           // no 32nd notes
     1.0f, false, true, true, 5, 0.2f},                // hook repeat + long tones

    // -------------------------------------------------------------------------
    // PowerfulShout (12) - Powerful shout style: big leaps, power sustain
    // -------------------------------------------------------------------------
    {VocalStylePreset::PowerfulShout, 12, 0.2f, false,  // leap=octave, medium synco
     1.0f, 1.0f, 0.75f, 1.0f,                           // chorus=75% (power needs sustain)
     0.0f, 0.0f, 0.0f, 0.0f,                            // no 32nd notes
     1.0f, false, true, true, 5, 0.2f},                 // hook repeat + long tones
};

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

namespace {

// ============================================================================
// Mood → DrumStyle Mapping Table
// ============================================================================
//
// Maps each Mood to its appropriate DrumStyle.
// Index corresponds to Mood enum value (0-19).
//
// DrumStyle categories:
// - Sparse:      Ballad, slow patterns (half-time feel)
// - Standard:    Default pop patterns (8th note hi-hats, 2&4 snare)
// - FourOnFloor: Dance/EDM (kick on every beat)
// - Upbeat:      Energetic (syncopated, driving)
// - Rock:        Rock patterns (crash accents, ride cymbal)
// - Synth:       Synth-oriented (tight 16th hi-hats, punchy kick)
//
constexpr DrumStyle MOOD_DRUM_STYLES[24] = {
    DrumStyle::Standard,     // 0: StraightPop
    DrumStyle::Upbeat,       // 1: BrightUpbeat
    DrumStyle::FourOnFloor,  // 2: EnergeticDance
    DrumStyle::Rock,         // 3: LightRock
    DrumStyle::Upbeat,       // 4: MidPop (mid-tempo brightness)
    DrumStyle::Sparse,       // 5: EmotionalPop (sparse drums to highlight vocals)
    DrumStyle::Standard,     // 6: Sentimental (more movement than Chill)
    DrumStyle::Sparse,       // 7: Chill
    DrumStyle::Sparse,       // 8: Ballad
    DrumStyle::FourOnFloor,  // 9: DarkPop (heavy kick for dark atmosphere)
    DrumStyle::Rock,         // 10: Dramatic (crash accents for dramatic impact)
    DrumStyle::Standard,     // 11: Nostalgic
    DrumStyle::Upbeat,       // 12: ModernPop
    DrumStyle::FourOnFloor,  // 13: ElectroPop
    DrumStyle::Upbeat,       // 14: IdolPop
    DrumStyle::Upbeat,       // 15: Anthem
    DrumStyle::Synth,        // 16: Yoasobi
    DrumStyle::Synth,        // 17: Synthwave
    DrumStyle::Synth,        // 18: FutureBass
    DrumStyle::Standard,     // 19: CityPop (groove feel, not synth)
    // Genre expansion moods
    DrumStyle::Standard,     // 20: RnBNeoSoul (standard with heavy swing)
    DrumStyle::Latin,        // 21: LatinPop (dembow rhythm)
    DrumStyle::Trap,         // 22: Trap (half-time snare, hi-hat rolls)
    DrumStyle::Sparse,       // 23: Lofi (sparse, laid-back)
};

// ============================================================================
// Mood → DrumGrooveFeel Mapping Table
// ============================================================================
//
// Maps each Mood to its appropriate groove feel.
// Swing adds a triplet feel to off-beat hi-hats, essential for:
// - Ballads (subtle swing for warmth)
// - CityPop (80s Japanese groove)
// - Jazz-influenced moods
//
// Index corresponds to Mood enum value (0-19).
//
constexpr DrumGrooveFeel MOOD_DRUM_GROOVES[24] = {
    DrumGrooveFeel::Straight,  // 0: StraightPop
    DrumGrooveFeel::Straight,  // 1: BrightUpbeat
    DrumGrooveFeel::Straight,  // 2: EnergeticDance
    DrumGrooveFeel::Straight,  // 3: LightRock
    DrumGrooveFeel::Straight,  // 4: MidPop
    DrumGrooveFeel::Straight,  // 5: EmotionalPop
    DrumGrooveFeel::Swing,     // 6: Sentimental (subtle swing)
    DrumGrooveFeel::Swing,     // 7: Chill (relaxed swing)
    DrumGrooveFeel::Swing,     // 8: Ballad (essential swing)
    DrumGrooveFeel::Straight,  // 9: DarkPop
    DrumGrooveFeel::Straight,  // 10: Dramatic
    DrumGrooveFeel::Swing,     // 11: Nostalgic (retro feel)
    DrumGrooveFeel::Straight,  // 12: ModernPop
    DrumGrooveFeel::Straight,  // 13: ElectroPop
    DrumGrooveFeel::Straight,  // 14: IdolPop
    DrumGrooveFeel::Straight,  // 15: Anthem
    DrumGrooveFeel::Straight,  // 16: Yoasobi (tight electronic)
    DrumGrooveFeel::Straight,  // 17: Synthwave (tight electronic)
    DrumGrooveFeel::Straight,  // 18: FutureBass (tight electronic)
    DrumGrooveFeel::Swing,     // 19: CityPop (80s groove essential)
    // Genre expansion moods
    DrumGrooveFeel::Shuffle,   // 20: RnBNeoSoul (heavy swing, almost shuffle)
    DrumGrooveFeel::Straight,  // 21: LatinPop (straight dembow feel)
    DrumGrooveFeel::Straight,  // 22: Trap (tight electronic half-time)
    DrumGrooveFeel::Shuffle,   // 23: Lofi (heavy swing essential)
};

}  // namespace

DrumStyle getMoodDrumStyle(Mood mood) {
  uint8_t idx = static_cast<uint8_t>(mood);
  if (idx < sizeof(MOOD_DRUM_STYLES) / sizeof(MOOD_DRUM_STYLES[0])) {
    return MOOD_DRUM_STYLES[idx];
  }
  return DrumStyle::Standard;  // fallback
}

DrumGrooveFeel getMoodDrumGrooveFeel(Mood mood) {
  uint8_t idx = static_cast<uint8_t>(mood);
  if (idx < sizeof(MOOD_DRUM_GROOVES) / sizeof(MOOD_DRUM_GROOVES[0])) {
    return MOOD_DRUM_GROOVES[idx];
  }
  return DrumGrooveFeel::Straight;  // fallback
}

// ============================================================================
// Bass Genre System Implementation
// ============================================================================
//
// Mood → BassGenre Mapping Table
// Maps each Mood to its appropriate BassGenre category.
//
// Genre categories:
// - Standard:   Default pop patterns
// - Ballad:     Slow, sustained (Ballad, Sentimental, Chill)
// - Rock:       Aggressive, power-driven (LightRock, Anthem, Yoasobi)
// - Dance:      High-energy (EnergeticDance, IdolPop, FutureBass)
// - Electronic: Sidechain, modern (ElectroPop, Synthwave)
// - Jazz:       Walking bass, groove (CityPop, ModernPop)
// - Idol:       Bright, energetic (BrightUpbeat, IdolPop)
//
constexpr BassGenre MOOD_BASS_GENRES[24] = {
    BassGenre::Standard,    // 0: StraightPop
    BassGenre::Idol,        // 1: BrightUpbeat
    BassGenre::Dance,       // 2: EnergeticDance
    BassGenre::Rock,        // 3: LightRock
    BassGenre::Idol,        // 4: MidPop (bright, energetic patterns)
    BassGenre::Standard,    // 5: EmotionalPop
    BassGenre::Ballad,      // 6: Sentimental
    BassGenre::Electronic,  // 7: Chill (ambient, sidechain feel)
    BassGenre::Ballad,      // 8: Ballad
    BassGenre::Electronic,  // 9: DarkPop (sidechain pulse for dark feel)
    BassGenre::Rock,        // 10: Dramatic (power-driven for dynamics)
    BassGenre::Jazz,        // 11: Nostalgic (walking bass for 80s retro feel)
    BassGenre::Jazz,        // 12: ModernPop
    BassGenre::Electronic,  // 13: ElectroPop
    BassGenre::Idol,        // 14: IdolPop
    BassGenre::Rock,        // 15: Anthem
    BassGenre::Rock,        // 16: Yoasobi
    BassGenre::Electronic,  // 17: Synthwave
    BassGenre::Dance,       // 18: FutureBass
    BassGenre::Jazz,        // 19: CityPop
    // Genre expansion moods
    BassGenre::RnB,         // 20: RnBNeoSoul (groove with chromatic approach)
    BassGenre::Latin,       // 21: LatinPop (tresillo 3+3+2 pattern)
    BassGenre::Trap808,     // 22: Trap (long sustained 808 sub-bass)
    BassGenre::Lofi,        // 23: Lofi (simple patterns, pedal tone preference)
};

// ============================================================================
// BASS GENRE PATTERN MAPPING TABLE
// ============================================================================
//
// | Genre      | Intro    | A (Verse)     | B (PreCho)    | Chorus        | Bridge   | Outro    |
// Mix          |
// |------------|----------|---------------|---------------|---------------|----------|----------|--------------|
// | Standard   | WN,RF,RF | RF,WN,SY      | SY,RF,DR      | SY,DR,RF      | RF,WN,SY | RF,WN,WN |
// DR,SY,AG     | | Ballad     | WN,RF,RF | WN,RF,RF      | RF,WN,WN      | RF,SY,SY      | WN,RF,RF
// | WN,RF,RF | RF,SY,SY     | | Rock       | WN,RF,RF | PD,RF,SY      | PD,DR,SY      | AG,PD,DR |
// RF,PD,SY | RF,WN,WN | AG,PD,DR     | | Dance      | WN,RF,RF | RF,SY,OJ      | SY,DR,OJ      |
// AG,DR,OJ      | RF,SY,SY | RF,WN,WN | AG,OJ,DR     | | Electronic | WN,RF,SP | SP,RF,OJ      |
// SP,DR,OJ      | AG,SP,OJ      | RF,SP,SY | SP,RF,RF | AG,OJ,DR     | | Jazz       | WN,RF,RF |
// GR,WK,SY      | GR,WK,SY      | GR,DR,WK      | WK,GR,RF | RF,WN,WN | DR,GR,SY     | | Idol |
// WN,RF,RF | RF,SY,OJ      | SY,DR,OJ      | DR,OJ,AG      | RF,SY,SY | RF,WN,WN | AG,OJ,DR     |
//
// Abbreviations:
//   WN=WholeNote, RF=RootFifth, SY=Syncopated, DR=Driving, WK=Walking
//   PD=PowerDrive, AG=Aggressive, SP=SidechainPulse, GR=Groove, OJ=OctaveJump
//   PT=PedalTone
//
using BP = BassPatternId;

constexpr BassGenrePatterns BASS_GENRE_PATTERNS[static_cast<int>(BassGenre::COUNT)] = {
    // Standard (default pop)
    {{
        {BP::WholeNote, BP::RootFifth, BP::RootFifth},   // Intro
        {BP::RootFifth, BP::WholeNote, BP::Syncopated},  // A
        {BP::Syncopated, BP::RootFifth, BP::Driving},    // B
        {BP::Syncopated, BP::Driving, BP::RootFifth},    // Chorus
        {BP::RootFifth, BP::WholeNote, BP::Syncopated},  // Bridge
        {BP::RootFifth, BP::WholeNote, BP::WholeNote},   // Outro
        {BP::Driving, BP::Syncopated, BP::Aggressive},   // Mix
    }},
    // Ballad (slow, sustained)
    {{
        {BP::PedalTone, BP::WholeNote, BP::RootFifth},    // Intro (tonic pedal)
        {BP::WholeNote, BP::RootFifth, BP::RootFifth},    // A
        {BP::RootFifth, BP::WholeNote, BP::WholeNote},    // B
        {BP::RootFifth, BP::Syncopated, BP::Syncopated},  // Chorus
        {BP::PedalTone, BP::WholeNote, BP::RootFifth},    // Bridge (dominant pedal)
        {BP::PedalTone, BP::WholeNote, BP::RootFifth},    // Outro (tonic pedal)
        {BP::RootFifth, BP::Syncopated, BP::Syncopated},  // Mix
    }},
    // Rock (aggressive, power-driven)
    {{
        {BP::WholeNote, BP::RootFifth, BP::RootFifth},    // Intro
        {BP::PowerDrive, BP::RootFifth, BP::Syncopated},  // A
        {BP::PowerDrive, BP::Driving, BP::Syncopated},    // B
        {BP::Aggressive, BP::PowerDrive, BP::Driving},    // Chorus
        {BP::RootFifth, BP::PowerDrive, BP::Syncopated},  // Bridge
        {BP::RootFifth, BP::WholeNote, BP::WholeNote},    // Outro
        {BP::Aggressive, BP::PowerDrive, BP::Driving},    // Mix
    }},
    // Dance (high-energy)
    {{
        {BP::WholeNote, BP::RootFifth, BP::RootFifth},    // Intro
        {BP::RootFifth, BP::Syncopated, BP::OctaveJump},  // A
        {BP::Syncopated, BP::Driving, BP::OctaveJump},    // B
        {BP::Aggressive, BP::Driving, BP::OctaveJump},    // Chorus
        {BP::RootFifth, BP::Syncopated, BP::Syncopated},  // Bridge
        {BP::RootFifth, BP::WholeNote, BP::WholeNote},    // Outro
        {BP::Aggressive, BP::OctaveJump, BP::Driving},    // Mix
    }},
    // Electronic (sidechain, modern EDM)
    {{
        {BP::PedalTone, BP::WholeNote, BP::SidechainPulse},    // Intro (tonic pedal)
        {BP::SidechainPulse, BP::RootFifth, BP::OctaveJump},   // A
        {BP::SidechainPulse, BP::Driving, BP::OctaveJump},     // B
        {BP::Aggressive, BP::SidechainPulse, BP::OctaveJump},  // Chorus
        {BP::PedalTone, BP::RootFifth, BP::SidechainPulse},    // Bridge (dominant pedal)
        {BP::PedalTone, BP::SidechainPulse, BP::RootFifth},    // Outro (tonic pedal)
        {BP::Aggressive, BP::OctaveJump, BP::Driving},         // Mix
    }},
    // Jazz (walking bass, groove - CityPop/ModernPop)
    {{
        {BP::WholeNote, BP::RootFifth, BP::RootFifth},  // Intro
        {BP::Groove, BP::Walking, BP::Syncopated},      // A
        {BP::Groove, BP::Walking, BP::Syncopated},      // B
        {BP::Groove, BP::Driving, BP::Walking},         // Chorus
        {BP::Walking, BP::Groove, BP::RootFifth},       // Bridge
        {BP::RootFifth, BP::WholeNote, BP::WholeNote},  // Outro
        {BP::Driving, BP::Groove, BP::Syncopated},      // Mix
    }},
    // Idol (bright, energetic)
    {{
        {BP::WholeNote, BP::RootFifth, BP::RootFifth},    // Intro
        {BP::RootFifth, BP::Syncopated, BP::OctaveJump},  // A
        {BP::Syncopated, BP::Driving, BP::OctaveJump},    // B
        {BP::Driving, BP::OctaveJump, BP::Aggressive},    // Chorus
        {BP::RootFifth, BP::Syncopated, BP::Syncopated},  // Bridge
        {BP::RootFifth, BP::WholeNote, BP::WholeNote},    // Outro
        {BP::Aggressive, BP::OctaveJump, BP::Driving},    // Mix
    }},
    // RnB (R&B/Neo-Soul - groove with chromatic approach, rootless voicing preference)
    {{
        {BP::PedalTone, BP::WholeNote, BP::RootFifth},   // Intro (tonic pedal)
        {BP::Groove, BP::Walking, BP::Syncopated},       // A (smooth groove)
        {BP::Groove, BP::Syncopated, BP::Walking},       // B (building groove)
        {BP::Groove, BP::Driving, BP::Syncopated},       // Chorus (full groove)
        {BP::PedalTone, BP::Groove, BP::RootFifth},      // Bridge (dominant pedal)
        {BP::PedalTone, BP::WholeNote, BP::RootFifth},   // Outro (tonic pedal)
        {BP::Groove, BP::Driving, BP::Syncopated},       // Mix
    }},
    // Latin (Latin Pop - tresillo 3+3+2 pattern)
    {{
        {BP::WholeNote, BP::RootFifth, BP::RootFifth},   // Intro
        {BP::Tresillo, BP::Syncopated, BP::RootFifth},   // A (tresillo pattern)
        {BP::Tresillo, BP::Syncopated, BP::Driving},     // B (tresillo + energy)
        {BP::Tresillo, BP::OctaveJump, BP::Aggressive},  // Chorus (full tresillo)
        {BP::RootFifth, BP::Tresillo, BP::Syncopated},   // Bridge
        {BP::RootFifth, BP::WholeNote, BP::WholeNote},   // Outro
        {BP::Aggressive, BP::Tresillo, BP::OctaveJump},  // Mix
    }},
    // Trap808 (Trap - long sustained 808 sub-bass)
    {{
        {BP::PedalTone, BP::SubBass808, BP::WholeNote},  // Intro (sustained 808)
        {BP::SubBass808, BP::WholeNote, BP::RootFifth},  // A (long 808 notes)
        {BP::SubBass808, BP::Syncopated, BP::WholeNote}, // B (808 with glide)
        {BP::SubBass808, BP::Syncopated, BP::RootFifth}, // Chorus (808 drive)
        {BP::PedalTone, BP::SubBass808, BP::WholeNote},  // Bridge (808 pedal)
        {BP::PedalTone, BP::SubBass808, BP::WholeNote},  // Outro (808 fadeout)
        {BP::SubBass808, BP::Aggressive, BP::Driving},   // Mix (808 energy)
    }},
    // Lofi (Lo-fi - simple patterns, pedal tone preference, low velocity)
    {{
        {BP::PedalTone, BP::WholeNote, BP::RootFifth},   // Intro (tonic pedal)
        {BP::RootFifth, BP::WholeNote, BP::Walking},     // A (simple walking)
        {BP::RootFifth, BP::Groove, BP::WholeNote},      // B (subtle groove)
        {BP::Groove, BP::RootFifth, BP::Walking},        // Chorus (relaxed groove)
        {BP::PedalTone, BP::WholeNote, BP::RootFifth},   // Bridge (pedal tone)
        {BP::PedalTone, BP::WholeNote, BP::RootFifth},   // Outro (tonic pedal)
        {BP::Groove, BP::RootFifth, BP::Walking},        // Mix
    }},
};

BassGenre getMoodBassGenre(Mood mood) {
  uint8_t idx = static_cast<uint8_t>(mood);
  if (idx < sizeof(MOOD_BASS_GENRES) / sizeof(MOOD_BASS_GENRES[0])) {
    return MOOD_BASS_GENRES[idx];
  }
  return BassGenre::Standard;  // fallback
}

// ============================================================================
// Mood Program Mapping Table
// ============================================================================
//
// Maps each Mood to appropriate MIDI program numbers for all melodic tracks.
// Program numbers follow General MIDI (GM) standard:
//   0 = Acoustic Grand Piano
//   1 = Bright Acoustic Piano
//   4 = Electric Piano 1
//   5 = Electric Piano 2
//  11 = Vibraphone
//  30 = Distortion Guitar
//  32 = Acoustic Bass
//  33 = Electric Bass (finger)
//  34 = Electric Bass (pick)
//  36 = Slap Bass 1
//  38 = Synth Bass 1
//  48 = String Ensemble 1
//  49 = String Ensemble 2
//  61 = Brass Section
//  80 = Lead 1 (square)
//  81 = Lead 2 (sawtooth)
//  89 = Pad 2 (warm)
//
// Structure: {vocal, chord, bass, motif, arpeggio, aux}
//
constexpr MoodProgramSet MOOD_PROGRAMS[24] = {
    // 0: StraightPop - Standard pop instruments
    // Structure: {vocal, chord, bass, motif, arpeggio, aux}
    // Note: arpeggio program is now managed by getArpeggioStyleForMood() (single source of truth).
    // The arpeggio column here uses the default (81) as a fallback only.
    {0, 4, 33, 81, 81, 89},
    // 1: BrightUpbeat - Bright piano, brighter overall
    {1, 5, 33, 81, 81, 89},
    // 2: EnergeticDance - Synth bass for dance energy
    {0, 4, 38, 81, 81, 89},
    // 3: LightRock - Distortion guitar, pick bass
    {0, 30, 34, 81, 81, 89},
    // 4: MidPop - Standard pop instruments
    {0, 4, 33, 81, 81, 89},
    // 5: EmotionalPop - Square lead, strings aux
    {0, 4, 33, 80, 81, 49},
    // 6: Sentimental - Vibraphone, acoustic bass, strings
    {11, 0, 32, 80, 81, 49},
    // 7: Chill - EP dominant, warm pad
    {4, 4, 33, 89, 81, 89},
    // 8: Ballad - Piano, acoustic bass, strings
    {0, 0, 32, 48, 81, 49},
    // 9: DarkPop - Synth bass for dark atmosphere
    {0, 4, 38, 81, 81, 89},
    // 10: Dramatic - Strings for dramatic impact
    {0, 48, 33, 81, 81, 49},
    // 11: Nostalgic - EP for retro feel
    {4, 4, 33, 80, 81, 89},
    // 12: ModernPop - Standard modern pop
    {0, 4, 33, 81, 81, 89},
    // 13: ElectroPop - Full synth setup
    {81, 81, 38, 81, 81, 89},
    // 14: IdolPop - Bright instruments
    {1, 5, 33, 81, 81, 89},
    // 15: Anthem - Strings for epic feel
    {0, 48, 33, 81, 81, 49},
    // 16: Yoasobi - Full synth anime style
    {81, 81, 38, 81, 81, 89},
    // 17: Synthwave - Synth lead, EP, synth bass
    {81, 4, 38, 81, 81, 89},
    // 18: FutureBass - Full synth EDM
    {81, 81, 38, 81, 81, 89},
    // 19: CityPop - EP, slap bass, brass
    {4, 4, 36, 61, 81, 61},
    // Genre expansion moods
    // 20: RnBNeoSoul - EP, finger bass, warm pad (Wurlitzer/Rhodes vibe)
    {4, 4, 33, 89, 81, 89},
    // 21: LatinPop - Brass, latin percussion feel, finger bass
    {0, 4, 33, 61, 81, 61},
    // 22: Trap - Synth lead, synth bass (808), dark pad
    {81, 81, 38, 81, 81, 89},
    // 23: Lofi - EP, acoustic bass, warm pad
    {4, 4, 32, 89, 81, 89},
};

const MoodProgramSet& getMoodPrograms(Mood mood) {
  uint8_t idx = static_cast<uint8_t>(mood);
  if (idx >= sizeof(MOOD_PROGRAMS) / sizeof(MOOD_PROGRAMS[0])) {
    idx = 0;  // fallback to StraightPop
  }
  return MOOD_PROGRAMS[idx];
}

const BassGenrePatterns& getBassGenrePatterns(BassGenre genre) {
  uint8_t idx = static_cast<uint8_t>(genre);
  if (idx >= static_cast<uint8_t>(BassGenre::COUNT)) {
    idx = 0;  // fallback to Standard
  }
  return BASS_GENRE_PATTERNS[idx];
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

VocalStylePreset selectRandomVocalStyle(uint8_t style_id, uint32_t seed) {
  if (style_id >= STYLE_PRESET_COUNT) {
    style_id = 0;
  }

  // Calculate total weight (skip entries with weight 0)
  uint32_t total_weight = 0;
  for (size_t i = 0; i < STYLE_VOCAL_COUNT; ++i) {
    if (STYLE_VOCAL_STYLES[style_id][i].weight > 0) {
      total_weight += STYLE_VOCAL_STYLES[style_id][i].weight;
    }
  }

  // If no weights defined, fallback to Standard
  if (total_weight == 0) {
    return VocalStylePreset::Standard;
  }

  // Use seed to generate a random value
  std::mt19937 rng(seed);
  std::uniform_int_distribution<uint32_t> dist(0, total_weight - 1);
  uint32_t roll = dist(rng);

  // Select style based on weighted random roll
  uint32_t cumulative = 0;
  for (size_t i = 0; i < STYLE_VOCAL_COUNT; ++i) {
    if (STYLE_VOCAL_STYLES[style_id][i].weight > 0) {
      cumulative += STYLE_VOCAL_STYLES[style_id][i].weight;
      if (roll < cumulative) {
        return STYLE_VOCAL_STYLES[style_id][i].style;
      }
    }
  }

  // Fallback: return first style
  return STYLE_VOCAL_STYLES[style_id][0].style;
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

  // Validate modulation timing (0-4)
  if (static_cast<uint8_t>(config.modulation_timing) > 4) {
    return SongConfigError::InvalidModulationTiming;
  }

  // Validate modulation amount
  if (config.modulation_timing != ModulationTiming::None) {
    if (config.modulation_semitones < 1 || config.modulation_semitones > 4) {
      return SongConfigError::InvalidModulationAmount;
    }
  }

  // Validate key (0-11)
  if (static_cast<uint8_t>(config.key) > 11) {
    return SongConfigError::InvalidKey;
  }

  // Validate composition style (0-2)
  if (static_cast<uint8_t>(config.composition_style) > 2) {
    return SongConfigError::InvalidCompositionStyle;
  }

  // Validate arpeggio pattern (0-3)
  if (static_cast<uint8_t>(config.arpeggio.pattern) > 3) {
    return SongConfigError::InvalidArpeggioPattern;
  }

  // Validate arpeggio speed (0-2)
  if (static_cast<uint8_t>(config.arpeggio.speed) > 2) {
    return SongConfigError::InvalidArpeggioSpeed;
  }

  // Validate vocal style (0-12)
  if (static_cast<uint8_t>(config.vocal_style) > 12) {
    return SongConfigError::InvalidVocalStyle;
  }

  // Validate melody template (0-7)
  if (static_cast<uint8_t>(config.melody_template) > 7) {
    return SongConfigError::InvalidMelodyTemplate;
  }

  // Validate melodic complexity (0-2)
  if (static_cast<uint8_t>(config.melodic_complexity) > 2) {
    return SongConfigError::InvalidMelodicComplexity;
  }

  // Validate hook intensity (0-3)
  if (static_cast<uint8_t>(config.hook_intensity) > 3) {
    return SongConfigError::InvalidHookIntensity;
  }

  // Validate vocal groove (0-5)
  if (static_cast<uint8_t>(config.vocal_groove) > 5) {
    return SongConfigError::InvalidVocalGroove;
  }

  // Validate call density (0-3)
  if (static_cast<uint8_t>(config.call_density) > 3) {
    return SongConfigError::InvalidCallDensity;
  }

  // Validate intro chant (0-2)
  if (static_cast<uint8_t>(config.intro_chant) > 2) {
    return SongConfigError::InvalidIntroChant;
  }

  // Validate mix pattern (0-2)
  if (static_cast<uint8_t>(config.mix_pattern) > 2) {
    return SongConfigError::InvalidMixPattern;
  }

  // Validate motif repeat scope (0-1)
  if (static_cast<uint8_t>(config.motif_repeat_scope) > 1) {
    return SongConfigError::InvalidMotifRepeatScope;
  }

  // Validate arrangement growth (0-1)
  if (static_cast<uint8_t>(config.arrangement_growth) > 1) {
    return SongConfigError::InvalidArrangementGrowth;
  }

  // Validate call/duration compatibility
  // Check if calls might be enabled (Enabled or Auto with potential call style)
  bool calls_may_be_enabled = (config.call_setting != CallSetting::Disabled);
  if (calls_may_be_enabled && config.target_duration_seconds > 0) {
    uint16_t resolved_bpm = (config.bpm != 0) ? config.bpm : preset.tempo_default;
    uint16_t min_seconds =
        getMinimumSecondsForCall(config.intro_chant, config.mix_pattern, resolved_bpm);

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
      0.0f,  // None
      8.0f,  // Standard (~8 sec)
      16.0f  // Tiger (~16 sec)
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

// ============================================================================
// VocalStylePreset Data API Implementation
// ============================================================================

const VocalStylePresetData& getVocalStylePresetData(VocalStylePreset style) {
  uint8_t idx = static_cast<uint8_t>(style);
  if (idx >= VOCAL_STYLE_PRESET_COUNT) {
    idx = static_cast<uint8_t>(VocalStylePreset::Standard);  // fallback
  }
  return VOCAL_STYLE_PRESET_DATA[idx];
}

// ============================================================================
// Name-based Lookup Functions
// ============================================================================

namespace {

// Case-insensitive string comparison
bool strcasecmpEqual(const char* a, const std::string& b) {
  const char* p = a;
  size_t i = 0;
  while (*p && i < b.size()) {
    char ac = (*p >= 'A' && *p <= 'Z') ? (*p + 32) : *p;
    char bc = (b[i] >= 'A' && b[i] <= 'Z') ? (b[i] + 32) : b[i];
    if (ac != bc) return false;
    ++p;
    ++i;
  }
  return (*p == '\0' && i == b.size());
}

}  // anonymous namespace

std::optional<Mood> findMoodByName(const std::string& name) {
  constexpr size_t count = sizeof(MOOD_NAMES) / sizeof(MOOD_NAMES[0]);
  for (size_t i = 0; i < count; ++i) {
    if (strcasecmpEqual(MOOD_NAMES[i], name)) {
      return static_cast<Mood>(i);
    }
  }
  return std::nullopt;
}

std::optional<StructurePattern> findStructurePatternByName(const std::string& name) {
  constexpr size_t count = sizeof(STRUCTURE_NAMES) / sizeof(STRUCTURE_NAMES[0]);
  for (size_t i = 0; i < count; ++i) {
    if (strcasecmpEqual(STRUCTURE_NAMES[i], name)) {
      return static_cast<StructurePattern>(i);
    }
  }
  return std::nullopt;
}

std::optional<uint8_t> findChordProgressionByName(const std::string& name) {
  // Chord progressions are referenced by their pattern string or common name
  // For now, this is a basic implementation; extend as needed
  // Common chord progression names
  static const std::pair<const char*, uint8_t> CHORD_NAMES[] = {
      {"canonical", 0},  // I-V-vi-IV
      {"pop", 0},
      {"fifties", 1},      // I-vi-IV-V
      {"doo_wop", 1},
      {"jazz", 2},         // ii-V-I-vi
      {"royal_road", 3},   // IV-V-iii-vi (王道進行)
      {"strong_wish", 4},  // I-IV-V-I
      {"four_chord", 5},   // vi-IV-I-V
      {"emotional", 6},    // I-iii-vi-IV
      {"jpop", 7},         // IV-iii-vi-V
      {"tsubasa", 8},      // I-V-vi-iii-IV
      {"dramatic", 9},     // vi-V-IV-V
      {"minor", 10},       // i-VII-VI-V
      {"sad", 11},         // vi-IV-V-I
      {"anthem", 12},      // I-IV-vi-V
      {"ballad", 13},      // I-V/VII-vi-IV
      {"pachelbel", 14},   // I-V-vi-iii-IV-I-IV-V
      {"blues", 15},       // I-I-I-I-IV-IV-I-I-V-IV-I-V
      {"vamp", 16},        // i-VII (2 chord)
      {"hypnotic", 17},    // vi-V (2 chord)
  };

  for (const auto& [chord_name, id] : CHORD_NAMES) {
    if (strcasecmpEqual(chord_name, name)) {
      return id;
    }
  }
  return std::nullopt;
}

}  // namespace midisketch
