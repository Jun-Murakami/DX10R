// iPlug2 WebViewEditorDelegate protocol: C++ calls these global functions to push
// state into the WebUI. Here we declare types only; main.tsx assigns the bodies.

declare global {
  interface Window {
    /** SetParamValueFromDelegate: normalized [0,1] parameter value (automation / preset restore) */
    SPVFD?: (paramIdx: number, normalizedValue: number) => void
    /** SetControlValueFromDelegate: non-param controls (meters etc.) */
    SCVFD?: (ctrlTag: number, normalizedValue: number) => void
    /** SetControlMessageFromDelegate: binary for a specific control (base64) */
    SCMFD?: (ctrlTag: number, msgTag: number, dataSize: number, base64: string) => void
    /** SendArbitraryMsgFromDelegate: arbitrary message; msgTag === -1 is the param JSON */
    SAMFD?: (msgTag: number, dataSize: number, base64: string) => void
    /** SendMidiMsgFromDelegate: MIDI event */
    SMMFD?: (status: number, data1: number, data2: number) => void

    /** WebView2 (Windows) */
    chrome?: {
      webview?: {
        postMessage: (msg: unknown) => void
      }
    }
    /** WKWebView (macOS). Pass a JS object; iPlug2 converts it to an NSDictionary. */
    webkit?: {
      messageHandlers?: {
        callback?: {
          postMessage: (msg: string) => void
        }
      }
    }
  }
}

export {}
