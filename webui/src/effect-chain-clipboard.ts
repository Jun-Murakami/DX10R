// Effect-chain (5-slot) clipboard serializer (ported from Synth-80). Serializes
// each slot's type / 8 generic params / bypass into a dedicated JSON; paste only
// accepts JSON whose `format` matches (so other apps' JSON / full patches aren't
// slurped in). Excludes Effect Chain Lock. Pure functions (no side effects).

import {
  EFFECT_PARAMS_PER_SLOT,
  EFFECT_TYPE_ENUM_RANGE,
  EFFECTS_SLOT_COUNT,
  effectSlotBypassIdx,
  effectSlotParamIdx,
  effectSlotTypeIdx,
  effectTypeIdxFromValue,
  effectTypeValueFromIdx,
} from './effect-params'

export const EFFECT_CHAIN_CLIPBOARD_FORMAT = 'dx10r-effect-chain'
export const EFFECT_CHAIN_CLIPBOARD_VERSION = 1

export interface EffectChainSlotData {
  type: number
  bypass: boolean
  params: number[]
}

export interface EffectChainClipboardData {
  format: typeof EFFECT_CHAIN_CLIPBOARD_FORMAT
  version: number
  slots: EffectChainSlotData[]
}

function clamp01(v: number): number {
  return v < 0 ? 0 : v > 1 ? 1 : v
}

export function serializeEffectChain(values: Record<number, number>): EffectChainClipboardData {
  const slots: EffectChainSlotData[] = []
  for (let slot = 0; slot < EFFECTS_SLOT_COUNT; slot++) {
    const params: number[] = []
    for (let p = 0; p < EFFECT_PARAMS_PER_SLOT; p++) {
      params.push(clamp01(values[effectSlotParamIdx(slot, p)] ?? 0))
    }
    slots.push({
      type: effectTypeIdxFromValue(values[effectSlotTypeIdx(slot)] ?? 0),
      bypass: Math.round(values[effectSlotBypassIdx(slot)] ?? 0) >= 1,
      params,
    })
  }
  return { format: EFFECT_CHAIN_CLIPBOARD_FORMAT, version: EFFECT_CHAIN_CLIPBOARD_VERSION, slots }
}

export function effectChainToJson(values: Record<number, number>): string {
  return JSON.stringify(serializeEffectChain(values), null, 2)
}

export function parseEffectChain(text: string): EffectChainClipboardData | null {
  let raw: unknown
  try {
    raw = JSON.parse(text)
  } catch {
    return null
  }
  if (typeof raw !== 'object' || raw === null) return null
  const obj = raw as Record<string, unknown>
  if (obj.format !== EFFECT_CHAIN_CLIPBOARD_FORMAT) return null
  if (!Array.isArray(obj.slots)) return null

  const slots: EffectChainSlotData[] = []
  for (const entry of obj.slots) {
    if (typeof entry !== 'object' || entry === null) return null
    const s = entry as Record<string, unknown>
    if (typeof s.type !== 'number' || !Number.isFinite(s.type)) return null
    if (!Array.isArray(s.params)) return null
    const params: number[] = []
    for (let p = 0; p < EFFECT_PARAMS_PER_SLOT; p++) {
      const v = s.params[p]
      params.push(typeof v === 'number' && Number.isFinite(v) ? clamp01(v) : 0)
    }
    const type = Math.min(EFFECT_TYPE_ENUM_RANGE - 1, Math.max(0, Math.round(s.type)))
    slots.push({ type, bypass: s.bypass === true, params })
  }
  if (slots.length === 0) return null
  return {
    format: EFFECT_CHAIN_CLIPBOARD_FORMAT,
    version: typeof obj.version === 'number' ? obj.version : EFFECT_CHAIN_CLIPBOARD_VERSION,
    slots,
  }
}

export function effectChainToParamWrites(data: EffectChainClipboardData): Array<[number, number]> {
  const writes: Array<[number, number]> = []
  const n = Math.min(EFFECTS_SLOT_COUNT, data.slots.length)
  for (let slot = 0; slot < n; slot++) {
    const s = data.slots[slot]
    writes.push([effectSlotTypeIdx(slot), effectTypeValueFromIdx(s.type)])
    for (let p = 0; p < EFFECT_PARAMS_PER_SLOT; p++) {
      writes.push([effectSlotParamIdx(slot, p), clamp01(s.params[p] ?? 0)])
    }
    writes.push([effectSlotBypassIdx(slot), s.bypass ? 1 : 0])
  }
  return writes
}
