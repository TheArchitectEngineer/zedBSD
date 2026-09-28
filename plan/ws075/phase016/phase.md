<!-- awesome-plan project=zedbsd record=ws075p016 -->

# ws075-p016: lease の替わり目で HDMI を点けたまま（login・logout の暗転を無くす）

Phase ID: `ws075-p016`
Parent: [WS075](../ws.md)
Status: cleared（2026-09-28。実機の passthrough で login 6 回・logout 5 回とも暗 0・黒 0（前は替わり目ごとに 180〜383 ms の register の上の暗・黒と transcoder の停止）、Shut Down と hold の期限切れで出力は止まる。実物の monitor の目視は未実施）
Phase disposition: normal
承認: 2026-09-28 main の依頼（WS075 の i915 の subagent、デモの頑健さ。[F-048](../../future-work.md) のこの構成での目標）。

## 範囲

- 今の暗転の時間を測る（greeter → session、session → greeter）。
- 最小で安全な変更: lease の替わり目で pipe と plane を止めず、最後の絵を次の lease の最初の flip まで保つ。GPU core の lease の
  受け渡しと i915 の resident display。HAL・UAPI は変えない。
- 実機（5330、passthrough）でデモの image の login・logout を各 5 回、暗転の測定と画面。

## 設計

今（p013）は lease ごとに resident run（modeset・2 枚の buffer・stop）をする: release で worker が display window を出て
stop の経路（pipe・plane・transcoder を止め、buffer を返す）、次の lease の最初の present で modeset し直し、buffer A（黒）を
出してから最初の flip。HDMI の信号は一度切れる（LCD は信号の再検出に数秒かかりうる）。

変更（i915 だけ。GPU core の lease の API は変えない）:

- `worker.c` の `i915_worker_loop()`: window の中の release は window を出ず、**hold** を始めて release を完了する
  （`drv_i915_present_hold_start()` を IRQ lock の下で、終わった lease の address space から panel の buffer を外す
  `drv_i915_present_hold_prepare()` を release の完了の前に）。worker の stop が要るとき、shutdown が hold を断ったときは
  従来どおり window を出る。待ちの中で `drv_i915_present_hold_over()`（hold が `I915_PRESENT_HOLD_MS` = 10 秒を過ぎた、または
  shutdown が終えた）なら window を出る（従来の stop の経路）。
- `present.c`: 次の lease の最初の frame（CPU copy・GPU copy）で hold を終え（`i915_present_hold_end()`、保った時間を log）、
  frame が buffer を覆わないときだけ終わった lease の絵の残る buffer を黒にする（`i915_present_clear_stale()`）。
  window が終わると hold の状態を消す。
- `i915.c`: PCI の shutdown の hook（`i915_shutdown()` → `drv_i915_present_shutdown()`）。以後の hold を断り、hold 中なら worker を
  起こして出力の stop を最大 2 秒待つ。Shut Down の後に誰の物でもない絵を残さない（従来どおり停止した画面）。
- `internal.h` の `struct i915_present_window` に hold の状態（`holding`・`hold_ended`・`hold_since`・`hold_until`、IRQ lock）と
  `stale_buffers`。

capture の build（`I915_TEST_CAPTURE`）は window を使わないので変わらない。

## 測り方（道具）

- `plan/ws075/tests/hdmi/h4-ctl.py watch SECONDS MS`: 2 つ目の QMP の socket で、passthrough の iGPU の BAR0 を `xp` で読み、pipe B の
  `TRANSCONF`（0x71008）・`PLANE_CTL_1_B`（0x71180）・`PLANE_SURFLIVE`（0x711ac）・frame counter を 20 ms ごとに標本化し、変化を
  `watch.log` に書く（`hdmi-h4-hw.sh fetch` が取る）。
- `plan/ws075/tests/hdmi/h4-blank.py OUTDIR`: `watch.log` と kernel の log の buffer A の surface から、**暗**（transcoder か plane が off）と
  **黒**（点け直しの後、消したままの buffer A を flip の前に出している間）の区間を出す。register の上の時間で、実物の monitor が
  信号を見つけ直す時間（LCD によっては 1〜3 秒）は含まない。
- `plan/ws075/tests/hdmi/h4-cycle.sh OUTDIR COUNT`: session から Log Out（App Home (22,16) → Log Out (1031,617)）→ greeter の lease を
  待ち撮影 → Enter で login → session の lease を待ち撮影、を COUNT 回。時刻は `actions.log`。

## 結果（実機 = 5330 の VFIO passthrough の QEMU guest。HDMI 1920x1280、pipe B、DVI。2026-09-28）

