# midi-sketch

A C++17 library for auto-generating pop music MIDI sketches. Designed to be lightweight and WebAssembly-ready.

> **Status: Proof of Concept**
>
> This is an early experiment. Not production-ready, not even alpha.
> APIs will change. Features are incomplete. Use at your own risk.

## Goal

Generate quick MIDI sketches for pop music composition:
- 5 tracks: Vocal (melody), Chord, Bass, Drums, SE
- Configurable structure, mood, and chord progressions
- Deterministic output via seed control
- Zero external dependencies for WASM deployment

## Build

```bash
# Native build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Run CLI (outputs output.mid)
./build/bin/midisketch_cli

# Run tests
ctest --test-dir build

# WASM build (requires Emscripten)
cmake -B build-wasm -DCMAKE_BUILD_TYPE=Release -DBUILD_WASM=ON
cmake --build build-wasm
```

## Quick Example

```cpp
#include "midisketch.h"

midisketch::MidiSketch sketch;
midisketch::GeneratorParams params;
params.structure_id = 0;  // Structure preset (0-4)
params.mood_id = 0;       // Mood preset (0-15)
params.chord_id = 0;      // Chord progression (0-15)
params.seed = 12345;      // Random seed (0 = auto)

sketch.generate(params);
auto midi_data = sketch.getMidi();  // SMF Type 1 binary
```

## License

TBD
