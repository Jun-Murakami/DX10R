# DX10R — mda DX10 (2-op FM) modern refit
# iPlug2 + WebView (Vite + React + MUI)

cmake_minimum_required(VERSION 3.22)

# Pin the macOS deployment target so all submodules and SDKs agree on it.
if(APPLE)
    set(DX10R_MIN_MACOS "10.15")
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "${DX10R_MIN_MACOS}" CACHE STRING "Minimum supported macOS version" FORCE)
    elseif(CMAKE_OSX_DEPLOYMENT_TARGET VERSION_GREATER DX10R_MIN_MACOS)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "${DX10R_MIN_MACOS}" CACHE STRING "Minimum supported macOS version" FORCE)
    endif()
    message(STATUS "macOS deployment target: ${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif()

# Single source of truth for the version string.
include(${CMAKE_SOURCE_DIR}/cmake/Version.cmake)

project(DX10R VERSION ${PROJECT_VERSION} LANGUAGES C CXX)

# C++17 baseline. Local C++20 features can still be opted into per-target.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC は既定でソースを Active Code Page で読むため、日本語環境では UTF-8 ソース中の
# 2 バイト文字が SJIS の 0x5C(\) と衝突して行継続が起き構文エラーになる。/utf-8 を強制。
if(MSVC)
    add_compile_options(/utf-8)
endif()

# --------------------------------------------------------------------------
# iPlug2 integration
# --------------------------------------------------------------------------
# iPlug2 は ./iPlug2/ に git submodule として配置 (SDK は download スクリプト / AAX は
# 丸コピー)。vendoring はしない。
set(IPLUG2_DIR "${CMAKE_SOURCE_DIR}/iPlug2" CACHE PATH "Path to the iPlug2 framework checkout")

if(NOT EXISTS "${IPLUG2_DIR}/iPlug")
    message(FATAL_ERROR
        "iPlug2 not found at ${IPLUG2_DIR}.\n"
        "  Run `git submodule update --init` and the SDK download step."
    )
endif()

# iPlug2 submodule へのローカルパッチを冪等適用する (iPlug2.cmake の前)。
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ApplyIPlug2Patch.cmake)

include(${IPLUG2_DIR}/iPlug2.cmake)

# --------------------------------------------------------------------------
# Plugin deploy paths (Windows): system-wide 標準パスに固定 (Release/Debug 排他)。
# 注意: Program Files への書込のため `cmake --build` は管理者ターミナルで実行する。
# --------------------------------------------------------------------------
if(WIN32)
    if(NOT IPLUG_VST3_DEPLOY_PATH)
        set(IPLUG_VST3_DEPLOY_PATH "$ENV{PROGRAMFILES}/Common Files/VST3"
            CACHE PATH "Override default VST3 deployment path" FORCE)
    endif()
    if(NOT IPLUG_CLAP_DEPLOY_PATH)
        set(IPLUG_CLAP_DEPLOY_PATH "$ENV{PROGRAMFILES}/Common Files/CLAP"
            CACHE PATH "Override default CLAP deployment path" FORCE)
    endif()
endif()

# --------------------------------------------------------------------------
# Plugin deploy paths (macOS): Windows と同思想で system-wide /Library に固定。
# 配布ビルド (build_macos.zsh) は -DIPLUG_DEPLOY_PLUGINS=OFF を渡すので無関係。
# 戻したいときは -DDX10R_DEPLOY_TO_SYSTEM=OFF。
# --------------------------------------------------------------------------
option(DX10R_DEPLOY_TO_SYSTEM "macOS: deploy dev builds to /Library (system-wide)" ON)
if(APPLE AND DX10R_DEPLOY_TO_SYSTEM)
    if(NOT IPLUG_VST3_DEPLOY_PATH)
        set(IPLUG_VST3_DEPLOY_PATH "/Library/Audio/Plug-Ins/VST3"
            CACHE PATH "Override default VST3 deployment path" FORCE)
    endif()
    if(NOT IPLUG_AU_DEPLOY_PATH)
        set(IPLUG_AU_DEPLOY_PATH "/Library/Audio/Plug-Ins/Components"
            CACHE PATH "Override default AU deployment path" FORCE)
    endif()
    if(NOT IPLUG_CLAP_DEPLOY_PATH)
        set(IPLUG_CLAP_DEPLOY_PATH "/Library/Audio/Plug-Ins/CLAP"
            CACHE PATH "Override default CLAP deployment path" FORCE)
    endif()
    if(NOT IPLUG_APP_DEPLOY_PATH)
        set(IPLUG_APP_DEPLOY_PATH "/Applications"
            CACHE PATH "Override default APP deployment path" FORCE)
    endif()
endif()

# --------------------------------------------------------------------------
# AAX SDK (optional). iPlug2 は AAX SDK を iPlug2/Dependencies/IPlug/AAX_SDK に
# ハードコードで探す。iPlug2 自身の .gitignore がその配下を覆うので SDK 本体は
# どちらの repo にもコミットされない。
# --------------------------------------------------------------------------
set(AAX_SDK_PATH "${IPLUG2_DIR}/Dependencies/IPlug/AAX_SDK" CACHE PATH "Path to AAX SDK")
if(EXISTS "${AAX_SDK_PATH}/Interfaces/AAX.h")
    message(STATUS "AAX SDK found at: ${AAX_SDK_PATH}")
    set(BUILD_AAX ON)
    # iPlug2's AAX.cmake reads AAX_SDK_DIR and only defaults it when undefined.
    # When the AAX SDK is add_subdirectory()'d, its own CMakeLists overwrites the
    # AAX_SDK_DIR *cache* entry with its binary dir (build/AAX_SDK), so on every
    # RE-configure iPlug2 then looks at build/AAX_SDK, fails, and drops the AAX
    # target. Pin AAX_SDK_DIR to the real source path with FORCE so detection is
    # stable across reconfigures (not just the first clean configure).
    set(AAX_SDK_DIR "${AAX_SDK_PATH}" CACHE PATH "AAX SDK source dir (iPlug2)" FORCE)
else()
    message(STATUS "AAX SDK not found at: ${AAX_SDK_PATH} - AAX format will be disabled")
    set(BUILD_AAX OFF)
endif()

# Plugin definition lives in plugin/CMakeLists.txt and uses iplug_add_plugin.
add_subdirectory(plugin)

# --------------------------------------------------------------------------
# WebUI helper targets (webui/ is created in Phase 2)
# --------------------------------------------------------------------------
add_custom_target(webui_install
    COMMAND npm install
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/webui
    COMMENT "Installing WebUI dependencies..."
)
add_custom_target(webui_dev
    COMMAND npm run dev
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/webui
    COMMENT "Starting WebUI development server..."
)
add_custom_target(webui_build
    COMMAND npm run build
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/webui
    COMMENT "Building WebUI for production..."
)

# --------------------------------------------------------------------------
# Configuration summary
# --------------------------------------------------------------------------
message(STATUS "")
message(STATUS "DX10R configuration:")
message(STATUS "  Version:        ${PROJECT_VERSION}")
message(STATUS "  Build type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard:   ${CMAKE_CXX_STANDARD}")
message(STATUS "  iPlug2 dir:     ${IPLUG2_DIR}")
if(BUILD_AAX)
    message(STATUS "  AAX build:      Enabled (${AAX_SDK_PATH})")
else()
    message(STATUS "  AAX build:      Disabled (AAX SDK not found)")
endif()
message(STATUS "")
