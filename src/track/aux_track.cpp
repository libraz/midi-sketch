/**
 * @file aux_track.cpp
 * @brief Implementation of aux track generation.
 */

#include "track/aux_track.h"
#include "core/chord.h"
#include "core/chord_utils.h"
#include "core/i_harmony_context.h"
#include "core/melody_templates.h"
#include "core/note_factory.h"
#include "core/timing_constants.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace midisketch {

// ============================================================================
// A1: AuxFunction Meta Information
// ============================================================================

namespace {

// Meta information table for each AuxFunction.
// Index matches AuxFunction enum value.
constexpr std::array<AuxFunctionMeta, 8> kAuxFunctionMetaTable = {{
    // PulseLoop: Rhythmic, ChordTone, EventProbability
    {AuxTimingRole::Rhythmic, AuxHarmonicRole::ChordTone,
     AuxDensityBehavior::EventProbability, 0.7f, 0.1f},
    // TargetHint: Reactive, Target, EventProbability
    {AuxTimingRole::Reactive, AuxHarmonicRole::Target,
     AuxDensityBehavior::EventProbability, 0.5f, 0.2f},
    // GrooveAccent: Rhythmic, Accent, EventProbability
    {AuxTimingRole::Rhythmic, AuxHarmonicRole::Accent,
     AuxDensityBehavior::EventProbability, 0.6f, 0.0f},
    // PhraseTail: Reactive, Following, SkipRatio
    {AuxTimingRole::Reactive, AuxHarmonicRole::Following,
     AuxDensityBehavior::SkipRatio, 0.4f, 0.3f},
    // EmotionalPad: Sustained, ChordTone, VoiceCount
    {AuxTimingRole::Sustained, AuxHarmonicRole::ChordTone,
     AuxDensityBehavior::VoiceCount, 1.0f, 0.4f},
    // Unison: Reactive, Unison, EventProbability (full density)
    {AuxTimingRole::Reactive, AuxHarmonicRole::Unison,
     AuxDensityBehavior::EventProbability, 1.0f, 0.0f},
    // MelodicHook: Rhythmic, ChordTone, EventProbability
    {AuxTimingRole::Rhythmic, AuxHarmonicRole::ChordTone,
     AuxDensityBehavior::EventProbability, 1.0f, 0.1f},
    // MotifCounter: Reactive, Following, EventProbability
    {AuxTimingRole::Reactive, AuxHarmonicRole::Following,
     AuxDensityBehavior::EventProbability, 0.8f, 0.2f},
}};


// Check if two notes overlap in time
bool notesOverlap(Tick start1, Tick end1, Tick start2, Tick end2) {
  return start1 < end2 && start2 < end1;
}

}  // namespace

const AuxFunctionMeta& getAuxFunctionMeta(AuxFunction func) {
  size_t idx = static_cast<size_t>(func);
  if (idx < kAuxFunctionMetaTable.size()) {
    return kAuxFunctionMetaTable[idx];
  }
  // Default to PulseLoop meta if out of range
  return kAuxFunctionMetaTable[0];
}

MidiTrack AuxTrackGenerator::generate(
    const AuxConfig& config,
    const AuxContext& ctx,
    const IHarmonyContext& harmony,
    std::mt19937& rng) {

  MidiTrack track;
  std::vector<NoteEvent> notes;

  switch (config.function) {
    case AuxFunction::PulseLoop:
      notes = generatePulseLoop(ctx, config, harmony, rng);
      break;
    case AuxFunction::TargetHint:
      notes = generateTargetHint(ctx, config, harmony, rng);
      break;
    case AuxFunction::GrooveAccent:
      notes = generateGrooveAccent(ctx, config, harmony, rng);
      break;
    case AuxFunction::PhraseTail:
      notes = generatePhraseTail(ctx, config, harmony, rng);
      break;
    case AuxFunction::EmotionalPad:
      notes = generateEmotionalPad(ctx, config, harmony, rng);
      break;
    case AuxFunction::Unison:
      notes = generateUnison(ctx, config, harmony, rng);
      break;
    case AuxFunction::MelodicHook:
      notes = generateMelodicHook(ctx, config, harmony, rng);
      break;
    case AuxFunction::MotifCounter:
      // MotifCounter requires VocalAnalysis, must be called directly
      // with generateMotifCounter() instead of through generate()
      break;
  }

  // Post-process: trim notes that sustain over chord changes (if non-chord tone in new chord)
  constexpr Tick kSuspensionThreshold = 240;  // 1/8 beat - allow short suspensions
  constexpr Tick kMinNoteDuration = 120;      // Minimum note length after trimming

  for (auto& note : notes) {
    Tick note_end = note.start_tick + note.duration;
    Tick chord_change = harmony.getNextChordChangeTick(note.start_tick);

    if (chord_change > 0 && chord_change > note.start_tick && chord_change < note_end) {
      // Note crosses chord boundary
      Tick overlap = note_end - chord_change;

      if (overlap > kSuspensionThreshold) {
        // Check if note is a chord tone in the new chord
        auto new_chord_tones = harmony.getChordTonesAt(chord_change);
        int note_pc = note.note % 12;
        bool is_chord_tone = std::find(new_chord_tones.begin(), new_chord_tones.end(), note_pc)
                             != new_chord_tones.end();

        if (!is_chord_tone) {
          // Trim note to end before chord change (with small gap for articulation)
          Tick new_duration = chord_change - note.start_tick - 10;
          if (new_duration >= kMinNoteDuration) {
            note.duration = new_duration;
          }
        }
      }
    }
  }

  NoteFactory factory(harmony);
  for (const auto& note : notes) {
    track.addNote(factory.create(note.start_tick, note.duration, note.note, note.velocity, NoteSource::Aux));
  }

  return track;
}

// ============================================================================
// Full Song Aux Generation (moved from Generator::generateAux)
// ============================================================================

