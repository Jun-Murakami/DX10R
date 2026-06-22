// Effect type ごとの knob metadata (UI 表示・実値変換)。
//
// IParam 上は各スロットの 8 generic param が 0..1 linear で固定。effect type
// ごとにこの 8 枠のどこに何があるかを定義し、UI では実値ラベル/単位/カーブで
// 表示する。C++ 側 DigitalChorus.cpp 等の setParam 解釈と完全に同期させること。
//
// 並び (paramOffset 0..7) の実装ルール:
//   - 最も触る param を offset 0 から並べる (例: Digital Chorus は Rate / Depth /
//     Mix / Feedback)。
//   - 不要な offset は metadata から外す (= UI に knob を出さない)。

export type EffectKnobCurve = 'linear' | 'log' | 'cube' | 'square'

export interface EffectKnobMeta {
  /** 0..7。スロット内の generic param の offset。 */
  paramOffset: number
  label: string
  /** 単位文字列 (Hz, %, ms など)。enumOptions 指定時は無視。 */
  unit?: string
  /** 表示用の実値レンジ (min..max)。0..1 normalized からこの範囲にマップ。 */
  min?: number
  max?: number
  /** 値変換カーブ。'log' は frequency 系、'cube' は時間系で使う。 */
  curve?: EffectKnobCurve
  /** enum 系: 段ごとのラベル。指定されると discrete 表示になる。 */
  enumOptions?: readonly string[]
  /** 数値表示の小数桁数 (default 2)。 */
  precision?: number
  /**
   * type 切替時に適用される 0..1 normalized デフォルト。effect 種別ごとに
   * 「これくらいから始めると良い感じ」の値を持っておく。未指定なら 0。
   */
  defaultValue?: number
  /**
   * 条件付き表示。指定された場合、同じ slot の `paramOffset` の値が enum 値
   * `enumIdx` と一致する時だけ knob を表示する。ex: Delay の Sync/Free Time
   * 切替で、Mode (paramOffset 0) の値に応じて 2 つの Time knob を出し分ける。
   * 参照先 paramOffset の knob は enumOptions を持っている前提。
   */
  visibleWhen?: { paramOffset: number; enumIdx: number }
  /**
   * 中心値を持つ piecewise-linear マッピング。
   *   v01=0   → min
   *   v01=0.5 → bipolarCenter
   *   v01=1   → max
   * EQ Gain (cut 大、boost 小、center=0dB) のような非対称用。指定時 UI 側は
   * Knob を bipolar 表示 (center detent) にする。curve 指定があっても無視される。
   */
  bipolarCenter?: number
  /**
   * 多言語ヒント Popover の lookup key (= synth80-param-hints.ts の UI_HINTS の key)。
   * effect ごとに同じラベル名でも意味が違う (Chorus の Rate と Phaser の Rate は
   * 別) ため、key は `fx.{effectName}.{paramName}` 形式で effect 別に持たせる。
   * 未指定なら hint なし (= 素のラベル表示にフォールバック)。
   */
  hintKey?: string
}

export interface EffectTypeMeta {
  /** C++ EffectType enum と同じ id (0=Off, 1=SynthChorus, ...)。 */
  id: number
  /** UI のタブ表示および dropdown 表示の名前。 */
  name: string
  /** edit page に並べる knob 群。順序が左→右の表示順。 */
  knobs: readonly EffectKnobMeta[]
}

/** id=0: バイパス (no knobs)。 */
export const EFFECT_OFF: EffectTypeMeta = {
  id: 0,
  name: 'Off',
  knobs: [],
}

/** id=1: JUNO-60 / JUNO-106 風 BBD chorus。 */
export const EFFECT_SYNTH_CHORUS: EffectTypeMeta = {
  id: 1,
  name: 'Synth Chorus',
  knobs: [
    // 0: Model。60 は暗く太め、106 は少し明るくステレオ反転感が強め。
    {
      paramOffset: 0,
      label: 'Model',
      enumOptions: ['60', '106'],
      defaultValue: 0,
      hintKey: 'fx.synthChorus.model',
    },
    // 1: Mode (3 段)。Rate/Depth はユーザー可変にせず、JUNO の fixed chorus
    // buttons として扱う。
    {
      paramOffset: 1,
      label: 'Mode',
      enumOptions: ['I', 'II', 'I+II'],
      defaultValue: 0,
      hintKey: 'fx.synthChorus.mode',
    },
    {
      paramOffset: 2,
      label: 'Width',
      unit: '%',
      min: 0,
      max: 200,
      precision: 0,
      bipolarCenter: 100,
      defaultValue: 0.5,
      hintKey: 'fx.synthChorus.width',
    },
    {
      paramOffset: 3,
      label: 'Amount',
      unit: '%',
      min: 0,
      max: 100,
      precision: 0,
      defaultValue: 1,
      hintKey: 'fx.synthChorus.amount',
    },
  ],
}

