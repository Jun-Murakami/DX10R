// Vite dev server (no iPlug2 host) support: seed the store so the full UI is
// interactive in a plain browser. setFromUI is optimistic, so sliders already
// work locally even without a host (iplugAPI.setParamValue just no-ops).

import { iplugAPI } from './bridge/iplug-bridge'
import { defaultNormMap } from './dx10r-params'
import { useParamStore } from './store/paramStore'

export function installDevMockIfStandalone(): void {
  if (iplugAPI.hasHostBridge()) return
  if (!import.meta.env.DEV) return
  console.info('[dev-mock] no host bridge; running standalone with seeded defaults')
  useParamStore.getState().setManyFromHost(defaultNormMap())
}
