// -----------------------------------------------------------------------------
// DX10R — mda DX10 (2-op FM) を iPlug2 で現代風にリファインしたインストゥルメント。
// 単一ターゲット (instrument)。識別子は Synth80 の house 規約 (PLUG_MFR_ID 'Jmbc' /
// com.junmurakami.*) に揃える。バージョンは VERSION ファイルを SoT とし、将来
// bump_version.ps1 でここを同期する。
// -----------------------------------------------------------------------------
#define PLUG_NAME "DX10R"
#define PLUG_MFR "Jun Murakami"
#define PLUG_VERSION_HEX 0x00000100
#define PLUG_VERSION_STR "0.1.0"
#define PLUG_UNIQUE_ID 'DX1r'
#define PLUG_MFR_ID 'Jmbc'
#define PLUG_URL_STR "https://junmurakami.com/dx10r"
#define PLUG_EMAIL_STR "contact@bucketrelay.com"
#define PLUG_COPYRIGHT_STR "Copyright 2026 Jun Murakami"
#define PLUG_CLASS_NAME DX10R

// BUNDLE_ID = BUNDLE_DOMAIN.BUNDLE_MFR.<api>.BUNDLE_NAME。各 *-Info.plist の
// CFBundleIdentifier 末尾と完全一致が必須 (macOS Release の WebUI ロードが
// bundleWithIdentifier で自分のバンドルを引くため)。スペース/大文字を避ける。
#define BUNDLE_NAME "DX10R"
#define BUNDLE_MFR "junmurakami"
#define BUNDLE_DOMAIN "com"

#define SHARED_RESOURCES_SUBPATH "DX10R"

// MIDI IN → stereo OUT の instrument。
#define PLUG_CHANNEL_IO "0-2"

#define PLUG_LATENCY 0
// PLUG_TYPE 1 = instrument, 0 = audio effect。
#define PLUG_TYPE 1
#define PLUG_DOES_MIDI_IN 1
#define PLUG_DOES_MIDI_OUT 0
#define PLUG_DOES_MPE 0
#define PLUG_DOES_STATE_CHUNKS 0
#define PLUG_HAS_UI 1
// 暫定サイズ。Phase 2 の UI レイアウト確定時に webui の EDITOR_SIZE と揃える。
#define PLUG_WIDTH 760
#define PLUG_HEIGHT 620
#define PLUG_FPS 60
#define PLUG_SHARED_RESOURCES 0
#define PLUG_HOST_RESIZE 1
#define PLUG_MIN_WIDTH 600
#define PLUG_MIN_HEIGHT 480
#define PLUG_MAX_WIDTH 100000
#define PLUG_MAX_HEIGHT 100000

#define AUV2_ENTRY DX10R_Entry
#define AUV2_ENTRY_STR "DX10R_Entry"
#define AUV2_FACTORY DX10R_Factory
#define AUV2_VIEW_CLASS DX10R_View
#define AUV2_VIEW_CLASS_STR "DX10R_View"

#define AAX_TYPE_IDS 'DX1I'
#define AAX_TYPE_IDS_AUDIOSUITE 'DX1A'
#define AAX_PLUG_MFR_STR "Jun Murakami"
#define AAX_PLUG_NAME_STR "DX10R\nDX1R"
#define AAX_PLUG_CATEGORY_STR "SWGenerators"
#define AAX_DOES_AUDIOSUITE 0
#define VST3_SUBCATEGORY "Instrument|Synth"

#define APP_NUM_CHANNELS 2
#define APP_N_VECTOR_WAIT 0
#define APP_MULT 1
#define APP_COPY_AUV3 0
#define APP_SIGNAL_VECTOR_SIZE 64
