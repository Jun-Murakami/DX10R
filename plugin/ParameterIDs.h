#pragma once

// DX10R parameter IDs — the single enumeration of plugin parameters.
// Faithful to mda DX10's 16 parameters, plus two DX10R additions:
//   * kParamSustain  — modern A/D/S/R extension (Sustain=0 reproduces the
//                      original DX10 decay-to-silence behaviour).
//   * kParamVolume   — master output level.
// Ranges / defaults / curves live in ParamSpecs.cpp (single source of truth).
//
// Pure header (no iPlug2 dependency): both the plugin and the DSP engine
// include it, so the engine can index live values by the same enum.

enum EParams
{
  // Carrier amplitude envelope (A/D/S/R).
  kParamAttack = 0,
  kParamDecay,
  kParamSustain,
  kParamRelease,

  // FM modulator:carrier frequency ratio (coarse integer + fine fraction).
  kParamCoarse,
  kParamFine,

  // Modulator envelope (modulation index over time) + velocity sensitivity.
  kParamModInit,
  kParamModDecay,
  kParamModSustain,
  kParamModRelease,
  kParamModVelocity,

  // Vibrato depth + LFO rate.
  kParamVibrato,
  kParamLfoRate,

  // Tone / pitch.
  kParamOctave,
  kParamFineTune,
  kParamWaveform,
  kParamModThru,

  // Master output.
  kParamVolume,

  // --- Effect chain: 5 serial slots, 10 contiguous IParams each, then chain lock.
  // Slot s base = kFxSlot0Type + s*10. Per slot: Type (enum), P0..P7 (generic
  // 0..1), Bypass (bool). See dx10::fx helpers below for (slot, offset) mapping.
  kFxSlot0Type, kFxSlot0P0, kFxSlot0P1, kFxSlot0P2, kFxSlot0P3, kFxSlot0P4, kFxSlot0P5, kFxSlot0P6, kFxSlot0P7, kFxSlot0Bypass,
  kFxSlot1Type, kFxSlot1P0, kFxSlot1P1, kFxSlot1P2, kFxSlot1P3, kFxSlot1P4, kFxSlot1P5, kFxSlot1P6, kFxSlot1P7, kFxSlot1Bypass,
  kFxSlot2Type, kFxSlot2P0, kFxSlot2P1, kFxSlot2P2, kFxSlot2P3, kFxSlot2P4, kFxSlot2P5, kFxSlot2P6, kFxSlot2P7, kFxSlot2Bypass,
  kFxSlot3Type, kFxSlot3P0, kFxSlot3P1, kFxSlot3P2, kFxSlot3P3, kFxSlot3P4, kFxSlot3P5, kFxSlot3P6, kFxSlot3P7, kFxSlot3Bypass,
  kFxSlot4Type, kFxSlot4P0, kFxSlot4P1, kFxSlot4P2, kFxSlot4P3, kFxSlot4P4, kFxSlot4P5, kFxSlot4P6, kFxSlot4P7, kFxSlot4Bypass,
  kFxChainLock,

  kNumParams
};

// Arbitrary WebUI <-> C++ message tags (SAMFUI/SAMFD payloads). Values MUST stay
// in sync with MSG_TAG in webui/src/bridge/iplug-bridge.ts.
enum EArbitraryMsgTags
{
  kMsgSavePreset = 0,      // JS -> C++: open native Save dialog, write current state
  kMsgLoadPreset,          // JS -> C++: open native Open dialog, load + apply a .dx10p
  kMsgClipboardWrite,      // JS -> C++: write payload text to the OS clipboard
  kMsgClipboardRead,       // JS -> C++: read OS clipboard, reply via kMsgClipboardReadResult
  kMsgClipboardReadResult, // C++ -> JS: clipboard text (SAMFD)
};

namespace dx10 {
// Effect-chain parameter layout helpers (5 slots × 10 contiguous params).
namespace fx {
constexpr int kFirstParam = kFxSlot0Type;  // 18
constexpr int kSlotStride = 10;            // Type + 8 generic + Bypass
constexpr int kSlotCount = 5;
constexpr int kNumTypeChoices = 12;        // Off + 11 effect types (EffectType::NumTypes)
// Is idx one of the 50 per-slot effect params (Type/P0..P7/Bypass)?
inline bool isSlotParam(int idx) {
  return idx >= kFirstParam && idx < kFirstParam + kSlotStride * kSlotCount;
}
inline int slotIdx(int idx) { return (idx - kFirstParam) / kSlotStride; }
inline int slotOffset(int idx) { return (idx - kFirstParam) % kSlotStride; }  // 0=Type, 1..8=P0..7, 9=Bypass
inline bool isType(int idx) { return isSlotParam(idx) && slotOffset(idx) == 0; }
inline bool isBypass(int idx) { return isSlotParam(idx) && slotOffset(idx) == 9; }
}  // namespace fx

// Editor size constants. Must stay in sync with plugin/config.h
// (PLUG_WIDTH/HEIGHT, PLUG_MIN/MAX_WIDTH/HEIGHT). The Windows high-DPI WebView
// bounds correction (DX10R.cpp) and the host checkSizeConstraint path both read
// these. DX10R has a single fixed layout (no expandable sections), so min/max
// are static.
namespace editor_size {
constexpr int kDefaultWidth = 1024;  // must match config.h PLUG_WIDTH
constexpr int kDefaultHeight = 740;  // must match config.h PLUG_HEIGHT
constexpr int kMinWidth = 600;
constexpr int kMinHeight = 480;
constexpr int kMaxWidth = 100000;  // effectively unbounded (responsive layout)
constexpr int kMaxHeight = 100000;
}  // namespace editor_size
}  // namespace dx10
