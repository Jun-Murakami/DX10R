#include "DX10R.h"
#include "IPlug_include_in_plug_src.h"

#include "ParamSpecs.h"

#include <cstdlib>

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
    const double step = (i == kParamVolume) ? 0.1 : 0.001;
    GetParam(i)->InitDouble(sp.name, sp.defaultVal, sp.minVal, sp.maxVal, step, sp.unit);
  }

  // Push the initial parameter values into the engine.
  for (int i = 0; i < kNumParams; ++i)
    OnParamChange(i);

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
      EnableScroll(false);
      return;
    }
#endif
    // Loads webui's production build (plugin/resources/web/index.html, written by
    // `npm run build`). RELEASE-bundle embedding is wired up in Phase 6.
    LoadIndexHtml(__FILE__, GetBundleID());
    EnableScroll(false);
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
  return false;
}

void DX10R::OnParamChange(int paramIdx)
{
  if (paramIdx == kParamVolume)
    mVoiceManager.setMasterGain(GetParam(kParamVolume)->DBToAmp());
  else
    mVoiceManager.setParam(paramIdx, GetParam(paramIdx)->GetNormalized());
}
