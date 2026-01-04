cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0048 NEW)
cmake_policy(SET CMP0077 NEW)
project(midi-sketch VERSION 0.1.0 LANGUAGES CXX)

# C++17 標準
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ビルドオプション
option(BUILD_CLI "Build CLI executable" ON)
option(BUILD_TESTING "Build tests" ON)
option(BUILD_WASM "Build for WebAssembly" OFF)

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
    "-sEXPORTED_FUNCTIONS=['_malloc','_free','_midisketch_create','_midisketch_destroy','_midisketch_generate','_midisketch_regenerate_melody','_midisketch_get_midi','_midisketch_free_midi','_midisketch_get_events','_midisketch_free_events','_midisketch_get_info','_midisketch_structure_count','_midisketch_mood_count','_midisketch_chord_count','_midisketch_structure_name','_midisketch_mood_name','_midisketch_chord_name','_midisketch_chord_display','_midisketch_mood_default_bpm','_midisketch_version','_midisketch_style_preset_count','_midisketch_style_preset_name','_midisketch_style_preset_display_name','_midisketch_style_preset_description','_midisketch_style_preset_tempo_default','_midisketch_style_preset_allowed_attitudes','_midisketch_get_progressions_by_style_ptr','_midisketch_get_forms_by_style_ptr','_midisketch_create_default_config_ptr','_midisketch_validate_config','_midisketch_generate_from_config']"
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