void AuxTrackGenerator::generateFullTrack(
    MidiTrack& track,
    const SongContext& song_ctx,
    IHarmonyContext& harmony,
    std::mt19937& rng) {

  if (!song_ctx.sections || !song_ctx.vocal_track || !song_ctx.progression) {
    return;
  }

  const MidiTrack& vocal_track = *song_ctx.vocal_track;
  const auto& progression = *song_ctx.progression;

  // Analyze vocal for MotifCounter generation
  VocalAnalysis vocal_analysis = analyzeVocal(vocal_track);

  // Extract motif from first chorus for intro placement
  cached_chorus_motif_.reset();
  for (const auto& section : *song_ctx.sections) {
    if (section.type == SectionType::Chorus) {
      std::vector<NoteEvent> chorus_notes;
      Tick section_end = section.start_tick + section.bars * TICKS_PER_BAR;
      for (const auto& note : vocal_track.notes()) {
        if (note.start_tick >= section.start_tick &&
            note.start_tick < section_end) {
          chorus_notes.push_back(note);
        }
      }
      if (!chorus_notes.empty()) {
        cached_chorus_motif_ = extractMotifFromChorus(chorus_notes);
        break;  // Only first chorus
      }
    }
  }

  // Get vocal tessitura for aux range calculation
  auto [vocal_low, vocal_high] = vocal_track.analyzeRange();
  TessituraRange main_tessitura = calculateTessitura(vocal_low, vocal_high);

  // Determine which aux configurations to use based on vocal style
  MelodyTemplateId template_id = getDefaultTemplateForStyle(
      song_ctx.vocal_style, SectionType::Chorus);

  AuxConfig aux_configs[3];
  uint8_t aux_count = 0;
  getAuxConfigsForTemplate(template_id, aux_configs, &aux_count);

  // Collect all generated notes for post-processing
  std::vector<NoteEvent> all_notes;

  // Process each section
  for (const auto& section : *song_ctx.sections) {
    // Phase 2.5: Skip sections where aux is disabled by track_mask
    if (!hasTrack(section.track_mask, TrackMask::Aux)) {
      continue;
    }

    // Skip interlude and outro (no aux needed)
    if (section.type == SectionType::Interlude ||
        section.type == SectionType::Outro) {
      continue;
    }

    Tick section_end = section.start_tick + section.bars * TICKS_PER_BAR;
    int chord_idx = (section.start_bar % progression.length);
    int8_t chord_degree = progression.at(chord_idx);

    // Create context for aux generation
    AuxContext ctx;
    ctx.section_start = section.start_tick;
    ctx.section_end = section_end;
    ctx.chord_degree = chord_degree;
    ctx.key_offset = 0;  // Always C major internally
    ctx.base_velocity = 80;
    ctx.main_tessitura = main_tessitura;
    ctx.main_melody = &vocal_track.notes();
    ctx.section_type = section.type;

    // Select aux configuration based on section type and vocal density
    AuxConfig config;

    if (section.type == SectionType::Intro) {
      // Intro: Use cached chorus motif if available, otherwise MelodicHook
      if (cached_chorus_motif_.has_value()) {
        // Apply hook-appropriate variation (80% Exact, 20% Fragmented)
        // WORKAROUND: Use local rng instead of rng reference.
        // Passing rng directly to applyVariation/selectHookVariation causes Segfault
        // in Release builds (-O2/-O3). The root cause appears to be compiler optimization
        // affecting std::mt19937& reference passing across translation units.
        std::mt19937 variation_rng(static_cast<uint32_t>(rng()));
        MotifVariation variation = selectHookVariation(variation_rng);
        Motif varied_motif = applyVariation(*cached_chorus_motif_, variation, 0, variation_rng);

        // Place chorus motif in intro (foreshadowing the hook)
        // Center of vocal range, snapped to scale
        int center = (song_ctx.vocal_low + song_ctx.vocal_high) / 2;
        uint8_t base_pitch = static_cast<uint8_t>(snapToNearestScaleTone(center, 0));
        uint8_t velocity = static_cast<uint8_t>(ctx.base_velocity * 0.8f);
        auto motif_notes = placeMotifInIntro(
            varied_motif, section.start_tick, section_end,
            base_pitch, velocity);
        for (auto note : motif_notes) {
          // Snap pitch to chord tone at this tick to avoid dissonance
          int8_t note_chord_degree = harmony.getChordDegreeAt(note.start_tick);
          int snapped_pitch = nearestChordTonePitch(note.note, note_chord_degree);
          note.note = static_cast<uint8_t>(std::clamp(snapped_pitch, 48, 84));
          all_notes.push_back(note);
        }
        continue;  // Skip aux generator for this section
      }
      // Fallback: Use MelodicHook (Fortune Cookie style backing hook)
      config.function = AuxFunction::MelodicHook;
      config.range_offset = 0;
      config.range_width = 6;
      config.velocity_ratio = 0.8f;
      config.density_ratio = 1.0f;
      config.sync_phrase_boundary = true;
    } else if (section.type == SectionType::A ||
               section.type == SectionType::B ||
               section.type == SectionType::Bridge) {
      // A/B/Bridge: Use MotifCounter for counter melody
      // This creates rhythmic complementation with vocal
      config.function = AuxFunction::MotifCounter;
      config.range_offset = -12;  // Below vocal
      config.range_width = 12;
      config.velocity_ratio = 0.7f;
      config.density_ratio = 0.8f;
      config.sync_phrase_boundary = true;

      // Generate MotifCounter directly (requires VocalAnalysis)
      auto counter_notes = generateMotifCounter(ctx, config, harmony, vocal_analysis, rng);
      for (const auto& note : counter_notes) {
        all_notes.push_back(note);
      }
      continue;  // Skip normal generation for this section
    } else if (section.type == SectionType::Chorus &&
               section.vocal_density == VocalDensity::Full) {
      // Chorus with full vocals: Use EmotionalPad for harmonic support
      // In pop music, the pad provides chord tones (root + fifth) beneath the vocal,
      // NOT melody doubling. Unison doubling should be done by backup vocals, not pad.
      config.function = AuxFunction::EmotionalPad;
      config.range_offset = -12;  // One octave below vocal for clarity
      config.range_width = 12;    // Reasonable pad range
      config.velocity_ratio = 0.6f;  // Softer than vocal
      config.density_ratio = 0.8f;   // Allow some space
      config.sync_phrase_boundary = false;  // Pad sustains independently
    } else if (aux_count > 0) {
      // Other sections: Use default aux config
      config = aux_configs[0];
    } else {
      // No aux config available, skip
      continue;
    }

    // Generate aux for this section
    MidiTrack section_aux = generate(config, ctx, harmony, rng);

    // Add notes to collected notes
    for (const auto& note : section_aux.notes()) {
      all_notes.push_back(note);
    }
  }

  // Post-process all notes
  postProcessNotes(all_notes, harmony);

  // Add to output track
  NoteFactory factory(harmony);
  for (const auto& note : all_notes) {
    track.addNote(factory.create(note.start_tick, note.duration, note.note,
                                  note.velocity, NoteSource::Aux));
  }
}

