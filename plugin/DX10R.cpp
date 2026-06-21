#include "DX10R.h"
#include "IPlug_include_in_plug_src.h"

DX10R::DX10R(const InstanceInfo& info)
: Plugin(info, MakeConfig(kNumParams, 1))
{
  GetParam(kParamVolume)->InitGain("Volume", -6.0, -70.0, 12.0);

#ifdef WEBVIEW_EDITOR_DELEGATE
#ifdef _DEBUG
  SetEnableDevTools(true);
#endif
  // Phase 0: load the in-tree placeholder page. Phase 2 switches to the Vite
  // dev server (DEBUG) / embedded production bundle (RELEASE), mirroring Synth80.
  mEditorInitFunc = [&]() {
    LoadIndexHtml(__FILE__, GetBundleID());
    EnableScroll(false);
  };
#endif
}

#if IPLUG_DSP
void DX10R::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  // Phase 0: silence. The 2-op FM voice engine arrives in Phase 1.
  for (int s = 0; s < nFrames; s++)
  {
    outputs[0][s] = 0.0;
    outputs[1][s] = 0.0;
  }
}

void DX10R::ProcessMidiMsg(const IMidiMsg& msg)
{
  // Phase 0: no-op (no voices yet).
}

void DX10R::OnReset()
{
}
#endif

bool DX10R::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
  return false;
}

void DX10R::OnParamChange(int paramIdx)
{
}
