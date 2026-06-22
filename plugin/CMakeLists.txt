# DX10R plugin target — wires up iPlug2's iplug_add_plugin helper.
# Phase 0: minimal single-target instrument (silent), WebView UI.

# Sanity check: top-level CMakeLists.txt must have included iPlug2.cmake first.
if(NOT DEFINED IPLUG2_DIR OR NOT EXISTS "${IPLUG2_DIR}/iPlug")
    message(FATAL_ERROR
        "[DX10R] iPlug2 not found at IPLUG2_DIR='${IPLUG2_DIR}'.\n"
        "  Run `git submodule update --init` and the SDK download step, then rerun cmake."
    )
endif()

find_package(iPlug2 REQUIRED)

# macOS の Standalone (SWELL ベース) は IPlugAPP_main.cpp が
# resources/main.rc_mac_dlg / main.rc_mac_menu を #include する。これらは main.rc
# から swell_resgen.pl で生成する派生物。iPlug2 の CMake helper は自動実行しないため、
# ここで configure 時に走らせる。(Linux は後続フェーズ)
if(APPLE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/main.rc")
    set(_resgen_pl "${IPLUG2_DIR}/WDL/swell/swell_resgen.pl")
    set(_main_rc "${CMAKE_CURRENT_SOURCE_DIR}/resources/main.rc")
    set(_rc_dlg "${CMAKE_CURRENT_SOURCE_DIR}/resources/main.rc_mac_dlg")
    set(_rc_menu "${CMAKE_CURRENT_SOURCE_DIR}/resources/main.rc_mac_menu")
    if(EXISTS "${_resgen_pl}")
        if(NOT EXISTS "${_rc_dlg}" OR NOT EXISTS "${_rc_menu}"
           OR "${_main_rc}" IS_NEWER_THAN "${_rc_dlg}"
           OR "${_main_rc}" IS_NEWER_THAN "${_rc_menu}")
            message(STATUS "[DX10R] Generating SWELL resources from main.rc")
            execute_process(
                COMMAND perl "${_resgen_pl}" "${_main_rc}"
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/resources"
                RESULT_VARIABLE _resgen_rc
                OUTPUT_QUIET
            )
            if(NOT _resgen_rc EQUAL 0)
                message(FATAL_ERROR "swell_resgen.pl failed (rc=${_resgen_rc}). Make sure perl is installed.")
            endif()
        endif()
    else()
        message(WARNING "[DX10R] swell_resgen.pl not found at ${_resgen_pl}")
    endif()
endif()

# DSP エンジン (mda DX10 を移植した 2-op FM)。Voice / Lfo はヘッダオンリ、
# VoiceManager に係数導出 (mda update()) と MIDI / レンダリングを集約。
set(DX10R_DSP_SOURCES
    dsp/Voice.h
    dsp/Lfo.h
    dsp/VoiceManager.h
    dsp/VoiceManager.cpp
)

set(DX10R_PLUGIN_SOURCES
    DX10R.cpp
    DX10R.h
    config.h
    ParameterIDs.h
    # ParamSpecs: min/max/default の真実源 (pure C++)。plugin / WASM / tests が共有。
    ParamSpecs.h
    ParamSpecs.cpp
    resources/resource.h
    ${DX10R_DSP_SOURCES}
)

iplug_add_plugin(DX10R
    SOURCES ${DX10R_PLUGIN_SOURCES}
    WEB_RESOURCES
        resources/web/index.html
    UI WEBVIEW
    EXCLUDE_FORMATS WAM AUV3 VST2
)

# --------------------------------------------------------------------------
# Windows: WebUI 埋め込み + native ファイルダイアログ用ライブラリ
# --------------------------------------------------------------------------
if(WIN32)
    foreach(_fmt vst3 clap aax app)
        set(_target "DX10R-${_fmt}")
        if(TARGET ${_target})
            target_link_libraries(${_target} PRIVATE Comdlg32 Shell32 Ole32)
        endif()
    endforeach()

    # iPlug2 の helper は main.rc を APP ターゲットにしか自動追加しない。
    # VST3/CLAP/AAX にも明示追加して、Phase 2 で index.html を RCDATA 埋め込みに
    # 切替えたときに全フォーマットへ焼き込めるようにしておく。
    set(_dx10r_main_rc "${CMAKE_CURRENT_SOURCE_DIR}/resources/main.rc")
    if(EXISTS "${_dx10r_main_rc}")
        # rc.exe は RCDATA 取り込みファイルを依存追跡しない。index.html 更新で確実に
        # 再コンパイルさせるため OBJECT_DEPENDS に入れておく (Phase 2 の embed 用)。
        set(_dx10r_web_index "${CMAKE_CURRENT_SOURCE_DIR}/resources/web/index.html")
        if(EXISTS "${_dx10r_web_index}")
            set_source_files_properties("${_dx10r_main_rc}" PROPERTIES
                OBJECT_DEPENDS "${_dx10r_web_index}")
        endif()
        foreach(_fmt vst3 clap aax)
            set(_target "DX10R-${_fmt}")
            if(TARGET ${_target})
                target_sources(${_target} PRIVATE "${_dx10r_main_rc}")
            endif()
        endforeach()
    endif()
endif()

# --------------------------------------------------------------------------
# AAX: AAX_Export OBJECT library の .obj を AAX target へ直接食わせる。
# INTERFACE → OBJECT の連鎖は VS generator で .obj が link 入力に伝播しないため
# (CMake の既知制約)、明示しないと AAX エントリポイントが export されず PT が弾く。
# --------------------------------------------------------------------------
if(TARGET DX10R-aax AND TARGET AAX_Export)
    target_sources(DX10R-aax PRIVATE $<TARGET_OBJECTS:AAX_Export>)
    message(STATUS "[DX10R] Wired AAX_Export object files into DX10R-aax")
endif()

# --------------------------------------------------------------------------
# macOS: WebUI を bundle 内 Resources/web へ POST_BUILD でステージング。
# --------------------------------------------------------------------------
if(APPLE)
    set(DX10R_WEB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources/web")
    foreach(_fmt vst3 clap aax au app)
        set(_target "DX10R-${_fmt}")
        if(TARGET ${_target})
            add_custom_command(TARGET ${_target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${DX10R_WEB_DIR}"
                    "$<TARGET_FILE_DIR:${_target}>/../Resources/web"
                COMMENT "[DX10R] Staging WebUI resources into ${_target} bundle Resources/web"
                VERBATIM
            )
        endif()
    endforeach()

    # Debug ビルドで AU を ~/Library へ自動 deploy させない (= /Library の正規署名版を
    # shadow して壊す事故の恒久対策)。詳細は cmake/SkipDebugAUDeploy.cmake 参照。
    if(IPLUG_DEPLOY_PLUGINS AND TARGET DX10R-au)
        add_custom_command(TARGET DX10R-au POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                -DREMOVE=$<CONFIG:Debug>
                "-DTARGET_PATH=$ENV{HOME}/Library/Audio/Plug-Ins/Components/DX10R.component"
                -P "${CMAKE_SOURCE_DIR}/cmake/SkipDebugAUDeploy.cmake"
            COMMENT "[DX10R] Debug: skip AU auto-deploy (avoid shadowing /Library release)"
            VERBATIM
        )
    endif()
endif()
