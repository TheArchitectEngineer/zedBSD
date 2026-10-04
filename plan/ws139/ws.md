<!-- awesome-plan project=zedbsd record=ws139 -->

# WS139: desktop の速さの改善（まとめたチケット）

<!-- awesome-plan-current:start -->
Status: planned
Primary Milestone: MG006
Related Milestones: MG003（実機の 5330）
Objectives: O2
Parent: [Master](../master.md)
Queue: none（Q1 が割り当てる）
Resume point: p001 から。p002 は 5330 の操作の承認（U2）を待つ。改善の Phase（p004 以降）は p003 のユーザーのレビューの後に定める。
<!-- awesome-plan-current:end -->

## 目標

2026-10-04 ユーザー:「F-071、F-072、F-070、はWSを立てて計画を作り、他の能力が低いセッションで処理できるようにしてください。」

[F-072](../future-work.md) の内容。2026-10-03 user「C5の200msは問題視しません。clearでOKです。理由は、あとでパフォーマンス改善のチケットを作ってまとめて改善するからです。」
- 最初の項目は C5 の窓の開閉の最初の frame（QEMU で 1 回目だけ 200 ms 前後、T1-006）。
- 他の速さの項目（WS094 の L3、WS127 の速さ、WS102 の手書きの遅れ、BUG-143 の入力の遅れなど）もここに集める。

散らばった速さの項目を 1 つの台帳にまとめる。同じ道具・同じ環境で測り直してから、まとめて直す。

## 環境の切り分け（大事）

速さの数字は、測った環境で意味が大きく変わる。数字には必ず環境の印を付ける。

| 印 | 環境 | GPU | 使い道 | 注意 |
| --- | --- | --- | --- | --- |
| **E1** | 開発の host の QEMU＋Venus（KVM） | 無い。Venus は host の lavapipe（CPU）で描く（[ws134-p003](../ws134/phase003/phase.md) の調べ: `lspci` は Matrox MGA G200e だけ、`vulkaninfo` は `llvmpipe`） | CPU の側の費用、呼び出しの回数、前後の比べ | fps・frame の時間は実機より大きく悪い。Venus の同期の呼び出しが 1 回約 10 ms（[F-064](../future-work.md)）。fps の判定に使わない（2026-10-04 Q1 の決め、ws134-p003） |
| **E2** | 5330 の host の QEMU＋Venus（host の i915） | 5330 の iGPU（host の i915 の driver） | **描画の性能の判定**（Guardrail 2026-10-03:「パフォーマンス改善でも、5330でVenusを使えば実質i915が使えますので、そうしてください。」） | 2026-10-04 時点で、この環境の desktop の計測の記録は無い（P1 の調べ）。手順は p002 で作る |
| **E3** | 5330 の i915 の passthrough | 5330 の iGPU（zedBSD の i915 の driver） | i915 の driver の改善の Phase だけ（Guardrail 2026-10-03） | 今ある実機の数字（C5 の最大 55 ms、C6 の 67.3 ms）はここで測った。iGPU と AX211 を同時に passthrough しない |
| **E4** | 5330 の単独の起動（USB） | zedBSD の i915 | 最後の受け入れ（安定版 S2、[WS133](../ws133/ws.md)） | 操作はユーザー。計測の道具が使えない所がある |

E1 の数字は、同じ E1 の前後を比べることにだけ使う。目標の判定は E2（driver の改善は E3、最後は E4）で行う。

## 台帳（2026-10-04 に集めた数字）

