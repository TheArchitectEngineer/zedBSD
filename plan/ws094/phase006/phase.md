<!-- awesome-plan project=zedbsd record=ws094-p006 -->

# ws094-p006: drag（desktop の中・folder へ・Files の窓との DnD）と touch

Status: cleared（2026-09-30、QEMU の Venus。実機は未実施。「まず動く」の範囲で区切った（main の方針 2026-09-30））
Disposition: normal
Parent: [WS094](../ws.md)
Queue: main の依頼（2026-09-30、worktree `.claude/worktrees/ws094-desktop`、branch `wt/ws094`、`git merge main -m WIP` の後）

## 範囲と受け入れ

design §4 の drag と touch を扱う。

- 選んだ icon を desktop の中で動かす。cell へ吸着し、場所を保存する。
- folder の icon の上に落とすと、その folder へ移す。
- Files の窓との DnD（text/uri-list、move・copy）: desktop から窓へ、窓から desktop へ。窓から落とした項目は、落とした所の cell に置く。
- touch: tap は click、double tap は開く、long press は context menu、long press の後の drag は icon の移動。

受け入れ: QEMU の Venus で、pointer と注入の touch の操作が上のとおりに働くこと（log・file・画面）。

## 設計

desktop surface は全ての窓の下にあるので、pointer が desktop の上にあるのか窓の上にあるのかは compositor にしか分からない。
そのため desktop の drag は、動き始めた時から compositor の drag and drop（`wl_data_device.start_drag`、`dnd.c` の `fm_dnd_start`）にする。
desktop の上で落ちたものは、desktop 自身への drop として戻ってくる。

| file | 内容 |
| --- | --- |
| `ui-desktop-drag.c`（新規） | `fm_desktop_drag_press`・`_motion`・`_release`: 項目の上の左 press を覚え、6 px 動いたら選択を DnD にする（`FM_REQUEST_DRAG_OUT`、`drag_outside`）。`fm_desktop_drop_event`: desktop の上の DnD の enter・motion・leave・drop・action と、自分の drag の終わり。的は、drag していない folder の項目、それ以外は desktop の folder と、その下の cell。的が変わったら compositor に答える。`fm_desktop_drop_draw`: folder の項目は地と輪、cell は縁を描く。`fm_desktop_drop_place`: desktop 自身の項目を desktop に落としたときは、file を動かさず cell を移す。押した項目は落とした cell へ、他の選択された項目は押した項目からの相対の位置へ移す。grid の外や他の項目の cell になる項目は、元の場所に残す。場所は保存する。`fm_desktop_dropped`: 他の窓から `~/Desktop` へ落ちた項目の名前に、落とした cell から空いた cell を順に保存する（操作が作った項目がそこに現れる） |
| `ui-desktop.c` | DnD の event を `fm_desktop_drop_event` へ渡す。項目の press は drag の候補にする。選択済みの項目の素の click は、release まで選択を保つ（複数の選択を drag できる）。drag 中の項目は淡く描く。drop の的を描く |
| `desktop-layout.c` | `fm_desktop_cell_at`（点の cell） |
| `main.c` | `main_dispatch`（desktop か窓か）。`main_drop` では、desktop の自分の項目の配置を先に扱って DnD を終える。他の窓から落ちた項目の場所も決める。drag の始まりの失敗と touch の pointer も `main_dispatch` を通す。desktop の touch は全面で gesture にする（`FM_TOUCH_CONTENT`、scroll は無い） |
| `files.h` | `struct fm_desktop` の drag の field、宣言 |

