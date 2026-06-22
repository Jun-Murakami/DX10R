// iPlug2 WebViewEditorDelegate への送信側ヘルパー。プラットフォーム差 (WebView2 /
// WKWebView) を吸収し、JS オブジェクトをそのまま postMessage する (= iPlug2 内蔵の
// IPlugSendMsg と同じ作法)。JSON.stringify した文字列を渡すと WebView2 では
// nlohmann::json アクセスで abort、WKWebView では無視されるので絶対に文字列化しない。

// Arbitrary-message tags. MUST stay in sync with EArbitraryMsgTags in
// plugin/ParameterIDs.h.
export const MSG_TAG = {
  SavePreset: 0,
  LoadPreset: 1,
  ClipboardWrite: 2,
  ClipboardRead: 3,
  ClipboardReadResult: 4,
} as const

type CppMessage =
  | { msg: 'BPCFUI'; paramIdx: number }
  | { msg: 'EPCFUI'; paramIdx: number }
  | { msg: 'SPVFUI'; paramIdx: number; value: number }
  | { msg: 'SAMFUI'; msgTag: number; ctrlTag: number; data: string }

function postMessageToCpp(payload: CppMessage): void {
  if (window.webkit?.messageHandlers?.callback) {
    // WKWebView は any を受け JS オブジェクトを NSDictionary に変換する。型上 string
    // 受付なので unknown 経由でキャストして「オブジェクトのまま」渡す。
    window.webkit.messageHandlers.callback.postMessage(payload as unknown as string)
    return
  }
  if (window.chrome?.webview) {
    window.chrome.webview.postMessage(payload)
    return
  }
  // 純ブラウザ実行 (host 未接続) ではサイレントに捨てる。dev のみログ。
  if (import.meta.env.DEV) {
    console.debug('[iplug-bridge] no host bridge attached, dropping message:', payload)
  }
}

function bytesToBase64(bytes: Uint8Array): string {
  let bin = ''
  for (let i = 0; i < bytes.length; i++) bin += String.fromCharCode(bytes[i])
  return btoa(bin)
}

// C++ sends base64-encoded UTF-8 via SAMFD; decode it correctly (atob alone
// mangles multi-byte UTF-8, e.g. Japanese paths under OneDrive).
function base64ToUtf8(base64: string): string {
  const bin = atob(base64)
  const bytes = new Uint8Array(bin.length)
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i)
  return new TextDecoder('utf-8').decode(bytes)
}

// Pending OS-clipboard-read request: resolved when C++ replies with
// ClipboardReadResult via SAMFD. Single in-flight read is enough for paste.
let clipboardResolver: ((text: string) => void) | null = null

// C++ -> JS arbitrary-message dispatcher. Wire window.SAMFD to this in main.tsx.
export function handleSamfd(msgTag: number, _dataSize: number, base64: string): void {
  if (msgTag === MSG_TAG.ClipboardReadResult) {
    const text = base64ToUtf8(base64)
    if (clipboardResolver) {
      clipboardResolver(text)
      clipboardResolver = null
    }
  }
}

// オートメーション同期: ノブ操作開始で beginParamChange、ドラッグ中に setParamValue、
// 離したら endParamChange。開始 / 終了を忘れると DAW がオートメーションを録らない。
export const iplugAPI = {
  beginParamChange(paramIdx: number): void {
    postMessageToCpp({ msg: 'BPCFUI', paramIdx })
  },
  setParamValue(paramIdx: number, normalizedValue: number): void {
    postMessageToCpp({ msg: 'SPVFUI', paramIdx, value: normalizedValue })
  },
  endParamChange(paramIdx: number): void {
    postMessageToCpp({ msg: 'EPCFUI', paramIdx })
  },
  /** Generic arbitrary message to C++ (SAMFUI). data is raw bytes (base64'd here). */
  sendArbitraryMsg(msgTag: number, ctrlTag: number, data: Uint8Array): void {
    postMessageToCpp({ msg: 'SAMFUI', msgTag, ctrlTag, data: bytesToBase64(data) })
  },
  /** Ask C++ to open a native Save dialog and write the current state to a .dx10p. */
  savePreset(): void {
    postMessageToCpp({ msg: 'SAMFUI', msgTag: MSG_TAG.SavePreset, ctrlTag: -1, data: '' })
  },
  /** Ask C++ to open a native Open dialog and load a .dx10p (applied via SPVFD). */
  loadPreset(): void {
    postMessageToCpp({ msg: 'SAMFUI', msgTag: MSG_TAG.LoadPreset, ctrlTag: -1, data: '' })
  },
  /** Write text to the OS clipboard via C++ (the null-origin WebView cannot). */
  clipboardWrite(text: string): void {
    postMessageToCpp({
      msg: 'SAMFUI',
      msgTag: MSG_TAG.ClipboardWrite,
      ctrlTag: -1,
      data: bytesToBase64(new TextEncoder().encode(text)),
    })
  },
  /** Read the OS clipboard via C++; resolves with the text ('' if unavailable). */
  clipboardRead(): Promise<string> {
    if (!iplugAPI.hasHostBridge()) return Promise.resolve('')
    return new Promise<string>((resolve) => {
      clipboardResolver = resolve
      postMessageToCpp({ msg: 'SAMFUI', msgTag: MSG_TAG.ClipboardRead, ctrlTag: -1, data: '' })
      // Safety: never hang paste if C++ doesn't reply.
      window.setTimeout(() => {
        if (clipboardResolver === resolve) {
          clipboardResolver = null
          resolve('')
        }
      }, 1500)
    })
  },
  /** WebView2 / WKWebView の host bridge に到達できる環境か。純ブラウザ dev では false。 */
  hasHostBridge(): boolean {
    if (typeof window === 'undefined') return false
    return !!(window.webkit?.messageHandlers?.callback || window.chrome?.webview)
  },
}
