// DX10R parameter metadata for the WebUI. idx matches the C++ EParams order
// (plugin/ParameterIDs.h). The slider works in a linear scaled domain [min,max];
// `format(scaled)` produces the displayed string. `defaultNorm` seeds the store
// before the host pushes real values via SPVFD (matches plugin/ParamSpecs.cpp).

export interface Dx10rParam {
  idx: number
  label: string
  section: string
  min: number
  max: number
  unit: string
  defaultNorm: number
  format: (scaled: number) => string
}

// % params: scaled domain is 0..100 (= norm * 100).
const pct = (v: number): string => `${Math.round(v)}`

// mda fine-ratio quantisation (mdaDX10::update). n is the normalized value.
const fineRatio = (n: number): string => {
  if (n < 0.5) return (0.2 * n * n).toFixed(3)
  const k = Math.floor(8.9 * n)
  const table: Record<number, number> = { 4: 0.25, 5: 0.33333333, 6: 0.5, 7: 0.66666667 }
  return (table[k] ?? 0.75).toFixed(3)
}

export const DX10R_PARAMS: Dx10rParam[] = [
  // Carrier amplitude envelope.
  {
    idx: 0,
    label: 'Attack',
    section: 'Amp Env',
    min: 0,
    max: 100,
    unit: '%',
    defaultNorm: 0.0,
    format: pct,
  },
  {
    idx: 1,
    label: 'Decay',
    section: 'Amp Env',
    min: 0,
    max: 100,
    unit: '%',
    defaultNorm: 0.65,
    format: pct,
  },
  {
    idx: 2,
    label: 'Sustain',
    section: 'Amp Env',
    min: 0,
    max: 100,
    unit: '%',
    defaultNorm: 0.0,
    format: pct,
  },
  {
    idx: 3,
    label: 'Release',
    section: 'Amp Env',
    min: 0,
    max: 100,
    unit: '%',
    defaultNorm: 0.441,
    format: pct,
  },

  // FM modulator:carrier ratio (computed displays; slider is normalized 0..1).
  {
    idx: 4,
    label: 'Coarse',
    section: 'FM Ratio',
    min: 0,
    max: 1,
    unit: '',
    defaultNorm: 0.842,
    format: (n) => `${Math.floor(40.1 * n * n)}`,
  },
  {
    idx: 5,
    label: 'Fine',
    section: 'FM Ratio',
    min: 0,
    max: 1,
    unit: '',
    defaultNorm: 0.329,
    format: fineRatio,
  },

  // Modulator envelope.
  {
    idx: 6,
    label: 'Mod Init',
    section: 'Modulator',
    min: 0,
    max: 100,
    unit: '%',
    defaultNorm: 0.23,
    format: pct,
  },
  {
    idx: 7,
    label: 'Mod Decay',
    section: 'Modulator',
    min: 0,
    max: 100,
    unit: '%',
    defaultNorm: 0.8,
    format: pct,
  },
  {
    idx: 8,
    label: 'Mod Sustain',
    section: 'Modulator',
    min: 0,
    max: 100,
    unit: '%',
    defaultNorm: 0.05,
    format: pct,
  },
  {
    idx: 9,
    label: 'Mod Release',
    section: 'Modulator',
    min: 0,
    max: 100,
    unit: '%',
    defaultNorm: 0.8,
    format: pct,
  },
  {
    idx: 10,
    label: 'Mod Vel',
    section: 'Modulator',
    min: 0,
    max: 100,
    unit: '%',
    defaultNorm: 0.9,
    format: pct,
  },

  // LFO / vibrato.
  {
    idx: 11,
    label: 'Vibrato',
    section: 'LFO',
    min: 0,
    max: 100,
    unit: '%',
    defaultNorm: 0.0,
    format: pct,
  },
  {
    idx: 12,
    label: 'LFO Rate',
    section: 'LFO',
    min: 0,
    max: 1,
    unit: 'Hz',
    defaultNorm: 0.414,
    format: (n) => (25 * n * n).toFixed(2),
  },

  // Tone / pitch.
  {
    idx: 13,
    label: 'Octave',
    section: 'Tone',
    min: 0,
    max: 1,
    unit: '',
    defaultNorm: 0.5,
    format: (n) => `${Math.floor(n * 6.9) - 3}`,
  },
  {
    idx: 14,
    label: 'Fine Tune',
    section: 'Tone',
    min: -100,
    max: 100,
    unit: 'ct',
    defaultNorm: 0.5,
    format: (v) => `${Math.round(v)}`,
  },
  {
    idx: 15,
    label: 'Waveform',
    section: 'Tone',
    min: 0,
    max: 100,
    unit: '%',
    defaultNorm: 0.447,
    format: pct,
  },
  {
    idx: 16,
    label: 'Mod Thru',
    section: 'Tone',
    min: 0,
    max: 100,
    unit: '%',
    defaultNorm: 0.0,
    format: pct,
  },

  // Master.
  {
    idx: 17,
    label: 'Volume',
    section: 'Master',
    min: -70,
    max: 12,
    unit: 'dB',
    defaultNorm: 0.853659,
    format: (v) => v.toFixed(1),
  },
]

export const SECTIONS = ['Amp Env', 'Modulator', 'FM Ratio', 'Tone', 'LFO', 'Master'] as const

export function paramsForSection(section: string): Dx10rParam[] {
  return DX10R_PARAMS.filter((p) => p.section === section)
}

export function defaultNormMap(): Record<number, number> {
  const out: Record<number, number> = {}
  for (const p of DX10R_PARAMS) out[p.idx] = p.defaultNorm
  return out
}
