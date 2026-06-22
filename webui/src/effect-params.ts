// DX10R effect-chain param-index helpers (mirrors C++ plugin/ParameterIDs.h).
//
// Layout: slot s (0..4) base = 18 + s*10 → Type = base, P1..P8 = base+1..base+8
// (generic 0..1 params), Bypass = base+9. Effect Chain Lock = 68.
// Type IParam is an enum of 12 choices (0=Off..11=PitchChorus); normalized = id/11.

export const FX_BASE = 18
export const SLOT_STRIDE = 10
export const EFFECTS_SLOT_COUNT = 5
export const EFFECT_PARAMS_PER_SLOT = 8
export const EFFECT_TYPE_ENUM_RANGE = 12 // Off..PitchChorus
export const EFFECT_CHAIN_LOCK_IDX = 68

const MAX_TYPE_ID = EFFECT_TYPE_ENUM_RANGE - 1 // 11

export const effectSlotTypeIdx = (slot: number): number => FX_BASE + slot * SLOT_STRIDE
export const effectSlotParamIdx = (slot: number, offset: number): number =>
  FX_BASE + slot * SLOT_STRIDE + 1 + offset
export const effectSlotBypassIdx = (slot: number): number => FX_BASE + slot * SLOT_STRIDE + 9

/** Type IParam normalized value → enum idx (0..11). */
export const effectTypeIdxFromValue = (v: number): number =>
  Math.min(MAX_TYPE_ID, Math.max(0, Math.round(v * MAX_TYPE_ID)))
/** enum idx → Type IParam normalized value. */
export const effectTypeValueFromIdx = (i: number): number => (MAX_TYPE_ID > 0 ? i / MAX_TYPE_ID : 0)
