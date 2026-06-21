# DX10R: Debug ビルドで AU を ~/Library へ自動 deploy させないためのヘルパー。
#
# 背景: iPlug2 の deploy (IPLUG_DEPLOY_PLUGINS=ON) は AU を per-user の
# ~/Library/Audio/Plug-Ins/Components へ POST_BUILD でコピーするが、そのコピーは
# CodeSign フェーズ**より前**に走るため未署名のまま配置される。Apple Silicon は
# 未署名 AU をロードできず、さらに per-user ~/Library は system /Library より優先
# されるので、Debug ビルドが .pkg の正規署名版 (/Library) を shadow して壊す。
#
# 配布は build_macos.zsh が -DIPLUG_DEPLOY_PLUGINS=OFF で deploy を止めるので無関係。
# Debug の dev ビルドだけがこの穴を踏む。そこで「deploy された直後に Debug 構成の
# ときだけ ~/Library のコピーを削除する」ことで shadow を防ぐ。
#
# 呼び出し: cmake -DREMOVE=$<CONFIG:Debug> -DTARGET_PATH=<deployed .component> -P <this>
#   REMOVE      "1" のとき (= Debug 構成) だけ削除する。
#   TARGET_PATH 削除対象の deployed bundle 絶対パス。

if(REMOVE STREQUAL "1" AND EXISTS "${TARGET_PATH}")
  message(STATUS "[DX10R] Debug build: removing auto-deployed AU to avoid shadowing /Library release: ${TARGET_PATH}")
  file(REMOVE_RECURSE "${TARGET_PATH}")
endif()
