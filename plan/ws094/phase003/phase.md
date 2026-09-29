<!-- awesome-plan project=zedbsd record=ws094-p003 -->

# ws094-p003: libkeiland の client の API と Files の `--desktop` の骨組み

Status: cleared（2026-09-30）
Disposition: normal
Parent: [WS094](../ws.md)
Queue: main の依頼（2026-09-30、worktree `.claude/worktrees/ws094-desktop`、branch `wt/ws094`）。
Approval: libkeiland への追加（`desktop.c` と header）は main の許可。KEILAND_VERSION は main の最新（13）の次の 14。

## 範囲と受け入れ

[design.md](../design.md) §3・§4・§7: libkeiland の client の API（`keiland_desktop_*`）と、Files の `--desktop`（desktop surface を取り、
`~/Desktop` の icon を右上から描く、監視）。入力は p004。受け入れ: QEMU の Venus で compositor が起こした `files --desktop` が icon を並べ、
file の増減に追従する画面と log、host の試験、build の warning 0、回帰、boot test。

## 変えたこと

| file | 内容 |
| --- | --- |
| `userland/desktop/libkeiland/desktop.c`（新規） | `keiland_desktop_create(display, surface, token, listener, data)`・`keiland_desktop_ack`・`keiland_desktop_destroy`。protocol の interface（`keiland_desktop_manager_v1`・`keiland_desktop_surface_v1`）はこの file に置く（libwayland の生成済みの code は変えない）。manager の検索は glass.c と同じ private queue の registry |
| `include/libc/keiland.h` | 宣言と `struct keiland_desktop_listener`、**KEILAND_VERSION 14** |
| `userland/desktop/libkeiland/exports.map`・`Makefile` | `keiland_desktop_*`、`desktop.c` |
| `userland/desktop/files/window.c`・`window.h` | `fm_window_open_desktop(window, display, token)`（xdg の role の代わりに desktop の role、configure で大きさを取り ack）、close で desktop を消す |
| `userland/desktop/files/main.c` | `--desktop`: `KEILAND_DESKTOP_TOKEN` を**複写してから** `unsetenv`、`~/Desktop`（無ければ作る）を開く、glass・menu・titlebar を開かない（窓の分は `main_open_decorations` に分けた）、入力は捨てる（p004）、描画は `fm_desktop_draw` |
| `userland/desktop/files/ui-desktop.c`（新規） | `fm_desktop_draw`（透明の地、96×104 の cell、64 px の icon（画像は thumbnail）、名前は slate の文字に白い縁取り、並びが変わると `ZFILES DESKTOP place …`・`ready items=…` を log）、`fm_desktop_cell`（右上から下へ、列が埋まれば左の列） |
| `userland/desktop/files/files.h`・`Makefile` | `app->desktop`・`desktop_logged`、宣言、`ui-desktop.c` |
| `userland/desktop/wayland/desktop.c` | 起動の既定を「起こす」に戻した（`--session` は `/etc/keiland/desktop` が `off` でなければ `/bin/files --desktop`。main の判断のとおり、Files の `--desktop` ができたので） |

- 監視は Files の今の 2 秒ごとの mtime の確認（`fm_ui_tick`）をそのまま使う。
- 名前は 1 行（cell の幅で省略）。2 行と中の省略は後の Phase。

試験（`plan/ws094/tests/`）: `host-desktop.c`・`host-desktop.sh`（cell の計算）、`files-desktop-guest.sh`（show・watch・window）。

## 確認

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| build | `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws094-amd64 build/ws094-amd64/bin/{wayland,files,imageview,textedit} build/ws094-amd64/dynamic/libkeiland.so …` | rc 0、warning 0 |
| style | `python3 plan/tools/style-check.py`（libkeiland/desktop.c・files/ui-desktop.c・wayland/desktop.c・host-desktop.c）、`git diff --check` | 0 件 |
| host | `sh plan/ws094/tests/host-desktop.sh`、`sh plan/tools/files/host-model.sh` | PASS（cell 6 件、files-model） |
| guest（QEMU、Venus） | main の `build/ws035-sq/hdd-image.img` の複写で `sh plan/ws094/tests/files-desktop-guest.sh build/ws094-shots/p003 install show watch window` | PASS: compositor が `--desktop-client='/bin/files --desktop'` で Files を起こし（token は環境）、role（0,34 1280×766）、Files の configure、5 項目が右上から（1 番目 1168,16、5 番目 1168,432）、`~/Desktop` に file を足すと数秒で 6 項目、消すと 5 項目、Files の窓は icon の上。画面 `build/ws094-shots/p003/`（desktop・added・window） |
| 回帰（Files） | `BIN=build/ws094-amd64 sh plan/tools/files/files-open.sh … mouse`（WS093 の起動の試験、main.c の窓の分を関数に分けた回帰） | PASS |
| 回帰（compositor） | `sh plan/ws094/tests/desktop-guest.sh … install role refuse input restart`（既定を戻した後） | PASS |
| boot | `plan/tools/boot-test.sh build/ws094-run/disk.img` | PASS（`build/ws094-shots/p003/boot-login.png`） |

- 判定は zdesktop の log（Files は compositor が起こすので同じ log）と画面。console・serial は読んでいない。

## 未実施・制限

- `--session`（sessiond の下）での既定の起動は未実施（`--desktop-client` で同じ経路を確かめた）。image の Files は main の image の build まで古い。
- desktop の Files から起こす app が token を継がないこと（`unsetenv`）は code の読みだけ（app の起動は p004 から）。
- 入力（選択・開く・menu・drag）は p004 以降。実機は未実施。

## Resume point

p004（選択・開く・keyboard・配置の保存と Clean Up）から。