/** id=2: Roland Dimension D / SDD-320 風 2-BBD matrix chorus。 */
export const EFFECT_STUDIO_CHORUS: EffectTypeMeta = {
  id: 2,
  name: 'Studio Chorus',
  knobs: [
    // Mode 4 段。Mode 4 は独立 preset ではなく Mode 3 + wet boost。
    // default Mode 2 (= idx 1) が一番「Dimension D らしさ」が出る
    // 落としどころ。0..1 の 4 段中 idx=1 → 1/3 ≒ 0.333。
    {
      paramOffset: 0,
      label: 'Mode',
      enumOptions: ['1', '2', '3', '4'],
      defaultValue: 0.3333,
      hintKey: 'fx.studioChorus.mode',
    },
    {
      paramOffset: 1,
      label: 'Width',
      unit: '%',
      min: 0,
      max: 200,
      precision: 0,
      bipolarCenter: 100,
      defaultValue: 0.5,
      hintKey: 'fx.studioChorus.width',
    },
  ],
}

/** id=5: Cascaded all-pass phaser (Phase 90 / Small Stone 系)。 */
export const EFFECT_PHASER: EffectTypeMeta = {
  id: 5,
  name: 'Phaser',
  knobs: [
    // Rate=0.13Hz: log(0.13/0.05)/log(10/0.05) = log(2.6)/log(200) = 0.9555/5.298 ≒ 0.1803
    {
      paramOffset: 0,
      label: 'Rate',
      unit: 'Hz',
      min: 0.05,
      max: 10,
      curve: 'log',
      precision: 2,
      defaultValue: 0.1803,
      hintKey: 'fx.phaser.rate',
    },
    {
      paramOffset: 1,
      label: 'Depth',
      unit: '%',
      min: 0,
      max: 100,
      precision: 0,
      defaultValue: 0.85,
      hintKey: 'fx.phaser.depth',
    },
    // Feedback default 70% (max 97% スケールに対する 70/97 ≒ 0.7216)。
    // 強めの resonance で notch のピーク・ディップが目立つ。
    // max 97% は C++ kMaxFeedback と同期 (帰還路の softclip + 帯域整形で安全)。
    {
      paramOffset: 2,
      label: 'Feedback',
      unit: '%',
      min: 0,
      max: 97,
      precision: 0,
      defaultValue: 0.7216,
      hintKey: 'fx.phaser.feedback',
    },
    // Stages 8 段 default。深めの phasing キャラ (Mu-tron Bi-Phase 系の濃さ)。
    // idx 2/3 = 0.6667。
    {
      paramOffset: 3,
      label: 'Stages',
      enumOptions: ['4', '6', '8', '12'],
      defaultValue: 0.6667,
      hintKey: 'fx.phaser.stages',
    },
    // Center=1200Hz: log(1200/200)/log(4000/200) = log(6)/log(20) = 1.7918/2.9957 ≒ 0.5981
    // 800Hz より上に置いて notch が高域寄りで目立つ。
    {
      paramOffset: 4,
      label: 'Center',
      unit: 'Hz',
      min: 200,
      max: 4000,
      curve: 'log',
      precision: 0,
      defaultValue: 0.5981,
      hintKey: 'fx.phaser.center',
    },
    {
      paramOffset: 5,
      label: 'Stereo',
      enumOptions: ['Mono', 'Stereo'],
      defaultValue: 1,
      hintKey: 'fx.phaser.stereo',
    },
    // Mix 100% default。Phaser は dry+phased の位相干渉が effect 本体なので、
    // Mix=50% だと phased 成分が 25% にしか効かず subtle になる。実機ペダルも
    // dry/wet 概念がないので 100% が standard。
    {
      paramOffset: 6,
      label: 'Mix',
      unit: '%',
      min: 0,
      max: 100,
      precision: 0,
      defaultValue: 1.0,
      hintKey: 'fx.phaser.mix',
    },
    // Sharp = biquad AP の Q (0.5..4.0 log、C++ kQMin/kQMax と同期)。
    // Q=0.5 は 1 次 AP × 2 相当の classic な緩さ。default 0.4 → Q≒1.15 で
    // ひと回り立ったノッチ。旧プリセット (param7=0) は Q=0.5 になり後方互換。
    {
      paramOffset: 7,
      label: 'Sharp',
      min: 0.5,
      max: 4,
      curve: 'log',
      precision: 2,
      defaultValue: 0.4,
      hintKey: 'fx.phaser.sharp',
    },
  ],
}

