<!-- awesome-plan project=zedbsd record=ws099-p002 -->

# ws099-p002: C5 App Home と Wiseview の開閉の最初の frame を早める

Status: HW_STATUS
Disposition: normal
Parent: [WS099](../ws.md)
Queue: なし（2026-09-30 Q1 の割り当て）
依存: p001（cleared）

## 範囲と受け入れ

- C5: App Home と Wiseview の開閉で、要求から最初の frame まで 100 ms 以内（窓 10 個）。合否は 5330 の実機で決める（WS094 と同じ扱い）。
  QEMU の値（`c5-transitions.sh`、前は 102〜215 ms）は参考で、QEMU でも縮むことを確かめる。
- まず log で要求から submit までの内訳を取る。直しの見当は、要求を受けた pass で frame を出す（frame pacing を待たない）ことと、最初の frame の準備を先に済ませること。
- 触る所は home.c と shell.c の Wiseview の周り。P1 の backdrop・glass と、P3 の keyboard.c は触らない（触っていない）。
- 回帰: C9、WS079-p010、C3（c3-swipe-back）、boot test。

## 内訳（QEMU の Venus、変更前、窓 10 個）

一時的に計測の印を足した build（`build/ws099/p002-c5-diag2/zdesktop.log`、印は後で消した）で、要求から最初の frame の submit までを分けた。

| 部分 | 時間 |
| --- | --- |
| 要求から frame の描き始め | 0 ms（要求を受けた pass で描き始めていた。frame pacing の待ちも、前の frame の fence の待ちも無かった） |
| `vkAcquireNextImageKHR` | 0〜1 ms |
| `vkResetCommandBuffer` | 10 ms |
| `vkBeginCommandBuffer` | 10 ms |
| App Home・Wiseview・窓・system bar の記録（CPU） | 1〜5 ms |
| `vkEndCommandBuffer` | 10 ms（App Home の最初の開きだけ 116 ms） |
| `vkQueueSubmit` | 20 ms |
| `vkQueuePresentKHR` | 50 ms |

- QEMU の約 100 ms は、Venus の API を呼ぶたびの往復（10 ms 単位）がほとんどで、App Home と Wiseview の中身の費用は 1〜5 ms。
- App Home の最初の開きだけ、`vkEndCommandBuffer` が 116 ms かかる。App Home を描くのが初めてのため、ホスト側（Venus・lavapipe）で一度だけ掛かる費用と見ている。
  Wiseview を先に開いても起き、2 回目からは起きない。アプリの一覧を先に読んでも変わらなかった（下）ので、zdesktop の CPU の仕事ではない。原因の特定は調べる上限に達したので止めた。
- 実機（i915、Venus を通らない）では API の往復は μs の桁の見込み。ただし実機では、アニメーションする app（Gears・Model viewer）が frame pacing の待ち（最大 50 ms）を起こし得る。

## 変更（`userland/desktop/wayland/`）

- `display.c`・`zwl.h`: `zwl_transition_request` を足した。App Home・Wiseview の開閉の要求で次の frame を frame pacing の待ち無しに描き、
  その submit までの時間を `ZWL FIRST_FRAME what=home-open|home-close|wiseview-open|wiseview-close frame= request_ms= ms=` として 1 回出す。
  この行は `--log-frames` 無しでも出るので、実機の session.log で測れる。
- `home.c`: `home_open`・`home_close` で要求を知らせる。アプリの一覧（`/etc/keiland/apps.conf` と各 program の有無の確認）を、
  最初の開きの frame の中ではなく、出力が出た後の `zwl_home_tick` で先に読む。
- `shell.c`: `wiseview_open_key`・`wiseview_close_key` で要求を知らせる。
- `compose.c`: `vkResetCommandBuffer` を呼ぶのをやめた。command pool に `RESET_COMMAND_BUFFER_BIT` があるので、`vkBeginCommandBuffer` が暗黙に reset する（Venus で往復が 1 回減る）。
  per-frame の行（`--log-frames` の時だけ）に `ZWL LAT draw|acquired|recorded` を足した。
- 注: `compose.c`・`display.c` は依頼の「home.c・shell.c の Wiseview の周り」の外だが、backdrop・glass・keyboard の部分は触っていない。

## 試験（`plan/ws099/tests/`）

- `c5-hw.sh IMAGE OUTDIR [ROUNDS]`（新規）: WS075 の `hdmi-h4-hw.sh` で 5330 の passthrough を使う。App Home の 10 個の app を開いた後、Wiseview（Super+Tab、Esc）と
  App Home（launcher、Esc）を ROUNDS 回開閉する。Terminal で session.log を disk に写して `ZWL FIRST_FRAME` を読み、全てが `LIMIT_MS`（100）以内なら PASS。

## 計測（QEMU の Venus、1280x800、窓 10 個、開閉 12 回ずつ、`c5-transitions.sh`）

| image | 最初の frame まで（App Home の最初の開き以外） | App Home の最初の開き | C5 の結果 |
| --- | --- | --- | --- |
| 前 1 回目 `build/ws099/p002-c5-before` | 102〜105 ms | 215 ms | 0/12 |
| 前 2 回目 `build/ws099/p002-regress/c5-before2` | 102〜105 ms | 214 ms | 0/12 |
| 後（reset の削除と pacing の免除）`p002-c5-after1` | 93〜95 ms | 204 ms | 10/12（1 回は frame の間隔 239 ms: 2 枚目の acquire が 136 ms 待った） |
| 後（一覧の先読みも）`p002-c5-after2` | 92〜95 ms | 202 ms | 11/12 |
| 後 2 回目 `p002-regress/c5-after3` | 90〜95 ms | 201 ms | 11/12 |

- QEMU でも約 10 ms 縮み、App Home の最初の開き以外は 100 ms を切った。`ZWL FIRST_FRAME` の値は `c5-parse.py` の値と同じだった。

## 実機（5330）

HW_RESULT

## 回帰

| 確認 | 結果 |
| --- | --- |
| build: criteria の image `build/ws099/p002-after2.img`、passthrough の demo `build/ws099/p002-pt.img`、WS079 の demo `build/ws099/p002-demo.img` | どれも exit 0、desktop の warning 0（noct の既存の 1 件は範囲外） |
| `criteria.sh` C3（p138・c3-swipe-back）・C9（p052・p053・p072・p076・p126・p128・p134・p137・p138・cursor-owner） | 全て **PASS**（`build/ws099/p002-regress/criteria/results.txt`） |
| WS079 `zdesktop-p010.sh` | **PASS**（`build/ws099/p002-p010.log`） |
| boot test | **PASS**（`build/ws099/p002-regress/boot/login.png`） |

## 残り

- App Home の最初の開きの 116 ms（QEMU の `vkEndCommandBuffer`）。ホスト側の一度だけの費用と見ているが、未確定。実機で出なければ QEMU だけの問題として扱う。
- 実機の frame pacing の待ちは、開閉の最初の frame だけ免除した。開閉の途中の frame の間隔は今までどおり。
