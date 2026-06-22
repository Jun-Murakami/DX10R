#include "DX10R.h"
#include "IPlug_include_in_plug_src.h"

#include "ParamSpecs.h"
#include "PresetFileDialog.h"
#include "SystemClipboard.h"
#include "resource.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#include <shlobj.h> // SHGetFolderPathW / CSIDL_PERSONAL (Documents)
#endif

// File-scope anchor so GetModuleHandleEx can resolve THIS module (the plugin
// DLL / standalone exe), not the host, when locating the embedded WebUI.
static void dx10rModuleAnchor() {}

// ---------------------------------------------------------------------------
// Windows DPI / WebView2 sizing helpers (ported from Synth-80).
// ---------------------------------------------------------------------------
// iPlug2's IWebViewImpl::SetWebViewBounds() always multiplies by
// GetScaleForHWND(parent). So callers must pass LOGICAL px and the controller
// bounds become physical. At >100% DPI the host's resize args are physical, so
// we must reconstruct logical px (client physical / scale) before passing them,
// else the WebView CSS viewport ends up monitor-scale too wide and the UI is
// clipped on the right.
namespace {

#ifdef OS_WIN
// Parent (plugin dialog / DAW window) DPI scale. Per-Monitor-V2 reports the
// current monitor DPI via GetDpiForWindow; older OSes fall back to 1.0.
float GetWindowDpiScale(HWND hwnd)
{
  if (!hwnd) return 1.0f;
  using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
  static GetDpiForWindowFn pGetDpiForWindow = []() -> GetDpiForWindowFn {
    HMODULE h = GetModuleHandleW(L"user32.dll");
    return h ? reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(h, "GetDpiForWindow")) : nullptr;
  }();
  if (!pGetDpiForWindow) return 1.0f;
  const UINT dpi = pGetDpiForWindow(hwnd);
  if (dpi == 0) return 1.0f;
  return static_cast<float>(dpi) / 96.0f;
}
#endif

// Clamp w/h into [min,max]; returns true if already in range (iPlug2 contract).
bool ConstrainEditorResizeToRange(int& w, int& h, int minW, int maxW, int minH, int maxH)
{
  bool inRange = true;
  if (w < minW) { w = minW; inRange = false; }
  if (w > maxW) { w = maxW; inRange = false; }
  if (h < minH) { h = minH; inRange = false; }
  if (h > maxH) { h = maxH; inRange = false; }
  return inRange;
}

#if defined(OS_WIN) && defined(AAX_API)
// Pro Tools Windows treats the AAX view as a 100% DPI coordinate space; if only
// WebView2/Chromium picks up the monitor DPI it scales up and clips. Pin DPR=1.
void InstallAAXWebViewScaleOverride()
{
  const char* kEnvName = "WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS";
  const char* kArg = "--force-device-scale-factor=1";
  char* existing = nullptr;
  size_t len = 0;
  if (_dupenv_s(&existing, &len, kEnvName) == 0 && existing != nullptr)
  {
    std::string combined(existing);
    free(existing);
    if (combined.find("--force-device-scale-factor") == std::string::npos)
    {
      if (!combined.empty()) combined += ' ';
      combined += kArg;
      _putenv_s(kEnvName, combined.c_str());
    }
    return;
  }
  _putenv_s(kEnvName, kArg);
}
#endif

}  // namespace

#if defined(OS_WIN) && defined(APP_API)
// Standalone (APP) window-frame drag must enforce the minimum size at the OS
// level. iPlug2's WM_GETMINMAXINFO handler (IPlugAPP_dialog.cpp) sets
// ptMinTrackSize then returns FALSE, so DefDlgProc/DefWindowProc overwrite it
// with the system default. We don't touch the submodule, so subclass the dialog
// and re-apply our min AFTER the default chain (= be the last writer). Single
// standalone instance → static state is fine. UI thread only.
static WNDPROC gAppOriginalWndProc = nullptr;
static DX10R* gAppMinMaxOwner = nullptr;

