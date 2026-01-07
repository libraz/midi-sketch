#include "track/vocal.h"
#include "core/chord.h"
#include "core/chord_utils.h"
#include "core/harmony_context.h"
#include "core/melody_templates.h"
#include "core/pitch_utils.h"
#include "core/velocity.h"
#include "track/melody_designer.h"
#include <algorithm>

namespace midisketch {

namespace {

// Get register shift for section type based on melody params
int8_t getRegisterShift(SectionType type, const StyleMelodyParams& params) {
  switch (type) {
    case SectionType::A:
      return params.verse_register_shift;
    case SectionType::B:
      return params.prechorus_register_shift;
    case SectionType::Chorus:
      return params.chorus_register_shift;
    case SectionType::Bridge:
      return params.bridge_register_shift;
    default:
      return 0;
  }
}

// Check if section type should have vocals
bool sectionHasVocals(SectionType type) {
  switch (type) {
    case SectionType::Intro:
    case SectionType::Interlude:
    case SectionType::Outro:
    case SectionType::Chant:
    case SectionType::MixBreak:
      return false;
    default:
      return true;
  }
}

// Apply modulation transpose to notes after modulation point
void applyModulation(std::vector<NoteEvent>& notes, Tick mod_tick, int8_t mod_amount) {
  if (mod_tick == 0 || mod_amount == 0) return;

  for (auto& note : notes) {
    if (note.startTick >= mod_tick) {
      note.note = static_cast<uint8_t>(
          std::clamp(static_cast<int>(note.note) + mod_amount, 0, 127));
    }
  }
}

// Apply velocity balance for track role
void applyVelocityBalance(std::vector<NoteEvent>& notes, float scale) {
  for (auto& note : notes) {
    int vel = static_cast<int>(note.velocity * scale);
    note.velocity = static_cast<uint8_t>(std::clamp(vel, 1, 127));
  }
}

// Remove overlapping notes by adjusting duration
void removeOverlaps(std::vector<NoteEvent>& notes) {
  if (notes.size() < 2) return;

  // Sort by start tick
  std::sort(notes.begin(), notes.end(),
            [](const NoteEvent& a, const NoteEvent& b) {
              return a.startTick < b.startTick;
            });

  // Adjust durations to prevent overlap
  for (size_t i = 0; i < notes.size() - 1; ++i) {
    Tick end_tick = notes[i].startTick + notes[i].duration;
    Tick next_start = notes[i + 1].startTick;

    if (end_tick > next_start) {
      // Truncate current note to end before next note
      Tick min_gap = 1;  // Minimum gap between notes
      Tick max_duration = next_start - notes[i].startTick - min_gap;
      notes[i].duration = std::max(static_cast<Tick>(1), max_duration);
    }
  }
}

}  // namespace

void generateVocalTrack(MidiTrack& track, Song& song,
                        const GeneratorParams& params, std::mt19937& rng,
                        const MidiTrack* motif_track,
                        const HarmonyContext* harmony_ctx) {
  // Unused - template is selected per-section below
  (void)params.vocal_style;

  // Determine effective vocal range
  uint8_t effective_vocal_low = params.vocal_low;
  uint8_t effective_vocal_high = params.vocal_high;

  // Adjust range for BackgroundMotif to avoid collision with motif
  if (params.composition_style == CompositionStyle::BackgroundMotif &&
      motif_track != nullptr && !motif_track->empty()) {
    auto [motif_low, motif_high] = motif_track->analyzeRange();

    if (motif_high > 72) {  // Motif in high register
      effective_vocal_high = std::min(effective_vocal_high, static_cast<uint8_t>(72));
      if (effective_vocal_high - effective_vocal_low < 12) {
        effective_vocal_low = std::max(static_cast<uint8_t>(48),
                                        static_cast<uint8_t>(effective_vocal_high - 12));
      }
    } else if (motif_low < 60) {  // Motif in low register
      effective_vocal_low = std::max(effective_vocal_low, static_cast<uint8_t>(65));
      if (effective_vocal_high - effective_vocal_low < 12) {
        effective_vocal_high = std::min(static_cast<uint8_t>(96),
                                         static_cast<uint8_t>(effective_vocal_low + 12));
      }
    }
  }

  // Calculate tessitura
  TessituraRange tessitura = calculateTessitura(effective_vocal_low, effective_vocal_high);

  // Get chord progression
  const auto& progression = getChordProgression(params.chord_id);

  // Velocity scale for composition style
  float velocity_scale = 1.0f;
  if (params.composition_style == CompositionStyle::BackgroundMotif) {
    velocity_scale = 0.7f;
  } else if (params.composition_style == CompositionStyle::SynthDriven) {
    velocity_scale = 0.75f;
  }

  // Create MelodyDesigner
  MelodyDesigner designer;

  // Create dummy HarmonyContext if not provided
  HarmonyContext dummy_harmony;
  const HarmonyContext& harmony = harmony_ctx ? *harmony_ctx : dummy_harmony;

  // Collect all notes
  std::vector<NoteEvent> all_notes;

  // Process each section
  for (const auto& section : song.arrangement().sections()) {
    // Skip sections without vocals
    if (!sectionHasVocals(section.type)) {
      continue;
    }

    // Get template for this section type (may differ by section)
    MelodyTemplateId section_template_id = getDefaultTemplateForStyle(
        params.vocal_style, section.type);
    const MelodyTemplate& section_tmpl = getTemplate(section_template_id);

    // Calculate section boundaries
    Tick section_start = section.start_tick;
    Tick section_end = section.start_tick + section.bars * TICKS_PER_BAR;

    // Get chord for this section
    int chord_idx = section.startBar % progression.length;
    int8_t chord_degree = progression.at(chord_idx);

    // Apply register shift for section (clamped to original range)
    int8_t register_shift = getRegisterShift(section.type, params.melody_params);
    // Register shift adjusts the preferred center but must not exceed original range
    uint8_t section_vocal_low = static_cast<uint8_t>(
        std::clamp(static_cast<int>(effective_vocal_low) + register_shift,
                   static_cast<int>(effective_vocal_low),
                   static_cast<int>(effective_vocal_high) - 6));  // At least 6 semitone range
    uint8_t section_vocal_high = static_cast<uint8_t>(
        std::clamp(static_cast<int>(effective_vocal_high) + register_shift,
                   static_cast<int>(effective_vocal_low) + 6,
                   static_cast<int>(effective_vocal_high)));  // Stay within original high

    // Recalculate tessitura for section
    TessituraRange section_tessitura = calculateTessitura(section_vocal_low, section_vocal_high);

    // Create section context
    MelodyDesigner::SectionContext ctx;
    ctx.section_type = section.type;
    ctx.section_start = section_start;
    ctx.section_end = section_end;
    ctx.section_bars = section.bars;
    ctx.chord_degree = chord_degree;
    ctx.key_offset = 0;  // Always C major internally
    ctx.tessitura = section_tessitura;
    ctx.vocal_low = section_vocal_low;
    ctx.vocal_high = section_vocal_high;

    // Generate melody for section
    std::vector<NoteEvent> section_notes = designer.generateSection(
        section_tmpl, ctx, harmony, rng);

    // Apply HarmonyContext collision avoidance
    if (harmony_ctx != nullptr) {
      for (auto& note : section_notes) {
        uint8_t safe_pitch = harmony_ctx->getSafePitch(
            note.note, note.startTick, note.duration, TrackRole::Vocal,
            section_vocal_low, section_vocal_high);
        note.note = safe_pitch;
      }
    }

    // Add to collected notes
    for (const auto& note : section_notes) {
      all_notes.push_back(note);
    }
  }

  // NOTE: Modulation is NOT applied internally.
  // MidiWriter applies modulation to all tracks when generating MIDI bytes.
  // This ensures consistent behavior and avoids double-modulation.

  // Remove overlapping notes
  removeOverlaps(all_notes);

  // Apply velocity scale
  applyVelocityBalance(all_notes, velocity_scale);

  // Add notes to track
  for (const auto& note : all_notes) {
    track.addNote(note.startTick, note.duration, note.note, note.velocity);
  }
}

}  // namespace midisketch
