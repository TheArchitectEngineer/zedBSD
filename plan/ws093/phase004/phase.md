<!-- awesome-plan project=zedbsd record=ws093-p004 -->

# ws093-p004: 全文の規約と回帰

Status: cleared（2026-09-29）
Disposition: normal
Parent: [WS093](../ws.md)
Queue: main の依頼（2026-09-29、worktree `.claude/worktrees/ws093-open`、branch `wt/ws093`、`git merge main -m WIP` の後、main a85ea4cc）

## 範囲と受け入れ

WS093 が変えた source（`userland/desktop/files/` の apps.c・files.h・menu.c・ui-context.c・ui-menu.c の差分、`plan/ws093/tests/host-default.c`）を
`plan/coding-style.md` の全文と照合して直し、回帰（host・build・guest・boot）を流す。

## 照合と直したこと

- `python3 plan/tools/style-check.py`（上の 5 file と host-default.c）: 0 件。
- 補いの走査 `python3 plan/tools/imageview/style-extra.py apps.c host-default.c` と、WS093 の差分（`git diff 353e9790 -- userland/desktop/files`、+607 −17 行）の目視。直したこと:
  - `fm_apps_for`（WS093 が利用者の一覧の場所を `apps_user_list` に分けた関数）の `(void)path;` を `UNUSED_PARAMETER(path);`（宣言の後、段落の前）に。
    apps.c に `UNUSED_PARAMETER` を定義（Files の header には無い）。
  - `menu_always`・`menu_system_default`: guard の直後の代入を段落に分けた（listing の item の guard と folder の guard）。
  - `ui-context.c`: 利用者の way の loop の後の区切りと Use System Default を注釈付きの段落に。
  - `apps_rewrite`・`apps_make_folders`: guard の後の段落と注釈。
- 残り（範囲外）: 補いの走査の 2 件は WS071 のままの `apps_parse_line`（3 節の条件を 1 行に置く）。WS093 は変えていない。
- 他の file の差分: `plan/tools/files/host-build.sh`（p003、libkeiland の 3 file）・`host-model.sh`（p003、新規）は shell。問題なし。

## 確認

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| style | 上のとおり、`git diff --check` | 0 件 |
| host | `sh plan/ws093/tests/host-default.sh`、`sh plan/tools/files/host-model.sh` | 両方 PASS（files-model は main a85ea4cc の PNG の期待の修正で FAIL 無し） |
| build | `sh build/ws093-build.sh`（files・imageview・textedit・wayland、config-amd64、`BUILD=build/ws093-amd64`） | 4 つとも rc 0、warning 0 |
| guest（QEMU、Venus） | main の `build/ws035-sq/hdd-image.img` の複写で `plan/ws093/tests/open-guest.sh build/ws093-shots/p004 mouse`・`always`・`info` | 3 つとも PASS（全ての種類の double click と Enter、Always Open With と Use System Default、情報の card の way の並び）、zdesktop の ERROR 0 |
| guest（QEMU、注入の touch） | main の `build/main-pen/hdd-image.img` の複写で `open-guest.sh … touch` | PASS（png と txt の double tap） |
| boot | `plan/tools/boot-test.sh build/ws093-run/disk.img` | PASS（`build/ws093-shots/p004/boot-login.png`） |

- 判定は log（SSH）・ps・画面。console・serial は読んでいない。使い終えた disk の複写は削除した。

## 未実施・制限

- 実機（i915、touch panel）は未実施。確認はすべて QEMU。

## Resume point

完了。WS093 の完了の処理（Phase の directory の削除、試験の `plan/tools/` への移動）は main。
