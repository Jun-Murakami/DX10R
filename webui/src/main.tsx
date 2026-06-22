import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'

import App from './App'
import { handleSamfd } from './bridge/iplug-bridge'
import { installDevMockIfStandalone } from './dev-mock'
import { defaultNormMap } from './dx10r-params'
import { useParamStore } from './store/paramStore'

// Seed defaults so the UI renders before the host pushes real values via SPVFD.
useParamStore.getState().registerDefaults(defaultNormMap())

// === C++ → JS receivers (iPlug2 WebViewEditorDelegate calls these globals) ===
// Must exist before the host's OnWebContentLoaded fires.
window.SPVFD = (paramIdx, normalizedValue) => {
  useParamStore.getState().setFromHost(paramIdx, normalizedValue)
}
// Phase 2 doesn't use these channels yet; install no-op stubs so the host's
// EvaluateJavaScript calls never hit "undefined is not a function".
window.SCVFD = () => {}
window.SCMFD = () => {}
// Arbitrary C++ → JS messages (clipboard-read result, etc.).
window.SAMFD = (msgTag: number, dataSize: number, base64: string) => {
  handleSamfd(msgTag, dataSize, base64)
}
window.SMMFD = () => {}

const root = document.getElementById('root')
if (!root) throw new Error('root element not found')

createRoot(root).render(
  <StrictMode>
    <App />
  </StrictMode>,
)

installDevMockIfStandalone()
