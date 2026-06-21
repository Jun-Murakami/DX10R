# ApplyIPlug2Patch.cmake
# ------------------------------------------------------------------------------
# iPlug2 (submodule) への修正パッチを configure のたびに冪等適用する。
#
# なぜパッチ方式か:
#   submodule を直接編集すると `git submodule update --init` (別マシン) や上流 pull で
#   消える。そこで修正は repo 管理下の patches/ に置き、ビルド時に当て直す。
#   (AGENTS: 「submodule に改造を入れない。回避は plugin 側 override / CMake で吸収」)
#
# 方式:
#   - 外側リポジトリの git で `git apply --directory=iPlug2 -p1` する。submodule の
#     .git は絶対パス gitdir を指すため `git -C iPlug2` は Docker/別マシンで失敗しうる。
#     外側 git なら host/container どちらでも動く。
#   - 冪等性: `--reverse --check` が成功すれば適用済み → skip。未適用のときだけ apply。
#     当たらない (上流が該当箇所を変更し陳腐化した) パッチはツリーを汚さず WARNING。
#     ※ パッチ内容を更新したのに旧版が当たったままのときは自動 revert されない。
#       その場合は `git -C iPlug2 checkout -- .` で pristine 化してから再 configure。
#
# 適用パッチ (IPLUG2_PATCH_NAMES の順):
#   1) iplug2-webview-autofill.patch : WebView2 の autofill を無効化。DAW 埋め込み時、
#      オートフィル・ドロップダウン表示中の Enter 確定でホスト⇔WebView2 のフォーカス
#      往復が起き DAW ごとフリーズする問題を回避する。
#   2) iplug2-webview-context-menu.patch : macOS WKWebView の native context menu を
#      DevTools 有効時だけ許可する。Release で残る「再読み込み」を実行すると
#      in-memory ロードした WebUI が失われ白画面になるため、配布版では menu を空にする。
#
# 使い方: include(${IPLUG2_DIR}/iPlug2.cmake) の「前」で include すること。
# ------------------------------------------------------------------------------

set(IPLUG2_DIR_NAME "iPlug2")
set(_iplug2_patch_dir "${CMAKE_CURRENT_SOURCE_DIR}/${IPLUG2_DIR_NAME}")
set(IPLUG2_PATCH_NAMES
    "iplug2-webview-autofill.patch"
    "iplug2-webview-context-menu.patch"
)

# 1) 親 ../patches にマスターがあればローカル patches/ へ上書き同期 (複数リポ単一ソース)。
file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/patches")
foreach(_patch IN LISTS IPLUG2_PATCH_NAMES)
    set(_master "${CMAKE_CURRENT_SOURCE_DIR}/../patches/${_patch}")
    if(EXISTS "${_master}")
        configure_file("${_master}" "${CMAKE_CURRENT_SOURCE_DIR}/patches/${_patch}" COPYONLY)
        message(STATUS "iplug2 patch: synced from master (${_patch})")
    endif()
endforeach()

# 2) 外側リポジトリの git で submodule 配下へ apply (冪等: reverse-check で適用済みを skip)。
if(IS_DIRECTORY "${_iplug2_patch_dir}")
    find_package(Git QUIET)
    if(Git_FOUND)
        foreach(_patch IN LISTS IPLUG2_PATCH_NAMES)
            set(_local "${CMAKE_CURRENT_SOURCE_DIR}/patches/${_patch}")
            if(EXISTS "${_local}")
                # 適用済みか？ (逆パッチが綺麗に当たれば適用済み)
                execute_process(
                    COMMAND "${GIT_EXECUTABLE}" apply --directory=${IPLUG2_DIR_NAME} -p1 --reverse --check "${_local}"
                    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                    RESULT_VARIABLE _rev ERROR_QUIET OUTPUT_QUIET)
                if(_rev EQUAL 0)
                    message(STATUS "iplug2 patch: already applied ${_patch}")
                else()
                    execute_process(
                        COMMAND "${GIT_EXECUTABLE}" apply --directory=${IPLUG2_DIR_NAME} -p1 "${_local}"
                        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                        RESULT_VARIABLE _app ERROR_VARIABLE _err)
                    if(_app EQUAL 0)
                        message(STATUS "iplug2 patch: applied ${_patch}")
                    else()
                        message(WARNING "iplug2 patch: FAILED to apply ${_patch}\n${_err}")
                    endif()
                endif()
            endif()
        endforeach()
    else()
        message(WARNING "iplug2 patch: Git not found; skipped (要手動 git apply)")
    endif()
endif()