static LRESULT CALLBACK DX10RAppWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  WNDPROC orig = gAppOriginalWndProc;
  if (msg == WM_GETMINMAXINFO && orig && gAppMinMaxOwner)
  {
    const LRESULT r = CallWindowProc(orig, hwnd, msg, wParam, lParam);
    auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
    const float scale = GetWindowDpiScale(hwnd);
    // ptMinTrackSize is the non-client-inclusive window minimum; add the frame +
    // title-bar so the *client* minimum is exactly our logical min × DPI scale.
    RECT rcClient{}, rcWindow{};
    GetClientRect(hwnd, &rcClient);
    GetWindowRect(hwnd, &rcWindow);
    const int ncW = (rcWindow.right - rcWindow.left) - rcClient.right;
    const int ncH = (rcWindow.bottom - rcWindow.top) - rcClient.bottom;
    mmi->ptMinTrackSize.x = static_cast<LONG>(gAppMinMaxOwner->GetMinWidth() * scale + 0.5f) + ncW;
    mmi->ptMinTrackSize.y = static_cast<LONG>(gAppMinMaxOwner->GetMinHeight() * scale + 0.5f) + ncH;
    return r;
  }
  if (orig) return CallWindowProc(orig, hwnd, msg, wParam, lParam);
  return DefWindowProc(hwnd, msg, wParam, lParam);
}
#endif

DX10R::DX10R(const InstanceInfo& info)
: Plugin(info, MakeConfig(kNumParams, 1))
{
  // Initialise every IParam from the single source of truth (ParamSpecs).
  // All params are linear InitDouble: the FM params live in mda's native [0,1]
  // domain, master Volume is a linear dB range. This keeps normalized↔scaled a
  // simple affine map that the WebUI horizontal sliders can mirror directly.
  for (int i = 0; i < kNumParams; ++i)
  {
    const auto& sp = dx10::params::spec(i);
    if (dx10::fx::isType(i))
    {
      // Effect slot Type: enum 0=Off .. 11 (EffectType order). WebUI relabels.
      GetParam(i)->InitEnum(sp.name, static_cast<int>(sp.defaultVal), dx10::fx::kNumTypeChoices,
                            "", IParam::kFlagsNone, "Effects");
    }
    else if (dx10::fx::isBypass(i) || i == kFxChainLock)
    {
      GetParam(i)->InitBool(sp.name, sp.defaultVal != 0.0, "", IParam::kFlagsNone, "Effects");
    }
    else
    {
      const double step = (i == kParamVolume) ? 0.1 : 0.001;
      GetParam(i)->InitDouble(sp.name, sp.defaultVal, sp.minVal, sp.maxVal, step, sp.unit);
    }
  }

  // Push the initial parameter values into the engine.
  for (int i = 0; i < kNumParams; ++i)
    OnParamChange(i);

  // Editor size range (host checkSizeConstraint + APP WM_GETMINMAXINFO read it).
  SetSizeConstraints(dx10::editor_size::kMinWidth, dx10::editor_size::kMaxWidth,
                     dx10::editor_size::kMinHeight, dx10::editor_size::kMaxHeight);

#ifdef WEBVIEW_EDITOR_DELEGATE
#ifdef _DEBUG
  SetEnableDevTools(true);
#endif
  mEditorInitFunc = [&]() {
#ifdef _DEBUG
    // Optional live-reload dev workflow: set env DX10R_DEV_SERVER (e.g.
    // http://127.0.0.1:5173) and run `cd webui && npm run dev` to load the Vite
    // dev server with HMR. Unset → load the built single-file UI from the tree.
    if (const char* devUrl = std::getenv("DX10R_DEV_SERVER"))
    {
      LoadURL(devUrl);
      EnableScroll(true);
      return;
    }
#endif
#if defined(_WIN32)
    // Windows: load the WebUI embedded as RCDATA (IDR_INDEX_HTML) IN-MEMORY via
    // LoadHTML. This avoids LoadIndexHtml's virtual-host file mapping, which
    // WebView2 HTTP-caches → stale UI after a rebuild. The bundle is a fully
    // self-contained single file, so an in-memory load needs no base URL.
    HMODULE hModule = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&dx10rModuleAnchor), &hModule);
    if (HRSRC hRes = FindResourceA(hModule, MAKEINTRESOURCEA(IDR_INDEX_HTML), RT_RCDATA))
    {
      if (HGLOBAL hData = LoadResource(hModule, hRes))
      {
        const char* data = static_cast<const char*>(LockResource(hData));
        const DWORD size = SizeofResource(hModule, hRes);
        if (data && size > 0)
        {
          LoadHTML(std::string(data, size).c_str());
          EnableScroll(true);
          return;
        }
      }
    }
    // Fallback if the resource is missing (shouldn't happen in a normal build).
    LoadIndexHtml(__FILE__, GetBundleID());
