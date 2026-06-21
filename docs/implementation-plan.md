# DX10R 実装プラン

mda DX10（2-op FM, OSS）を iPlug2 + WebView で現代風にリファインする。Synth80 をスキャフォールド主テンプレート、ZeroComp を UI スタイリング参照とする。3 リポジトリ（mda_DX10_src / Synth80 / ZeroComp）は読み取り専用。

## 確定済みの設計判断（2026-06-22）

| 項目 | 決定 |
| --- | --- |
| プラットフォーム | Windows / macOS / Linux |
| フォーマット | VST3 / AU / AAX / CLAP / Standalone（+ Linux は LV2 追加） |
| Amp エンベロープ | フル A/D/S/R（原典 A/D/R に Sustain 追加） |
| ポリフォニー | 16 voices |
| FM エンジン | 2-op 忠実 + 品質改善（de-zip / アンチエイリアス / エフェクトチェーン） |
| 種別 | インストゥルメント単独（FX 版なし） |

## 全体方針

- **DSP の正本**は `docs/mda_DX10_src/mdaDX10.cpp` の `update()` / `process*()` / `noteOn()` / `processEvents()`。係数導出はそのまま移植し、Sustain stage と de-zip（パラメータ平滑化）を足す。
- **スキャフォールド・プリセット・ADSR UI・エフェクトチェーン**は Synth80 から移植（`namespace s80::` → `dx10::` などへリネーム）。
- **横スライダー + 数値入力**は ZeroComp の `HorizontalParameter.tsx` を移植し、データ層を iPlug2 `paramStore` に差し替える。
- パラメータ値域は `plugin/ParamSpecs.cpp`（SSoT）に集約。非線形カーブは `plugin/dsp/CalibratedShapes.h` の pure 関数。

---

## Phase 0 — リポジトリ・スキャフォールド / ツールチェーン

**ゴール**: 空の WebUI を載せた iPlug2 + WebView プラグインが VST3 / Standalone で起動する。

- [x] `CLAUDE.md`（`@AGENTS.md` のみ）/ `AGENTS.md` / `.gitignore`（`docs/mda_DX10_src/` 除外）/ `.gitattributes` / `VERSION`(0.1.0) / 本プラン
- [x] `git init`（branch main）、`git submodule add https://github.com/iPlug2/iPlug2.git iPlug2`（junction 不可・フレッシュ追加）
- [x] SDK: VST3 / CLAP / CLAP_HELPERS を download スクリプトで取得。**AAX_SDK は Synth80 から丸コピー**（`iPlug2/Dependencies/IPlug/AAX_SDK`）。`.env` / dev `pfx` は Phase 6（配布）で対応。
- [x] `cmake/`（`Version.cmake` / `ApplyIPlug2Patch.cmake` / `SkipDebugAUDeploy.cmake`）を移植・DX10R 化
- [x] `patches/`（iPlug2 WebView2 autofill 無効化 / WKWebView context menu）を移植。fresh clone へ clean apply 確認済み
- [x] トップ `CMakeLists.txt` + `plugin/CMakeLists.txt`（`iplug_add_plugin(DX10R ...)`、`EXCLUDE_FORMATS WAM AUV3 VST2`）※ Linux LV2 は Phase 6
- [x] `plugin/config.h`（単一ターゲット instrument、識別子確定: ID `'DX1r'` / MFR `'Jmbc'` / `com.junmurakami.dx10r`）
- [x] `plugin/resources/`（resource.h / main.rc / 各 *-Info.plist を iPlug2 example から DX10R 化 / web/index.html プレースホルダ）
- [x] 最小 `DX10R.h`/`.cpp`（`Plugin` + WebView、無音 ProcessBlock、placeholder param `kParamVolume`）
- [x] **CMake configure 成功**（VS 2022 / x64、`DX10R.sln`: vst3 / clap / app / **aax** ターゲット生成）
- [x] **VST3 ビルド疎通 OK**: `DX10R-vst3` が Debug ビルド成功 → `C:\Program Files\Common Files\VST3\DX10R.vst3` へ自動 deploy。WebView プレースホルダ表示の無音 instrument として起動可能
- [x] AAX SDK 配置修正（nested copy → flat、`Interfaces/AAX.h` 検出）→ 再 configure で `DX10R-aax` ターゲット生成 + `AAX_Export` object 配線確認
- [ ] `plugin/ParameterIDs.h` / `ParamSpecs.{h,cpp}`（SSoT skeleton）は Phase 1 で追加
- [ ] 最小 `webui/`（Vite + React + bridge）は Phase 2 で構築

**Phase 0 完了。** 残: `.env`(PACE WRAP_GUID 取得済) + `dx10r-dev.pfx` 発行 / Vite dev server URL 切替 / index.html の RCDATA 埋め込みは Phase 2・6 で対応。

**リスク**: iPlug2 の **Linux + WebView（WebKitGTK）対応は発展途上**。Synth80 の `build_linux.sh` はプレースホルダだった。Linux は最後に着手し、WebView が不安定なら Standalone/CLAP/VST3 優先で段階対応する（本プランの最大の不確実性）。

## Phase 1 — FM エンジン移植（DSP コア）

**ゴール**: MIDI で 16 voice の 2-op FM が鳴る。原典の音色を再現し、Sustain と de-zip を追加。