| ID | 項目 | 今の数字 | 環境 | 目標（出どころ） | 出どころの記録 | 計測の道具 |
| --- | --- | --- | --- | --- | --- | --- |
| P-01 | C5: App Home・Wiseview の開閉の最初の frame（窓 10 個） | E1: 1 回目だけ first 201〜202 ms・gap 243 ms、2・3 回目は ok（T1-006、criteria の image d169912dd）。1 回目以外の first は 90〜95 ms（ws099-p002 の直した後の表、`p002-c5-after3`）。E1 の内訳（ws099-p002 の診断の build）: `vkEndCommandBuffer` が App Home の最初の開きだけ 116 ms（他は 10 ms）、Venus の呼び出しの往復が大半。E3: 34 回の全てが 100 ms 以内、最大 55 ms、App Home の最初の開きは 12 ms（ws099-p002） | E1・E3 | 100 ms 以内（[WS099](../ws099/ws.md) の C5、実機） | `plan/agents/T1/requests.md` の T1-006、`plan/ws099/phase002/phase.md` | `plan/ws099/tests/c5-transitions.sh`＋`c5-parse.py`（E1）、`c5-hw.sh`（E3）。`ZWL FIRST_FRAME`・`ZWL COMPOSE … at_ms=` |
| P-02 | Text Editor・Terminal の直接の入力の遅れ（BUG-143 の残り） | E1: textedit-direct 中央値 306〜337・最大 416〜516 ms、terminal-direct 300〜311 / 309〜321 ms（T1-024・T1-046・T1-051。VNC の撮影の遅れを含む上限）。ユーザーの観察は「すべての文字入力に 500 ms」。Text Editor の key ごとの CPU の描画は host の 1920x1080 で 25 → 3.8 ms に済み（ws095-p015）。原因は未特定 | E1 | 未定（U3） | [BUG-143](../bugs/BUG-143.md)、`plan/ws095/phase015/phase.md` | `plan/ws095/tests/latency-bug143.sh` の 1・2 段（`type-latency.py`: QMP で key を送り、VNC の画面の変化まで） |
| P-03 | compositor の 1 回の合成と frame の間隔 | E1: `ZWL PERF compose` の draw_ms 88〜104（submit+present 68〜79）、frame_ms 98〜115、合成は 1 秒に 4 回。窓を 480x320 にしても同じ。System Monitor の sim の fps 4.6〜4.7（callback 209〜222 ms）（T1-057） | E1 | System Monitor の fps 15 以上（ws134-p003、5330 の値で判定） | `plan/ws134/phase003/phase.md` の T1-057 | `ZWL PERF compose`（5 秒ごと、`main.c` の `zwl_perf_report`）、`ZMON FRAME`、`plan/ws134/tests/monitor-p003.sh` の 3・4 段 |
| P-04 | desktop の icon 100 個（WS094 L3） | E1: (a) 起動 → `DESKTOP ready` 1954 ms、(a') 最初の描画 2973 ms、(b) 追加 → 表示 1344 ms、(c) click → 選択の frame 90 ms（ws094-p009、3 回の中央値）。(c) の残りは Vulkan の呼び出し（submit 32〜51、present 4〜51、fence 1〜11 ms） | E1 | (a) ≤1500、(b) ≤2500、(c) ≤50 ms、`SLOW-FRAME` 0（[WS094](../ws094/ws.md)、5330 で判定、ws094-p012 は planned） | `plan/ws094/phase009/phase.md` | `plan/ws094/tests/files-desktop-guest.sh perf100` |
| P-05 | Files の速さ（WS127 F4） | E1: (a) 起動 → 最初の frame（1000 項目）1794 ms、(b) sidebar → 1000 個の icon 576 ms、(c) click → 選択の色 273 ms。`SLOW-FRAME` の present は 406〜2585 ms | E1 | 1000 項目を開いて ≤1000 ms、選択 ≤50 ms（仮、[WS127](../ws127/ws.md)、5330 で判定、ws127-p007 は planning） | `plan/ws127/requirements.md` §4 | `plan/ws127/tests/latency.py`、Files の `SLOW-FRAME`（250 ms 以上） |
| P-06 | 手書きの遅れ（WS102） | E1: 点の入力から描いた frame まで 20〜53 ms、続けて描く時の frame の間隔 130〜143 ms（約 7 枚/秒） | E1 | 17 ms（60 Hz、WS102 L3 p010、5330 か Windows の QEMU） | `plan/ws102/phase008/phase.md` | `ZWL OSK hand frame lag_ms= gap_ms=` |
| P-07 | C6: pointer を動かしてから cursor が出る flip まで（10 app） | E3: 中央値 67.3 ms（WS075 L2、5 run）。L3 の目標 50 ms は未達。文字の draw をまとめる差分が `plan/ws075/phase031/exp/text-batch.patch` に残る（検証の前に止めた） | E3 | 50 ms（WS075 L3、WS099 の C6） | `plan/ws075/ws.md` の p024〜p031 | `plan/ws075/tests/hdmi/measure-apps.sh`・`c6.py`・`engine-gdb.sh` |
| P-08 | pen の遅れと buffer の import | E1: pen の遅れの中央値 117 ms、frame の間隔 113 ms（ws099-p015）。App Home → Files の最初の frame 2407 ms（ws099-p016 の直した後）。注: 「buffer 1 つの import 約 210 ms（うち `vkQueueWaitIdle` 約 100 ms）」は直す前の値で、p016 がその待ちを次の合成の barrier にまとめて無くした。残りは F-064（Venus の 10 ms の刻み） | E1 | 未定 | `plan/ws099/ws.md`・`phase015`・`phase016` | `plan/ws099/tests/p015-pen-latency.sh`・`p015-lat.py`（`ZWL LAT pen/adopt/submit/shown`） |
| P-09 | Venus と起動の固定の費用 | E1: Venus の同期の呼び出し 1 回約 10 ms（F-064）、画像 1 つの作成 約 200 ms・swapchain の作成 600〜700 ms（F-056）、壁紙の blur 約 80 ms（CPU、F-060） | E1 | 未定 | [F-021・F-056・F-060・F-064](../future-work.md) | `ZWL STARTUP step= ms=` |
| P-10 | 電池の時の描画（BUG-159） | E4: 電池で動くと cursor の描画が約 5 fps に落ちる | E4 | 未定 | [BUG-159](../bugs/BUG-159.md)、`plan/ws133/s1-results.md` | ユーザーの観察（電源の管理の WS と関係） |

- 台帳は、この WS の Phase が測り直したら更新する。古い数字は消さずに、日付と環境を付けて残す。
- BUG-143 は 2026-10-04 に resolved（IME の確定の遅れは直った）。P-02（直接の入力の遅れ）はその残りで、ticket の reopen か新しい ticket かは Q1 が決める。
  BUG-159（P-10）は tracking。どちらも、直した Phase の結果を ticket に書き、disposition は Q1 が決める。
- 台帳の値は image が揃っていない（C5 は criteria の image、P-03 は monitor の image、P-02 は Settings＋IME の image）。
  p001 からは `plan/ws139/tests/config-amd64-perf.mk` の 1 つの image で測る。古い値と新しい値を直に比べない。
- `ZWL FIRST_FRAME` と `ZWL COMPOSE at_ms=` は、画面に出た時刻ではなく submit の時刻である（`compose.c:396-405`）。

## 計測の道具（今あるもの）

- **compositor の log**（`userland/desktop/wayland/`）。時刻は `CLOCK_MONOTONIC`（`wire.c`。ms の分解能、F-052）。
  - 計測を切り替える環境変数は無い。`--log-frames` だけで切り替える。
  - `ZWL PERF <N>ms: passes= timeouts= | poll% work%`・`ZWL PERF compose frames= draw_ms= (acquire … submit+present …) frame_ms=`・`ZWL PERF shm copies= copy_ms=`:
    5 秒ごとに常に出る（`main.c` の `zwl_perf_report`、1044 行付近）。
  - `ZWL FIRST_FRAME what= frame= request_ms= ms=`: 常に出る（`compose.c:399`）。
  - `ZWL COMPOSE frame= image= windows= at_ms=`・`ZWL LAT draw|acquired|recorded|submit|shown frame= at_us=`: `--log-frames` の時だけ（`compose.c:348-453`）。
  - `ZWL STARTUP step= ms=`（`main.c` の `startup_step`）、`ZWL MODE window switch_ms=`（`display.c`）、`ZWL OSK hand frame lag_ms= gap_ms=`（`keyboard.c`）。
  - `ZWL HOME|WISEVIEW … at_ms=`（`home.c`・`shell.c`）、`ZWL IME bypass after_ms=`（`input-method.c`）。
- **app の log**: Files の `SLOW-FRAME draw= present= copy= acquire= queue= wait=`（250 ms 以上、`files/main.c:39・675`）、
  System Monitor の `ZMON FRAME fps= … build_ms acquire_ms record_ms submit_ms present_ms callback_ms`（`monitor/main.c:911`）。
- **host から見る道具**（QEMU の表示の更新と VNC の往復を含む上限）: `plan/ws095/tests/type-latency.py`、`plan/ws127/tests/latency.py`。
- **E3 の道具**: `plan/ws075/tests/hdmi/measure-apps.sh`・`c6.py`・`engine-gdb.sh`・`perf-gdb.sh`・`h4-ctl.py`（`flock /tmp/i915-hw.lock` の下）。
- **E1 の C1〜C10**: `plan/ws099/tests/criteria.sh`（閾値は 15-21 行付近）。

## 完了の条件

1. p003 で直すと決めた項目に、同じ道具で E1 と E2 の測り直しの値がある。測れない項目（例: P-07 は E3 の道具、P-10 は E4 の観察）は、理由と代わりの環境を書く。p001・p002 で測るのは P-01〜P-03。P-04〜P-09 を測り直す Phase が要るかは p003 で決める。
2. ユーザーがレビューで選んだ項目（p003 の決め）が、E2（driver の項目は E3）で目標を満たす。
   満たせない項目は、理由と残りを Future Work か Bug に移す（ユーザーの承認）。
3. 直した所で、E1 の回帰（C1〜C9 の `criteria.sh`、変えた app の試験）が PASS する。
4. 変えた C が全文規約に合う（最後の Phase）。

## 関係する source の path（候補。Phase ごとに p003 で決める）

- compositor: `userland/desktop/wayland/compose.c`・`main.c`・`glass.c`・`backdrop.c`・`display.c`・`home.c`・`shell.c`
- libkeiui（app の present）: `userland/desktop/libkeiui/`（`keiui_present_frame`）
- app: `userland/desktop/files/`・`textedit/`・`terminal/`・`monitor/`
- libvulkan の Venus の道: `userland/desktop/libvulkan/`、kernel の Venus の driver（`CONFIG_DRIVER_PCI_VENUS`）
- 計測の道具: `plan/ws139/tests/`（新）

## Guardrail の注意

- **iGPU の passthrough（E3）は i915 の driver の改善の Phase だけ**で使う。描画の性能は E2 で測る（Guardrail 2026-10-03）。
  iGPU と AX211 を同時に passthrough しない。
- compositor は libvulkan だけで GPU を扱う。GPU の UAPI の ioctl を compositor に足さない（Guardrail「compositor は libvulkan だけを使う」、
  checker `plan/tools/gpu-boundary/v1-check.sh`）。最適化のための直の道を足すなら、Guardrail の macro の規則に従い、Q1 に相談する。
- HAL の API（`include/hal/hal.h`）を変える必要が出たら、止めて差分を Q1 に出す。
- QEMU は自分で起動しない。E1 は T1/T2 に依頼する。E2・E3 の 5330 の操作は U2 の決めに従う。
- 負荷・耐久の試験（C10 の 60 分など）は別枠。ユーザーに確かめて夜間に流す。
- 細かい直しごとに回帰を回さない。Phase の終わりにまとめて T に依頼する。

## ユーザーの判断

- **U1 判定の環境**: 目標の判定を E2（5330 の Venus）で行う（Guardrail どおり、推奨）。C6（P-07）のように今までの記録が E3 の項目も、E2 で測り直すか。
  - E3 の数字との比べが要るなら、E3 は i915 の driver の項目に限って使う。
- **U2 5330 の操作**: E2 の計測で次を行ってよいか、誰が行うか（推奨は T1/T2。実装の担当は script を書くところまで）。
  - 5330 に ssh で入り、home に tree（`git archive`）と image（約 2 GB）を送る。
  - centris の `/tmp/i915-hw.lock` を約 45 分持つ（その間 passthrough の試験は待つ）。
  - `~/bigbang/igpu-mode.sh host` で iGPU を host の i915 に付け替え、終わりに `vfio` に戻す。
  - 5330 の上で QEMU を起動する。
- **U3 P-02 の目標**: Text Editor・Terminal の直接の入力の遅れの目標の値（例: key から画面まで 50 ms 以内）。
  ユーザーの観察（500 ms）は E4 の値か E1 の値か。
- **U4 項目の優先順**: p003 で E1・E2 の数字を見て、直す項目と順を決める。
  候補の順（P1 の案）: P-03（合成の費用。全ての項目の土台）→ P-01・P-04・P-05・P-08（Venus の同期の呼び出しの回数）→ P-02（present の道）→ P-07（文字の draw のまとめ）→ P-06 → P-09・P-10。
- **U5 host に GPU を付けるか**: 開発の host に GPU が無いので、E1 では fps の判定ができない。host に GPU を足すかは、ユーザーの設備の判断。

## Phase

| Phase | 内容 | 状態 | 依存 |
| --- | --- | --- | --- |
| [p001](phase001/phase.md) | 計測の image と一括の script（P-01・P-02・P-03 を 1 つの guest で 10 分程度）、台帳の表を作る道具、E1 の基準値（T に依頼） | planned | — |
| [p002](phase002/phase.md) | E2（5330 の host の i915 の Venus）で同じ script を流す手順と、E2 の基準値 | planned | p001、U2 |
| p003 | 台帳のレビュー: E1・E2 の表をユーザーに示し、直す項目・目標・順を決める（U1・U3・U4）。p004 以降の Phase を定める | planned | p001・p002 |
| p004〜 | 改善の Phase（p003 で定める。1 Phase は 1 つの原因の直しと、その前後の E1・E2 の計測） | — | p003 |
| 最後 | 全文規約の見直し、E1 の回帰（C1〜C9）、台帳の最終の値 | — | 改善の Phase |
