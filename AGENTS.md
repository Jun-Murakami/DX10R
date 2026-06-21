必ず日本語で回答すること。

## DX10R 開発用 ルール（AGENTS）

この文書は **iPlug2 + WebView（Vite + React 19 + MUI v9 + Zustand）** 構成で、mda（Paul Kellett 氏）が OSS 公開した 2-op FM シンセ **DX10** を「現代風にリファイン」するための、常時参照すべき最小ルールである。

### 目的とスコープ

- **目的**: mda DX10 の 2-operator FM（キャリア + モジュレータ 1 基）の音色キャラクターを忠実に再現しつつ、現代的な使い勝手（フル ADSR・16 voice・パラメータ平滑化・アンチエイリアス・エフェクトチェーン）を備えたインストゥルメント・プラグインを作る。
- **対象フォーマット**: VST3 / AU / AAX / CLAP / Standalone（Windows / macOS）+ VST3 / LV2 / CLAP / Standalone（Linux）。インストゥルメント単独（FX 版は作らない）。
- **原典**: `docs/mda_DX10_src/`（`mdaDX10.cpp` / `mdaDX10.h`）。DSP の正本ロジックはここ。**この `docs/mda_DX10_src/` は `.gitignore` 済み**（再配布しない / 参照専用）。

### 確定済みの設計判断（2026-06-22）

1. **対応プラットフォーム**: Windows / macOS / Linux すべて。
2. **Amp エンベロープ**: 原典は A/D/R（Sustain 無し・必ず減衰）だが、**フル A/D/S/R に拡張**する（現代風リファインの核心）。ADSR UI は Synth80 のものをそのまま使う。
3. **ポリフォニー**: **16 voices**（原典は 8）。
4. **FM エンジン**: 2-op FM の音色は維持。**品質改善のみ**（パラメータ平滑化 = de-zip、アンチエイリアス、エフェクトチェーン移植）。オペレータ追加や feedback などの音色拡張はしない。

### 3 つの参照リポジトリと役割

DX10R は既存 2 リポジトリの資産を組み合わせて構築する。**3 リポジトリとも変更しない（読み取り専用の参照元）**。

- **Synth80**（`../Synth80`, iPlug2）= **スキャフォールドの主テンプレート**。
  - CMake + iPlug2 submodule 構成、`plugin/` + `plugin/dsp/`、`webui/`（React 19 + MUI v9 + Zustand + Biome）、`wasm/`、ビルドスクリプト、`VERSION` / `bump_version.ps1`、プリセット埋め込みの仕組みをここから複製する。
  - **ADSR UI**（`webui/src/components/ADSRGraph.tsx` / `EnvelopePanel.tsx`）、**プリセット管理**（`.80p` フラット JSON / `SerializeState` / `PresetBar.tsx` / `bankStore.ts` / ネイティブファイルダイアログ）、**5 スロット・エフェクトチェーン**（`plugin/dsp/effects/` + `webui/src/components/EffectsRack.tsx`）を**そっくりそのまま移植**する。
- **ZeroComp**（`../ZeroComp`, JUCE）= **UI スタイリングの参照**。
  - 横スライダー + 数値入力の見た目は `webui/src/components/HorizontalParameter.tsx` を**移植**する。基本コンポーネントは横スライダー + 数値入力に統一。
  - 注意: ZeroComp はパラメータを JUCE の `juce-framework-frontend-mirror`（`getSliderState`）から読む。DX10R は iPlug2 なので、**見た目はそのまま・データ層だけ Synth80 の `paramStore` / iplug-bridge に差し替える**。
  - 数値入力欄は `block-host-shortcuts` クラスでキーイベントの DAW 転送を抑制する。微調整修飾キーは Ctrl / Cmd / Shift いずれでも発火させる（市販プラグイン互換）。

### アーキテクチャ原則（Synth80 準拠）

- プラグイン本体は `plugin/` 直下。DSP は `plugin/dsp/` 配下で `namespace dx10::dsp` に統一する。
- C++ 側は iPlug2 `Plugin` + `WebViewEditorDelegate` を継承し、値の正本は **C++ 側 `IParam`（normalized [0,1]）**。WebUI 側の Zustand store はミラー + 楽観的更新に留める。
- iPlug2 は `iPlug2/` git submodule。submodule 自体は改造しない。必要な回避は `patches/*.patch` + `cmake/ApplyIPlug2Patch.cmake` で configure 時に冪等適用する（Synth80 の 2 patch = WebView2 autofill 無効化 / WKWebView context menu も基本そのまま流用予定）。
- **パラメータの真実源 (SSoT)**: 各 IParam の `min / max / default / curve` は `plugin/ParamSpecs.cpp` を唯一の真実源とし、plugin / WASM / テストの 3 target が同じ TU をリンクして読む。非線形カーブは `plugin/dsp/CalibratedShapes.h` の pure 関数として実装し、`IParam::Shape` 派生と WASM 側 wrapper が同じ関数を呼ぶ（plugin / WASM のカーブ drift を構造的に不可能にする）。