/** id=6: Modulated short-delay flanger (EHX Electric Mistress / MXR 系)。 */
export const EFFECT_FLANGER: EffectTypeMeta = {
  id: 6,
  name: 'Flanger',
  knobs: [
    // Rate=0.3Hz: log(0.3/0.05)/log(8/0.05) = log(6)/log(160) = 1.792/5.075 ≒ 0.3531
    {
      paramOffset: 0,
      label: 'Rate',
      unit: 'Hz',
      min: 0.05,
      max: 8,
      curve: 'log',
      precision: 2,
      defaultValue: 0.3531,
      hintKey: 'fx.flanger.rate',
    },
    {
      paramOffset: 1,
      label: 'Depth',
      unit: '%',
      min: 0,
      max: 100,
      precision: 0,
      defaultValue: 0.6,
      hintKey: 'fx.flanger.depth',
    },
    // Feedback default 80% (max 97% スケールに対する 80/97 ≒ 0.8247)。
    // max 97% は C++ kMaxFeedback と同期 (帰還路の softclip + 帯域整形で安全)。
    {
      paramOffset: 2,
      label: 'Feedback',
      unit: '%',
      min: 0,
      max: 97,
      precision: 0,
      defaultValue: 0.8247,
      hintKey: 'fx.flanger.feedback',
    },
    // Manual=2ms: log(2/0.5)/log(12/0.5) = log(4)/log(24) = 1.386/3.178 ≒ 0.4362
    {
      paramOffset: 3,
      label: 'Manual',
      unit: 'ms',
      min: 0.5,
      max: 12,
      curve: 'log',
      precision: 2,
      defaultValue: 0.4362,
      hintKey: 'fx.flanger.manual',
    },
    {
      paramOffset: 4,
      label: 'Stereo',
      enumOptions: ['Mono', 'Stereo'],
      defaultValue: 1,
      hintKey: 'fx.flanger.stereo',
    },
    // Mix 100% default。Flanger も dry+delayed の comb 干渉が effect 本体で、
    // Mix=50% だと delayed 成分が 25% になり notch 深度が -6dB しか出ない。
    {
      paramOffset: 5,
      label: 'Mix',
      unit: '%',
      min: 0,
      max: 100,
      precision: 0,
      defaultValue: 1.0,
      hintKey: 'fx.flanger.mix',
    },
    // Color = wet 加算 + feedback の極性。Inv は hollow な逆相フランジ。
    {
      paramOffset: 6,
      label: 'Color',
      enumOptions: ['Normal', 'Inv'],
      defaultValue: 0,
      hintKey: 'fx.flanger.color',
    },
    // TZF = through-zero flanging。Mix 100% 推奨 (wet に Manual 分の遅延)。
    {
      paramOffset: 7,
      label: 'TZF',
      enumOptions: ['Off', 'On'],
      defaultValue: 0,
      hintKey: 'fx.flanger.tzf',
    },
  ],
}

/** id=7: 3-band DJ-style EQ (low shelf / mid peak / high shelf)。 */
export const EFFECT_EQ3BAND: EffectTypeMeta = {
  id: 7,
  name: 'EQ',
  knobs: [
    // Label は 2 行表記で knob 列幅を他 effect と揃える (EffectKnob の Typography は
    // whiteSpace 'pre' で \n を改行として尊重する)。
    // Low shelf。Gain は asymmetric (-36 dB cut .. +12 dB boost、center=0)。
    {
      paramOffset: 0,
      label: 'Low\nGain',
      unit: 'dB',
      min: -36,
      max: 12,
      precision: 1,
      bipolarCenter: 0,
      defaultValue: 0.5,
      hintKey: 'fx.eq3band.lowGain',
    },
    // Low Freq=200Hz: log(200/50)/log(500/50) = log(4)/log(10) ≒ 0.6020
    {
      paramOffset: 1,
      label: 'Low\nFreq',
      unit: 'Hz',
      min: 50,
      max: 500,
      curve: 'log',
      precision: 0,
      defaultValue: 0.602,
      hintKey: 'fx.eq3band.lowFreq',
    },
    {
      paramOffset: 2,
      label: 'Mid\nGain',
      unit: 'dB',
      min: -36,
      max: 12,
      precision: 1,
      bipolarCenter: 0,
      defaultValue: 0.5,
      hintKey: 'fx.eq3band.midGain',
    },
    // Mid Freq=1000Hz: log(1000/200)/log(5000/200) = log(5)/log(25) = 0.5
    {
      paramOffset: 3,
      label: 'Mid\nFreq',
      unit: 'Hz',
      min: 200,
      max: 5000,
      curve: 'log',
      precision: 0,
      defaultValue: 0.5,
      hintKey: 'fx.eq3band.midFreq',
    },
    {
      paramOffset: 4,
      label: 'High\nGain',
      unit: 'dB',
      min: -36,
      max: 12,
      precision: 1,
      bipolarCenter: 0,
      defaultValue: 0.5,
      hintKey: 'fx.eq3band.highGain',
    },
    // High Freq=5000Hz: log(5000/2000)/log(12000/2000) ≒ 0.5114
    {
      paramOffset: 5,
      label: 'High\nFreq',
      unit: 'Hz',
      min: 2000,
      max: 12000,
      curve: 'log',
      precision: 0,
      defaultValue: 0.5114,
      hintKey: 'fx.eq3band.highFreq',
    },
  ],
}

