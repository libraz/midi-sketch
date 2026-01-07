#include "track/vocal.h"
#include "core/chord.h"
#include "core/chord_utils.h"
#include "core/harmony_context.h"
#include "core/melody_templates.h"
#include "core/pitch_utils.h"
#include "core/velocity.h"
#include "track/melody_designer.h"
#include <algorithm>
#include <unordered_map>

namespace midisketch {

namespace {

// Cached phrase for section repetition
struct CachedPhrase {
  std::vector<NoteEvent> notes;  // Notes with timing relative to section start
  uint8_t bars;                   // Section length when cached
  uint8_t vocal_low;              // Vocal range when cached
  uint8_t vocal_high;
};

// Shift note timings by offset
std::vector<NoteEvent> shiftTiming(const std::vector<NoteEvent>& notes, Tick offset) {
  std::vector<NoteEvent> result;
  result.reserve(notes.size());
  for (const auto& note : notes) {
    NoteEvent shifted = note;
    shifted.startTick += offset;
    result.push_back(shifted);
  }
  return result;
}

// Adjust pitches to new vocal range
std::vector<NoteEvent> adjustPitchRange(const std::vector<NoteEvent>& notes,
                                         uint8_t orig_low, uint8_t orig_high,
                                         uint8_t new_low, uint8_t new_high) {
  if (orig_low == new_low && orig_high == new_high) {
    return notes;  // No adjustment needed
  }

  std::vector<NoteEvent> result;
  result.reserve(notes.size());

  // Calculate shift based on center points
  int orig_center = (orig_low + orig_high) / 2;
  int new_center = (new_low + new_high) / 2;
  int shift = new_center - orig_center;

  for (const auto& note : notes) {
    NoteEvent adjusted = note;
    int new_pitch = static_cast<int>(note.note) + shift;
    // Clamp to new range
    new_pitch = std::clamp(new_pitch, static_cast<int>(new_low), static_cast<int>(new_high));
    adjusted.note = static_cast<uint8_t>(new_pitch);
    result.push_back(adjusted);
  }
  return result;
}

// Convert notes to relative timing (subtract section start)
std::vector<NoteEvent> toRelativeTiming(const std::vector<NoteEvent>& notes, Tick section_start) {
  std::vector<NoteEvent> result;
  result.reserve(notes.size());
  for (const auto& note : notes) {
    NoteEvent relative = note;
    relative.startTick -= section_start;
    result.push_back(relative);
  }
  return result;
}

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

// Apply hook intensity effects to section notes
// Higher intensity = longer notes at section start, more emphasis
void applyHookIntensity(std::vector<NoteEvent>& notes, SectionType section_type,
                        HookIntensity intensity, Tick section_start) {
  if (intensity == HookIntensity::Off || notes.empty()) {
    return;
  }

  // Hook points: Chorus start, B section climax
  bool is_hook_section = (section_type == SectionType::Chorus ||
                          section_type == SectionType::B);
  if (!is_hook_section && intensity != HookIntensity::Strong) {
    return;  // Only Strong applies to all sections
  }

  // Find notes at or near section start (first beat)
  Tick hook_window = TICKS_PER_BEAT * 2;  // First 2 beats
  std::vector<size_t> hook_note_indices;

  for (size_t i = 0; i < notes.size(); ++i) {
    if (notes[i].startTick >= section_start &&
        notes[i].startTick < section_start + hook_window) {
      hook_note_indices.push_back(i);
    }
  }

  if (hook_note_indices.empty()) return;

  // Apply effects based on intensity
  float duration_mult = 1.0f;
  float velocity_boost = 0.0f;

  switch (intensity) {
    case HookIntensity::Light:
      duration_mult = 1.3f;   // 30% longer
      velocity_boost = 5.0f;  // Slight velocity boost
      break;
    case HookIntensity::Normal:
      duration_mult = 1.5f;   // 50% longer
      velocity_boost = 10.0f;
      break;
    case HookIntensity::Strong:
      duration_mult = 2.0f;   // Double duration
      velocity_boost = 15.0f;
      break;
    default:
      break;
  }

  // Apply to first few notes (depending on intensity)
  size_t max_notes = (intensity == HookIntensity::Light) ? 1 :
                     (intensity == HookIntensity::Normal) ? 2 : 3;
  size_t apply_count = std::min(hook_note_indices.size(), max_notes);

  for (size_t i = 0; i < apply_count; ++i) {
    size_t idx = hook_note_indices[i];
    notes[idx].duration = static_cast<Tick>(notes[idx].duration * duration_mult);
    notes[idx].velocity = static_cast<uint8_t>(
        std::clamp(static_cast<int>(notes[idx].velocity + velocity_boost), 1, 127));
  }
}

// Apply groove feel timing adjustments
void applyGrooveFeel(std::vector<NoteEvent>& notes, VocalGrooveFeel groove) {
  if (groove == VocalGrooveFeel::Straight || notes.empty()) {
    return;  // No adjustment for straight timing
  }

  constexpr Tick TICK_8TH = TICKS_PER_BEAT / 2;   // 240
  constexpr Tick TICK_16TH = TICKS_PER_BEAT / 4;  // 120

  for (auto& note : notes) {
    // Get position within beat
    Tick beat_pos = note.startTick % TICKS_PER_BEAT;
    Tick shift = 0;

    switch (groove) {
      case VocalGrooveFeel::OffBeat:
        // Shift on-beat notes slightly late, emphasize off-beats
        if (beat_pos < TICK_16TH) {
          shift = TICK_16TH / 2;  // Push on-beats late
        }
        break;

      case VocalGrooveFeel::Swing:
        // Swing: delay second 8th note of each beat pair
        if (beat_pos >= TICK_8TH - TICK_16TH && beat_pos < TICK_8TH + TICK_16TH) {
          // Second 8th note: push later for swing feel
          shift = TICK_16TH / 2;
        }
        break;

      case VocalGrooveFeel::Syncopated:
        // Push notes on beats 2 and 4 earlier (anticipation)
        {
          Tick bar_pos = note.startTick % TICKS_PER_BAR;
          // Beats 2 and 4 (at 480 and 1440 ticks)
          if ((bar_pos >= TICKS_PER_BEAT - TICK_16TH && bar_pos < TICKS_PER_BEAT + TICK_16TH) ||
              (bar_pos >= TICKS_PER_BEAT * 3 - TICK_16TH && bar_pos < TICKS_PER_BEAT * 3 + TICK_16TH)) {
            shift = -TICK_16TH / 2;  // Anticipate
          }
        }
        break;

      case VocalGrooveFeel::Driving16th:
        // Slight rush on all 16th notes (energetic feel)
        if (beat_pos % TICK_16TH < TICK_16TH / 4) {
          shift = -TICK_16TH / 4;  // Slight rush
        }
        break;

      case VocalGrooveFeel::Bouncy8th:
        // Bouncy: first 8th slightly short, second 8th delayed
        if (beat_pos < TICK_8TH) {
          // First 8th: no shift but make duration shorter
          if (note.duration > TICK_8TH) {
            note.duration = note.duration * 85 / 100;  // 85% duration
          }
        } else {
          // Second 8th: slight delay
          shift = TICK_16TH / 3;
        }
        break;

      default:
        break;
    }

    // Apply shift (ensure non-negative)
    if (shift != 0) {
      int64_t new_tick = static_cast<int64_t>(note.startTick) + shift;
      note.startTick = static_cast<Tick>(std::max(static_cast<int64_t>(0), new_tick));
    }
  }
}

}  // namespace

void generateVocalTrack(MidiTrack& track, Song& song,
                        const GeneratorParams& params, std::mt19937& rng,
                        const MidiTrack* motif_track,
                        const HarmonyContext* harmony_ctx) {

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

  // Phrase cache for section repetition (same section type → same melody)
  std::unordered_map<SectionType, CachedPhrase> phrase_cache;

  // Process each section
  for (const auto& section : song.arrangement().sections()) {
    // Skip sections without vocals
    if (!sectionHasVocals(section.type)) {
      continue;
    }

    // Get template: use explicit template if specified, otherwise auto-select by style/section
    MelodyTemplateId section_template_id =
        (params.melody_template != MelodyTemplateId::Auto)
            ? params.melody_template
            : getDefaultTemplateForStyle(params.vocal_style, section.type);
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

    std::vector<NoteEvent> section_notes;

    // Check phrase cache for repeated sections
    auto cache_it = phrase_cache.find(section.type);
    if (cache_it != phrase_cache.end() && cache_it->second.bars == section.bars) {
      // Cache hit: reuse cached phrase with timing adjustment
      const CachedPhrase& cached = cache_it->second;

      // Shift timing to current section start
      section_notes = shiftTiming(cached.notes, section_start);

      // Adjust pitch range if different
      section_notes = adjustPitchRange(section_notes,
                                        cached.vocal_low, cached.vocal_high,
                                        section_vocal_low, section_vocal_high);

      // Re-apply getSafePitch (chord context may differ)
      if (harmony_ctx != nullptr) {
        for (auto& note : section_notes) {
          uint8_t safe_pitch = harmony_ctx->getSafePitch(
              note.note, note.startTick, note.duration, TrackRole::Vocal,
              section_vocal_low, section_vocal_high);
          note.note = safe_pitch;
        }
      }
    } else {
      // Cache miss: generate new melody
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

      section_notes = designer.generateSection(section_tmpl, ctx, harmony, rng);

      // Apply HarmonyContext collision avoidance
      if (harmony_ctx != nullptr) {
        for (auto& note : section_notes) {
          uint8_t safe_pitch = harmony_ctx->getSafePitch(
              note.note, note.startTick, note.duration, TrackRole::Vocal,
              section_vocal_low, section_vocal_high);
          note.note = safe_pitch;
        }
      }

      // Apply hook intensity effects at hook points (Chorus, B section)
      applyHookIntensity(section_notes, section.type, params.hook_intensity, section_start);

      // Cache the phrase (with relative timing)
      CachedPhrase cache_entry;
      cache_entry.notes = toRelativeTiming(section_notes, section_start);
      cache_entry.bars = section.bars;
      cache_entry.vocal_low = section_vocal_low;
      cache_entry.vocal_high = section_vocal_high;
      phrase_cache[section.type] = std::move(cache_entry);
    }

    // Add to collected notes
    for (const auto& note : section_notes) {
      all_notes.push_back(note);
    }
  }

  // NOTE: Modulation is NOT applied internally.
  // MidiWriter applies modulation to all tracks when generating MIDI bytes.
  // This ensures consistent behavior and avoids double-modulation.

  // Apply groove feel timing adjustments
  applyGrooveFeel(all_notes, params.vocal_groove);

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
