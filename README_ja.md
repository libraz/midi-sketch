# midi-sketch

[![CI](https://github.com/libraz/midi-sketch/actions/workflows/ci.yml/badge.svg)](https://github.com/libraz/midi-sketch/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/libraz/midi-sketch/branch/main/graph/badge.svg)](https://codecov.io/gh/libraz/midi-sketch)
[![Version](https://img.shields.io/badge/version-1.0.0-blue.svg)](https://github.com/libraz/midi-sketch)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue)](https://github.com/libraz/midi-sketch/blob/main/LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue?logo=c%2B%2B)](https://en.cppreference.com/w/cpp/17)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20WebAssembly-lightgrey)](https://github.com/libraz/midi-sketch)

ポップス楽曲のMIDIスケッチを自動生成するC++17ライブラリです。
外部ライブラリに依存せず、WebAssemblyとしてブラウザ上でも動作します。

---

### [デモ](https://midisketch.libraz.net/ja/) | [ドキュメント](https://midisketch.libraz.net/ja/docs/getting-started.html)

---

## 特徴

- **8トラック出力** — Vocal / Chord / Bass / Motif / Arpeggio / Aux / Drums / SE
- **豊富なプリセット** — 曲構成(18) × スタイル(17) × ムード(20) × コード進行(22)
- **高度なメロディ** — フレーズベース生成、HookIntensity、MelodicComplexity、VocalStyleProfile
- **音楽理論** — ボイスリーディング、非和声音、テンションコード、セクション別ダイナミクス
- **作曲スタイル** — MelodyLead、BackgroundMotif、SynthDriven
- **再現性** — シード値による再現可能な生成

## ビルド

```bash
make build              # ネイティブビルド
make test               # テスト実行
./build/bin/midisketch_cli  # output.mid を生成

# WASMビルド（要 Emscripten）
source ~/emsdk/emsdk_env.sh && make wasm
```

## 使い方

### C++ API

```cpp
#include "midisketch.h"

midisketch::MidiSketch sketch;
midisketch::GeneratorParams params;
params.structure_id = 1;   // BuildUp (0-17)
params.mood_id = 0;        // StraightPop (0-19)
params.chord_id = 0;       // カノン進行 (0-21)
params.seed = 12345;

sketch.generate(params);
auto midi = sketch.getMidi();       // SMFバイナリ
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

## 出力トラック

| トラック | Ch | Program | 役割 |
|---------|-----|---------|------|
| Vocal | 0 | Piano | メロディ |
| Chord | 1 | E.Piano | コード |
| Bass | 2 | E.Bass | ベース |
| Motif | 3 | Synth Lead | 背景リフ |
| Arpeggio | 4 | Saw Lead | アルペジオ |
| Aux | 5 | Warm Pad | サブメロディ |
| Drums | 9 | — | GM準拠 |
| SE | 15 | — | マーカー |

## ドキュメント

- [はじめに](https://midisketch.libraz.net/ja/docs/getting-started.html)
- [APIリファレンス (JavaScript)](https://midisketch.libraz.net/ja/docs/api-js.html)
- [APIリファレンス (C++)](https://midisketch.libraz.net/ja/docs/api-cpp.html)
- [プリセット一覧](https://midisketch.libraz.net/ja/docs/presets.html)
- [トラック生成](https://midisketch.libraz.net/ja/docs/track-generators.html)
- [アーキテクチャ](https://midisketch.libraz.net/ja/docs/architecture.html)
- [CLIリファレンス](https://midisketch.libraz.net/ja/docs/cli.html)

## ライセンス

[Apache-2.0](LICENSE) / [商用](LICENSE-COMMERCIAL) デュアルライセンス。商用利用のお問い合わせ: libraz@libraz.net

### スコープについて

本プロジェクトは **MIDIスケッチ生成エンジン** を提供するものです。
完成された楽曲制作システムではありません。

音源・ボーカル合成・オーディオレンダリングは本プロジェクトのスコープ外です。

## 作者

libraz <libraz@libraz.net>