#else
    // macOS: iPlug2 loads index.html from the bundle (release) / src tree (debug).
    LoadIndexHtml(__FILE__, GetBundleID());
#endif
    EnableScroll(true);
  };
#endif
}

#if IPLUG_DSP
void DX10R::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  // Walk the block in segments split at each MIDI event offset, so notes start
  // sample-accurately (mirrors mda DX10's per-frame note dispatch).
  int pos = 0;
  while (pos < nFrames)
  {
    while (!mMidiQueue.Empty() && mMidiQueue.Peek().mOffset <= pos)
    {
      HandleMidiMsg(mMidiQueue.Peek());
      mMidiQueue.Remove();
    }

    int next = nFrames;
    if (!mMidiQueue.Empty())
    {
      const int off = mMidiQueue.Peek().mOffset;
      next = (off < nFrames) ? off : nFrames;
      if (next <= pos)
        next = pos + 1; // safety: always make progress
    }

    mVoiceManager.render(outputs[0], outputs[1], pos, next - pos);
    pos = next;
  }

  mMidiQueue.Flush(nFrames);

  // Effect chain: run the synth's stereo output through the 5-slot serial rack.
  // Tempo is control-rate (per block); the rack processes per sample so delay /
  // reverb tails stay continuous across blocks.
  mEffectsRack.setTempo(GetTempo());
  sample* outL = outputs[0];
  sample* outR = outputs[1];
  for (int s = 0; s < nFrames; ++s)
  {
    double l = outL[s];
    double r = outR[s];
    mEffectsRack.process(l, r);
    outL[s] = l;
    outR[s] = r;
  }
}

void DX10R::ProcessMidiMsg(const IMidiMsg& msg)
{
  mMidiQueue.Add(msg);
}

void DX10R::OnReset()
{
  mVoiceManager.setSampleRate(GetSampleRate());
  mVoiceManager.reset();
  mMidiQueue.Clear();
  mEffectsRack.prepare(GetSampleRate());
  mEffectsRack.reset();
}
#endif

void DX10R::HandleMidiMsg(const IMidiMsg& msg)
{
  switch (msg.StatusMsg())
  {
    case IMidiMsg::kNoteOn:
      if (msg.Velocity() > 0)
        mVoiceManager.noteOn(msg.NoteNumber(), msg.Velocity());
      else
        mVoiceManager.noteOff(msg.NoteNumber());
      break;

    case IMidiMsg::kNoteOff:
      mVoiceManager.noteOff(msg.NoteNumber());
      break;

    case IMidiMsg::kPitchWheel:
      mVoiceManager.setPitchBend(msg.PitchWheel());
      break;

    case IMidiMsg::kControlChange:
    {
      const int cc = msg.mData1 & 0x7F;
      const int val = msg.mData2 & 0x7F;
      if (cc == 1) // mod wheel
        mVoiceManager.setModWheel(val);
      else if (cc == 7) // channel volume
        mVoiceManager.setVolumeCC(val);
      else if (cc == 64) // sustain pedal
        mVoiceManager.setSustainPedal(val >= 64);
      else if (cc >= 0x7A) // all sound/notes off, reset controllers, etc.
        mVoiceManager.panic();
      break;
    }

    default:
      break;
  }
}

