import { create } from 'zustand'

import { iplugAPI } from '../bridge/iplug-bridge'

// 値の正本は C++ 側 IParam (normalized [0,1])。この store はそのミラー + 楽観的更新。
// zustand の selector フックは内部で useSyncExternalStore を使うため tearing-free。

interface ParamStore {
  /** id → normalized [0,1] の現在値 */
  values: Record<number, number>
  isInitialized: boolean

  /** 初期デフォルト (host 値が SPVFD で届く前の表示用) を流し込む。 */
  registerDefaults: (defaults: Record<number, number>) => void
  /** ホスト (C++) からの値更新 (SPVFD)。楽観的更新を上書きする最終値。echo back しない。 */
  setFromHost: (idx: number, normalized: number) => void
  /** ホストからの複数値を 1 回の store 更新で反映する。 */
  setManyFromHost: (values: Record<number, number>) => void
  /** UI 操作からの値更新。楽観的にストアへ反映しつつ C++ にも送る。 */
  setFromUI: (idx: number, normalized: number) => void
  beginGesture: (idx: number) => void
  endGesture: (idx: number) => void
}

export const useParamStore = create<ParamStore>((set) => ({
  values: {},
  isInitialized: false,

  registerDefaults: (defaults) => set({ values: { ...defaults }, isInitialized: true }),

  setFromHost: (idx, normalized) => set((s) => ({ values: { ...s.values, [idx]: normalized } })),

  setManyFromHost: (values) =>
    set((s) => ({ values: { ...s.values, ...values }, isInitialized: true })),

  setFromUI: (idx, normalized) => {
    set((s) => ({ values: { ...s.values, [idx]: normalized } }))
    iplugAPI.setParamValue(idx, normalized)
  },

  beginGesture: (idx) => iplugAPI.beginParamChange(idx),
  endGesture: (idx) => iplugAPI.endParamChange(idx),
}))

/** 単一パラメータの normalized [0,1] 値を購読する。 */
export function useParam(idx: number): number {
  return useParamStore((s) => s.values[idx] ?? 0)
}
