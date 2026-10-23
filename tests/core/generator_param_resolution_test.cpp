/**
 * @file generator_param_resolution_test.cpp
 * @brief Tests for values the generator resolves and has to keep.
 *
 * Two of the generator's parameters are resolved rather than taken verbatim,
 * and both are read back out later: the automatic seed, which is what the
 * exported metadata carries and therefore what a regeneration replays, and the
 * riff policy, which addictive mode fixes regardless of which blueprint is in
 * use. A resolved value that is not written back leaves the caller holding a
 * request instead of a result.
 */

#include <gtest/gtest.h>

#include <vector>

#include "core/config_converter.h"
#include "core/coordinator.h"
#include "core/generator.h"
#include "core/preset_data.h"
#include "core/production_blueprint.h"
#include "core/rhythm_sync_lead.h"
#include "core/section_types.h"
#include "core/song.h"
#include "core/types.h"
#include "midisketch.h"

namespace midisketch {
namespace {

GeneratorParams baseParams() {
  GeneratorParams params;
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::ElectroPop;
  params.chord_id = 0;
  params.key = Key::C;
  params.bpm = 120;
  params.seed = 42;
  params.humanize = false;
  return params;
}

std::vector<std::pair<Tick, uint8_t>> vocalShape(const Song& song) {
  std::vector<std::pair<Tick, uint8_t>> out;
  for (const auto& note : song.vocal().notes()) {
    out.push_back({note.start_tick, note.note});
  }
  return out;
}

// ============================================================================
// Resolved seed write-back
// ============================================================================

TEST(GeneratorSeedResolutionTest, RegenerateVocalKeepsTheSeedItActuallyUsed) {
  Generator gen;
  gen.generateVocal(baseParams());
  ASSERT_FALSE(gen.getSong().vocal().empty());

  gen.regenerateVocal(0);  // 0 asks for a fresh automatic seed
  EXPECT_NE(gen.getParams().seed, 0u)
      << "The requested value, not the resolved one, was left in the parameters";
  EXPECT_NE(gen.getParams().seed, 42u) << "The seed of the previous take was left in place";
}

TEST(GeneratorSeedResolutionTest, ReplayingTheKeptSeedReproducesTheTake) {
  Generator gen;
  gen.generateVocal(baseParams());
  gen.regenerateVocal(0);
  const uint32_t kept_seed = gen.getParams().seed;
  ASSERT_NE(kept_seed, 0u);
  const auto take = vocalShape(gen.getSong());
  ASSERT_FALSE(take.empty());

  Generator replay;
  replay.generateVocal(baseParams());
  replay.regenerateVocal(kept_seed);
  EXPECT_EQ(vocalShape(replay.getSong()), take)
      << "The take cannot be reproduced from the seed the generator kept";
}

TEST(GeneratorSeedResolutionTest, RegenerateVocalWithAnExplicitSeedKeepsThatSeed) {
  Generator gen;
  gen.generateVocal(baseParams());
  gen.regenerateVocal(99);
  EXPECT_EQ(gen.getParams().seed, 99u);
}

// ============================================================================
// Addictive mode riff policy
// ============================================================================

// Addictive mode promises a riff that repeats verbatim. LockedContour would
// allow each section to revoice the same shape, so the resolved policy has to
// be LockedPitch whichever blueprint the song runs with.
TEST(GeneratorAddictiveModeTest, CallerRequestedAddictiveModeLocksTheRiffPitches) {
  constexpr uint8_t kBlueprints[] = {0, 1, 2, 3, 4, 6, 8, 9};

  for (uint8_t blueprint : kBlueprints) {
    GeneratorParams params = baseParams();
    params.blueprint_id = blueprint;
    params.addictive_mode = true;

    Generator gen;
    gen.generate(params);
    EXPECT_EQ(gen.getParams().riff_policy, RiffPolicy::LockedPitch)
        << "blueprint=" << static_cast<int>(blueprint);
    EXPECT_TRUE(gen.getParams().addictive_mode) << "blueprint=" << static_cast<int>(blueprint);
  }
}

// Without the request, a blueprint that does not declare addictive mode keeps
// its own riff policy; otherwise the test above would pass for the wrong reason.
TEST(GeneratorAddictiveModeTest, WithoutTheRequestTheBlueprintPolicyStands) {
  // The blueprint has to declare a policy that is neither LockedPitch nor the
  // one GeneratorParams starts with. Traditional declares Free, which is that
  // starting value, so the resolution could stop reading the blueprint entirely
  // and the expectations below would still hold.
  constexpr uint8_t kBlueprintId = 2;  // StoryPop: Evolving, no addictive mode
  const auto& blueprint = getProductionBlueprint(kBlueprintId);
  ASSERT_NE(blueprint.riff_policy, RiffPolicy::LockedPitch);
  ASSERT_NE(blueprint.riff_policy, GeneratorParams{}.riff_policy);
  ASSERT_FALSE(blueprint.addictive_mode);

  GeneratorParams params = baseParams();
  params.blueprint_id = kBlueprintId;
  params.addictive_mode = false;

  Generator gen;
  gen.generate(params);
  EXPECT_EQ(gen.getParams().riff_policy, blueprint.riff_policy);
  EXPECT_FALSE(gen.getParams().addictive_mode);
}

// A default may fill only what the caller left unset.
TEST(GeneratorExplicitChoiceTest, AddictiveModeKeepsTheCallersMotifShape) {
  GeneratorParams params = baseParams();
  params.blueprint_id = 9;  // BehavioralLoop: addictive by declaration
  params.addictive_mode = true;
  params.motif.note_count = 4;
  params.motif_note_count_explicit = true;
  params.motif.rhythm_density = MotifRhythmDensity::Sparse;
  params.motif_rhythm_density_explicit = true;
  params.motif.length = MotifLength::Bars2;
  params.motif_length_explicit = true;

  Generator gen;
  gen.generate(params);
  ASSERT_TRUE(gen.getParams().addictive_mode);
  EXPECT_EQ(gen.getParams().motif.note_count, 4);
  EXPECT_EQ(gen.getParams().motif.rhythm_density, MotifRhythmDensity::Sparse);
  EXPECT_EQ(gen.getParams().motif.length, MotifLength::Bars2);

  // Fields left to the defaults still take the loop's.
  GeneratorParams implicit = baseParams();
  implicit.blueprint_id = 9;
  implicit.addictive_mode = true;
  Generator loop;
  loop.generate(implicit);
  EXPECT_EQ(loop.getParams().motif.note_count, 8);
  EXPECT_EQ(loop.getParams().motif.rhythm_density, MotifRhythmDensity::Driving);
  const MotifLength expected_length =
      loop.getParams().motif.rhythm_template == MotifRhythmTemplate::HalfNoteSparse
          ? MotifLength::Bars2
          : MotifLength::Bars1;
  EXPECT_EQ(loop.getParams().motif.length, expected_length);
}

TEST(GeneratorExplicitChoiceTest, ModulationComesFromThisSongsConfigOnly) {
  SongConfig modulated = createDefaultSongConfig(0);
  modulated.seed = 4242;
  modulated.blueprint_id = 0;
  modulated.form = StructurePattern::FullPop;
  modulated.form_explicit = true;
  modulated.modulation_timing = ModulationTiming::LastChorus;
  modulated.modulation_semitones = 2;
  SongConfig plain = modulated;
  plain.modulation_timing = ModulationTiming::None;

  Generator gen;
  gen.generateFromConfig(modulated);
  ASSERT_GT(gen.getSong().modulationAmount(), 0) << "fixture must modulate";
  gen.generateFromConfig(plain);
  EXPECT_EQ(gen.getSong().modulationAmount(), 0)
      << "an earlier song's modulation leaked into an explicit None";
  EXPECT_EQ(gen.getParams().modulation_timing, ModulationTiming::None);

  // A pre-set made for one generation applies to it, and only to it.
  Generator preset;
  preset.setModulationTiming(ModulationTiming::LastChorus, 1);
  preset.generate(baseParams());
  EXPECT_EQ(preset.getSong().modulationAmount(), 1);
  preset.generate(baseParams());
  EXPECT_EQ(preset.getSong().modulationAmount(), 0);
}

TEST(GeneratorExplicitChoiceTest, RhythmSyncLeadKeepsAnExplicitLeap) {
  SongConfig config = createDefaultSongConfig(14);  // Anime Opening
  config.seed = 777;
  config.blueprint_id = 1;  // RhythmLock
  config.mood = static_cast<uint8_t>(Mood::AnimeHighEnergy);
  config.mood_explicit = true;
  config.melody_max_leap = 5;

  Generator gen;
  gen.generateFromConfig(config);
  ASSERT_TRUE(isRhythmSyncLeadSetting(gen.getParams(), gen.resolvedBlueprintId()))
      << "fixture must reach the RhythmSync lead setting";
  EXPECT_EQ(gen.getParams().melody_params.max_leap_interval, 5);
}

SongConfig rhythmSyncLeadConfig() {
  SongConfig config = createDefaultSongConfig(14);  // Anime Opening
  config.seed = 777;
  config.blueprint_id = 1;  // RhythmLock
  config.mood = static_cast<uint8_t>(Mood::AnimeHighEnergy);
  config.mood_explicit = true;
  return config;
}

TEST(GeneratorExplicitChoiceTest, RhythmSyncLeadKeepsEveryExplicitVocalChoice) {
  SongConfig config = rhythmSyncLeadConfig();
  config.vocal_style = VocalStylePreset::Ballad;
  config.hook_intensity = HookIntensity::Light;
  config.hook_intensity_explicit = true;
  config.vocal_groove = VocalGrooveFeel::Swing;
  config.vocal_groove_explicit = true;
  config.melody_syncopation_prob = 0;

  Generator gen;
  gen.generateFromConfig(config);
  ASSERT_TRUE(isRhythmSyncLeadSetting(gen.getParams(), gen.resolvedBlueprintId()))
      << "fixture must reach the RhythmSync lead setting";
  const GeneratorParams& params = gen.getParams();
  EXPECT_EQ(params.vocal_style, VocalStylePreset::Ballad);
  EXPECT_EQ(params.hook_intensity, HookIntensity::Light);
  EXPECT_EQ(params.vocal_groove, VocalGrooveFeel::Swing);
  EXPECT_FLOAT_EQ(params.melody_params.syncopation_prob, 0.0f);
}

TEST(GeneratorExplicitChoiceTest, RhythmSyncLeadStyleBringsItsMelodyControlsAlong) {
  Generator gen;
  gen.generateFromConfig(rhythmSyncLeadConfig());
  ASSERT_TRUE(isRhythmSyncLeadSetting(gen.getParams(), gen.resolvedBlueprintId()))
      << "fixture must reach the RhythmSync lead setting";
  const GeneratorParams& params = gen.getParams();
  ASSERT_EQ(params.vocal_style, VocalStylePreset::Vocaloid);
  const VocalStylePresetData& style = getVocalStylePresetData(VocalStylePreset::Vocaloid);
  EXPECT_FLOAT_EQ(params.melody_params.consecutive_same_note_prob,
                  style.consecutive_same_note_prob);
  EXPECT_FLOAT_EQ(params.melody_params.chorus_density_modifier, style.chorus_density_modifier);
}

TEST(GeneratorExplicitChoiceTest, RhythmSyncLeadHonoursAnExplicitSyncopationOff) {
  SongConfig config = rhythmSyncLeadConfig();
  config.enable_syncopation = false;
  config.enable_syncopation_explicit = true;

  Generator gen;
  gen.generateFromConfig(config);
  ASSERT_TRUE(isRhythmSyncLeadSetting(gen.getParams(), gen.resolvedBlueprintId()))
      << "fixture must reach the RhythmSync lead setting";
  EXPECT_FLOAT_EQ(gen.getParams().melody_params.syncopation_prob, 0.0f);

  config.enable_syncopation_explicit = false;
  Generator defaulted;
  defaulted.generateFromConfig(config);
  EXPECT_GE(defaulted.getParams().melody_params.syncopation_prob, 0.30f);
}

TEST(GeneratorExplicitChoiceTest, RhythmSyncLeadStyleSwapKeepsRegisterAndSyllabicOverrides) {
  SongConfig config = rhythmSyncLeadConfig();
  config.melody_chorus_register_shift = 3;
  config.syllabic_sub_rate = 20;

  Generator gen;
  gen.generateFromConfig(config);
  ASSERT_TRUE(isRhythmSyncLeadSetting(gen.getParams(), gen.resolvedBlueprintId()))
      << "fixture must reach the RhythmSync lead setting";
  ASSERT_EQ(gen.getParams().vocal_style, VocalStylePreset::Vocaloid);
  EXPECT_EQ(gen.getParams().melody_params.chorus_register_shift, 3);
  EXPECT_FLOAT_EQ(gen.getParams().melody_params.syllabic_sub_ratio, 0.20f);
}

TEST(GeneratorExplicitChoiceTest, AddictiveModeKeepsAnExplicitHookIntensity) {
  SongConfig config = createDefaultSongConfig(0);
  config.seed = 4242;
  config.addictive_mode = true;
  config.hook_intensity = HookIntensity::Light;
  config.hook_intensity_explicit = true;

  Generator gen;
  gen.generateFromConfig(config);
  EXPECT_TRUE(gen.getParams().addictive_mode);
  EXPECT_EQ(gen.getParams().hook_intensity, HookIntensity::Light);

  config.hook_intensity_explicit = false;
  Generator defaulted;
  defaulted.generateFromConfig(config);
  EXPECT_EQ(defaulted.getParams().hook_intensity, HookIntensity::Maximum);
}

TEST(GeneratorPlanParityTest, StandaloneCoordinatorPlansTheSectionsTheGeneratorDoes) {
  for (uint8_t blueprint : {uint8_t{0}, uint8_t{1}}) {  // one Traditional, one RhythmSync
    SongConfig config = createDefaultSongConfig(0);
    config.seed = 31337;
    config.blueprint_id = blueprint;
    const GeneratorParams params = ConfigConverter::convert(config);

    Generator gen;
    gen.generateVocal(params);
    const auto& expected = gen.getSong().arrangement().sections();
    ASSERT_FALSE(expected.empty());

    Coordinator coordinator;
    coordinator.initialize(gen.getParams());
    const auto& planned = coordinator.getArrangement().sections();
    ASSERT_EQ(planned.size(), expected.size()) << "blueprint " << int(blueprint);
    for (size_t i = 0; i < planned.size(); ++i) {
      EXPECT_EQ(planned[i].density_percent, expected[i].density_percent)
          << "blueprint " << int(blueprint) << " section " << i;
      EXPECT_EQ(planned[i].base_velocity, expected[i].base_velocity)
          << "blueprint " << int(blueprint) << " section " << i;
      EXPECT_EQ(planned[i].layer_events.size(), expected[i].layer_events.size())
          << "blueprint " << int(blueprint) << " section " << i;
    }
  }
}

TEST(GeneratorPlanParityTest, StandaloneCoordinatorResolvesTheImplicitTempoTheGeneratorDoes) {
  for (uint8_t style : {uint8_t{0}, uint8_t{16}}) {  // a pop preset and Emotional Ballad
    SongConfig config = createDefaultSongConfig(style);
    config.seed = 5150;
    config.bpm = 0;
    const GeneratorParams params = ConfigConverter::convert(config);

    Generator gen;
    gen.generate(params);
    Coordinator coordinator;
    coordinator.initialize(params);
    EXPECT_EQ(coordinator.getBpm(), gen.getSong().bpm()) << "style " << int(style);
    EXPECT_EQ(coordinator.getBlueprintId(), gen.resolvedBlueprintId()) << "style " << int(style);
  }
}

TEST(GeneratorRegenerateVocalTest, RepeatedComplexityUpdatesDoNotCompound) {
  SongConfig config = createDefaultSongConfig(0);
  config.seed = 2024;

  VocalConfig update;
  update.present_fields = VocalConfig::Seed | VocalConfig::MelodicComplexityField;
  update.melodic_complexity = MelodicComplexity::Simple;
  update.seed = 99;

  MidiSketch sketch;
  sketch.generateFromConfig(config);
  sketch.regenerateVocal(update);
  const float once = sketch.getParams().melody_params.note_density;
  sketch.regenerateVocal(update);
  sketch.regenerateVocal(update);
  const float thrice = sketch.getParams().melody_params.note_density;

  SongConfig final_config = config;
  final_config.melodic_complexity = MelodicComplexity::Simple;
  const float converted = ConfigConverter::convert(final_config).melody_params.note_density;

  EXPECT_FLOAT_EQ(once, thrice);
  EXPECT_FLOAT_EQ(thrice, converted);
}

TEST(GeneratorRegenerateVocalTest, UpdatesWithoutASongConfigDoNotCompoundEither) {
  SongConfig config = createDefaultSongConfig(0);
  config.seed = 2024;
  config.melody_max_leap = 9;  // a caller override that must survive the rebuild

  VocalConfig update;
  update.present_fields = VocalConfig::Seed | VocalConfig::MelodicComplexityField;
  update.melodic_complexity = MelodicComplexity::Simple;
  update.seed = 99;

  Generator gen;
  gen.generate(ConfigConverter::convert(config));
  gen.regenerateVocal(update);
  const float once = gen.getParams().melody_params.note_density;
  gen.regenerateVocal(update);
  gen.regenerateVocal(update);

  SongConfig simple = config;
  simple.melodic_complexity = MelodicComplexity::Simple;
  const StyleMelodyParams converted = ConfigConverter::convert(simple).melody_params;
  const StyleMelodyParams& after = gen.getParams().melody_params;
  EXPECT_FLOAT_EQ(after.note_density, once);
  EXPECT_FLOAT_EQ(after.note_density, converted.note_density);
  EXPECT_FLOAT_EQ(after.tension_usage, converted.tension_usage);
  EXPECT_EQ(after.max_leap_interval, 9) << "the caller's leap override is not a derived control";
}

TEST(GeneratorRegenerateVocalTest, ReturningToStandardComplexityRestoresTheConvertedControls) {
  SongConfig config = createDefaultSongConfig(0);
  config.seed = 2024;

  MidiSketch sketch;
  sketch.generateFromConfig(config);
  const float original = sketch.getParams().melody_params.note_density;

  VocalConfig update;
  update.present_fields = VocalConfig::Seed | VocalConfig::MelodicComplexityField;
  update.seed = 99;
  update.melodic_complexity = MelodicComplexity::Complex;
  sketch.regenerateVocal(update);
  ASSERT_NE(sketch.getParams().melody_params.note_density, original);

  update.melodic_complexity = MelodicComplexity::Standard;
  sketch.regenerateVocal(update);
  EXPECT_FLOAT_EQ(sketch.getParams().melody_params.note_density, original);
}

TEST(GeneratorExplicitChoiceTest, AnExplicitZeroProbabilitySilencesAMoodFamilyItsFlagCannot) {
  // The flags are the union of the caller's and the mood's; the probabilities
  // are the caller's once chord_ext_prob_explicit is set.
  SongConfig config = createDefaultSongConfig(16);  // Emotional Ballad: sevenths by mood
  config.chord_ext_prob_explicit = true;
  config.chord_extension.enable_7th = false;
  config.chord_extension.seventh_probability = 0.0f;
  const GeneratorParams params = ConfigConverter::convert(config);
  EXPECT_TRUE(params.chord_extension.enable_7th);
  EXPECT_EQ(params.chord_extension.seventh_probability, 0.0f);
}

}  // namespace
}  // namespace midisketch