/** id=10: Dattorro plate-style algorithmic reverb (Hall / Plate / Room mode)。 */
export const EFFECT_REVERB: EffectTypeMeta = {
  id: 10,
  name: 'Reverb',
  knobs: [
    // 表示順は Mode → Pre-Delay → Decay → Damping → Diffusion → Tone → Width → Mix。
    {
      paramOffset: 0,
      label: 'Mode',
      enumOptions: ['Hall', 'Plate', 'Room'],
      defaultValue: 0.5,
      hintKey: 'fx.reverb.mode',
    }, // Plate (idx 1) = 0.5
    // Pre-Delay 35 ms linear: 35/200 = 0.175
    {
      paramOffset: 1,
      label: 'Pre-\nDelay',
      unit: 'ms',
      min: 0,
      max: 200,
      precision: 0,
      defaultValue: 0.175,
      hintKey: 'fx.reverb.preDelay',
    },
    // Decay は目標 RT60 (sec、log map 0.3..10)。Mode を切替えても表示秒数は維持され、
    // C++ 側で internal feedback gain が Mode 倍率に応じて再計算される。
    // default 2.7 sec: log(2.7/0.3) / log(10/0.3) = ln(9)/ln(33.333) ≒ 0.6266
    {
      paramOffset: 2,
      label: 'Decay',
      unit: 's',
      min: 0.3,
      max: 10,
      curve: 'log',
      precision: 1,
      defaultValue: 0.6266,
      hintKey: 'fx.reverb.decay',
    },
    // Damping 0.35
    {
      paramOffset: 3,
      label: 'Damping',
      unit: '%',
      min: 0,
      max: 100,
      precision: 0,
      defaultValue: 0.35,
      hintKey: 'fx.reverb.damping',
    },
    // Diffusion 0.5
    {
      paramOffset: 4,
      label: 'Diffusion',
      unit: '%',
      min: 0,
      max: 100,
      precision: 0,
      defaultValue: 0.5,
      hintKey: 'fx.reverb.diffusion',
    },
    // Tone 0.5
    {
      paramOffset: 5,
      label: 'Tone',
      unit: '%',
      min: 0,
      max: 100,
      precision: 0,
      defaultValue: 0.5,
      hintKey: 'fx.reverb.tone',
    },
    // Width 100% (normal stereo、center detent)
    {
      paramOffset: 6,
      label: 'Width',
      unit: '%',
      min: 0,
      max: 200,
      precision: 0,
      bipolarCenter: 100,
      defaultValue: 0.5,
      hintKey: 'fx.reverb.width',
    },
    // Mix 15% (リバーブは控えめに、シンセに対して自然になじむ量)
    {
      paramOffset: 7,
      label: 'Mix',
      unit: '%',
      min: 0,
      max: 100,
      precision: 0,
      defaultValue: 0.15,
      hintKey: 'fx.reverb.mix',
    },
  ],
}