void AuxTrackGenerator::postProcessNotes(
    std::vector<NoteEvent>& notes,
    IHarmonyContext& harmony) {

  constexpr Tick kAnticipationThreshold = 120;  // Notes starting this close to change are "anticipations"
  constexpr Tick kMinNoteDuration = 120;        // Minimum note length for split

  std::vector<NoteEvent> notes_to_add;

  // First pass: resolve notes that sustain over chord changes
  for (size_t i = 0; i < notes.size(); ++i) {
    auto& note = notes[i];
    Tick note_end = note.start_tick + note.duration;
    Tick chord_change = harmony.getNextChordChangeTick(note.start_tick);

    if (chord_change > 0 && chord_change > note.start_tick && chord_change < note_end) {
      // Check if note is a chord tone in the new chord
      auto new_chord_tones = harmony.getChordTonesAt(chord_change);
      int note_pc = note.note % 12;
      bool is_chord_tone = std::find(new_chord_tones.begin(), new_chord_tones.end(), note_pc)
                           != new_chord_tones.end();

      if (!is_chord_tone) {
        Tick time_before_change = chord_change - note.start_tick;
        Tick time_after_change = note_end - chord_change;

        // Find nearest chord tone in new chord that doesn't clash with other tracks
        int octave = note.note / 12;
        int best_pitch = note.note;
        int best_dist = 100;
        for (int tone : new_chord_tones) {
          for (int oct_offset = -1; oct_offset <= 1; ++oct_offset) {
            int candidate = (octave + oct_offset) * 12 + tone;
            if (candidate < 36 || candidate > 96) continue;  // Reasonable range
            int dist = std::abs(candidate - static_cast<int>(note.note));
            if (dist < best_dist && dist > 0) {
              // Check for clashes with other tracks at chord_change tick
              bool clashes = !harmony.isPitchSafe(
                  static_cast<uint8_t>(candidate), chord_change, time_after_change, TrackRole::Aux);
              if (!clashes) {
                best_dist = dist;
                best_pitch = candidate;
              }
            }
          }
        }
        // If all candidates clash, still use the nearest chord tone (better than non-chord tone)
        if (best_pitch == note.note) {
          for (int tone : new_chord_tones) {
            int candidate = octave * 12 + tone;
            if (candidate >= 36 && candidate <= 96) {
              best_pitch = candidate;
              break;
            }
          }
        }

        if (time_before_change < kAnticipationThreshold) {
          // Anticipation: change entire note to new chord tone
          note.note = static_cast<uint8_t>(best_pitch);
        } else if (time_before_change >= kMinNoteDuration && time_after_change >= kMinNoteDuration) {
          // Split note: keep first part, add resolved second part
          note.duration = time_before_change;

          NoteEvent resolved_note;
          resolved_note.start_tick = chord_change;
          resolved_note.duration = time_after_change;
          resolved_note.note = static_cast<uint8_t>(best_pitch);
          resolved_note.velocity = static_cast<uint8_t>(note.velocity * 0.9f);
          notes_to_add.push_back(resolved_note);
        } else {
          // Cannot split well - just change to chord tone
          note.note = static_cast<uint8_t>(best_pitch);
        }
      }
    }
  }

  // Add resolved notes
  for (const auto& note : notes_to_add) {
    notes.push_back(note);
  }

  // Second pass: fix any remaining clashes with Bass track
  for (size_t i = 0; i < notes.size(); ++i) {
    auto& note = notes[i];
    Tick note_end = note.start_tick + note.duration;

    // Check if this note clashes with bass
    if (!harmony.isPitchSafe(note.note, note.start_tick, note.duration, TrackRole::Aux)) {
      // Check if note crosses a chord boundary - need to consider both chords
      Tick chord_change = harmony.getNextChordChangeTick(note.start_tick);
      bool crosses_chord = (chord_change > 0 && chord_change > note.start_tick && chord_change < note_end);

      // Get chord tones - if crosses chord, need tones that work in both
      auto start_chord_tones = harmony.getChordTonesAt(note.start_tick);
      std::vector<int> valid_tones;

      if (crosses_chord) {
        // Find tones that are chord tones in BOTH chords
        auto end_chord_tones = harmony.getChordTonesAt(chord_change);
        for (int tone : start_chord_tones) {
          if (std::find(end_chord_tones.begin(), end_chord_tones.end(), tone) != end_chord_tones.end()) {
            valid_tones.push_back(tone);
          }
        }
        // If no common tones, use start chord tones and trim note
        if (valid_tones.empty()) {
          valid_tones = start_chord_tones;
          // Trim note to before chord change
          if (chord_change - note.start_tick >= kMinNoteDuration) {
            note.duration = chord_change - note.start_tick - 10;
          }
        }
      } else {
        valid_tones = start_chord_tones;
      }

      int octave = note.note / 12;
      int best_pitch = note.note;
      int best_dist = 100;

      for (int tone : valid_tones) {
        for (int oct_offset = -1; oct_offset <= 1; ++oct_offset) {
          int candidate = (octave + oct_offset) * 12 + tone;
          if (candidate < 36 || candidate > 96) continue;

          // Check if this candidate is safe (use trimmed duration if applicable)
          if (harmony.isPitchSafe(static_cast<uint8_t>(candidate),
                                   note.start_tick, note.duration, TrackRole::Aux)) {
            int dist = std::abs(candidate - static_cast<int>(note.note));
            if (dist < best_dist) {
              best_dist = dist;
              best_pitch = candidate;
            }
          }
        }
      }

      if (best_pitch != note.note) {
        note.note = static_cast<uint8_t>(best_pitch);
      }
    }
  }
}

std::vector<NoteEvent> AuxTrackGenerator::generatePulseLoop(
    const AuxContext& ctx,
    const AuxConfig& config,
    const IHarmonyContext& harmony,
    std::mt19937& rng) {

  std::vector<NoteEvent> result;

  // A1: Get function meta for dissonance tolerance
  const auto& meta = getAuxFunctionMeta(AuxFunction::PulseLoop);

  uint8_t aux_low, aux_high;
  calculateAuxRange(config, ctx.main_tessitura, aux_low, aux_high);

  // Get chord tones for the section
  ChordTones ct = getChordTones(ctx.chord_degree);
  if (ct.count == 0) return result;

  // Create a short repeating pattern (2-4 notes)
  std::uniform_int_distribution<int> pattern_len_dist(2, 4);
  int pattern_length = pattern_len_dist(rng);

  // Build pattern pitches from chord tones
  std::vector<uint8_t> pattern_pitches;
  int base_octave = aux_low / 12;

  for (int i = 0; i < pattern_length && i < static_cast<int>(ct.count); ++i) {
    int pc = ct.pitch_classes[i % ct.count];
    if (pc < 0) continue;
    uint8_t pitch = static_cast<uint8_t>(base_octave * 12 + pc);
    if (pitch >= aux_low && pitch <= aux_high) {
      pattern_pitches.push_back(pitch);
    }
  }

  if (pattern_pitches.empty()) return result;

  // Calculate velocity
  uint8_t velocity = static_cast<uint8_t>(ctx.base_velocity * config.velocity_ratio);

  // Repeat pattern throughout section
  Tick note_duration = TICK_EIGHTH;
  Tick current_tick = ctx.section_start;
  size_t pattern_idx = 0;

  while (current_tick < ctx.section_end) {
    // A2: Apply density ratio (EventProbability behavior)
    std::uniform_real_distribution<float> density_dist(0.0f, 1.0f);
    if (density_dist(rng) > config.density_ratio * meta.base_density) {
      current_tick += note_duration;
      continue;
    }

    uint8_t pitch = pattern_pitches[pattern_idx % pattern_pitches.size()];

    // A7: Check for collision with function-specific tolerance
    pitch = getSafePitch(pitch, current_tick, note_duration,
                         ctx.main_melody, harmony, aux_low, aux_high,
                         ctx.chord_degree, meta.dissonance_tolerance);

    result.push_back({current_tick, note_duration, pitch, velocity});

    current_tick += note_duration;
    pattern_idx++;
  }

  return result;
}

std::vector<NoteEvent> AuxTrackGenerator::generateTargetHint(
    const AuxContext& ctx,
    const AuxConfig& config,
    const IHarmonyContext& harmony,
    std::mt19937& rng) {

  std::vector<NoteEvent> result;

  if (!ctx.main_melody || ctx.main_melody->empty()) return result;

  // A1: Get function meta
  const auto& meta = getAuxFunctionMeta(AuxFunction::TargetHint);

  uint8_t aux_low, aux_high;
  calculateAuxRange(config, ctx.main_tessitura, aux_low, aux_high);

  uint8_t velocity = static_cast<uint8_t>(ctx.base_velocity * config.velocity_ratio);

  // A4: Use phrase boundaries from vocal if available
  std::vector<Tick> phrase_ends;
  if (ctx.phrase_boundaries && !ctx.phrase_boundaries->empty()) {
    // Use vocal's phrase boundaries for coordination
    for (const auto& boundary : *ctx.phrase_boundaries) {
      if (boundary.is_breath &&
          boundary.tick > ctx.section_start &&
          boundary.tick <= ctx.section_end) {
        phrase_ends.push_back(boundary.tick);
      }
    }
  } else {
    // Fallback: Find phrase boundaries in main melody (gaps > quarter note)
    for (size_t i = 0; i + 1 < ctx.main_melody->size(); ++i) {
      const auto& note = (*ctx.main_melody)[i];
      const auto& next = (*ctx.main_melody)[i + 1];
      Tick gap = next.start_tick - (note.start_tick + note.duration);
      if (gap > TICK_QUARTER) {
        phrase_ends.push_back(note.start_tick + note.duration);
      }
    }
  }

  // Add hints before phrase ends
  for (Tick phrase_end : phrase_ends) {
    // A2: Apply density ratio (EventProbability behavior)
    std::uniform_real_distribution<float> density_dist(0.0f, 1.0f);
    if (density_dist(rng) > config.density_ratio * meta.base_density) continue;

    // Play hint note half a bar before phrase end
    Tick hint_start = phrase_end - TICK_HALF;
    if (hint_start < ctx.section_start) continue;

    // Get a chord tone as hint
    ChordTones ct = getChordTones(ctx.chord_degree);
    if (ct.count == 0) continue;

    std::uniform_int_distribution<int> tone_dist(0, ct.count - 1);
    int pc = ct.pitch_classes[tone_dist(rng)];
    if (pc < 0) continue;

    int octave = (aux_low + aux_high) / 2 / 12;
    uint8_t pitch = static_cast<uint8_t>(octave * 12 + pc);
    pitch = std::clamp(pitch, aux_low, aux_high);

    // A7: Use function-specific dissonance tolerance
    pitch = getSafePitch(pitch, hint_start, TICK_QUARTER,
                         ctx.main_melody, harmony, aux_low, aux_high,
                         ctx.chord_degree, meta.dissonance_tolerance);

    result.push_back({hint_start, TICK_QUARTER, pitch, velocity});
  }

  return result;
}

