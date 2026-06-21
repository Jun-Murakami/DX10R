#pragma once

// DX10R — mda DX10 (2-op FM) modern refit on iPlug2 + WebView.
// Phase 0: minimal silent scaffold. The FM voice engine (ported from
// docs/mda_DX10_src/mdaDX10.cpp into plugin/dsp/) arrives in Phase 1, and the
// full parameter set moves to plugin/ParameterIDs.h + ParamSpecs.cpp.

#include "IPlug_include_in_plug_hdr.h"

using namespace iplug;

enum EParams
{
  // Placeholder so iPlug2 has a valid parameter set during Phase 0.
  // Replaced by the real DX10R parameters (carrier ADSR, modulator env,
  // ratio, tone, mod, master) in Phase 1 via ParameterIDs.h.
  kParamVolume = 0,
  kNumParams
};

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
};