/** id=9: Multi-algorithm distortion / saturation (5 アルゴリズム)。 */
export const EFFECT_DISTORTION: EffectTypeMeta = {
  id: 9,
  name: 'Distortion',
  knobs: [
    {
      paramOffset: 0,
      label: 'Algo',
      enumOptions: ['Clip', 'Overdrive', 'Waveshaper', 'Tube', 'Tape'],
      defaultValue: 0.25,
      hintKey: 'fx.distortion.algo',
    }, // Overdrive (idx 1) = 1/4 = 0.25
    // Drive=6 dB linear: 6 / 36 ≒ 0.1667
    {
      paramOffset: 1,
      label: 'Drive',
      unit: 'dB',
      min: 0,
      max: 36,
      precision: 1,
      defaultValue: 0.1667,
      hintKey: 'fx.distortion.drive',
    },
    // Output=0 dB bipolar、center detent at 0.5。
    {
      paramOffset: 2,
      label: 'Output',
      unit: 'dB',
      min: -24,
      max: 12,
      precision: 1,
      bipolarCenter: 0,
      defaultValue: 0.5,
      hintKey: 'fx.distortion.output',
    },
    // Mix=100% (距離化系は基本 full wet で味付け、薄めにしたい時だけ下げる)
    {
      paramOffset: 3,
      label: 'Mix',
      unit: '%',
      min: 0,
      max: 100,
      precision: 0,
      defaultValue: 1.0,
      hintKey: 'fx.distortion.mix',
    },
  ],
}

/** id=8: Soft-knee feedforward compressor with 4 character modes (VCA/Opto/FET/VariMu)。 */
export const EFFECT_COMPRESSOR: EffectTypeMeta = {
  id: 8,
  name: 'Compressor',
  knobs: [
    // 表示順は Mode → Thresh → Ratio → Knee → Attack → Release → Mix。
    // paramOffset は IParam 互換のため変更しないので、knobs[] 並びで UI 表示順を制御。
    {
      paramOffset: 5,
      label: 'Mode',
      enumOptions: ['Clean', 'Opto', 'FET', 'Vari-Mu'],
      defaultValue: 0,
      hintKey: 'fx.compressor.mode',
    },
    // Threshold=-12 dB linear: (-12 - (-60)) / (0 - (-60)) = 48 / 60 = 0.8
    {
      paramOffset: 0,
      label: 'Thresh',
      unit: 'dB',
      min: -60,
      max: 0,
      precision: 1,
      defaultValue: 0.8,
      hintKey: 'fx.compressor.thresh',
    },
    // Ratio=4:1 square curve: sqrt((4-1)/(20-1)) = sqrt(3/19) ≒ 0.397
    {
      paramOffset: 1,
      label: 'Ratio',
      unit: ':1',
      min: 1,
      max: 20,
      curve: 'square',
      precision: 1,
      defaultValue: 0.397,
      hintKey: 'fx.compressor.ratio',
    },
    // Knee=6 dB linear (0..24): 6/24 = 0.25。Vari-Mu モードでは内部で +12 dB 加算。
    {
      paramOffset: 6,
      label: 'Knee',
      unit: 'dB',
      min: 0,
      max: 24,
      precision: 0,
      defaultValue: 0.25,
      hintKey: 'fx.compressor.knee',
    },
    // Attack=10 ms log: log(10/0.1)/log(200/0.1) = log(100)/log(2000) ≒ 0.6062
    {
      paramOffset: 2,
      label: 'Attack',
      unit: 'ms',
      min: 0.1,
      max: 200,
      curve: 'log',
      precision: 1,
      defaultValue: 0.6062,
      hintKey: 'fx.compressor.attack',
    },
    // Release=100 ms log: log(100/5)/log(1000/5) = log(20)/log(200) ≒ 0.5654
    {
      paramOffset: 3,
      label: 'Release',
      unit: 'ms',
      min: 5,
      max: 1000,
      curve: 'log',
      precision: 0,
      defaultValue: 0.5654,
      hintKey: 'fx.compressor.release',
    },
    // Mix 100% (auto-makeup 込みなのでデフォルトは full wet)。
    {
      paramOffset: 4,
      label: 'Mix',
      unit: '%',
      min: 0,
      max: 100,
      precision: 0,
      defaultValue: 1.0,
      hintKey: 'fx.compressor.mix',
    },
  ],
}