std::vector<NoteEvent> AuxTrackGenerator::generateGrooveAccent(
    const AuxContext& ctx,
    const AuxConfig& config,
    const IHarmonyContext& harmony,
    std::mt19937& rng) {

  std::vector<NoteEvent> result;

  // A1: Get function meta
  const auto& meta = getAuxFunctionMeta(AuxFunction::GrooveAccent);

  uint8_t aux_low, aux_high;
  calculateAuxRange(config, ctx.main_tessitura, aux_low, aux_high);

  uint8_t velocity = static_cast<uint8_t>(ctx.base_velocity * config.velocity_ratio);

  // Get root of chord for accent
  ChordTones ct = getChordTones(ctx.chord_degree);
  if (ct.count == 0) return result;

  int root_pc = ct.pitch_classes[0];
  int octave = aux_low / 12;
  uint8_t root_pitch = static_cast<uint8_t>(octave * 12 + root_pc);
  root_pitch = std::clamp(root_pitch, aux_low, aux_high);

  // A5: Place accents on beat 2 and 4 (backbeat)
  // Future: Could vary based on VocalGrooveFeel from params
  Tick bar_length = TICKS_PER_BAR;
  Tick current_bar = (ctx.section_start / bar_length) * bar_length;

  while (current_bar < ctx.section_end) {
    // Beat 2
    Tick beat2 = current_bar + TICKS_PER_BEAT;
    if (beat2 >= ctx.section_start && beat2 < ctx.section_end) {
      // A2: Apply density ratio (EventProbability behavior)
      std::uniform_real_distribution<float> density_dist(0.0f, 1.0f);
      if (density_dist(rng) < config.density_ratio * meta.base_density) {
        // A7: Use function-specific dissonance tolerance (very low for accents)
        uint8_t pitch = getSafePitch(root_pitch, beat2, TICK_EIGHTH,
                                     ctx.main_melody, harmony, aux_low, aux_high,
                                     ctx.chord_degree, meta.dissonance_tolerance);
        result.push_back({beat2, TICK_EIGHTH, pitch, velocity});
      }
    }

    // Beat 4
    Tick beat4 = current_bar + TICKS_PER_BEAT * 3;
    if (beat4 >= ctx.section_start && beat4 < ctx.section_end) {
      std::uniform_real_distribution<float> density_dist(0.0f, 1.0f);
      if (density_dist(rng) < config.density_ratio * meta.base_density) {
        uint8_t pitch = getSafePitch(root_pitch, beat4, TICK_EIGHTH,
                                     ctx.main_melody, harmony, aux_low, aux_high,
                                     ctx.chord_degree, meta.dissonance_tolerance);
        result.push_back({beat4, TICK_EIGHTH, pitch, velocity});
      }
    }

    current_bar += bar_length;
  }

  return result;
}

std::vector<NoteEvent> AuxTrackGenerator::generatePhraseTail(
    const AuxContext& ctx,
    const AuxConfig& config,
    const IHarmonyContext& harmony,
    std::mt19937& rng) {

  std::vector<NoteEvent> result;

  if (!ctx.main_melody || ctx.main_melody->empty()) return result;

  // A1: Get function meta
  const auto& meta = getAuxFunctionMeta(AuxFunction::PhraseTail);

  uint8_t aux_low, aux_high;
  calculateAuxRange(config, ctx.main_tessitura, aux_low, aux_high);

  uint8_t velocity = static_cast<uint8_t>(ctx.base_velocity * config.velocity_ratio);

  // A4: Use phrase boundaries from vocal if available
  std::vector<std::pair<Tick, uint8_t>> phrase_info;  // (end_tick, last_pitch)
  if (ctx.phrase_boundaries && !ctx.phrase_boundaries->empty()) {
    // Use vocal's phrase boundaries for tail placement
    for (const auto& boundary : *ctx.phrase_boundaries) {
      if (boundary.is_breath &&
          boundary.tick >= ctx.section_start &&
          boundary.tick < ctx.section_end) {
        // Find the last melody note before this boundary
        uint8_t last_pitch = 60;  // Default
        for (const auto& note : *ctx.main_melody) {
          Tick note_end = note.start_tick + note.duration;
          if (note_end <= boundary.tick && note_end > boundary.tick - TICKS_PER_BAR) {
            last_pitch = note.note;
          }
        }
        phrase_info.push_back({boundary.tick, last_pitch});
      }
    }
  }

  // Fallback: Find phrase endings in main melody
  if (phrase_info.empty()) {
    for (size_t i = 0; i < ctx.main_melody->size(); ++i) {
      const auto& note = (*ctx.main_melody)[i];
      Tick note_end = note.start_tick + note.duration;

      bool is_phrase_end = false;
      if (i == ctx.main_melody->size() - 1) {
        is_phrase_end = true;
      } else {
        const auto& next = (*ctx.main_melody)[i + 1];
        Tick gap = next.start_tick - note_end;
        is_phrase_end = (gap > TICK_QUARTER);
      }

      if (is_phrase_end) {
        phrase_info.push_back({note_end, note.note});
      }
    }
  }

  // Generate tail notes
  for (const auto& [phrase_end, last_pitch] : phrase_info) {
    // A2: Apply density ratio (SkipRatio behavior)
    std::uniform_real_distribution<float> density_dist(0.0f, 1.0f);
    if (density_dist(rng) > config.density_ratio * meta.base_density) continue;

    // Add tail note after phrase ending
    Tick tail_start = phrase_end + TICK_EIGHTH;
    if (tail_start >= ctx.section_end) continue;

    // Use a note below the phrase ending
    int tail_pitch = last_pitch - 2;  // Step down
    tail_pitch = snapToNearestScaleTone(tail_pitch, ctx.key_offset);
    tail_pitch = std::clamp(tail_pitch, static_cast<int>(aux_low),
                            static_cast<int>(aux_high));

    // A7: Use function-specific dissonance tolerance (moderate for tails)
    uint8_t pitch = getSafePitch(static_cast<uint8_t>(tail_pitch),
                                 tail_start, TICK_EIGHTH,
                                 ctx.main_melody, harmony, aux_low, aux_high,
                                 ctx.chord_degree, meta.dissonance_tolerance);

    result.push_back({tail_start, TICK_EIGHTH, pitch,
                      static_cast<uint8_t>(velocity * 0.8f)});
  }

  return result;
}

