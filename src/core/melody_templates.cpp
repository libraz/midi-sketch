/**
 * @file melody_templates.cpp
 * @brief Melody template definitions and accessors.
 */

#include "core/melody_templates.h"

namespace midisketch {

namespace {

// ============================================================================
// Template Definitions (7 templates)
// ============================================================================

// Template 1: PlateauTalk (NewJeans/Billie style)
// High plateau with talk-sing characteristics
// Target pitch is now mandatory for melodic direction.
constexpr MelodyTemplate kPlateauTalk = {
    "PlateauTalk",
    // Pitch constraints
    4,     // tessitura_range: narrow ±4 semitones
    0.7f,  // plateau_ratio: 70% same pitch
    2,     // max_step: 2 semitones

    // Target pitch (mandatory for melodic direction)
    true,   // has_target_pitch: ENABLED - creates forward motion
    0.6f,   // target_attraction_start: late attraction (plateau first)
    0.5f,   // target_attraction_strength: moderate (preserve plateau feel)

    // Rhythm
    true,   // rhythm_driven
    0.4f,   // sixteenth_density: 40%

    // Vocal constraints
    true,   // vowel_constraint
    true,   // leap_as_event

    // Phrase characteristics
    0.6f,   // phrase_end_resolution
    0.1f,   // long_note_ratio: few long notes
    0.15f,  // tension_allowance: low tension

    // Human body constraints
    8,      // max_phrase_beats
    0.2f,   // high_register_plateau_boost
    1,      // post_high_rest_beats

    // Modern pop features
    3,      // hook_note_count
    3,      // hook_repeat_count
    true    // allow_talk_sing
};

// Template 2: RunUpTarget (YOASOBI/Ado style)
// Run up to target note with high energy
constexpr MelodyTemplate kRunUpTarget = {
    "RunUpTarget",
    // Pitch constraints
    6,     // tessitura_range: medium ±6 semitones
    0.2f,  // plateau_ratio: low, keep moving
    3,     // max_step: 3 semitones

    // Target pitch
    true,   // has_target_pitch
    0.5f,   // target_attraction_start: start midway
    0.8f,   // target_attraction_strength: strong attraction

    // Rhythm
    true,   // rhythm_driven
    0.6f,   // sixteenth_density: 60%

    // Vocal constraints
    true,   // vowel_constraint
    true,   // leap_as_event

    // Phrase characteristics
    0.9f,   // phrase_end_resolution: high resolution
    0.15f,  // long_note_ratio
    0.25f,  // tension_allowance: moderate

    // Human body constraints
    6,      // max_phrase_beats: shorter phrases
    0.3f,   // high_register_plateau_boost
    2,      // post_high_rest_beats

    // Modern pop features
    4,      // hook_note_count
    2,      // hook_repeat_count
    false   // allow_talk_sing: not talk-sing
};

// Template 3: DownResolve (B-melody generic)
// Descending resolution, pre-chorus style
constexpr MelodyTemplate kDownResolve = {
    "DownResolve",
    // Pitch constraints
    5,     // tessitura_range: medium
    0.4f,  // plateau_ratio: moderate
    2,     // max_step

    // Target pitch
    true,   // has_target_pitch
    0.6f,   // target_attraction_start: late attraction
    0.7f,   // target_attraction_strength

    // Rhythm
    false,  // rhythm_driven: melody-driven
    0.2f,   // sixteenth_density: low

    // Vocal constraints
    true,   // vowel_constraint
    true,   // leap_as_event

    // Phrase characteristics
    0.95f,  // phrase_end_resolution: very high
    0.25f,  // long_note_ratio
    0.2f,   // tension_allowance

    // Human body constraints
    8,      // max_phrase_beats
    0.15f,  // high_register_plateau_boost
    1,      // post_high_rest_beats

    // Modern pop features
    2,      // hook_note_count
    2,      // hook_repeat_count
    false   // allow_talk_sing
};

// Template 4: HookRepeat (TikTok/K-POP)
// Short repeating hook for maximum catchiness
// Target pitch creates a "destination" even for short hooks.
constexpr MelodyTemplate kHookRepeat = {
    "HookRepeat",
    // Pitch constraints
    3,     // tessitura_range: very narrow
    0.5f,  // plateau_ratio: moderate
    2,     // max_step

    // Target pitch (mandatory for melodic direction)
    true,   // has_target_pitch: ENABLED - hook resolves to target
    0.5f,   // target_attraction_start: mid-phrase attraction
    0.7f,   // target_attraction_strength: strong (hook clarity)

    // Rhythm
    true,   // rhythm_driven
    0.5f,   // sixteenth_density

    // Vocal constraints
    false,  // vowel_constraint: less strict for hooks
    true,   // leap_as_event

    // Phrase characteristics
    0.8f,   // phrase_end_resolution
    0.2f,   // long_note_ratio
    0.1f,   // tension_allowance: low

    // Human body constraints
    4,      // max_phrase_beats: very short
    0.1f,   // high_register_plateau_boost
    1,      // post_high_rest_beats

    // Modern pop features
    2,      // hook_note_count: minimum for hook
    4,      // hook_repeat_count: maximum repetition
    true    // allow_talk_sing
};

// Template 5: SparseAnchor (Official髭男dism style)
// Sparse anchor notes with space
constexpr MelodyTemplate kSparseAnchor = {
    "SparseAnchor",
    // Pitch constraints
    7,     // tessitura_range: wide
    0.3f,  // plateau_ratio: low
    4,     // max_step: larger steps allowed

    // Target pitch
    true,   // has_target_pitch
    0.7f,   // target_attraction_start: late
    0.5f,   // target_attraction_strength: moderate

    // Rhythm
    false,  // rhythm_driven: melody-driven
    0.1f,   // sixteenth_density: very low

    // Vocal constraints
    true,   // vowel_constraint
    true,   // leap_as_event

    // Phrase characteristics
    0.85f,  // phrase_end_resolution
    0.4f,   // long_note_ratio: many long notes
    0.3f,   // tension_allowance: higher

    // Human body constraints
    12,     // max_phrase_beats: longer phrases
    0.25f,  // high_register_plateau_boost
    2,      // post_high_rest_beats

    // Modern pop features
    3,      // hook_note_count
    2,      // hook_repeat_count
    false   // allow_talk_sing
};

// Template 6: CallResponse (Duet style)
// Call and response pattern
constexpr MelodyTemplate kCallResponse = {
    "CallResponse",
    // Pitch constraints
    5,     // tessitura_range
    0.35f, // plateau_ratio
    3,     // max_step

    // Target pitch
    true,   // has_target_pitch
    0.4f,   // target_attraction_start: early
    0.6f,   // target_attraction_strength

    // Rhythm
    true,   // rhythm_driven
    0.3f,   // sixteenth_density

    // Vocal constraints
    true,   // vowel_constraint
    true,   // leap_as_event

    // Phrase characteristics
    0.75f,  // phrase_end_resolution
    0.2f,   // long_note_ratio
    0.2f,   // tension_allowance

    // Human body constraints
    4,      // max_phrase_beats: short for call-response
    0.2f,   // high_register_plateau_boost
    1,      // post_high_rest_beats

    // Modern pop features
    3,      // hook_note_count
    2,      // hook_repeat_count
    false   // allow_talk_sing
};

// Template 7: JumpAccent (Emotional peak)
// Jump accent for climactic moments
constexpr MelodyTemplate kJumpAccent = {
    "JumpAccent",
    // Pitch constraints
    8,     // tessitura_range: widest
    0.25f, // plateau_ratio: low
    5,     // max_step: large steps for jumps

    // Target pitch
    true,   // has_target_pitch
    0.3f,   // target_attraction_start: early
    0.9f,   // target_attraction_strength: very strong

    // Rhythm
    false,  // rhythm_driven: melody-driven
    0.15f,  // sixteenth_density: low

    // Vocal constraints
    true,   // vowel_constraint
    true,   // leap_as_event: important for jumps

    // Phrase characteristics
    0.9f,   // phrase_end_resolution: high
    0.35f,  // long_note_ratio
    0.35f,  // tension_allowance: high for drama

    // Human body constraints
    6,      // max_phrase_beats
    0.3f,   // high_register_plateau_boost: significant
    2,      // post_high_rest_beats

    // Modern pop features
    3,      // hook_note_count
    2,      // hook_repeat_count
    false   // allow_talk_sing
};

// Template array for lookup
const MelodyTemplate* kTemplates[] = {
    nullptr,        // Auto (index 0) - not used directly
    &kPlateauTalk,  // 1
    &kRunUpTarget,  // 2
    &kDownResolve,  // 3
    &kHookRepeat,   // 4
    &kSparseAnchor, // 5
    &kCallResponse, // 6
    &kJumpAccent    // 7
};

// Default fallback template
constexpr MelodyTemplate kDefaultTemplate = kPlateauTalk;

// ============================================================================
// Style × Section → Template Mapping Table
// ============================================================================
//
// This table defines style-specific template overrides for each section.
// If a (style, section) pair is not found, the section default is used.
//
// Format: {style, section, template_id}
//
struct StyleSectionTemplate {
  VocalStylePreset style;
  SectionType section;
  MelodyTemplateId template_id;
};

constexpr StyleSectionTemplate kStyleSectionOverrides[] = {
    // -------------------------------------------------------------------------
    // Verse (Section A) overrides
    // -------------------------------------------------------------------------
    // Vocaloid/UltraVocaloid: energetic run-up style
    {VocalStylePreset::Vocaloid, SectionType::A, MelodyTemplateId::RunUpTarget},
    {VocalStylePreset::UltraVocaloid, SectionType::A, MelodyTemplateId::RunUpTarget},
    // CityPop/CoolSynth: talk-sing plateau (explicit, same as default)
    {VocalStylePreset::CityPop, SectionType::A, MelodyTemplateId::PlateauTalk},
    {VocalStylePreset::CoolSynth, SectionType::A, MelodyTemplateId::PlateauTalk},
    // Ballad: sparse anchor notes
    {VocalStylePreset::Ballad, SectionType::A, MelodyTemplateId::SparseAnchor},

    // -------------------------------------------------------------------------
    // Chorus overrides
    // -------------------------------------------------------------------------
    // Vocaloid/UltraVocaloid: keep energetic run-up
    {VocalStylePreset::Vocaloid, SectionType::Chorus, MelodyTemplateId::RunUpTarget},
    {VocalStylePreset::UltraVocaloid, SectionType::Chorus, MelodyTemplateId::RunUpTarget},
    // Idol/BrightKira: catchy hook repeat (explicit, same as default)
    {VocalStylePreset::Idol, SectionType::Chorus, MelodyTemplateId::HookRepeat},
    {VocalStylePreset::BrightKira, SectionType::Chorus, MelodyTemplateId::HookRepeat},
    // PowerfulShout/Rock: dramatic jump accent
    {VocalStylePreset::PowerfulShout, SectionType::Chorus, MelodyTemplateId::JumpAccent},
    {VocalStylePreset::Rock, SectionType::Chorus, MelodyTemplateId::JumpAccent},
    // Ballad: sparse anchor
    {VocalStylePreset::Ballad, SectionType::Chorus, MelodyTemplateId::SparseAnchor},
};

constexpr size_t kStyleSectionOverrideCount =
    sizeof(kStyleSectionOverrides) / sizeof(kStyleSectionOverrides[0]);

// ============================================================================
// Section Default Templates
// ============================================================================
//
// Default template for each section type when no style override exists.
//
// Format: {section, default_template}
//
struct SectionDefaultTemplate {
  SectionType section;
  MelodyTemplateId template_id;
};

constexpr SectionDefaultTemplate kSectionDefaults[] = {
    {SectionType::Intro, MelodyTemplateId::SparseAnchor},
    {SectionType::Outro, MelodyTemplateId::SparseAnchor},
    {SectionType::A, MelodyTemplateId::PlateauTalk},       // Verse default
    {SectionType::B, MelodyTemplateId::DownResolve},       // Pre-chorus
    {SectionType::Chorus, MelodyTemplateId::HookRepeat},   // Chorus default
    {SectionType::Bridge, MelodyTemplateId::JumpAccent},
    {SectionType::Interlude, MelodyTemplateId::SparseAnchor},
    {SectionType::Chant, MelodyTemplateId::CallResponse},
    {SectionType::MixBreak, MelodyTemplateId::CallResponse},
};

constexpr size_t kSectionDefaultCount =
    sizeof(kSectionDefaults) / sizeof(kSectionDefaults[0]);

}  // namespace

const MelodyTemplate& getTemplate(MelodyTemplateId id) {
  uint8_t idx = static_cast<uint8_t>(id);
  if (idx == 0 || idx > MELODY_TEMPLATE_COUNT) {
    // Auto or out of range - return default
    return kDefaultTemplate;
  }
  return *kTemplates[idx];
}

MelodyTemplateId getDefaultTemplateForStyle(VocalStylePreset style,
                                             SectionType section) {
  // 1. Check for style-specific override
  for (size_t i = 0; i < kStyleSectionOverrideCount; ++i) {
    if (kStyleSectionOverrides[i].style == style &&
        kStyleSectionOverrides[i].section == section) {
      return kStyleSectionOverrides[i].template_id;
    }
  }

  // 2. Use section default
  for (size_t i = 0; i < kSectionDefaultCount; ++i) {
    if (kSectionDefaults[i].section == section) {
      return kSectionDefaults[i].template_id;
    }
  }

  // 3. Fallback
  return MelodyTemplateId::PlateauTalk;
}

void getAuxConfigsForTemplate(MelodyTemplateId id,
                               AuxConfig* out_configs,
                               uint8_t* out_count) {
  if (!out_configs || !out_count) return;

  // Default: no aux tracks
  *out_count = 0;

  switch (id) {
    case MelodyTemplateId::PlateauTalk:
      // Add pulse loop for rhythm support
      out_configs[0] = {
          AuxFunction::PulseLoop,
          -12,   // range_offset: one octave below
          5,     // range_width
          0.6f,  // velocity_ratio
          0.5f,  // density_ratio
          true   // sync_phrase_boundary
      };
      *out_count = 1;
      break;

    case MelodyTemplateId::RunUpTarget:
      // Add target hint and groove accent
      out_configs[0] = {
          AuxFunction::TargetHint,
          0,     // range_offset: same range
          7,     // range_width
          0.5f,  // velocity_ratio
          0.3f,  // density_ratio
          true   // sync_phrase_boundary
      };
      out_configs[1] = {
          AuxFunction::GrooveAccent,
          -7,    // range_offset: below
          5,     // range_width
          0.7f,  // velocity_ratio
          0.4f,  // density_ratio
          false  // sync_phrase_boundary
      };
      *out_count = 2;
      break;

    case MelodyTemplateId::DownResolve:
      // Add phrase tail for breathing
      out_configs[0] = {
          AuxFunction::PhraseTail,
          0,     // range_offset
          5,     // range_width
          0.5f,  // velocity_ratio
          0.2f,  // density_ratio
          true   // sync_phrase_boundary
      };
      *out_count = 1;
      break;

    case MelodyTemplateId::HookRepeat:
      // Add pulse loop for rhythm emphasis
      out_configs[0] = {
          AuxFunction::PulseLoop,
          -12,   // range_offset
          4,     // range_width: narrow
          0.7f,  // velocity_ratio
          0.6f,  // density_ratio
          false  // sync_phrase_boundary
      };
      *out_count = 1;
      break;

    case MelodyTemplateId::SparseAnchor:
      // Add emotional pad for atmosphere
      out_configs[0] = {
          AuxFunction::EmotionalPad,
          -5,    // range_offset: slightly below
          8,     // range_width: wide
          0.4f,  // velocity_ratio: soft
          0.15f, // density_ratio: very sparse
          true   // sync_phrase_boundary
      };
      *out_count = 1;
      break;

    case MelodyTemplateId::CallResponse:
      // Add target hint for response parts
      out_configs[0] = {
          AuxFunction::TargetHint,
          0,     // range_offset
          6,     // range_width
          0.6f,  // velocity_ratio
          0.4f,  // density_ratio
          true   // sync_phrase_boundary
      };
      *out_count = 1;
      break;

    case MelodyTemplateId::JumpAccent:
      // Add phrase tail and emotional pad
      out_configs[0] = {
          AuxFunction::PhraseTail,
          0,     // range_offset
          5,     // range_width
          0.5f,  // velocity_ratio
          0.25f, // density_ratio
          true   // sync_phrase_boundary
      };
      out_configs[1] = {
          AuxFunction::EmotionalPad,
          -7,    // range_offset
          6,     // range_width
          0.4f,  // velocity_ratio
          0.2f,  // density_ratio
          true   // sync_phrase_boundary
      };
      *out_count = 2;
      break;

    default:
      // Auto or unknown - no aux tracks
      *out_count = 0;
      break;
  }
}

}  // namespace midisketch