/** id=4: Tempo sync / free 切替 + Mono / Ping-Pong stereo digital delay。 */
export const EFFECT_DELAY: EffectTypeMeta = {
  id: 4,
  name: 'Delay',
  knobs: [
    // 0: Mode (Sync = host BPM 追従、Free = ms 直指定)
    {
      paramOffset: 0,
      label: 'Mode',
      enumOptions: ['Sync', 'Free'],
      defaultValue: 0,
      hintKey: 'fx.delay.mode',
    },
    // 1: Time Sync (12 段、短い順、Mode=Sync の時だけ表示)
    //    1/64 〜 1/2 までで delay 用途を一通りカバー。
    {
      paramOffset: 1,
      label: 'Time',
      enumOptions: [
        '1/64',
        '1/32',
        '1/16T',
        '1/16',
        '1/8T',
        '1/16.',
        '1/8',
        '1/4T',
        '1/8.',
        '1/4',
        '1/4.',
        '1/2',
      ],
      visibleWhen: { paramOffset: 0, enumIdx: 0 },
      // default 1/8 = idx 6 → 6/11 = 0.5455
      defaultValue: 0.5455,
      hintKey: 'fx.delay.timeSync',
    },
    // 2: Time Free (1..2000 ms log、Mode=Free の時だけ表示)
    //    default 350 ms: log(350/1)/log(2000/1) = log(350)/log(2000) = 5.858/7.601 = 0.7707
    {
      paramOffset: 2,
      label: 'Time',
      unit: 'ms',
      min: 1,
      max: 2000,
      curve: 'log',
      precision: 0,
      visibleWhen: { paramOffset: 0, enumIdx: 1 },
      defaultValue: 0.7707,
      hintKey: 'fx.delay.timeFree',
    },
    // 3: Stereo Mode
    {
      paramOffset: 3,
      label: 'Stereo',
      enumOptions: ['Mono', 'Ping-Pong'],
      defaultValue: 0,
      hintKey: 'fx.delay.stereo',
    },
    // 4: Feedback (0..95%)
    {
      paramOffset: 4,
      label: 'Feedback',
      unit: '%',
      min: 0,
      max: 95,
      precision: 0,
      defaultValue: 0.4,
      hintKey: 'fx.delay.feedback',
    },
    // 5: Tone (LP cutoff 500..18000 Hz log, default 8kHz - log(8/0.5)/log(18/0.5) = log(16)/log(36) = 2.773/3.584 ≒ 0.7738)
    {
      paramOffset: 5,
      label: 'Tone',
      unit: 'Hz',
      min: 500,
      max: 18000,
      curve: 'log',
      precision: 0,
      defaultValue: 0.7738,
      hintKey: 'fx.delay.tone',
    },
    // 6: Width。Mono mode では L=R で side=0 のため効かない、Ping-Pong で効く。
    {
      paramOffset: 6,
      label: 'Width',
      unit: '%',
      min: 0,
      max: 200,
      precision: 0,
      bipolarCenter: 100,
      defaultValue: 0.5,
      hintKey: 'fx.delay.width',
    },
    // 7: Mix
    {
      paramOffset: 7,
      label: 'Mix',
      unit: '%',
      min: 0,
      max: 100,
      precision: 0,
      defaultValue: 0.3,
      hintKey: 'fx.delay.mix',
    },
  ],
}

// ===== Pitch Chorus (80 年代ラックハーモナイザー系の pitch-shift detune chorus) =====
// Time-domain 2-tap pitch shifter + LFO mod + 固定 delay + feedback。LFO で
// delay 周期を揺らす Digital Chorus / Synth Chorus / Studio Chorus とは原理が違い、
// ピッチを微小にずらしてうなりで厚みを作る方式。
// default は MicroPitch 代表 preset 231 (L=+9 cent / 0 ms、R=-9 cent / 25 ms)。
const PITCH_CHORUS_KNOBS: readonly EffectKnobMeta[] = [
  {
    paramOffset: 0,
    label: 'Pitch A',
    unit: 'cent',
    min: -50,
    max: 50,
    precision: 1,
    bipolarCenter: 0,
    defaultValue: 0.59, // +9 cent
    hintKey: 'fx.pitchChorus.pitchA',
  },
  {
    paramOffset: 1,
    label: 'Pitch B',
    unit: 'cent',
    min: -50,
    max: 50,
    precision: 1,
    bipolarCenter: 0,
    defaultValue: 0.41, // -9 cent
    hintKey: 'fx.pitchChorus.pitchB',
  },
  {
    paramOffset: 2,
    label: 'Delay A',
    unit: 'ms',
    min: 0,
    max: 500,
    precision: 1,
    defaultValue: 0, // 0 ms
    hintKey: 'fx.pitchChorus.delayA',
  },
  {
    paramOffset: 3,
    label: 'Delay B',
    unit: 'ms',
    min: 0,
    max: 500,
    precision: 1,
    defaultValue: 0.05, // 25 ms
    hintKey: 'fx.pitchChorus.delayB',
  },
  // Mod Depth: 最大 ±2 ms の read tap 揺らし。0 で完全静的 detune。
  {
    paramOffset: 4,
    label: 'Mod Depth',
    unit: '%',
    min: 0,
    max: 100,
    precision: 0,
    defaultValue: 0.1,
    hintKey: 'fx.pitchChorus.modDepth',
  },
  // Mod Rate: log map 0.05..6 Hz。default 0.5 Hz → log(10)/log(120) ≒ 0.481。
  {
    paramOffset: 5,
    label: 'Mod Rate',
    unit: 'Hz',
    min: 0.05,
    max: 6,
    curve: 'log',
    precision: 2,
    defaultValue: 0.481,
    hintKey: 'fx.pitchChorus.modRate',
  },
  // Feedback: UI 0..100%、内部で × 0.7 にスケールされ最大 fb=0.7。pitch dive / rise 用。
  {
    paramOffset: 6,
    label: 'Feedback',
    unit: '%',
    min: 0,
    max: 100,
    precision: 0,
    defaultValue: 0,
    hintKey: 'fx.pitchChorus.feedback',
  },
  {
    paramOffset: 7,
    label: 'Mix',
    unit: '%',
    min: 0,
    max: 100,
    precision: 0,
    defaultValue: 0.5,
    hintKey: 'fx.pitchChorus.mix',
  },
]

