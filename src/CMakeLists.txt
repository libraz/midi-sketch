# Core sources (required for both WASM and CLI)
set(MIDISKETCH_CORE_SOURCES
  midisketch.cpp
  midisketch_c.cpp
  core/types.cpp
  core/generator.cpp
  core/structure.cpp
  core/chord.cpp
  core/velocity.cpp
  core/preset_data.cpp
  core/midi_track.cpp
  core/arrangement.cpp
  core/song.cpp
  core/harmony_context.cpp
  core/harmony_coordinator.cpp
  core/coordinator.cpp
  core/i_track_base.cpp
  core/track_base.cpp
  core/chord_progression_tracker.cpp
  core/track_collision_detector.cpp
  core/track_registration_guard.cpp
  core/motif.cpp
  core/motif_transform.cpp
  core/pitch_utils.cpp
  core/chord_utils.cpp
  core/melody_types.cpp
  core/melody_templates.cpp
  core/melody_evaluator.cpp
  core/melody_embellishment.cpp
  core/piano_roll_safety.cpp
  core/collision_resolver.cpp
  core/config_converter.cpp
  core/modulation_calculator.cpp
  core/post_processor.cpp
  core/section_utils.cpp
  core/production_blueprint.cpp
  core/euclidean_rhythm.cpp
  core/emotion_curve.cpp
  core/swing_quantize.cpp
  core/pitch_bend_curves.cpp
  core/note_creator.cpp
  core/note_timeline_utils.cpp
  core/timing_offset_calculator.cpp
  midi/midi_writer.cpp
  track/track.cpp
  track/vocal/phrase_variation.cpp
  track/vocal/vocal_helpers.cpp
  track/vocal/melody_designer.cpp
  track/vocal/vocal_analysis.cpp
  track/vocal/phrase_planner.cpp
  track/chord/voicing_generator.cpp
  track/chord/voice_leading.cpp
  track/chord/bass_coordination.cpp
  track/drums.cpp
  track/drums/drum_constants.h
  track/drums/fill_generator.cpp
  track/drums/ghost_notes.cpp
  track/drums/kick_patterns.cpp
  track/drums/hihat_control.cpp
  track/drums/percussion_generator.cpp
  track/drums/drum_track_generator.cpp
  track/drums/beat_processors.cpp
  track/melody/melody_utils.cpp
  track/melody/hook_rhythm_patterns.cpp
  track/melody/pitch_resolver.cpp
  track/melody/rhythm_generator.cpp
  track/melody/motif_support.cpp
  track/melody/contour_direction.cpp
  track/melody/note_constraints.cpp
  track/melody/pitch_constraints.cpp
  track/melody/constraint_pipeline.cpp
  track/melody/leap_resolution.cpp
  track/melody/isolated_note_resolver.cpp
  track/melody/phrase_note_generator.cpp
  track/melody/melody_generation_pipeline.cpp
  track/generators/vocal.cpp
  track/generators/bass.cpp
  track/generators/chord.cpp
  track/generators/motif.cpp
  track/generators/aux.cpp
  track/generators/arpeggio.cpp
  track/generators/drums.cpp
  track/generators/se.cpp
  track/generators/guitar.cpp
  instrument/fretted/fretted_instrument_base.cpp
  instrument/fretted/bass_model.cpp
  instrument/fretted/guitar_model.cpp
  instrument/fretted/fretted_note_factory.cpp
  instrument/drums/drum_performer.cpp
)

# CLI-only sources (not needed for WASM)
set(MIDISKETCH_CLI_SOURCES
  midi/midi_reader.cpp
  midi/midi_validator.cpp
  midi/ump.cpp
  midi/midi2_writer.cpp
  midi/midi2_reader.cpp
  analysis/dissonance.cpp
)

# Combined sources for full library
set(MIDISKETCH_SOURCES
  ${MIDISKETCH_CORE_SOURCES}
  ${MIDISKETCH_CLI_SOURCES}
)

# Create static library
add_library(midisketch_lib STATIC ${MIDISKETCH_SOURCES})
target_include_directories(midisketch_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(BUILD_WASM)
  # WASM executable (core sources only, excludes CLI-only modules)
  add_executable(midisketch ${MIDISKETCH_CORE_SOURCES})
  target_include_directories(midisketch PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  # MIDISKETCH_WASM: General WASM build flag
  # MIDISKETCH_NO_NOTE_PROVENANCE: Disable NoteEvent provenance fields (~40 bytes/note savings)
  target_compile_definitions(midisketch PRIVATE MIDISKETCH_WASM MIDISKETCH_NO_NOTE_PROVENANCE)

  # Copy to dist directory
  add_custom_command(TARGET midisketch POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/dist
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:midisketch> ${CMAKE_SOURCE_DIR}/dist/
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/bin/midisketch.wasm ${CMAKE_SOURCE_DIR}/dist/
    COMMENT "Copying WASM files to dist/"
  )
elseif(BUILD_CLI)
  # CLI executable for testing
  add_executable(midisketch_cli cli_main.cpp)
  target_link_libraries(midisketch_cli PRIVATE midisketch_lib)

  # Benchmark executable for profiling
  add_executable(bench_generate ${CMAKE_SOURCE_DIR}/tools/bench_generate.cpp)
  target_link_libraries(bench_generate PRIVATE midisketch_lib)
endif()