bool DX10R::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
  // WebUI -> C++ arbitrary messages (SAMFUI). UI thread only — file dialogs /
  // file I/O / clipboard are safe here, never from ProcessBlock.
  switch (msgTag)
  {
    case kMsgSavePreset:
      handleSavePreset();
      return true;

    case kMsgLoadPreset:
      handleLoadPreset();
      return true;

    case kMsgClipboardWrite:
    {
      const std::string text(static_cast<const char*>(pData), static_cast<size_t>(dataSize));
      dx10::SetSystemClipboardText(text);
      return true;
    }

    case kMsgClipboardRead:
    {
      std::string text;
      dx10::GetSystemClipboardText(text);
      SendArbitraryMsgFromDelegate(kMsgClipboardReadResult, static_cast<int>(text.size()), text.data());
      return true;
    }

    default:
      return false;
  }
}

void DX10R::OnParamChange(int paramIdx)
{
  if (paramIdx == kParamVolume)
  {
    mVoiceManager.setMasterGain(GetParam(kParamVolume)->DBToAmp());
    return;
  }
  if (dx10::fx::isSlotParam(paramIdx))
  {
    applyEffectParamChange(paramIdx);
    return;
  }
  if (paramIdx == kFxChainLock)
    return; // consulted only on preset load; no audio effect

  // FM parameters (0..16): mda's native normalized [0,1] domain.
  mVoiceManager.setParam(paramIdx, GetParam(paramIdx)->GetNormalized());
}

void DX10R::applyEffectParamChange(int paramIdx)
{
  const int slot = dx10::fx::slotIdx(paramIdx);
  const int off = dx10::fx::slotOffset(paramIdx);
  if (off == 0)
  {
    // Type change: map the enum index → EffectType, switch the slot, then
    // reapply its 8 generic params (their meaning is type-dependent). Mirrors
    // Synth-80; per-effect default values are written by the WebUI on switch.
    const int raw = GetParam(paramIdx)->Int();
    const auto type = (raw > 0 && raw < static_cast<int>(dx10::dsp::EffectType::NumTypes))
                          ? static_cast<dx10::dsp::EffectType>(raw)
                          : dx10::dsp::EffectType::Off;
    mEffectsRack.setSlotType(slot, type);
    mEffectsRack.slot(slot).resetActive();
    std::array<double, dx10::dsp::kEffectParamsPerSlot> values{};
    for (int p = 0; p < dx10::dsp::kEffectParamsPerSlot; ++p)
      values[p] = GetParam(paramIdx + 1 + p)->Value();
    mEffectsRack.reapplyAllParams(slot, values);
  }
  else if (off == 9)
  {
    mEffectsRack.setSlotBypassed(slot, GetParam(paramIdx)->Int() != 0);
  }
  else
  {
    // off 1..8 → effect param 0..7 (generic 0..1).
    mEffectsRack.setSlotParam(slot, off - 1, GetParam(paramIdx)->Value());
  }
}

// ---------------------------------------------------------------------------
// User preset save/load (.dx10p flat JSON). UI thread only (via OnMessage).
// ---------------------------------------------------------------------------
namespace {

// <Documents>/DX10R, created if missing. Empty path on failure.
std::filesystem::path GetPresetsDir()
{
  namespace fs = std::filesystem;
  fs::path docs;
#if defined(_WIN32)
  wchar_t buf[MAX_PATH];
  if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, buf)))
    docs = fs::path(buf);
  if (docs.empty())
    if (const char* up = std::getenv("USERPROFILE")) docs = fs::path(up) / "Documents";
#else
  if (const char* home = std::getenv("HOME")) docs = fs::path(home) / "Documents";
#endif
  if (docs.empty()) return {};
  fs::path dir = docs / "DX10R";
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir;
}

} // namespace

void DX10R::handleSavePreset()
{
  std::filesystem::path path;
  if (!dx10::preset::PromptForPresetFile(mNativeParent, dx10::preset::FileDialogAction::Save,
                                         GetPresetsDir(), "MyPreset.dx10p", path))
    return;

  // Flat JSON of every param's normalized value: {"0": v, ..., "68": v}
  // (same shape as the factory docs/presets/*.dx10p, but the full 0..68 set).
  std::string json = "{\n";
  for (int i = 0; i < kNumParams; ++i)
  {
    char line[64];
    std::snprintf(line, sizeof(line), "  \"%d\": %.6g%s\n", i, GetParam(i)->GetNormalized(),
                  (i + 1 < kNumParams) ? "," : "");
    json += line;
  }
  json += "}\n";

  std::ofstream ofs(path, std::ios::binary);
  if (ofs) ofs << json;
}

