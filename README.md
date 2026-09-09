# midi-sketch

[![CI](https://img.shields.io/github/actions/workflow/status/libraz/midi-sketch/ci.yml?branch=main&label=CI)](https://github.com/libraz/midi-sketch/actions)
[![codecov](https://codecov.io/gh/libraz/midi-sketch/branch/main/graph/badge.svg)](https://codecov.io/gh/libraz/midi-sketch)
[![Version](https://img.shields.io/badge/version-0.3.0-blue.svg)](https://github.com/libraz/midi-sketch)
[![License](https://img.shields.io/badge/license-AGPL--3.0%20%2F%20Commercial-green)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue?logo=c%2B%2B)](https://en.cppreference.com/w/cpp/17)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20WebAssembly-lightgrey)](https://github.com/libraz/midi-sketch)

A C++17 library for auto-generating pop music MIDI sketches. Designed for WebAssembly deployment with zero external dependencies.

---

### [Live Demo](https://midisketch.libraz.net/) | [Documentation](https://midisketch.libraz.net/docs/getting-started)

---

## Features

- **9 Track Output**: Vocal, Chord, Bass, Motif, Arpeggio, Aux, Guitar, Drums, SE
- **Rich Presets**: Structure (18) × StylePreset (17) × Mood (24) × Chord Progression (22)
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
#include "core/preset_data.h"
#include "midisketch.h"

int main() {
  midisketch::MidiSketch sketch;
  auto config = midisketch::createDefaultSongConfig(0);
  config.seed = 12345;

  sketch.generateFromConfig(config);
  const auto midi = sketch.getMidi();  // SMF binary
  return midi.empty() ? 1 : 0;
}
```

### JavaScript / TypeScript (WASM)

```typescript
import { createDefaultConfig, init, MidiSketch } from '@libraz/midi-sketch';

await init();
const sketch = new MidiSketch();
const config = createDefaultConfig(0);
config.seed = 12345;

sketch.generateFromConfig(config);

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
| Guitar | 6 | Clean Guitar | Harmonic accompaniment |
| Drums | 9 | - | GM Drums |
| SE | 15 | - | Markers |

## Documentation

- [Getting Started](https://midisketch.libraz.net/docs/getting-started)
- [API Reference (JavaScript)](https://midisketch.libraz.net/docs/api-js)
- [API Reference (C++)](https://midisketch.libraz.net/docs/api-cpp)
- [Presets](https://midisketch.libraz.net/docs/presets)
- [Track Generators](https://midisketch.libraz.net/docs/track-generators)
- [Architecture](https://midisketch.libraz.net/docs/architecture)
- [CLI Reference](https://midisketch.libraz.net/docs/cli)

## License

[AGPL-3.0](LICENSE) / [Commercial](LICENSE-COMMERCIAL) dual license. Free to use, modify, and redistribute under AGPL-3.0; embedding in closed-source products or proprietary SaaS requires a commercial license. For commercial inquiries: libraz@libraz.net

### Scope Notice

This project provides a core engine, not a complete music generation system.

Sound sources, vocals, and rendering are out of scope.

## Author

libraz <libraz@libraz.net>