### パラメータ計画（DX10 16 params → DX10R）

原典の 16 パラメータ + Sustain + Master Volume を基本とする（エフェクトチェーンのパラメータは別途）。値域・正規化は `ParamSpecs.cpp` で定義。

- **Amp Envelope（キャリア）**: Attack / Decay / **Sustain（新規）** / Release
- **Modulator**: Mod Init（初期レベル）/ Mod Decay / Mod Sustain / Mod Release / Mod Velocity
- **FM Ratio**: Coarse / Fine（原典の `rati` = floor(40.1·x²)、`ratf` の量子化テーブルを踏襲）
- **Tone / Pitch**: Octave / Fine Tune（cents）/ Waveform（"rich" = 5 次正弦近似の倍音量）/ Mod Thru（modmix）
- **Modulation**: Vibrato / LFO Rate
- **Master**: Volume

原典の DSP 係数導出（`update()` 内の `tune` / `ratio` / `catt` / `cdec` / `crel` / `mdec` / `mrel` / `rich` / `modmix` / `dlfo`）はそのまま移植し、Sustain stage と de-zip を足す。

### iPlug2 / WebView ブリッジ（Synth80 準拠）

- C++ → JS は `window.SPVFD / SCVFD / SCMFD / SAMFD / SMMFD` を受信口にする。
- JS → C++ は `iplugAPI.{beginParamChange, setParamValue, endParamChange, sendMidi, sendArbitraryMsg}`。
- ノブ操作開始で `BPCFUI`、ドラッグ中に `SPVFUI`、離したら `EPCFUI`（オートメーション同期）。
- `postMessage` には JS オブジェクトをそのまま渡す（`JSON.stringify` した文字列を渡さない）。
- 任意メッセージは `plugin/ParameterIDs.h` の `EArbitraryMsgTags` と `webui/src/bridge/iplug-bridge.ts` の `MSG_TAG` を必ずペアで更新する。

### DSP / オーディオスレッド原則

- `OnParamChange(int paramIdx)` はオーディオスレッドから呼ばれうる。`new` / `push_back` / lock / file I/O / logging 禁止。係数差し替えのみ。UI 専用処理は `OnParamChangeUI`。
- 高頻度メーター値は `std::atomic` で audio → UI に渡し、`OnIdle`（約 30Hz）で送る。
- 全ボイス Off のときは silence を返し計算をスキップ（原典の "empty block" バイパス相当）。

### React / WebUI 規約（Synth80 準拠）

- TypeScript は `strict` + `any` 禁止。Lint / format は **Biome** に一本化（ESLint / Prettier は使わない）。
- 外部ストア購読は `useSyncExternalStore`。`useEffect` は最小限（新規追加・変更時は理由を 1 行コメント）。C++ コールバック内から最新値を読む場合は Latest Ref Pattern。
- MUI テーマはダーク基調（背景 `#606F77` / paper `#252525` / primary `#4fc3f7` / accent `#ffab00`）。フォントは `@fontsource/jost`、数値表示は `@fontsource/red-hat-mono`。
- 共通 UI は `components/`。横スライダー + 数値入力（`HorizontalParameter`）を基本とする。

### コーディング規約

- C++ は C++17 基準、明示的な型・早期 return・深いネスト回避。例外は原則不使用、戻り値でエラー伝搬。コメントは「なぜ」中心に最小限。新規 DSP は `plugin/dsp/` 配下 `namespace dx10::dsp`。
- Web はコンポーネントを小さく疎結合に。インライン style ではなく `sx` / `styled`。

### ビルド（Synth80 準拠 / 確定後に追記）

- WebUI dev server: `cd webui && npm run dev`（`http://127.0.0.1:5173/`）
- WebUI 本番ビルド: `cd webui && npm run build`（出力を iPlug2 が bundle に埋め込む）
- Debug VST3 build: `cmake --build build --config Debug --target DX10R-vst3`（ターゲット名は確定後に追記）
- WASM（Web デモ用 DSP）: `wasm/src/wasm_exports.cpp` を Emscripten でビルド。emsdk は `D:/Synching/code/JUCE/emsdk`。**DSP を変えたら必ず WASM も再ビルドする**。
- 配布: `build_windows.ps1` / `build_macos.zsh` / `build_linux.sh`。バージョンは `VERSION` を SSoT とし `bump_version.ps1` で同期。

### ライセンス

- 原典 mda DX10 は GPL-2.0 / MIT のデュアル（`docs/mda_DX10_src/` の各ライセンス参照）。DX10R は派生物なのでライセンス整合を必ず確認する（Synth80 の商用ライセンス機構 = LicenseManager / 起動制限はデフォルトでは移植しない）。

### バージョン管理 / 運用

- `VERSION` を正本とし `bump_version.ps1` で各所同期。
- **コミットはユーザが明示的に指示しない限り行わない。**
- 実装ロードマップは `docs/implementation-plan.md` を参照。