void DX10R::handleLoadPreset()
{
  std::filesystem::path path;
  if (!dx10::preset::PromptForPresetFile(mNativeParent, dx10::preset::FileDialogAction::Open,
                                         GetPresetsDir(), "", path))
    return;

  std::ifstream ifs(path, std::ios::binary);
  if (!ifs) return;
  const std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  applyPresetJson(text);
}

void DX10R::applyPresetJson(const std::string& json)
{
  // Hand-parse the flat {"<idx>": <number>} map (our own format; no JSON lib).
  std::array<double, kNumParams> parsed{};
  std::array<bool, kNumParams> present{};
  const char* s = json.c_str();
  while (*s)
  {
    if (*s != '"') { ++s; continue; }
    const char* k = s + 1;
    int idx = 0;
    bool digits = false;
    while (*k >= '0' && *k <= '9') { idx = idx * 10 + (*k - '0'); ++k; digits = true; }
    if (!digits || *k != '"') { s = k; continue; }
    const char* c = k + 1;
    while (*c == ' ' || *c == '\t') ++c;
    if (*c != ':') { s = c; continue; }
    ++c;
    while (*c == ' ' || *c == '\t') ++c;
    char* endp = nullptr;
    const double v = std::strtod(c, &endp);
    if (endp != c && idx >= 0 && idx < kNumParams)
    {
      parsed[static_cast<size_t>(idx)] = v;
      present[static_cast<size_t>(idx)] = true;
    }
    s = (endp && endp > c) ? endp : c;
  }

  // Effect Chain Lock: when ON, a loaded patch must NOT overwrite the effect
  // slot params (18..67) so the user can swap the tone but keep their chain. The
  // lock (idx 68) and the FM/master params (0..17) always load.
  const bool chainLocked = GetParam(kFxChainLock)->Bool();

  // Pass 1: set IParam values first (so an effect Type re-apply, which reads its
  // slot's 8 generic Values, sees consistent state).
  for (int i = 0; i < kNumParams; ++i)
  {
    if (!present[static_cast<size_t>(i)]) continue;
    if (chainLocked && dx10::fx::isSlotParam(i)) continue;
    GetParam(i)->SetNormalized(parsed[static_cast<size_t>(i)]);
  }
  // Pass 2: update the DSP engine + push the new value to the WebUI (SPVFD).
  for (int i = 0; i < kNumParams; ++i)
  {
    if (!present[static_cast<size_t>(i)]) continue;
    if (chainLocked && dx10::fx::isSlotParam(i)) continue;
    OnParamChange(i);
    SendParameterValueFromDelegate(i, GetParam(i)->GetNormalized(), true);
  }
}

// ---------------------------------------------------------------------------
// Window / editor sizing (ported from Synth-80; simplified — DX10R is a single
// fixed layout with no expandable sections and no window-size persistence).
// ---------------------------------------------------------------------------

#if defined(OS_WIN) && defined(AAX_API)
void DX10R::SetAAXWebViewBoundsPhysical(int widthPx, int heightPx)
{
  HWND hwnd = static_cast<HWND>(mNativeParent);
  const float scale = GetWindowDpiScale(hwnd);
  const float invScale = scale > 0.0f ? 1.0f / scale : 1.0f;
  // iPlug2's SetWebViewBounds multiplies by GetScaleForHWND(parent). AAX/PT view
  // size is 100% DPI physical px, so pre-divide to land the final bounds on it.
  SetWebViewBounds(0, 0, static_cast<float>(widthPx) * invScale,
                   static_cast<float>(heightPx) * invScale);
}
#endif