**前**（`build/h4-demo`、main の kernel、worktree の `build/h4-base/`）: login 4 回・logout 3 回の全てで pipe B を止めて modeset し直した。

| 替わり目 | 暗（transcoder・plane off） | 黒（消した buffer A） | 合計 |
| --- | --- | --- | --- |
| login（greeter → session）4 回 | 180・160・182・179 ms | 0・202・0・182 ms | 180・362・182・361 ms |
| logout（session → greeter）3 回 | 180・183・180 ms | 203・183・0 ms | 383・366・180 ms |

黒の 0 は、最初の flip が 20 ms の標本の間に来たもの。これに加えて実物の HDMI の monitor は transcoder の停止で信号を失い、
再同期の時間（passthrough では測れない）だけ暗くなる。

**後**（`build/h4-hold2`: この変更 + ws075-p015 の timer の較正の修正、worktree の `build/h4-hold/`）: login 6 回・logout 5 回（受け入れの
5 回ずつ）の 11 回の替わり目で、**暗 0・黒 0**（`h4-blank.py` の区間なし、`TRANSCONF` は `c0000000`、`PLANE_CTL` は `94000000` のまま、
live の surface は A・B の間を flip で移るだけ）。kernel の log は 11 回とも `lease released; holding the last picture` →
`the next lease's first frame after holding the last picture for N ms (no modeset)`、N = 65・71・65・71・69・74・63・74・65・72・65（と
Shut Down の前の logout の 60）ms。この間、画面は前の lease の最後の絵（greeter または session）のまま。

- **Shut Down**（greeter、(1840,1238)）: `SESSIOND POWER poweroff` → greeter の release（hold）→ PCI shutdown で
  `the held picture was stopped for the shutdown`、`ended PASS (show rc=5; flips 2778, stop confirmed, buffers released=1, power refs held 0,
  first anomaly: none)`（rc=5 は run log の容量超過で従来どおり）。watch: `TRANSCONF`・`PLANE_CTL`・`SURFLIVE` が 0 に。QMP: 4 CPU とも
  `HLT=1`・`RFL=00000002`（halt）。
- fault・fatal は 0。`XXX` の行は既存の 2 種だけ（capset の vendor flags、Gears の GL の pipeline の dynamic state 2・3）。
- 画面（scanout の live の buffer）: `build/ws075-shots/ws075-p016-hold-{logout1,logout5}-greeter.png`・`ws075-p016-hold-{login1,login5}-session.png`、
  前: `ws075-p016-before-logout1-greeter.png`・`ws075-p016-before-login1-session.png`、App Home: `ws075-p016-app-home.png`。
- **hold の期限切れ**（`build/h4-timeout/`、同じ image）: session の Terminal で `kill -STOP 12`（sessiond、`ps -A` で確認）→ Log Out。
  compositor は sessiond の応答を待たず終わり、`lease released; holding the last picture (buffer A)` の後に次の lease は来ず、
  `released after 54 flip(s); the reference stop path follows` → `ended PASS (show rc=0; flips 54, stop confirmed, ...)`。その後の register:
  `TRANSCONF` B = 0、`PLANE_CTL_1_B` = 0（出力は止まった）。hold と停止の間の時間は log に時刻が無く測っていない（10 秒の設定）。

## 確認

- build: `plan/ws075/demo/build-demo-image.sh build/h4-hold passthrough`（hold だけ）・`build/h4-hold2 passthrough`（hold + p015 の HAL）。
  compiler の warning 0。
- QEMU の boot test（GPU の無い q35、`build/h4-hold2` の image）: PASS（p015 と同じ run）。
- 規約: `plan/tools/style-check.py` の指摘は既存の同じ形の行（critical section の lock・unlock の段落、`display = device->display;` の段落）
  だけ。diff の空白の検査は清浄。
- host の試験: resident display の worker・present の host 試験は無い（証拠は上の実機の run）。
- 未実施: 実物の HDMI の monitor・LCD の目視（passthrough では register と scanout の buffer だけ。信号の再検出の時間が消えたかは LCD で
  確かめる必要がある）。eDP（`display=auto`）での login・logout（同じ経路だが実機では未実施）。bare metal。

## 残り

- F-048 の全体（fd の受け渡しと revoke、Venus の最後の画）は未着手。この Phase は i915 の resident display の範囲で暗転を無くした。
- 登録していない道具: `h4-cycle.sh`・`h4-blank.py`・`h4-ctl.py watch` は `plan/master.md` の Tools の HDMI の行に足す（main）。
