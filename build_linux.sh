#!/usr/bin/env bash

# DX10R Linux Release Build Script — best-effort / NOT YET SUPPORTED.
#
# DX10R is built on iPlug2, whose Linux backend is upstream-incomplete at the
# pinned submodule revision:
#
#   - iPlug2/IPlug/IPlug_include_in_plug_hdr.h
#       #elif defined OS_LINUX  →  //TODO:   (BUNDLE_ID / APP_GROUP_ID undefined)
#   - iPlug2/IPlug/IPlugTimer.h
#       #else  →  #error NOT IMPLEMENTED      (no Linux Timer_impl)
#   - WebView: no webkit2gtk binding wired for the WebViewEditorDelegate.
#
# A working Linux build requires patching iPlug2 itself (POSIX Timer, BUNDLE_ID,
# webkit2gtk WebView). DX10R's primary scope is Windows + macOS (see AGENTS.md);
# Linux is a later attempt. This stub gives a clear message rather than a
# confusing C++ compilation failure.

set -e
cyan="\033[36m"; yellow="\033[33m"; reset="\033[0m"
cat <<EOF
${cyan}============================================${reset}
${cyan}   DX10R Linux build is NOT YET SUPPORTED${reset}
${cyan}============================================${reset}

${yellow}iPlug2's Linux backend (Timer / BUNDLE_ID / webkit2gtk WebView) is
upstream-incomplete for OS_LINUX.${reset}

Supported now: Windows (build_windows.ps1) and macOS (build_macos.zsh).
Linux is planned as a later best-effort once the iPlug2 Linux + WebView
gaps are addressed (patch upstream, or a webkit2gtk binding).
EOF
exit 1
