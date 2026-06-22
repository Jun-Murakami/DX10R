#include "SystemClipboard.h"

#ifdef _WIN32

#include <windows.h>

#include <string>

// Windows: CF_UNICODETEXT clipboard with UTF-8 <-> UTF-16 conversion. OnMessage
// runs on the UI thread.
namespace dx10 {

bool SetSystemClipboardText(const std::string& utf8) {
  const int srcLen = static_cast<int>(utf8.size());
  const int wlen = srcLen == 0 ? 0 : MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), srcLen, nullptr, 0);
  if (wlen < 0) return false;
  if (!OpenClipboard(nullptr)) return false;
  bool ok = false;
  if (EmptyClipboard()) {
    const size_t count = static_cast<size_t>(wlen) + 1;  // + trailing NUL
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, count * sizeof(wchar_t));
    if (hMem != nullptr) {
      if (auto* dst = static_cast<wchar_t*>(GlobalLock(hMem))) {
        if (wlen > 0) MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), srcLen, dst, wlen);
        dst[wlen] = L'\0';
        GlobalUnlock(hMem);
        if (SetClipboardData(CF_UNICODETEXT, hMem) != nullptr) {
          ok = true;  // ownership transfers to the OS — do not GlobalFree.
        } else {
          GlobalFree(hMem);
        }
      } else {
        GlobalFree(hMem);
      }
    }
  }
  CloseClipboard();
  return ok;
}

bool GetSystemClipboardText(std::string& utf8Out) {
  utf8Out.clear();
  if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return false;
  if (!OpenClipboard(nullptr)) return false;
  bool ok = false;
  if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
    if (const auto* src = static_cast<const wchar_t*>(GlobalLock(h))) {
      const int u8len = WideCharToMultiByte(CP_UTF8, 0, src, -1, nullptr, 0, nullptr, nullptr);
      if (u8len > 0) {
        std::string buf(static_cast<size_t>(u8len), '\0');
        WideCharToMultiByte(CP_UTF8, 0, src, -1, buf.data(), u8len, nullptr, nullptr);
        if (!buf.empty() && buf.back() == '\0') buf.pop_back();  // drop the NUL
        utf8Out = std::move(buf);
        ok = true;
      }
      GlobalUnlock(h);
    }
  }
  CloseClipboard();
  return ok;
}

}  // namespace dx10

#endif  // _WIN32