## 確認

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| build | `make … BUILD=build/ws094-amd64 build/ws094-amd64/bin/files …/bin/wayland …/dynamic/libkeiland.so` | rc 0、warning 0 |
| style | `plan/tools/style-check.py`（ui-desktop-drag.c・ui-desktop.c・desktop-layout.c・host-desktop.c・main.c）、`git diff --check` | 0 件（main.c の既存の 2 件を除く） |
| host | `host-desktop.sh`（p006 の分を足した: 点の cell、自分の項目の cell への移動と保存、他の項目の cell では動かない、他の窓から落ちた 2 項目の空いた cell） | PASS。`host-model.sh` も PASS |
| guest: drag（新しい手順、base image） | `files-desktop-guest.sh build/ws094-shots/p006-final install show drag` | PASS（下） |
| guest: touch（新しい手順、pen image `build/main-pen/hdd-image.img` の複写） | `files-desktop-guest.sh build/ws094-shots/p006 install show touch` | PASS（下） |
| 回帰: p003・p004・p005 の手順 | `… install show watch input saved`、`… install show menu` | PASS、PASS |
| 回帰: Files | `BIN=build/ws094-amd64 files-open.sh … mouse` | PASS |
| 回帰: probe | `desktop-guest.sh … install role refuse input home-drag dnd restart` | 2 回とも restart の手順だけ FAIL（下の注）。他の手順は PASS |
| boot test | `plan/tools/boot-test.sh build/ws094-run/disk.img` | PASS（`build/ws094-boot-test/login.png`） |

guest の drag の手順で確かめたこと（log・file・画面。console・serial は読んでいない）:

- notes.txt を (900,400) へ drag した。`DESKTOP drag start`、`drop enter self=1 files=1`、`drop place column=3 row=3`、`move … column=3 row=3 error=0` の順に出て、配置は 3,3 になった。drag 中は的の cell の縁と compositor の badge が出て、元の icon は淡くなる（`drag-over.png`、`drag-moved.png`）。
- photo.png を folder の Projects へ drag した。`drop target=…/Projects`、`DROP operation=move … destination=…/Projects` が出て、file は `Projects/photo.png` へ移った。
- Files の窓（`~/Docs`、list 表示）の note.txt を desktop の (60,600) へ drag した。`drop enter self=0 files=1`、`drop self=0 destination=/tmp/dhome/Desktop … column=12 row=5`、`dropped name=note.txt column=12 row=5` が出て、file は `~/Desktop/note.txt` へ移り、12,5 に置かれた（`drop-in.png`）。
- desktop の report.pdf を Files の窓へ drag した。Files の窓に `DROP operation=move … destination=/tmp/dhome/Docs` が出て、file は `~/Docs/report.pdf` へ移った（`drop-out.png`）。

touch の手順（`/bin/touchinject`）で確かめたこと:

- Projects を double tap すると、新しい Files の窓で開いた。
- report.pdf を long press すると、context menu が押した所に開いた（`touch-menu.png`）。
- script.sh を long press してから動かすと、`TOUCH hold`・`DESKTOP drag start`・`move name=script.sh column=4 row=4` が出て、4,4 に置かれた（`touch-moved.png`）。

## 見つけたこと・制限

- 注（probe の restart）: `desktop-guest.sh` の restart の手順で、試験用の probe が `--timeout-s=3` の後に終わらないことがある。
  - log では `role` と `ack` の後に `gone` も `exited` も出ず、起動の上限の行が出ない。
  - p002 で 1 回観察した。p006 では 2 回続けて出た。単独で 5 回起こすと、2 回は同じく止まり、3 回は再現しなかった。
  - p006 は compositor を変えていない（p005 で C9 を含めて確認済みの compositor のまま）。そのため p006 の変更とは無関係と判断した。
  - 原因（probe の loop・libwayland-client・libc・kernel の poll や time）は未調査。main に報告し、bug の起票は main に委ねる。
- 「まず動く」の範囲で区切った（main の方針 2026-09-30）。次のものは後の段（ws.md の L3〜L5）または Future とする。
  - Ctrl を押した drag の copy（今は compositor の action のまま）。
  - drag の icon を desktop 自身で描くこと（今は compositor の badge）。
  - 落とした項目の名前がぶつかって変わったときの配置（名前が変わると、空いた cell に入る）。
- 未実施: 実機。

## Resume point

2026-09-30: cleared。WS094 は L1・L2 まで済み。次の段は ws.md の「段」（L3 の p008 から。main が順を決める）。
