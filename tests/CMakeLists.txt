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
  core/generator_vocal_test.cpp
  core/generator_motif_test.cpp
  core/generator_dynamics_test.cpp
  core/midi_track_test.cpp
  core/arrangement_test.cpp
  core/song_test.cpp
  core/style_preset_test.cpp
  core/harmony_integration_test.cpp
  core/call_test.cpp
  core/randomness_test.cpp
  core/motif_test.cpp
  core/struct_layout_test.cpp
  core/pitch_utils_test.cpp
  core/chord_utils_test.cpp
  core/melody_templates_test.cpp
  midi/midi_writer_test.cpp
  track/arpeggio_test.cpp
  track/vocal_test.cpp
  track/chord_track_test.cpp
  track/bass_test.cpp
  track/drums_test.cpp
  track/melody_designer_test.cpp
  analysis/dissonance_test.cpp
  api/c_api_test.cpp
)

add_executable(midisketch_tests ${TEST_SOURCES})
target_link_libraries(midisketch_tests PRIVATE
  midisketch_lib
  GTest::gtest_main
)

gtest_discover_tests(midisketch_tests)
