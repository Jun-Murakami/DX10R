#pragma once

// DX10R — mda DX10 (2-op FM) modern refit on iPlug2 + WebView.
// Phase 1: the FM voice engine (plugin/dsp/, ported from
// docs/mda_DX10_src/mdaDX10.cpp) is wired up; MIDI plays 16-voice polyphonic FM.

#include "IPlug_include_in_plug_hdr.h"
#include "IPlugMidi.h"

#include "ParameterIDs.h"
#include "dsp/VoiceManager.h"
#include "dsp/effects/EffectsRack.h"

#include <string>

using namespace iplug;

class DX10R final : public Plugin
{
public:
  DX10R(const InstanceInfo& info);

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void ProcessMidiMsg(const IMidiMsg& msg) override;
  void OnReset() override;
#endif

  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;
  void OnParamChange(int paramIdx) override;

  // Window / editor sizing. Overridden to fix Windows high-DPI WebView2 bounds
  // (the WebView CSS viewport must equal the logical editor size at any DPI,
  // else the UI is clipped on the right at >100% scale). Ported from Synth-80.
  void* OpenWindow(void* pParent) override;
  void CloseWindow() override;
  void OnParentWindowResize(int width, int height) override;
  bool ConstrainEditorResize(int& w, int& h) const override;

private:
  void HandleMidiMsg(const IMidiMsg& msg);
  // Dispatch an effect-chain IParam change (Type / generic / Bypass) to the rack.
  void applyEffectParamChange(int paramIdx);
  // User preset save/load (.dx10p flat JSON). UI-thread only (via OnMessage):
  // native file dialogs + file I/O are not real-time safe.
  void handleSavePreset();
  void handleLoadPreset();
  void applyPresetJson(const std::string& json);
#if defined(OS_WIN) && defined(AAX_API)
  // Pro Tools / AAX views are 100% DPI physical px; pre-divide the bounds so
  // iPlug2's internal GetScaleForHWND() multiplication lands on the PT view size.
  void SetAAXWebViewBoundsPhysical(int widthPx, int heightPx);
#endif

  dx10::dsp::VoiceManager mVoiceManager;
  dx10::dsp::EffectsRack mEffectsRack;
  IMidiQueue mMidiQueue;

  // --- Windows DPI / WebView sizing state ---
  void* mNativeParent = nullptr; // parent HWND captured in OpenWindow
#if defined(OS_WIN) && defined(APP_API)
  // Correct only the iPlug2 ClientResize wrong-size right after OpenWindow at
  // non-1.0 DPI; subsequent user drags are respected.
  bool mDpiInitPending = false;
#endif
#if defined(OS_WIN) && defined(VST3_API)
  // VST3 hosts treat getSize()/resizeView() values as physical px on Windows, so
  // we keep the logical editor size separately for the WebView bounds.
  int mVST3LogicalEditorWidth = dx10::editor_size::kDefaultWidth;
  int mVST3LogicalEditorHeight = dx10::editor_size::kDefaultHeight;
  bool mVST3HasLogicalEditorSize = false;
  bool mInitialSizeCorrectionDone = false;
#endif
};