std::vector<NoteEvent> AuxTrackGenerator::generateEmotionalPad(
    const AuxContext& ctx,
    const AuxConfig& config,
    const IHarmonyContext& harmony,
    std::mt19937& rng) {

  std::vector<NoteEvent> result;

  // A1: Get function meta
  const auto& meta = getAuxFunctionMeta(AuxFunction::EmotionalPad);

  uint8_t aux_low, aux_high;
  calculateAuxRange(config, ctx.main_tessitura, aux_low, aux_high);

  uint8_t velocity = static_cast<uint8_t>(ctx.base_velocity * config.velocity_ratio);

  // Get chord tones for sustained pad
  ChordTones ct = getChordTones(ctx.chord_degree);
  if (ct.count < 2) return result;

  // Create sustained tones on root and fifth
  int root_pc = ct.pitch_classes[0];
  int fifth_pc = (ct.count >= 3) ? ct.pitch_classes[2] : ct.pitch_classes[1];

  int octave = aux_low / 12;
  uint8_t root_pitch = static_cast<uint8_t>(octave * 12 + root_pc);
  uint8_t fifth_pitch = static_cast<uint8_t>(octave * 12 + fifth_pc);

  root_pitch = std::clamp(root_pitch, aux_low, aux_high);
  fifth_pitch = std::clamp(fifth_pitch, aux_low, aux_high);

  // Place sustained tones - check safety per bar to avoid clashes
  // with melody changes during long sustain
  Tick pad_duration = TICKS_PER_BAR;  // Check per bar instead of 2 bars
  Tick current_tick = ctx.section_start;

  // A2: VoiceCount behavior - calculate how many voices based on density
  int voice_count = static_cast<int>(2.0f * config.density_ratio * meta.base_density);
  voice_count = std::clamp(voice_count, 1, 3);

  while (current_tick < ctx.section_end) {
    Tick actual_duration = std::min(pad_duration, ctx.section_end - current_tick);

    // Update chord degree for current position (may change mid-section)
    int8_t current_chord_degree = harmony.getChordDegreeAt(current_tick);
    ChordTones current_ct = getChordTones(current_chord_degree);
    if (current_ct.count >= 2) {
      root_pc = current_ct.pitch_classes[0];
      fifth_pc = (current_ct.count >= 3) ? current_ct.pitch_classes[2] : current_ct.pitch_classes[1];
      root_pitch = static_cast<uint8_t>(octave * 12 + root_pc);
      fifth_pitch = static_cast<uint8_t>(octave * 12 + fifth_pc);
      root_pitch = std::clamp(root_pitch, aux_low, aux_high);
      fifth_pitch = std::clamp(fifth_pitch, aux_low, aux_high);
    }

    // A6: Check if this is near section end for tension notes
    bool is_section_ending = (ctx.section_end - current_tick <= TICKS_PER_BAR * 2);

    // Root note (always)
    uint8_t safe_root = getSafePitch(root_pitch, current_tick, actual_duration,
                                     ctx.main_melody, harmony, aux_low, aux_high,
                                     current_chord_degree, meta.dissonance_tolerance);
    result.push_back({current_tick, actual_duration, safe_root, velocity});

    // Fifth note (if voice_count >= 2)
    if (voice_count >= 2 &&
        std::abs(static_cast<int>(fifth_pitch) - static_cast<int>(safe_root)) > 2) {
      uint8_t safe_fifth = getSafePitch(fifth_pitch, current_tick, actual_duration,
                                        ctx.main_melody, harmony, aux_low, aux_high,
                                        current_chord_degree, meta.dissonance_tolerance);
      if (safe_fifth != safe_root) {
        result.push_back({current_tick, actual_duration, safe_fifth,
                          static_cast<uint8_t>(velocity * 0.9f)});
      }
    }

    // A6: Add tension note (9th or sus4) at section ending
    if (is_section_ending && voice_count >= 2) {
      std::uniform_real_distribution<float> tension_dist(0.0f, 1.0f);
      if (tension_dist(rng) < 0.5f) {  // 50% chance of tension
        // Add 9th (2 semitones above root) or sus4 (5 semitones above root)
        int tension_pc = (tension_dist(rng) < 0.5f) ? (root_pc + 2) % 12 : (root_pc + 5) % 12;
        uint8_t tension_pitch = static_cast<uint8_t>(octave * 12 + tension_pc);
        tension_pitch = std::clamp(tension_pitch, aux_low, aux_high);

        // Tension notes use higher dissonance tolerance
        uint8_t safe_tension = getSafePitch(tension_pitch, current_tick, actual_duration,
                                            ctx.main_melody, harmony, aux_low, aux_high,
                                            current_chord_degree, 0.5f);
        if (safe_tension != safe_root && safe_tension != fifth_pitch) {
          result.push_back({current_tick, actual_duration, safe_tension,
                            static_cast<uint8_t>(velocity * 0.7f)});  // Softer tension
        }
      }
    }

    current_tick += pad_duration;
  }

  return result;
}

void AuxTrackGenerator::calculateAuxRange(
    const AuxConfig& config,
    const TessituraRange& main_tessitura,
    uint8_t& out_low, uint8_t& out_high) {

  int center = main_tessitura.center + config.range_offset;
  int half_width = config.range_width / 2;

  out_low = static_cast<uint8_t>(std::clamp(center - half_width, 36, 96));
  out_high = static_cast<uint8_t>(std::clamp(center + half_width, 36, 96));

  if (out_low > out_high) {
    std::swap(out_low, out_high);
  }
}

// A4: Find breath points (phrase boundaries) within a time range.
std::vector<Tick> AuxTrackGenerator::findBreathPointsInRange(
    const std::vector<PhraseBoundary>* boundaries,
    Tick start, Tick end) {
  std::vector<Tick> result;
  if (!boundaries) return result;

  for (const auto& boundary : *boundaries) {
    if (boundary.is_breath && boundary.tick >= start && boundary.tick < end) {
      result.push_back(boundary.tick);
    }
  }
  return result;
}

bool AuxTrackGenerator::isPitchSafe(
    uint8_t pitch, Tick start, Tick duration,
    const std::vector<NoteEvent>* main_melody,
    const IHarmonyContext& harmony,
    float dissonance_tolerance) {

  // Check against main melody
  if (main_melody) {
    for (const auto& note : *main_melody) {
      if (notesOverlap(start, start + duration,
                       note.start_tick, note.start_tick + note.duration)) {
        int interval = std::abs(static_cast<int>(pitch) - static_cast<int>(note.note));
        interval = interval % 12;

        // A7: With higher tolerance, allow more intervals
        // Base case: minor 2nd (1) and major 7th (11) are dissonant
        bool is_dissonant = (interval == 1 || interval == 11);

        // With tolerance > 0.3, also allow tritone (6)
        if (dissonance_tolerance < 0.3f && interval == 6) {
          is_dissonant = true;
        }

        // With tolerance > 0, probabilistically allow dissonance
        if (is_dissonant && dissonance_tolerance > 0.0f) {
          // Random check would need RNG, so just use threshold
          if (dissonance_tolerance < 0.5f) {
            return false;  // Still reject
          }
          // High tolerance: allow
        } else if (is_dissonant) {
          return false;
        }
      }
    }
  }

  // Also check against HarmonyContext
  return harmony.isPitchSafe(pitch, start, duration, TrackRole::Aux);
}