/** id=11: Pitch-shift detune chorus (時間軸 2-tap shifter)。 */
export const EFFECT_PITCH_CHORUS: EffectTypeMeta = {
  id: 11,
  name: 'Pitch Chorus',
  knobs: PITCH_CHORUS_KNOBS,
}

/** id=3: 単純な digital chorus (Phase A 実装済み)。 */
export const EFFECT_DIGITAL_CHORUS: EffectTypeMeta = {
  id: 3,
  name: 'Digital Chorus',
  knobs: [
    // Rate=1.52 Hz: log(30.4)/log(120) = 0.7132 (min=0.05, max=6, log curve 逆算)
    {
      paramOffset: 0,
      label: 'Rate',
      unit: 'Hz',
      min: 0.05,
      max: 6,
      curve: 'log',
      precision: 2,
      defaultValue: 0.7132,
      hintKey: 'fx.digitalChorus.rate',
    },
    {
      paramOffset: 1,
      label: 'Depth',
      unit: '%',
      min: 0,
      max: 100,
      precision: 0,
      defaultValue: 0.05,
      hintKey: 'fx.digitalChorus.depth',
    },
    // Voices 1..4 enum。default 2 (= idx 1 → 1/3 ≒ 0.3333)。1 voice は単一 LFO で
    // 既存挙動に等しく、2..4 で voice ごとの rate spread (= Detune) が活きる。
    {
      paramOffset: 5,
      label: 'Voices',
      enumOptions: ['1', '2', '3', '4'],
      defaultValue: 0.3333,
      hintKey: 'fx.digitalChorus.voices',
    },
    // Detune 0..1 → ±0..5% rate spread。0 で全 voice 同 rate (= 単純並列)、
    // 0.3 程度で「Eventide 系の厚み」、1.0 でフランジャ寄りの広がり。
    {
      paramOffset: 6,
      label: 'Detune',
      unit: '%',
      min: 0,
      max: 100,
      precision: 0,
      defaultValue: 0.3,
      hintKey: 'fx.digitalChorus.detune',
    },
    // Width: 0% = mono, 100% = original (center detent), 200% = wider。
    {
      paramOffset: 2,
      label: 'Width',
      unit: '%',
      min: 0,
      max: 200,
      precision: 0,
      bipolarCenter: 100,
      defaultValue: 0.5,
      hintKey: 'fx.digitalChorus.width',
    },
    {
      paramOffset: 3,
      label: 'Feedback',
      unit: '%',
      min: 0,
      max: 100,
      precision: 0,
      defaultValue: 0.5,
      hintKey: 'fx.digitalChorus.feedback',
    },
    {
      paramOffset: 4,
      label: 'Mix',
      unit: '%',
      min: 0,
      max: 100,
      precision: 0,
      defaultValue: 0.5,
      hintKey: 'fx.digitalChorus.mix',
    },
  ],
}

/**
 * UI ドロップダウンの並び順。配列順 = タブで切替えたときの並び。id とは独立で、
 * 「BBD 系 chorus → 時間軸 pitch-shift detune chorus → 汎用 digital chorus →
 * 時間 / 空間系」の流れに並べるため、Pitch Chorus (id=11) は Studio Chorus
 * (id=2) の直後に挿入する。id は不変なので IParam / DAW automation との互換は保たれる。
 */
