<!-- awesome-plan project=zedbsd record=ws094-p008 -->

# ws094-p008: L3a 100 項目の計測の道具と基準値（計測だけ）

Status: cleared（2026-09-30、QEMU の Venus。実機は未実施。計測だけの Phase で、目標に届かない値は p009 で直す）
Disposition: normal
Parent: [WS094](../ws.md)
Queue: main（Q1）の依頼（2026-09-30、P4、worktree `.claude/worktrees/ws090-widgets`、branch `wt/ws090`、main を merge した後）

## 範囲と受け入れ

WS094 の段 L3（ws.md の「段（L1〜L5）」）の計測の手順を作り、100 項目の `~/Desktop` で 3 つの数値の基準値を出す。直さない。

- (a) `files --desktop` の起動から `DESKTOP ready` まで（目標 1500 ms）
- (b) file を 1 つ足してから表示まで（目標 2500 ms）
- (c) click から選択の frame まで（目標 50 ms）、`SLOW-FRAME` の行が 0
- 3 回の中央値。判定は log の時刻だけで、画面は確かめの 1 枚。

受け入れ: 手順 `perf100` が 3 回の値と中央値を出し、基準値を記録すること。

## 変えたもの

| file | 変更 |
| --- | --- |
| `userland/desktop/wayland/desktop.c` | `ZWL DESKTOP start` の行に `at_ms=`（zdesktop の CLOCK_MONOTONIC の ms） |
| `userland/desktop/files/ui-desktop.c` | `DESKTOP ready` の行に `at_ms=`（同じ時計）と `newest_age_ms=`（その描画の時点で、最も新しい項目の mtime からの経過。実時間の時計）。項目の選択の時刻を `select_ms` に残す |
| `userland/desktop/files/files.h` | `struct fm_desktop` の `select_ms` |
| `userland/desktop/files/main.c` | 選択の後の最初の present で `DESKTOP select-frame ms=`（選択の時刻から present の後まで） |
| `plan/ws094/tests/files-desktop-guest.sh` | 手順 `perf100`。`install` は先に compositor を止める（graphical な image の greeter の compositor が `/bin/wayland` を開いたままで、put が失敗した） |

`perf100` の中身: `/tmp/dhome100/Desktop` に画像 20（make-images.py の有効な 6 枚の複写。png・jpg・gif）、folder 10、text 70 を作る。
3 回、compositor を立て直して、(a) `ready items=100` の at_ms − `ZWL DESKTOP start` の at_ms、(b) `added-N.txt` を作って
`ready items=101` の `newest_age_ms`、(c) folder-1〜3 の 3 回の click の `select-frame ms`、`SLOW-FRAME` の行数を集める。
中央値（(c) は 9 回の click の中央値）を目標と比べて `RESULT` と `L3 ...` の行に出す（`perf100.txt`、`perf100.png`）。
ws.md は `plan/tools/files/make-home.sh` を広げる案だったが、guest の中で作るほうが put が 6 回で済むので、手順の中で作った。

## 確認

| 確認 | 命令 | 結果 |
| --- | --- | --- |
| build | `make -j16 ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk BUILD=build/amd64 build/amd64/bin/files build/amd64/bin/wayland build/amd64/dynamic/libkeiland.so` | rc 0、warning 0 |
| style | `plan/tools/style-check.py`（ui-desktop.c・files.h・main.c・wayland/desktop.c）、`git diff --check` | 0 件（main.c の既存の 2 件、187 行・573 行を除く） |
| guest（1 回目） | `BIN=build/amd64 files-desktop-guest.sh build/ws094-shots/p008 install perf100`（image は `build/ws081/demo-win-venus.img` の複写） | put wayland が FAILED（greeter の compositor が使用中）。古い wayland のまま走り (a) が -1。install を直した |
| guest（2 回目） | 同じ | PASS。下の値 |
| boot test | `OUTPUT=build/ws094-p008-boot plan/tools/boot-test.sh build/ws094-run/disk.img` | PASS（`build/ws094-p008-boot/login.png`） |

## 基準値（QEMU の Venus、Lavapipe。2026-09-30）

| 回 | (a) start→ready | (b) 追加→表示 | (c) click→frame（3 回） | SLOW-FRAME |
| --- | --- | --- | --- | --- |
| 1 | 2854 ms | 1094 ms | 146・72・95 ms | 0 |
| 2 | 2908 ms | 1260 ms | 144・143・95 ms | 0 |
| 3 | 2946 ms | 799 ms | 137・79・69 ms | 0 |
| **中央値** | **2908 ms（目標 1500、超過）** | **1094 ms（目標 2500、以内）** | **95 ms（目標 50、超過）** | **0（目標 0、以内）** |

`SLOW-FRAME` の閾値は 250 ms（`MAIN_SLOW_FRAME_MS`）なので、(c) の 69〜146 ms の frame は数えられていない。

## 見つけたこと（p009 への材料）

- **(a) が目標の約 2 倍。** log の行に時刻が無いので内訳は未計測。p009 は、exec・接続・configure・listing・thumbnail の読み・最初の描画の
  各時点に at_ms を足して内訳を取り、大きい所から直す。最初の描画の前に 20 枚の thumbnail を同期で読んでいる可能性がある
  （`ZFILES THUMB` の行は `READY` の直後に並ぶ）。
- **(c) の 69〜146 ms は、選択の 1 回で desktop 全面（100 項目）を描き直している**のが疑わしい。p009 は、描き直しの範囲か、描いた結果の cache を試す。
  Lavapipe の上の値なので、実機の値（L5）とは別に扱う。
- **100 項目のうち 91 しか画面に出ない**（`ready items=100 cells=91`）。1280×766 の desktop の cell は 13 列 × 7 行で、残りの 9 項目
  （picture-12〜20 など）は表示されない。画面にも無い（`perf100.png`）。L3 の数値の目標ではないが、多い項目の扱い（あふれの表示・縮小）は
  design の判断が要る。main へ報告する。
- jpg・gif の thumbnail は `error=21` で作られず、汎用の icon で描かれる（png は作られる）。p009 の範囲外。imageview の読み手の対応の問題として報告する。

## 未実施

- 実機（5330）での計測（L5、p012）。
- (a)(c) の内訳の計測（p009）。