uint8_t AuxTrackGenerator::getSafePitch(
    uint8_t desired, Tick start, Tick duration,
    const std::vector<NoteEvent>* main_melody,
    const IHarmonyContext& harmony,
    uint8_t low, uint8_t high,
    [[maybe_unused]] int8_t chord_degree,
    float dissonance_tolerance) {

  // Get actual chord degree at this tick (not section start)
  int8_t actual_chord_degree = harmony.getChordDegreeAt(start);

  // Check if this is a strong beat (beat 1 or 3)
  // Use full beat range to catch notes slightly off the beat
  Tick bar_pos = start % TICKS_PER_BAR;
  bool is_strong_beat = (bar_pos < TICKS_PER_BEAT) ||
                        (bar_pos >= 2 * TICKS_PER_BEAT && bar_pos < 3 * TICKS_PER_BEAT);

  // Strong beats: prefer chord tones for harmonic stability
  if (is_strong_beat) {
    // Find nearest chord tone
    ChordTones ct = getChordTones(actual_chord_degree);
    int octave = desired / 12;
    int best_pitch = desired;
    int best_dist = 100;

    for (uint8_t i = 0; i < ct.count; ++i) {
      int pc = ct.pitch_classes[i];
      if (pc < 0) continue;

      for (int oct_offset = -1; oct_offset <= 1; ++oct_offset) {
        int candidate = (octave + oct_offset) * 12 + pc;
        if (candidate < low || candidate > high) continue;

        if (isPitchSafe(static_cast<uint8_t>(candidate), start, duration,
                        main_melody, harmony, dissonance_tolerance)) {
          int dist = std::abs(candidate - static_cast<int>(desired));
          if (dist < best_dist) {
            best_dist = dist;
            best_pitch = candidate;
          }
        }
      }
    }

    if (best_dist < 100) {
      return static_cast<uint8_t>(std::clamp(best_pitch, static_cast<int>(low), static_cast<int>(high)));
    }
  }

  // Weak beats or no safe chord tone found: check if desired is safe
  if (isPitchSafe(desired, start, duration, main_melody, harmony, dissonance_tolerance)) {
    return desired;
  }

  // Try chord tones nearby
  ChordTones ct = getChordTones(actual_chord_degree);
  int octave = desired / 12;

  int best_safe_pitch = -1;
  int best_safe_dist = 100;
  int best_chord_pitch = -1;
  int best_chord_dist = 100;

  for (uint8_t i = 0; i < ct.count; ++i) {
    int pc = ct.pitch_classes[i];
    if (pc < 0) continue;

    for (int oct_offset = -1; oct_offset <= 1; ++oct_offset) {
      int candidate = (octave + oct_offset) * 12 + pc;
      if (candidate < low || candidate > high) continue;

      int dist = std::abs(candidate - static_cast<int>(desired));

      // Track nearest chord tone (regardless of safety)
      if (dist < best_chord_dist) {
        best_chord_dist = dist;
        best_chord_pitch = candidate;
      }

      // Track nearest safe chord tone
      if (isPitchSafe(static_cast<uint8_t>(candidate), start, duration,
                      main_melody, harmony, dissonance_tolerance)) {
        if (dist < best_safe_dist) {
          best_safe_dist = dist;
          best_safe_pitch = candidate;
        }
      }
    }
  }

  // Prefer safe chord tone, fall back to any chord tone (better than non-chord tone clash)
  int result = (best_safe_pitch >= 0) ? best_safe_pitch :
               (best_chord_pitch >= 0) ? best_chord_pitch : desired;

  return static_cast<uint8_t>(std::clamp(result,
                                          static_cast<int>(low),
                                          static_cast<int>(high)));
}

// ============================================================================
// F: Unison - Doubles the main melody
// ============================================================================

std::vector<NoteEvent> AuxTrackGenerator::generateUnison(
    const AuxContext& ctx,
    const AuxConfig& config,
    [[maybe_unused]] const IHarmonyContext& harmony,
    std::mt19937& rng) {

  std::vector<NoteEvent> result;
  if (!ctx.main_melody || ctx.main_melody->empty()) return result;

  // Check derivability: unison requires stable rhythm (contour less important)
  DerivabilityScore score = analyzeDerivability(*ctx.main_melody);
  // Unison only needs rhythm stability - contour doesn't matter for pitch doubling
  if (score.rhythm_stability < 0.5f) {
    // Rhythm too irregular for unison doubling
    return result;
  }

  // Timing offset distribution (±5-10 ticks for natural doubling feel)
  std::uniform_int_distribution<int> offset_dist(5, 10);
  std::uniform_int_distribution<int> sign_dist(0, 1);

  for (const auto& note : *ctx.main_melody) {
    // Only process notes within section range
    if (note.start_tick < ctx.section_start ||
        note.start_tick >= ctx.section_end) continue;

    NoteEvent unison = note;

    // Add slight timing offset for natural doubling feel
    int offset = offset_dist(rng) * (sign_dist(rng) ? 1 : -1);
    unison.start_tick = static_cast<Tick>(
        std::max(static_cast<int>(ctx.section_start),
                 static_cast<int>(note.start_tick) + offset));

    // Reduce velocity for background effect
    unison.velocity = static_cast<uint8_t>(
        std::clamp(static_cast<int>(note.velocity * config.velocity_ratio),
                   1, 127));

    result.push_back(unison);
  }

  return result;
}

// ============================================================================
// F+: Harmony - Creates harmony line based on main melody
// ============================================================================

std::vector<NoteEvent> AuxTrackGenerator::generateHarmony(
    const AuxContext& ctx,
    const AuxConfig& config,
    const IHarmonyContext& harmony,
    HarmonyMode mode,
    std::mt19937& rng) {

  std::vector<NoteEvent> result;
  if (!ctx.main_melody || ctx.main_melody->empty()) return result;

  // Check derivability: harmony benefits from stable rhythm and reasonable contour
  DerivabilityScore score = analyzeDerivability(*ctx.main_melody);
  // Harmony needs rhythm stability and pitch simplicity (contour is nice-to-have)
  if (score.rhythm_stability < 0.5f || score.pitch_simplicity < 0.4f) {
    // Melody too complex for parallel harmony
    return result;
  }

  // Timing offset distribution
  std::uniform_int_distribution<int> offset_dist(3, 8);
  std::uniform_int_distribution<int> sign_dist(0, 1);

  int note_count = 0;
  for (const auto& note : *ctx.main_melody) {
    // Only process notes within section range
    if (note.start_tick < ctx.section_start ||
        note.start_tick >= ctx.section_end) continue;

    NoteEvent harm = note;

    // Determine harmony interval based on mode
    int interval = 0;
    switch (mode) {
      case HarmonyMode::UnisonOnly:
        interval = 0;
        break;
      case HarmonyMode::ThirdAbove:
        interval = 3;  // Minor 3rd (could be 4 for major 3rd)
        break;
      case HarmonyMode::ThirdBelow:
        interval = -3;
        break;
      case HarmonyMode::Alternating:
        // Alternate between unison and third above
        interval = (note_count % 2 == 0) ? 0 : 3;
        break;
    }

    // Add slight timing offset FIRST
    int offset = offset_dist(rng) * (sign_dist(rng) ? 1 : -1);
    harm.start_tick = static_cast<Tick>(
        std::max(static_cast<int>(ctx.section_start),
                 static_cast<int>(note.start_tick) + offset));

    // Apply interval and snap to chord tone at the ACTUAL placement tick
    int new_pitch = note.note + interval;
    int8_t chord_degree = harmony.getChordDegreeAt(harm.start_tick);
    new_pitch = nearestChordTonePitch(new_pitch, chord_degree);

    // Clamp to reasonable range
    harm.note = static_cast<uint8_t>(std::clamp(new_pitch, 48, 84));

    // Reduce velocity
    harm.velocity = static_cast<uint8_t>(
        std::clamp(static_cast<int>(note.velocity * config.velocity_ratio),
                   1, 127));

    result.push_back(harm);
    ++note_count;
  }

  return result;
}

// ============================================================================
// G: MelodicHook - Creates memorable hook phrase
// ============================================================================

