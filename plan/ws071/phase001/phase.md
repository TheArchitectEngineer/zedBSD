<!-- awesome-plan project=zedbsd record=ws071p001 -->

# ws071-p001: 設計（範囲、8 つの決定、画面と部品、file 操作の model、試験、Phase の分割）

Phase ID: `ws071-p001`
Parent: [WS071](../ws.md)
Status: cleared（2026-09-27）
Phase disposition: normal
Queue: なし（2026-09-27 ユーザーの指示「最大7つのサブエージェントを併走させて、WSをcompleteさせていってください」でサブエージェントが
worktree の branch で実行。main の Queue への反映は統合する main の session）

## 範囲

[spec.md](../spec.md) を元に、program の名前と置き場所、描画の構成、画面の構成、model（場所・履歴・選択・task・ゴミ箱・undo・
clipboard）、保存先（タグ・recent・sidebar）、開く・関連付け、検索、プレビュー・Quick Look・情報、System Menu と context menu
（WS070 protocol の version 2）、サムネイルの decoder、DnD、装置、試験、Phase の分割を決める。ws.md の「p001 で決めること」8 点は
戻せる既定を選び、理由と共に記録する。コードは変えない。

## 受け入れ

1. [design.md](../design.md) に上の全部があり、仕様案と違える所に理由がある。
2. 8 点（と設計中に出た判断）が design.md §15「判断が要る点（既定で進めた）」に既定と理由と共にある。
3. 実装の Phase（p002〜p011）が ws.md の表にあり、各 Phase が Venus の QEMU で試験できる。
4. worktree で build ができる（sysroot、既存の client の build）。

## 結果（2026-09-27）

cleared。

- [design.md](../design.md): program（§1: `zdesktop-files`、1 process 1 窓とタブ）、描画（§2: CPU の canvas を Vulkan で貼る。host で
  画面を PNG にして試験できることが主な理由）、画面（§3、dashboard §3.1、icon・list §3.2）、部品（§4）、model（§5: 場所と履歴、
  一覧、協調の task、freedesktop の Trash、undo、file の clipboard）、保存先（§6: タグは xattr `user.zdesktop.tags`、recent は
  libzdesktop の新しい API）、開く（§7）、検索（§8）、preview・Quick Look・Info（§9）、menubar と context menu（§10、protocol
  version 2 の確定）、PNG（§11: WS035 の D2〜D4 に従い libz-compat・libpng-compat の decode を作る）、DnD（§12: 窓の中だけ）、
  mount（§13）、試験（§14）、判断が要る点 15 項目（§15）、Phase の割り当て（§16）、共有 file（§17）。
- 調べた事実: zdesktop の globals は wl_compositor・xdg_wm_base・zed_gpu_buffer_v1・wl_output・wl_seat（pointer・axis・frame あり）・
  wl_shm・xdg_menu_manager_v1（version 1）で、`wl_data_device`・xdg_popup・subsurface は無い。GPU buffer の窓は不透明で合成され、
  角丸と影は zdesktop が付ける。UFS・tmpfs は xattr を持ち、libc に getxattr 等がある。`setmntent(MOUNTED)` は kernel に mount を
  聞く。PNG の decoder は userland に無い（WS035 p040・p041 は planning）。Inter に日本語は無く、host に TrueType の
  Droid Sans Fallback（Apache-2.0）がある。zdesktop-terminal は `--command=` で command を走らせられる。libc に SHA-256
  （`sha2.h`）・`flock`・`posix_spawn` がある。
- worktree の準備: `build/llvm`・`ws035-fonts`・`ws035-wallpaper`・`NoctLang` を main の tree への symlink にし、`make sysroot-amd64`
  （2 分半、llvm の source は自分の `build/llvm-source` に展開された。main の build には書いていない）。
  `make ZEDBSD_CONFIG=plan/ws070/tests/config-amd64-menu.mk BUILD=build/amd64 build/amd64/bin/zdesktop-terminal` が warning 0 で通った。
- 設計の敵対的レビュー（design-reviewer のサブエージェント）は**未実施**: AGENTS.md はサブエージェントを禁じ、2026-09-27 の例外は
  main session が WS ごとに起動するものに限る。自分で §15 の項目と §17 の衝突を見直した。
