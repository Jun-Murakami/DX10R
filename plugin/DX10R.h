#pragma once

// DX10R — mda DX10 (2-op FM) modern refit on iPlug2 + WebView.
// Phase 1: the FM voice engine (plugin/dsp/, ported from
// docs/mda_DX10_src/mdaDX10.cpp) is wired up; MIDI plays 16-voice polyphonic FM.

#include "IPlug_include_in_plug_hdr.h"
#include "IPlugMidi.h"

#include "ParameterIDs.h"
#include "dsp/VoiceManager.h"

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

private:
  void HandleMidiMsg(const IMidiMsg& msg);

  dx10::dsp::VoiceManager mVoiceManager;
  IMidiQueue mMidiQueue;
};