std::vector<NoteEvent> AuxTrackGenerator::generateMelodicHook(
    const AuxContext& ctx,
    const AuxConfig& config,
    const IHarmonyContext& harmony,
    std::mt19937& rng) {

  std::vector<NoteEvent> result;

  // Calculate aux range
  uint8_t aux_low, aux_high;
  calculateAuxRange(config, ctx.main_tessitura, aux_low, aux_high);

  // Hook pattern: AAAB style (3 repeats + variation)
  // Each hook phrase is 2 bars (8 beats)
  constexpr Tick HOOK_PHRASE_TICKS = TICKS_PER_BAR * 2;

  // Simple hook motif: 4 notes per bar
  constexpr int NOTES_PER_BAR = 4;
  constexpr Tick NOTE_DURATION = TICKS_PER_BEAT;

  Tick current_tick = ctx.section_start;

  // Generate base hook pattern (first 2 bars)
  std::vector<NoteEvent> base_hook;
  int8_t chord_degree = harmony.getChordDegreeAt(ctx.section_start);

  // Start from chord root in aux range
  int base_pitch = nearestChordTonePitch(
      (aux_low + aux_high) / 2, chord_degree);

  // Simple melodic pattern: root, 3rd, 5th, 3rd
  std::array<int, 4> intervals = {0, 4, 7, 4};  // Major chord intervals

  const auto& meta = getAuxFunctionMeta(AuxFunction::MelodicHook);

  for (int i = 0; i < NOTES_PER_BAR * 2; ++i) {
    int pitch = base_pitch + intervals[i % 4];
    pitch = std::clamp(pitch, static_cast<int>(aux_low), static_cast<int>(aux_high));

    // Apply safety check to avoid clashes with vocal
    pitch = getSafePitch(static_cast<uint8_t>(pitch), current_tick, NOTE_DURATION,
                         ctx.main_melody, harmony, aux_low, aux_high,
                         chord_degree, meta.dissonance_tolerance);

    NoteEvent note;
    note.start_tick = current_tick;
    note.duration = NOTE_DURATION - TICKS_PER_BEAT / 8;  // Slight gap
    note.note = static_cast<uint8_t>(pitch);
    note.velocity = static_cast<uint8_t>(ctx.base_velocity * config.velocity_ratio);

    base_hook.push_back(note);
    current_tick += NOTE_DURATION;
  }

  // Repeat base hook with variations (AAAB pattern)
  Tick section_length = ctx.section_end - ctx.section_start;
  int phrases_needed = static_cast<int>(section_length / HOOK_PHRASE_TICKS);

  std::uniform_int_distribution<int> variation_dist(-2, 2);

  for (int phrase = 0; phrase < phrases_needed; ++phrase) {
    Tick phrase_start = ctx.section_start + phrase * HOOK_PHRASE_TICKS;

    for (const auto& note : base_hook) {
      NoteEvent hook_note = note;
      hook_note.start_tick = phrase_start + (note.start_tick - ctx.section_start);

      // Apply variation on the B phrase (every 4th phrase)
      if (phrase % 4 == 3) {
        int variation = variation_dist(rng);
        int new_pitch = hook_note.note + variation;
        hook_note.note = static_cast<uint8_t>(
            std::clamp(new_pitch, static_cast<int>(aux_low), static_cast<int>(aux_high)));
      }

      // Skip if outside section
      if (hook_note.start_tick >= ctx.section_end) continue;

      // Re-check safety for repeated/varied notes
      int8_t current_chord = harmony.getChordDegreeAt(hook_note.start_tick);
      hook_note.note = getSafePitch(hook_note.note, hook_note.start_tick, hook_note.duration,
                                     ctx.main_melody, harmony, aux_low, aux_high,
                                     current_chord, meta.dissonance_tolerance);

      result.push_back(hook_note);
    }
  }

  return result;
}

// ============================================================================
// H: MotifCounter - Counter melody derived from vocal
// ============================================================================

std::vector<NoteEvent> AuxTrackGenerator::generateMotifCounter(
    const AuxContext& ctx,
    const AuxConfig& config,
    const IHarmonyContext& harmony,
    const VocalAnalysis& vocal_analysis,
    std::mt19937& rng) {

  std::vector<NoteEvent> result;

  if (!ctx.main_melody || ctx.main_melody->empty()) return result;

  // A1: Get function meta
  const auto& meta = getAuxFunctionMeta(AuxFunction::MotifCounter);

  // Calculate counter melody range (separated from vocal)
  // If vocal is in high register, use low register and vice versa
  uint8_t aux_low, aux_high;
  int vocal_center = (vocal_analysis.lowest_pitch + vocal_analysis.highest_pitch) / 2;

  if (vocal_center >= 72) {  // Vocal is high (C5+)
    // Place counter in lower register
    aux_low = 48;   // C3
    aux_high = 67;  // G4
  } else if (vocal_center <= 60) {  // Vocal is low (C4-)
    // Place counter in higher register
    aux_low = 72;   // C5
    aux_high = 84;  // C6
  } else {
    // Vocal is in middle, use config offset
    calculateAuxRange(config, ctx.main_tessitura, aux_low, aux_high);
    // Ensure separation: shift if overlapping
    if (aux_low >= vocal_analysis.lowest_pitch - 12 &&
        aux_high <= vocal_analysis.highest_pitch + 12) {
      // Try going an octave lower
      if (aux_low > 48) {
        aux_low -= 12;
        aux_high -= 12;
      } else {
        aux_low += 12;
        aux_high += 12;
      }
    }
  }

  uint8_t velocity = static_cast<uint8_t>(ctx.base_velocity * config.velocity_ratio);

  // Rhythmic complementation: Determine counter note density based on vocal density
  // Dense vocal → sparse counter, sparse vocal → dense counter
  Tick base_note_duration;
  if (vocal_analysis.density > 0.6f) {
    // Vocal is dense, use longer notes (sparse counter)
    base_note_duration = TICK_HALF;
  } else if (vocal_analysis.density < 0.3f) {
    // Vocal is sparse, use shorter notes (dense counter)
    base_note_duration = TICK_EIGHTH;
  } else {
    // Medium density, use quarter notes
    base_note_duration = TICK_QUARTER;
  }

  // Iterate through vocal phrases to create counter phrases
  for (const auto& phrase : vocal_analysis.phrases) {
    Tick phrase_start = phrase.start_tick;
    Tick phrase_end = phrase.end_tick;

    // Skip if phrase is outside section
    if (phrase_end <= ctx.section_start || phrase_start >= ctx.section_end) {
      continue;
    }

    // Adjust to section boundaries
    phrase_start = std::max(phrase_start, ctx.section_start);
    phrase_end = std::min(phrase_end, ctx.section_end);

    // Generate counter notes for this phrase
    Tick current_tick = phrase_start;

    while (current_tick < phrase_end) {
      // Apply density ratio
      std::uniform_real_distribution<float> density_dist(0.0f, 1.0f);
      if (density_dist(rng) > config.density_ratio * meta.base_density) {
        current_tick += base_note_duration;
        continue;
      }

      // Get vocal direction at this tick for contrary motion
      // getVocalDirectionAt returns: -1=descending, 0=static, 1=ascending
      int8_t vocal_direction = getVocalDirectionAt(vocal_analysis, current_tick);
      int vocal_pitch = getVocalPitchAt(vocal_analysis, current_tick);

      // Get chord degree at current tick (not section start)
      int8_t current_chord_degree = harmony.getChordDegreeAt(current_tick);

      // Determine counter pitch using contrary motion
      int counter_pitch;
      ChordTones ct = getChordTones(current_chord_degree);

      if (vocal_pitch > 0 && ct.count > 0) {
        // Calculate target based on contrary motion
        int target_pitch = (aux_low + aux_high) / 2;

        if (vocal_direction > 0) {
          // Vocal going up → counter goes down
          target_pitch = aux_low + (aux_high - aux_low) / 3;
        } else if (vocal_direction < 0) {
          // Vocal going down → counter goes up
          target_pitch = aux_high - (aux_high - aux_low) / 3;
        }
        // vocal_direction == 0: static → use middle register

        // Snap to nearest chord tone at current tick
        counter_pitch = nearestChordTonePitch(target_pitch, current_chord_degree);
        counter_pitch = std::clamp(counter_pitch, static_cast<int>(aux_low),
                                    static_cast<int>(aux_high));
      } else {
        // Fallback: use middle of range on chord tone
        counter_pitch = nearestChordTonePitch((aux_low + aux_high) / 2, current_chord_degree);
      }

      // Get safe pitch (avoid collisions)
      Tick note_duration = std::min(base_note_duration, phrase_end - current_tick);

      // Check for chord change during this note (anticipation handling)
      // If note starts close to chord change and extends past it, use new chord's tones
      Tick next_chord_change = harmony.getNextChordChangeTick(current_tick);
      constexpr Tick kAnticipationThreshold = 120;  // 1/16 beat - notes starting this close to change are "anticipations"

      if (next_chord_change > 0 &&
          next_chord_change > current_tick &&
          next_chord_change < current_tick + note_duration &&
          next_chord_change - current_tick < kAnticipationThreshold) {
        // This note anticipates the next chord - use new chord's tones
        int8_t next_chord_degree = harmony.getChordDegreeAt(next_chord_change);
        counter_pitch = nearestChordTonePitch(counter_pitch, next_chord_degree);
        counter_pitch = std::clamp(counter_pitch, static_cast<int>(aux_low),
                                    static_cast<int>(aux_high));
        current_chord_degree = next_chord_degree;  // Update for getSafePitch
      }

      uint8_t safe_pitch = getSafePitch(
          static_cast<uint8_t>(counter_pitch), current_tick, note_duration,
          ctx.main_melody, harmony, aux_low, aux_high,
          current_chord_degree, meta.dissonance_tolerance);

      // Add note
      result.push_back({current_tick, note_duration, safe_pitch, velocity});

      current_tick += base_note_duration;
    }
  }

  // If no phrases were found, generate based on rest positions
  if (result.empty() && !vocal_analysis.rest_positions.empty()) {
    // Play during vocal rests (call-and-response style)
    for (const Tick& rest_start : vocal_analysis.rest_positions) {
      if (rest_start < ctx.section_start || rest_start >= ctx.section_end) {
        continue;
      }

      // Apply density
      std::uniform_real_distribution<float> density_dist(0.0f, 1.0f);
      if (density_dist(rng) > config.density_ratio) {
        continue;
      }

      // Get chord degree at current tick
      int8_t current_chord_degree = harmony.getChordDegreeAt(rest_start);

      // Get chord tone for this position
      int counter_pitch = nearestChordTonePitch((aux_low + aux_high) / 2, current_chord_degree);
      counter_pitch = std::clamp(counter_pitch, static_cast<int>(aux_low),
                                  static_cast<int>(aux_high));

      uint8_t safe_pitch = getSafePitch(
          static_cast<uint8_t>(counter_pitch), rest_start, TICK_QUARTER,
          ctx.main_melody, harmony, aux_low, aux_high,
          current_chord_degree, meta.dissonance_tolerance);

      result.push_back({rest_start, TICK_QUARTER, safe_pitch, velocity});
    }
  }

  // Post-process: resolve notes that sustain over chord changes
  // Instead of trimming, resolve to nearest chord tone (musical suspension resolution)
  constexpr Tick kSuspensionThreshold = 240;  // 1/8 beat - allow short suspensions
  constexpr Tick kMinNoteDuration = 120;      // Minimum note length

  std::vector<NoteEvent> resolved_result;
  for (auto& note : result) {
    Tick note_end = note.start_tick + note.duration;
    Tick chord_change = harmony.getNextChordChangeTick(note.start_tick);

    if (chord_change > 0 && chord_change > note.start_tick && chord_change < note_end) {
      // Note crosses chord boundary
      Tick overlap = note_end - chord_change;

      if (overlap > kSuspensionThreshold) {
        // Check if note is a chord tone in the new chord
        auto new_chord_tones = harmony.getChordTonesAt(chord_change);
        int note_pc = note.note % 12;
        bool is_chord_tone = std::find(new_chord_tones.begin(), new_chord_tones.end(), note_pc)
                             != new_chord_tones.end();

        if (!is_chord_tone) {
          // Split note and resolve second part to nearest chord tone
          Tick first_duration = chord_change - note.start_tick;
          Tick second_duration = note_end - chord_change;

          if (first_duration >= kMinNoteDuration && second_duration >= kMinNoteDuration) {
            // First part: original pitch until chord change
            NoteEvent first_part = note;
            first_part.duration = first_duration;
            resolved_result.push_back(first_part);

            // Second part: resolve to nearest chord tone
            int octave = note.note / 12;
            int best_pitch = note.note;
            int best_dist = 100;

            for (int tone : new_chord_tones) {
              for (int oct_offset = -1; oct_offset <= 1; ++oct_offset) {
                int candidate = (octave + oct_offset) * 12 + tone;
                if (candidate < aux_low || candidate > aux_high) continue;
                int dist = std::abs(candidate - static_cast<int>(note.note));
                if (dist < best_dist && dist > 0) {  // Must be different pitch
                  best_dist = dist;
                  best_pitch = candidate;
                }
              }
            }

            NoteEvent second_part;
            second_part.start_tick = chord_change;
            second_part.duration = second_duration;
            second_part.note = static_cast<uint8_t>(best_pitch);
            second_part.velocity = static_cast<uint8_t>(note.velocity * 0.9f);  // Slightly softer resolution
            resolved_result.push_back(second_part);
            continue;  // Skip adding original note
          }
        }
      }
    }
    resolved_result.push_back(note);
  }

  return resolved_result;
}

