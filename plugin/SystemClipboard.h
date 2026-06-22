#pragma once

#include <string>

namespace dx10 {

// OS system clipboard (plain UTF-8 text) read/write, used by the effect-rack
// Copy/Paste. The plugin WebView is a null-origin context (in-memory LoadHTML),
// where navigator.clipboard is unavailable / permission-gated, so clipboard
// access is routed through C++ here. OnMessage (UI thread) is the only caller.

// Returns true on success. utf8 may be empty.
bool SetSystemClipboardText(const std::string& utf8);

// Returns true and fills utf8Out on success; false (and clears utf8Out) if the
// clipboard has no readable text.
bool GetSystemClipboardText(std::string& utf8Out);

}  // namespace dx10
