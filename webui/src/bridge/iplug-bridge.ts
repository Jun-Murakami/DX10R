// iPlug2 WebViewEditorDelegate への送信側ヘルパー。プラットフォーム差 (WebView2 /
// WKWebView) を吸収し、JS オブジェクトをそのまま postMessage する (= iPlug2 内蔵の
// IPlugSendMsg と同じ作法)。JSON.stringify した文字列を渡すと WebView2 では
// nlohmann::json アクセスで abort、WKWebView では無視されるので絶対に文字列化しない。

type CppMessage =
  | { msg: 'BPCFUI'; paramIdx: number }
  | { msg: 'EPCFUI'; paramIdx: number }
  | { msg: 'SPVFUI'; paramIdx: number; value: number }

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
  /** WebView2 / WKWebView の host bridge に到達できる環境か。純ブラウザ dev では false。 */
  hasHostBridge(): boolean {
    if (typeof window === 'undefined') return false
    return !!(window.webkit?.messageHandlers?.callback || window.chrome?.webview)
  },
}