// ============================================================================
// Derivability Analysis
// ============================================================================

DerivabilityScore analyzeDerivability(const std::vector<NoteEvent>& notes) {
  DerivabilityScore score = {0.0f, 0.0f, 0.0f};

  if (notes.size() < 4) {
    // Too few notes to analyze meaningfully
    return {0.5f, 0.5f, 0.5f};
  }

  // === Rhythm Stability ===
  // Analyze how consistent note durations are
  {
    std::vector<Tick> durations;
    durations.reserve(notes.size());
    for (const auto& note : notes) {
      durations.push_back(note.duration);
    }

    // Calculate duration variance
    Tick sum = 0;
    for (Tick d : durations) {
      sum += d;
    }
    Tick mean = sum / static_cast<Tick>(durations.size());

    float variance = 0.0f;
    for (Tick d : durations) {
      float diff = static_cast<float>(d - mean);
      variance += diff * diff;
    }
    variance /= static_cast<float>(durations.size());

    // Normalize: low variance = high stability
    // Typical duration ~240 ticks (eighth note), variance up to 60000
    float normalized_variance = variance / 60000.0f;
    score.rhythm_stability = std::max(0.0f, 1.0f - normalized_variance);
  }

  // === Contour Clarity ===
  // Analyze how clear the melodic direction is
  {
    int direction_changes = 0;
    int consistent_direction = 0;
    int prev_direction = 0;

    for (size_t i = 1; i < notes.size(); ++i) {
      int diff = static_cast<int>(notes[i].note) - static_cast<int>(notes[i - 1].note);
      int direction = (diff > 0) ? 1 : (diff < 0) ? -1 : 0;

      if (direction != 0) {
        if (prev_direction != 0 && direction != prev_direction) {
          direction_changes++;
        } else if (prev_direction != 0 && direction == prev_direction) {
          consistent_direction++;
        }
        prev_direction = direction;
      }
    }

    // Clear contour: few direction changes, or many consistent directions
    float total_movements = static_cast<float>(direction_changes + consistent_direction);
    if (total_movements > 0) {
      float consistency_ratio = static_cast<float>(consistent_direction) / total_movements;
      score.contour_clarity = 0.4f + consistency_ratio * 0.6f;
    } else {
      score.contour_clarity = 0.5f;
    }
  }

  // === Pitch Simplicity ===
  // Analyze how simple the pitch relationships are
  {
    int simple_intervals = 0;  // unison, 2nd, 3rd
    int complex_intervals = 0; // 4th and larger

    for (size_t i = 1; i < notes.size(); ++i) {
      int interval = std::abs(static_cast<int>(notes[i].note) -
                              static_cast<int>(notes[i - 1].note));
      if (interval <= 4) {
        simple_intervals++;
      } else {
        complex_intervals++;
      }
    }

    float total = static_cast<float>(simple_intervals + complex_intervals);
    if (total > 0) {
      score.pitch_simplicity = static_cast<float>(simple_intervals) / total;
    } else {
      score.pitch_simplicity = 0.5f;
    }
  }

  return score;
}

}  // namespace midisketch
