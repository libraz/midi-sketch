# Google Test
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# Test sources
set(TEST_SOURCES
  core/types_test.cpp
  core/structure_test.cpp
  core/chord_test.cpp
  core/generator_test.cpp
  core/generator_di_test.cpp
  core/generator_vocal_test.cpp
  core/generator_vocal_first_test.cpp
  core/generator_motif_test.cpp
  core/generator_dynamics_test.cpp
  core/midi_track_test.cpp
  core/arrangement_test.cpp
  core/song_test.cpp
  core/style_preset_test.cpp
  core/harmony_integration_test.cpp
  core/track_registration_guard_test.cpp
  core/call_test.cpp
  core/randomness_test.cpp
  core/motif_test.cpp
  core/motif_transform_test.cpp
  core/struct_layout_test.cpp
  core/pitch_utils_test.cpp
  core/chord_utils_test.cpp
  core/melody_templates_test.cpp
  core/melody_evaluator_test.cpp
  core/hook_utils_test.cpp
  core/melody_embellishment_test.cpp
  core/piano_roll_test.cpp
  core/music_theory_test.cpp
  core/velocity_test.cpp
  core/post_processor_test.cpp
  core/modulation_calculator_test.cpp
  core/modulation_bgm_test.cpp
  core/modulation_vocal_range_test.cpp
  core/section_utils_test.cpp
  core/json_helpers_test.cpp
  core/production_blueprint_test.cpp
  core/euclidean_rhythm_test.cpp
  core/staggered_entry_test.cpp
  core/layer_schedule_test.cpp
  core/emotion_curve_test.cpp
  core/emotion_curve_velocity_test.cpp
  core/harmonic_rhythm_test.cpp
  core/harmonic_rhythm_subdivision_test.cpp
  core/swing_quantize_test.cpp
  core/exit_pattern_test.cpp
  core/pitch_bend_test.cpp
  core/secondary_dominant_test.cpp
  core/slash_chord_test.cpp
  core/motif_motion_test.cpp
  core/genre_expansion_test.cpp
  core/section_modifier_test.cpp
  core/coordinator_test.cpp
  core/note_creator_test.cpp
  midi/midi_writer_smf1_test.cpp
  midi/cc_event_test.cpp
  midi/midi_reader_test.cpp
  midi/midi_validator_test.cpp
  midi/ump_test.cpp
  midi/midi2_writer_test.cpp
  midi/midi2_reader_test.cpp
  track/arpeggio_test.cpp
  track/vocal_test.cpp
  track/vocal_helpers_test.cpp
  track/chord_track_test.cpp
  track/bass_test.cpp
  track/bass_with_vocal_test.cpp
  track/bass_diatonic_test.cpp
  track/chord_with_context_test.cpp
  track/drums_test.cpp
  track/melody_designer_test.cpp
  track/motif_test.cpp
  track/aux_track_test.cpp
  track/aux_dissonance_regression_test.cpp
  track/aux_chorus_behavior_test.cpp
  track/phrase_ending_test.cpp
  track/bass_dissonance_regression_test.cpp
  track/bass_music_theory_test.cpp
  track/bass_physical_model_test.cpp
  track/dissonance_fixes_test.cpp
  track/se_test.cpp
  track/vocal_analysis_test.cpp
  track/swing_control_test.cpp
  track/motif_vocal_coordination_test.cpp
  track/fretted_instrument_test.cpp
  instrument/drum_performer_test.cpp
  analysis/dissonance_test.cpp
  analysis/non_diatonic_detection_test.cpp
  api/c_api_test.cpp
  integration/dissonance_integration_test.cpp
  integration/dissonance_diagnostic_test.cpp
)

add_executable(midisketch_tests ${TEST_SOURCES})
target_include_directories(midisketch_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(midisketch_tests PRIVATE
  midisketch_lib
  GTest::gtest_main
)

gtest_discover_tests(midisketch_tests)
