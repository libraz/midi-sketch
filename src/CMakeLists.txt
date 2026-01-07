# Core library sources
set(MIDISKETCH_SOURCES
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
  core/motif.cpp
  core/pitch_utils.cpp
  core/chord_utils.cpp
  midi/midi_writer.cpp
  track/track.cpp
  track/vocal.cpp
  track/chord_track.cpp
  track/bass.cpp
  track/drums.cpp
  track/se.cpp
  track/motif.cpp
  track/arpeggio.cpp
  analysis/dissonance.cpp
)

# Create static library
add_library(midisketch_lib STATIC ${MIDISKETCH_SOURCES})
target_include_directories(midisketch_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(BUILD_WASM)
  # WASM executable
  add_executable(midisketch ${MIDISKETCH_SOURCES})
  target_include_directories(midisketch PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

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
endif()
