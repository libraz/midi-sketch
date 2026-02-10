# midi-sketch

[![CI](https://github.com/libraz/midi-sketch/actions/workflows/ci.yml/badge.svg)](https://github.com/libraz/midi-sketch/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/libraz/midi-sketch/branch/main/graph/badge.svg)](https://codecov.io/gh/libraz/midi-sketch)
[![Version](https://img.shields.io/badge/version-1.0.0-blue.svg)](https://github.com/libraz/midi-sketch)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue)](https://github.com/libraz/midi-sketch/blob/main/LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue?logo=c%2B%2B)](https://en.cppreference.com/w/cpp/17)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20WebAssembly-lightgrey)](https://github.com/libraz/midi-sketch)

A C++17 library for auto-generating pop music MIDI sketches. Designed for WebAssembly deployment with zero external dependencies.

---

### [Live Demo](https://midisketch.libraz.net/) | [Documentation](https://midisketch.libraz.net/docs/getting-started.html)

---

## Features

- **8 Track Output**: Vocal, Chord, Bass, Motif, Arpeggio, Aux, Drums, SE
- **Rich Presets**: Structure (18) × StylePreset (17) × Mood (20) × Chord Progression (22)
- **Advanced Melody**: Phrase-based generation, HookIntensity, MelodicComplexity, VocalStyleProfile
- **Music Theory**: Voice leading, non-chord tones, chord extensions, dynamic velocity
- **Composition Styles**: MelodyLead, BackgroundMotif, SynthDriven
- **Deterministic**: Seed-based reproducible generation

## Build

```bash
make build              # Native build
make test               # Run tests
./build/bin/midisketch_cli  # Generate output.mid

# WASM build (requires Emscripten)
source ~/emsdk/emsdk_env.sh && make wasm
```

## Quick Example

### C++ API

```cpp
#include "midisketch.h"

midisketch::MidiSketch sketch;
midisketch::GeneratorParams params;
params.structure_id = 1;   // BuildUp (0-17)
params.mood_id = 0;        // StraightPop (0-19)
params.chord_id = 0;       // Canon progression (0-21)
params.seed = 12345;

sketch.generate(params);
auto midi = sketch.getMidi();       // SMF binary
```

### JavaScript / TypeScript (WASM)

```typescript
import { init, MidiSketch } from '@libraz/midi-sketch';

await init();
const sketch = new MidiSketch();

sketch.generate({
  structureId: 1,
  moodId: 0,
  chordId: 0,
  seed: 12345
});

const midiData = sketch.getMidi();  // Uint8Array
sketch.destroy();
```

## Output Tracks

| Track | Channel | Program | Purpose |
|-------|---------|---------|---------|
| Vocal | 0 | Piano | Melody |
| Chord | 1 | E.Piano | Chords |
| Bass | 2 | E.Bass | Bass line |
| Motif | 3 | Synth Lead | Background |
| Arpeggio | 4 | Saw Lead | Arpeggio |
| Aux | 5 | Warm Pad | Sub-melody |
| Drums | 9 | - | GM Drums |
| SE | 15 | - | Markers |

## Documentation

- [Getting Started](https://midisketch.libraz.net/docs/getting-started.html)
- [API Reference (JavaScript)](https://midisketch.libraz.net/docs/api-js.html)
- [API Reference (C++)](https://midisketch.libraz.net/docs/api-cpp.html)
- [Presets](https://midisketch.libraz.net/docs/presets.html)
- [Track Generators](https://midisketch.libraz.net/docs/track-generators.html)
- [Architecture](https://midisketch.libraz.net/docs/architecture.html)
- [CLI Reference](https://midisketch.libraz.net/docs/cli.html)

## License

[Apache-2.0](LICENSE) / [Commercial](LICENSE-COMMERCIAL) dual license. For commercial inquiries: libraz@libraz.net

### Scope Notice

This project provides a core engine, not a complete music generation system.

Sound sources, vocals, and rendering are out of scope.

## Author

libraz <libraz@libraz.net>
