<!-- awesome-plan project=zedbsd record=ws094-p004 -->

# ws094-p004: 選択・開く・keyboard・配置の保存と Clean Up

Status: uncleared（2026-09-30、main のラップアップで途中で止めた。未完了の確認は下の Resume point）
Disposition: normal
Parent: [WS094](../ws.md)
Queue: main の依頼（2026-09-30、worktree `.claude/worktrees/ws094-desktop`、branch `wt/ws094`、`git merge main -m WIP` の後）

## 範囲と受け入れ

design §4: 選択（click・Ctrl・Shift・rubber band）、double click・Enter で開く（file は WS093 の `fm_open_entry`、folder は新しい Files の窓）、
keyboard（矢印・Ctrl+A・Esc）、配置の保存（`~/.config/keiland/desktop-layout`）と Clean Up。起こした app が token を継がないことを実際に確かめる。

## できたこと（commit 64139a88 まで。build 済み、warning 0）

| file | 内容 |
| --- | --- |
| `userland/desktop/files/desktop-layout.c`（新規） | grid（`fm_desktop_grid`・`fm_desktop_cell_rect`、右上から列ごとに）、配置（`fm_desktop_arrange`: 保存の場所が grid の中で空いていればそこ、他は空いた cell を順に）、layout の file（`fm_desktop_layout_path`・`_read`・`_write`（新しい file に書いて rename）・`_set`（1 項目の場所を保存）・`fm_desktop_clean_up`（file を消す）・`fm_desktop_release`） |
| `userland/desktop/files/ui-desktop.c` | 配置は layout から（`desktop_layout`、size か listing が変わった時に計算し直す、保存の場所は最初に 1 回読む）、選択の見た目（icon の後ろの淡い地、名前の青い pill）、rubber band、入力 `fm_desktop_event`（左 click で選択、Ctrl で追加、Shift で範囲、空いた所から band、double click で開く、Enter で選択を開く（最大 8）、矢印で最も近い項目へ、Ctrl+A・Esc）、`fm_desktop_open_selected`（file は `fm_open_entry(…, 0)`、folder は `fm_apps_spawn` で `/bin/files FOLDER`）、`fm_desktop_item_at` |
| `userland/desktop/files/files.h`・`main.c`・`Makefile` | `struct fm_desktop`（places・saved・band・click）、`app->desk`、宣言、desktop の入力を `fm_desktop_event` へ、終わりに `fm_desktop_release` |

試験: `plan/ws094/tests/host-desktop.c`（grid・配置・保存の読み書き・Clean Up、20 件）、`files-desktop-guest.sh` に手順 `input` を足した。

## 確認（ここまで）

| 確認 | 結果 |
| --- | --- |
| build（`make … BUILD=build/ws094-amd64 build/ws094-amd64/bin/files`） | rc 0、warning 0 |
| style（ui-desktop.c・desktop-layout.c・host-desktop.c） | 0 件 |
| host（`sh plan/ws094/tests/host-desktop.sh`） | PASS（20 件） |
| guest（QEMU、Venus、`files-desktop-guest.sh build/ws094-shots/p004 install show input`） | PASS: click で photo.png の選択（`selected.png`、淡い地と青い pill）、↑ で notes.txt、Enter で利用者の一覧の way（`env > ~/env.txt # %f`）が起き、**起こされた program の環境に `KEILAND_DESKTOP_TOKEN` が無い**（`HOME` はある）、Projects の double click で新しい Files の窓、空いた所からの band で先頭の項目を選ぶ（log）。zdesktop の ERROR 0 |

## 未実施（次に行うこと）

- guest での配置の保存: layout の file を先に置いて、その場所に置かれることの画面と log（host では確認済み）。icon を動かす操作は p006。
- Clean Up の画面からの操作: design では空いた所の context menu（p005）。p004 では `fm_desktop_clean_up` を作り host で確認した。
- `band.png` は band の途中の画面が一つ前の frame（取り込みの遅れ）で、選択の見た目が写っていない。撮り直すこと。
- 回帰: `plan/tools/files/files-open.sh mouse`、probe の `desktop-guest.sh`、`plan/tools/files/host-model.sh`、boot test（このラウンドでは未実施）。
- 実機は未実施。

## Resume point

1. `git merge main -m WIP`。
2. build: `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws094-amd64 build/ws094-amd64/bin/files`（wayland・imageview・textedit は build 済み）。
3. `files-desktop-guest.sh` に手順 `saved`（`/tmp/dhome/.config/keiland/desktop-layout` に `notes.txt<TAB>2<TAB>3` を置いてから compositor を起こし、`ZFILES DESKTOP place name=notes.txt column=2 row=3` と画面）を足して流す。band の画面を撮り直す。
4. 回帰（上の「未実施」）と boot test を流し、cleared にして ws.md を更新する。