export const EFFECT_TYPE_DEFS: readonly EffectTypeMeta[] = [
  EFFECT_OFF,
  EFFECT_SYNTH_CHORUS,
  EFFECT_STUDIO_CHORUS,
  EFFECT_PITCH_CHORUS,
  EFFECT_DIGITAL_CHORUS,
  EFFECT_DELAY,
  EFFECT_PHASER,
  EFFECT_FLANGER,
  EFFECT_EQ3BAND,
  EFFECT_COMPRESSOR,
  EFFECT_DISTORTION,
  EFFECT_REVERB,
]

// id → EffectTypeMeta の lookup table。配列順を変えても id 解決は壊れない。
const EFFECT_TYPE_BY_ID: ReadonlyMap<number, EffectTypeMeta> = new Map(
  EFFECT_TYPE_DEFS.map((m) => [m.id, m]),
)

/** id を渡して EffectTypeMeta を返す。未定義 / 予約値は Off 扱い。 */
export function getEffectTypeMeta(id: number): EffectTypeMeta {
  return EFFECT_TYPE_BY_ID.get(id) ?? EFFECT_OFF
}

// ===== 0..1 normalized ⇄ effect 内部実値の変換ヘルパー =====================

/** 0..1 → 表示用実値 (min..max, curve 適用)。 */
export function effectValueFromNormalized(meta: EffectKnobMeta, value01: number): number {
  if (meta.enumOptions) {
    // discrete enum: 0..1 を 0..(N-1) に量子化。
    const n = meta.enumOptions.length
    const idx = Math.min(n - 1, Math.max(0, Math.round(value01 * (n - 1))))
    return idx
  }
  const min = meta.min ?? 0
  const max = meta.max ?? 1
  const v = Math.min(1, Math.max(0, value01))
  // bipolarCenter があれば piecewise linear 優先 (curve 指定は無視)。
  if (meta.bipolarCenter !== undefined) {
    const c = meta.bipolarCenter
    return v < 0.5 ? min + (c - min) * (v * 2) : c + (max - c) * ((v - 0.5) * 2)
  }
  switch (meta.curve) {
    case 'log':
      if (min > 0 && max > min) return min * (max / min) ** v
      return min + (max - min) * v
    case 'cube':
      return min + (max - min) * (v * v * v)
    case 'square':
      return min + (max - min) * (v * v)
    default:
      return min + (max - min) * v
  }
}

/** 表示用実値 → 0..1 normalized。 */
export function effectValueToNormalized(meta: EffectKnobMeta, real: number): number {
  if (meta.enumOptions) {
    const n = meta.enumOptions.length
    const idx = Math.min(n - 1, Math.max(0, Math.round(real)))
    return n > 1 ? idx / (n - 1) : 0
  }
  const min = meta.min ?? 0
  const max = meta.max ?? 1
  if (max <= min) return 0
  // bipolarCenter があれば piecewise inverse。
  if (meta.bipolarCenter !== undefined) {
    const c = meta.bipolarCenter
    if (real <= c) {
      const t = (real - min) / (c - min) // 0..1
      return Math.min(0.5, Math.max(0, t * 0.5))
    }
    const t = (real - c) / (max - c) // 0..1
    return Math.min(1, Math.max(0.5, 0.5 + t * 0.5))
  }
  switch (meta.curve) {
    case 'log':
      if (min > 0 && real > 0) {
        return Math.min(1, Math.max(0, Math.log(real / min) / Math.log(max / min)))
      }
      return Math.min(1, Math.max(0, (real - min) / (max - min)))
    case 'cube': {
      const t = Math.min(1, Math.max(0, (real - min) / (max - min)))
      return Math.cbrt(t)
    }
    case 'square': {
      const t = Math.min(1, Math.max(0, (real - min) / (max - min)))
      return Math.sqrt(t)
    }
    default:
      return Math.min(1, Math.max(0, (real - min) / (max - min)))
  }
}

/** 表示用文字列フォーマット (例: "1.50 Hz", "50 %", "I+II")。 */
export function formatEffectValue(meta: EffectKnobMeta, value01: number): string {
  if (meta.enumOptions) {
    const n = meta.enumOptions.length
    const idx = Math.min(n - 1, Math.max(0, Math.round(value01 * (n - 1))))
    return meta.enumOptions[idx]
  }
  const real = effectValueFromNormalized(meta, value01)
  const precision = meta.precision ?? 2
  const numStr = real.toFixed(precision)
  return meta.unit ? `${numStr} ${meta.unit}` : numStr
}