void* DX10R::OpenWindow(void* pParent)
{
#if defined(OS_WIN) && defined(AAX_API)
  InstallAAXWebViewScaleOverride();
#endif
#ifdef OS_WIN
  mNativeParent = pParent;
#if defined(VST3_API)
  // Open the WebView at LOGICAL px even if physical px is transiently parked in
  // the editor size for the VST3 host.
  SetEditorSize(mVST3LogicalEditorWidth, mVST3LogicalEditorHeight);
#endif
#endif

  void* result = WebViewEditorDelegate::OpenWindow(pParent);

#ifdef OS_WIN
  // iPlug2's OpenWebView only stores the parent HWND; the controller bounds are
  // initialised async to (0,0,0,0) → invisible WebView. Set them explicitly.
  // (Crucial for AAX, whose wrapper never calls an onSize equivalent.)
#if defined(AAX_API)
  SetAAXWebViewBoundsPhysical(GetEditorWidth(), GetEditorHeight());
#else
  SetWebViewBounds(0, 0, static_cast<float>(GetEditorWidth()),
                   static_cast<float>(GetEditorHeight()));
#endif

#if defined(APP_API)
  if (HWND hwnd = static_cast<HWND>(pParent))
  {
    // Install the OS-level minimum-size hard stop once.
    if (!gAppOriginalWndProc)
    {
      gAppMinMaxOwner = this;
      gAppOriginalWndProc = reinterpret_cast<WNDPROC>(
          SetWindowLongPtr(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(DX10RAppWndProc)));
    }
    // At non-1.0 DPI iPlug2's ClientResize parks the window at logical-as-physical
    // (too small); defer the correction to OnParentWindowResize.
    mDpiInitPending = (GetWindowDpiScale(hwnd) != 1.0f);
  }
  else
  {
    mDpiInitPending = false;
  }
#endif
#endif
  return result;
}

void DX10R::CloseWindow()
{
  WebViewEditorDelegate::CloseWindow();
#ifdef OS_WIN
#if defined(VST3_API)
  // Restore logical size so the next getSize()/OpenWebView doesn't reuse a stale
  // physical px or the 0x0 from close.
  SetEditorSize(mVST3LogicalEditorWidth, mVST3LogicalEditorHeight);
#endif
#if defined(APP_API)
  if (gAppOriginalWndProc)
  {
    if (HWND hwnd = static_cast<HWND>(mNativeParent))
      SetWindowLongPtr(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(gAppOriginalWndProc));
    gAppOriginalWndProc = nullptr;
    gAppMinMaxOwner = nullptr;
  }
#endif
  mNativeParent = nullptr;
#endif
}

bool DX10R::ConstrainEditorResize(int& w, int& h) const
{
  using namespace dx10::editor_size;
#if defined(OS_WIN) && defined(VST3_API)
  // Cubase et al. treat the checkSizeConstraint ViewRect as physical px on high
  // DPI; returning logical min would let the window shrink to (min × 1/scale)
  // CSS px and clip the UI. Scale the constraint to physical when DPI != 1.
  if (HWND hwnd = static_cast<HWND>(mNativeParent))
  {
    const float scale = GetWindowDpiScale(hwnd);
    if (scale != 1.0f)
    {
      const int minW = static_cast<int>(kMinWidth * scale + 0.5f);
      const int maxW = static_cast<int>(kMaxWidth * scale + 0.5f);
      const int minH = static_cast<int>(kMinHeight * scale + 0.5f);
      const int maxH = static_cast<int>(kMaxHeight * scale + 0.5f);
      return ConstrainEditorResizeToRange(w, h, minW, maxW, minH, maxH);
    }
  }
#endif
  return ConstrainEditorResizeToRange(w, h, kMinWidth, kMaxWidth, kMinHeight, kMaxHeight);
}