- [ ] `plugin/dsp/` モジュール: `Voice`（2-op: carrier + modulator, 5 次正弦近似 + 倍音 "rich"）/ `Envelope`（carrier A/D/S/R + modulator env）/ `Lfo`（vibrato 用サイン）/ `VoiceManager`（16 voice 割当、最も静かな voice を奪う原典ロジック）
- [ ] 原典 `update()` の係数導出（`tune` / `rati` / `ratf` / `ratio` / `catt` / `cdec` / `crel` / `mdec` / `mrel` / `rich` / `modmix` / `dlfo`）を移植。Sustain stage を carrier env に追加。
- [ ] de-zip（パラメータ平滑化）と波形整形のアンチエイリアス（5 次正弦近似のナイキスト付近対策）
- [ ] MIDI: note on/off、velocity、pitch bend、mod wheel、sustain CC、volume CC、all-notes-off（原典 `processEvents()` 準拠）
- [ ] `ParamSpecs.cpp` に全 param 定義、`CalibratedShapes.h` に Octave / Ratio / Fine Tune などのカーブ
- [ ] Catch2 ユニットテスト（envelope / カーブ / plugin↔WASM 等価）

## Phase 2 — WebUI 基盤 + パラメータブリッジ + ADSR UI

**ゴール**: 全パラメータが横スライダー + 数値入力で操作でき、ADSR グラフが動く。

- [ ] `webui/` を Synth80 から移植（Vite + React 19 + MUI v9 + Zustand + Biome、`bridge/iplug-bridge.ts`、`store/paramStore.ts`、`theme/`、dev-mock）
- [ ] ZeroComp `HorizontalParameter.tsx` を移植し、`useJuceSliderValue` → Synth80 の `paramStore`（`useParamValue` / `useParamGesture`）に差し替え。`block-host-shortcuts` / 微調整修飾キー（Ctrl/Cmd/Shift）/ ホイール / 数値入力ドラッグを維持。
- [ ] Synth80 ADSR UI（`ADSRGraph.tsx` / `EnvelopePanel.tsx`）を移植し carrier A/D/S/R + modulator env に接続
- [ ] メイン UI レイアウト（セクション分け: Amp Env / Modulator / Ratio / Tone / Mod / Master）
- [ ] paramStore ↔ host 同期・bridge JSON 整形の Vitest

## Phase 3 — プリセット管理移植

**ゴール**: `.dx10p`（フラット JSON）でプリセット保存/読込、32 種の mda ファクトリプリセットが付属。

- [ ] Synth80 のプリセット機構を移植: `SerializeState`/`UnserializeState`、`PresetBar.tsx`、`bankStore.ts`、ネイティブファイルダイアログ、ビルド時埋め込み（`InitPatchEmbedded.h` / `FactoryPresetsEmbedded.h`）
- [ ] 原典 32 プリセット（`mdaDX10.cpp` の `fillpatch(...)` テーブル）を `.dx10p` に変換。Sustain 追加に伴うマッピング（原典 16 値 → 新 param）を変換スクリプトで吸収。
- [ ] ファクトリバンクを `docs/presets/` に配置しビルド時埋め込み

## Phase 4 — エフェクトチェーン移植

**ゴール**: Synth80 と同等の 5 スロット直列エフェクトラックが動く。

- [ ] `plugin/dsp/effects/`（`EffectBase` / `EffectsRack` + 11 種）を移植、`ParamSpecs` に 5 スロット × 9 param 追加
- [ ] `webui/src/components/EffectsRack.tsx`（`@dnd-kit` 並べ替え）移植
- [ ] Effect Chain Lock、per-slot Bypass、Type 切替時の default 一括書換え

## Phase 5 — WASM Web デモ

**ゴール**: ブラウザで同一 DSP が鳴る Web デモ。

- [ ] `wasm/src/wasm_exports.cpp` + `wasm/CMakeLists.txt` を移植（plugin/dsp + ParamSpecs を共有コンパイル）
- [ ] `webui/src/web/`（dev-mock / web エントリ）と `public-web/wasm/` 配信
- [ ] plugin↔WASM カーブ等価テスト

## Phase 6 — 配布・仕上げ

**ゴール**: 3 OS でインストーラ/パッケージが出る。

- [ ] `build_windows.ps1` / `build_macos.zsh` / `build_linux.sh`、`installer.iss`、`bump_version.ps1` を移植・DX10R 化
- [ ] コード署名（Authenticode / AAX PACE / macOS notarization）、`.env` 運用
- [ ] Linux ターゲット仕上げ（最大の不確実性 — Phase 0 のリスク参照）
- [ ] `README.md` / `LICENSE`（GPL/MIT 整合）、bundle ID 衝突対策（メモリ: macOS pkg の CFBundleIdentifier 一意化）

---

## 未決定 / 後続で確認したい事項

- **プラグイン識別子**: unique ID（4 文字、原典は `'MDAx'`）、manufacturer code、bundle ID prefix（`com.junmurakami.dx10r` を仮置き）。
- **ライセンス方針**: 原典 GPL/MIT のため、Synth80 の商用ライセンス機構は移植しない前提。公開/商用方針で再確認。
- **エフェクトチェーンの範囲**: 「そっくりそのまま」= Synth80 の全 11 種を移植する前提。サブセットで良ければ短縮可能。
