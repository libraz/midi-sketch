# midi-sketch

[![CI](https://img.shields.io/github/actions/workflow/status/libraz/midi-sketch/ci.yml?branch=main&label=CI)](https://github.com/libraz/midi-sketch/actions)
[![codecov](https://codecov.io/gh/libraz/midi-sketch/branch/main/graph/badge.svg)](https://codecov.io/gh/libraz/midi-sketch)
[![Version](https://img.shields.io/badge/version-0.3.0-blue.svg)](https://github.com/libraz/midi-sketch)
[![License](https://img.shields.io/badge/license-AGPL--3.0%20%2F%20Commercial-green)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue?logo=c%2B%2B)](https://en.cppreference.com/w/cpp/17)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20WebAssembly-lightgrey)](https://github.com/libraz/midi-sketch)

ポップス楽曲のMIDIスケッチを自動生成するC++17ライブラリです。
外部ライブラリに依存せず、WebAssemblyとしてブラウザ上でも動作します。

---

### [デモ](https://midisketch.libraz.net/ja/) | [ドキュメント](https://midisketch.libraz.net/ja/docs/getting-started)

---

## 特徴

- **9トラック出力** — Vocal / Chord / Bass / Motif / Arpeggio / Aux / Guitar / Drums / SE
- **豊富なプリセット** — 曲構成(18) × スタイル(17) × ムード(24) × コード進行(22)
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
#include "core/preset_data.h"
#include "midisketch.h"

int main() {
  midisketch::MidiSketch sketch;
  auto config = midisketch::createDefaultSongConfig(0);
  config.seed = 12345;

  sketch.generateFromConfig(config);
  const auto midi = sketch.getMidi();  // SMFバイナリ
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

## 出力トラック

| トラック | Ch | Program | 役割 |
|---------|-----|---------|------|
| Vocal | 0 | Piano | メロディ |
| Chord | 1 | E.Piano | コード |
| Bass | 2 | E.Bass | ベース |
| Motif | 3 | Synth Lead | 背景リフ |
| Arpeggio | 4 | Saw Lead | アルペジオ |
| Aux | 5 | Warm Pad | サブメロディ |
| Guitar | 6 | Clean Guitar | 和声伴奏 |
| Drums | 9 | — | GM準拠 |
| SE | 15 | — | マーカー |

## ドキュメント

- [はじめに](https://midisketch.libraz.net/ja/docs/getting-started)
- [APIリファレンス (JavaScript)](https://midisketch.libraz.net/ja/docs/api-js)
- [APIリファレンス (C++)](https://midisketch.libraz.net/ja/docs/api-cpp)
- [プリセット一覧](https://midisketch.libraz.net/ja/docs/presets)
- [トラック生成](https://midisketch.libraz.net/ja/docs/track-generators)
- [アーキテクチャ](https://midisketch.libraz.net/ja/docs/architecture)
- [CLIリファレンス](https://midisketch.libraz.net/ja/docs/cli)

## ライセンス

[AGPL-3.0](LICENSE) / [商用](LICENSE-COMMERCIAL) デュアルライセンス。AGPL-3.0 の条件下では自由に利用・改変・再配布できます。クローズドソース製品やプロプライエタリな SaaS への組み込みには商用ライセンスが必要です。商用利用のお問い合わせ: libraz@libraz.net

### スコープについて

本プロジェクトは **MIDIスケッチ生成エンジン** を提供するものです。
完成された楽曲制作システムではありません。

音源・ボーカル合成・オーディオレンダリングは本プロジェクトのスコープ外です。

## 作者

libraz <libraz@libraz.net>