void DX10R::OnParentWindowResize(int width, int height)
{
#if defined(OS_WIN) && defined(AAX_API)
  // AAX usually doesn't call this, but if it does, match the PT 100% physical px.
  SetAAXWebViewBoundsPhysical(width, height);
  EditorResizeFromUI(width, height, false);
  return;
#endif

#if defined(OS_WIN) && !defined(AAX_API)
  HWND hwnd = static_cast<HWND>(mNativeParent);
#if defined(VST3_API)
  if (!hwnd)
    return; // pre-attach resize: ignore (DX10R doesn't persist window size)
#endif
  if (hwnd)
  {
    const float scale = GetWindowDpiScale(hwnd);
    RECT rcClient{};
    GetClientRect(hwnd, &rcClient);
    const int physW = rcClient.right;
    const int physH = rcClient.bottom;
    if (physW <= 0 || physH <= 0)
      return;

#if defined(VST3_API)
    // First onSize from a new instance: VST3 hosts (Cubase) interpret getSize()'s
    // return as physical px, so a default (760,620) open parks the parent client
    // at 760 physical = 507 logical CSS at 1.5×. Detect and resize once to
    // default × scale physical so the window opens at the intended logical size.
    if (!mInitialSizeCorrectionDone && scale != 1.0f)
    {
      mInitialSizeCorrectionDone = true;
      const int defaultW = dx10::editor_size::kDefaultWidth;
      const int defaultH = dx10::editor_size::kDefaultHeight;
      const int minW = dx10::editor_size::kMinWidth;
      const int minH = dx10::editor_size::kMinHeight;
      const int targetPhysW = static_cast<int>(defaultW * scale + 0.5f);
      const int targetPhysH = static_cast<int>(defaultH * scale + 0.5f);
      const bool looksLikeDefaultAsPhysical =
          std::abs(physW - defaultW) <= 4 && std::abs(physH - defaultH) <= 4;
      const double logicalWf = static_cast<double>(physW) / scale;
      const double logicalHf = static_cast<double>(physH) / scale;
      const bool looksLikeShrunkBelowMin =
          logicalWf < static_cast<double>(minW) - 1.0 || logicalHf < static_cast<double>(minH) - 1.0;
      const int minPhysW = static_cast<int>(minW * scale + 0.5f);
      const int minPhysH = static_cast<int>(minH * scale + 0.5f);
      const bool looksLikeMinAsPhysical =
          std::abs(physW - minPhysW) <= 4 && std::abs(physH - minPhysH) <= 4;
      if (looksLikeDefaultAsPhysical || looksLikeShrunkBelowMin || looksLikeMinAsPhysical)
      {
        EditorResizeFromUI(targetPhysW, targetPhysH, true);
        return; // host re-fires onSize; bounds get set on that pass
      }
    }
#endif

#if defined(APP_API)
    // Standalone: iPlug2's ClientResize sends a wrong size; drive the window to
    // the target physical size until reached, then respect user drags.
    const int targetPhysW = static_cast<int>(GetEditorWidth() * scale);
    const int targetPhysH = static_cast<int>(GetEditorHeight() * scale);
    if (mDpiInitPending && scale != 1.0f)
    {
      if (width == targetPhysW && height == targetPhysH)
      {
        mDpiInitPending = false;
      }
      else
      {
        RECT rcWindow{};
        GetWindowRect(hwnd, &rcWindow);
        const int ncW = (rcWindow.right - rcWindow.left) - rcClient.right;
        const int ncH = (rcWindow.bottom - rcWindow.top) - rcClient.bottom;
        SetWindowPos(hwnd, nullptr, 0, 0, targetPhysW + ncW, targetPhysH + ncH,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
        return;
      }
    }
#endif

    // Read the actual confirmed client physical px and divide by scale to pass
    // LOGICAL px to SetWebViewBounds (the args' unit is host-dependent, so we
    // trust GetClientRect instead). This makes CSS viewport == logical size.
    const int logicalW = static_cast<int>(physW / scale);
    const int logicalH = static_cast<int>(physH / scale);
    SetWebViewBounds(0, 0, static_cast<float>(logicalW), static_cast<float>(logicalH));
#if defined(VST3_API)
    mVST3LogicalEditorWidth = logicalW;
    mVST3LogicalEditorHeight = logicalH;
    mVST3HasLogicalEditorSize = true;
    SetEditorSize(physW, physH); // host treats getSize() as physical px
#endif
    EditorResizeFromUI(logicalW, logicalH, false);
    return;
  }
#endif

  // Non-Windows, or no parent HWND: fall back to the base implementation.
  WebViewEditorDelegate::OnParentWindowResize(width, height);
}
