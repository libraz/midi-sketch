cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0048 NEW)
cmake_policy(SET CMP0077 NEW)
project(midi-sketch VERSION 1.0.0 LANGUAGES CXX)

# C++17 標準
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ビルドオプション
option(BUILD_CLI "Build CLI executable" ON)
option(BUILD_TESTING "Build tests" ON)
option(BUILD_WASM "Build for WebAssembly" OFF)
option(ENABLE_COVERAGE "Enable code coverage" OFF)

# 出力ディレクトリ
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# テスト有効化
enable_testing()

# コンパイラ警告
if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Code Coverage
if(ENABLE_COVERAGE AND NOT BUILD_WASM)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(STATUS "Enabling code coverage")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage -O0 -g")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --coverage -O0 -g")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} --coverage")
  else()
    message(WARNING "Code coverage is only supported with GCC or Clang")
  endif()
endif()

# compile_commands.json 生成
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ソースディレクトリ
set(SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
include_directories(${SOURCE_DIR})

# WASM 設定
if(BUILD_WASM)
  message(STATUS "Building for WebAssembly")
  set(CMAKE_EXECUTABLE_SUFFIX ".js")

  add_compile_options(-Os -flto)
  add_link_options(-Os -flto)

  add_compile_options(
    -sWASM=1
    -sMODULARIZE=1
    -sEXPORT_ES6=1
  )

  add_link_options(
    -sWASM=1
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    "-sEXPORTED_FUNCTIONS=['_malloc','_free','_midisketch_create','_midisketch_destroy','_midisketch_get_midi','_midisketch_get_vocal_preview_midi','_midisketch_free_midi','_midisketch_get_events','_midisketch_free_events','_midisketch_get_info','_midisketch_structure_count','_midisketch_mood_count','_midisketch_chord_count','_midisketch_structure_name','_midisketch_mood_name','_midisketch_chord_name','_midisketch_chord_display','_midisketch_mood_default_bpm','_midisketch_version','_midisketch_style_preset_count','_midisketch_style_preset_name','_midisketch_style_preset_display_name','_midisketch_style_preset_description','_midisketch_style_preset_tempo_default','_midisketch_style_preset_allowed_attitudes','_midisketch_get_progressions_by_style_ptr','_midisketch_get_forms_by_style_ptr','_midisketch_create_default_config_ptr','_midisketch_validate_config','_midisketch_generate_from_config','_midisketch_config_error_string','_midisketch_generate_vocal','_midisketch_regenerate_vocal','_midisketch_generate_accompaniment','_midisketch_generate_accompaniment_with_config','_midisketch_regenerate_accompaniment','_midisketch_regenerate_accompaniment_with_config','_midisketch_generate_with_vocal','_midisketch_set_vocal_notes','_midisketch_get_piano_roll_safety','_midisketch_get_piano_roll_safety_at','_midisketch_get_piano_roll_safety_with_context','_midisketch_free_piano_roll_data','_midisketch_reason_to_string','_midisketch_collision_to_string']"
    "-sEXPORTED_RUNTIME_METHODS=['cwrap','ccall','UTF8ToString','stringToUTF8','lengthBytesUTF8','HEAPU8','HEAPU32']"
    -sALLOW_MEMORY_GROWTH=1
    -sSTACK_SIZE=1048576
  )

  set(WASM_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/dist")
endif()

# サブディレクトリ
add_subdirectory(src)

if(BUILD_TESTING AND NOT BUILD_WASM)
  add_subdirectory(tests)
endif()

# Code Coverage Report Targets
if(ENABLE_COVERAGE AND NOT BUILD_WASM)
  find_program(LCOV_PATH lcov)
  find_program(GENHTML_PATH genhtml)

  if(LCOV_PATH AND GENHTML_PATH)
    include(ProcessorCount)
    ProcessorCount(N)
    if(NOT N EQUAL 0)
      set(CTEST_PARALLEL_ARGS --parallel ${N})
    endif()

    # Detect lcov version to use appropriate options
    execute_process(
      COMMAND ${LCOV_PATH} --version
      OUTPUT_VARIABLE LCOV_VERSION_OUTPUT
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    string(REGEX MATCH "[0-9]+\\.[0-9]+" LCOV_VERSION "${LCOV_VERSION_OUTPUT}")
    message(STATUS "Found lcov version: ${LCOV_VERSION}")

    if(LCOV_VERSION VERSION_GREATER_EQUAL "2.0")
      set(LCOV_IGNORE_ERRORS --ignore-errors mismatch,gcov,source,unused)
      set(LCOV_BRANCH_OPT --rc branch_coverage=1)
      set(GENHTML_IGNORE_ERRORS --ignore-errors source,unmapped)
    else()
      set(LCOV_IGNORE_ERRORS "")
      set(LCOV_BRANCH_OPT --rc lcov_branch_coverage=1)
      set(GENHTML_IGNORE_ERRORS "")
    endif()

    add_custom_target(coverage
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/coverage
      COMMAND ${LCOV_PATH} --directory . --zerocounters
      COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure ${CTEST_PARALLEL_ARGS}
      COMMAND ${LCOV_PATH} --directory . --capture --output-file ${CMAKE_BINARY_DIR}/coverage/coverage.info ${LCOV_BRANCH_OPT} ${LCOV_IGNORE_ERRORS}
      COMMAND ${LCOV_PATH} --remove ${CMAKE_BINARY_DIR}/coverage/coverage.info
        '/usr/*' '*/tests/*' '*/_deps/*'
        --output-file ${CMAKE_BINARY_DIR}/coverage/coverage_filtered.info ${LCOV_BRANCH_OPT} ${LCOV_IGNORE_ERRORS}
      COMMAND ${GENHTML_PATH} ${CMAKE_BINARY_DIR}/coverage/coverage_filtered.info
        --output-directory ${CMAKE_BINARY_DIR}/coverage/html ${LCOV_BRANCH_OPT} ${GENHTML_IGNORE_ERRORS}
      COMMAND ${CMAKE_COMMAND} -E echo "Coverage report generated: ${CMAKE_BINARY_DIR}/coverage/html/index.html"
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      COMMENT "Generating code coverage report"
    )

    add_custom_target(coverage-clean
      COMMAND ${LCOV_PATH} --directory . --zerocounters
      COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/coverage
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      COMMENT "Cleaning coverage data"
    )
  else()
    message(WARNING "lcov and genhtml not found. Coverage report target will not be available.")
    message(WARNING "Install with: sudo apt-get install lcov (Linux)")
  endif()
endif()
