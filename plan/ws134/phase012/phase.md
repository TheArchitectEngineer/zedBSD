<!-- awesome-plan project=zedbsd record=ws134-p012 -->
# ws134-p012: M3c compositor の `kl_system_monitor_v1` と libkeiland の `kl_system_monitor_*`

Status: in-progress（q662、P2 generation8、2026-10-04。実装・build・host 試験済み、T の QEMU の試験待ち）
Disposition: normal
Parent: [WS134](../ws.md)
設計: [design.md](../design.md) §1.3（compositor の拡張と libkeiland の API）
依存: p008（cleared）、p011（T1-068 PASS）。2026-10-04 Q1: WS131・WS113 は止まっていて衝突は無い。manager の version 2 は未定義なので
`get_monitor` を **version 2**（v1 の最後の opcode の次、6）にする（WS113 の `get_displays` はその次の version）。

## 実装

- `userland/desktop/keiland/kl-system-protocol.h`: `KL_SYSTEM_MANAGER_VERSION 2`、request 6 `get_monitor(new_id, period_ms)`（since 2）、
  capability `KL_SYSTEM_CAPABILITY_MONITOR 0x20`、`kl_system_monitor_v1`（request destroy・ack(serial)・set_period(ms)、event info・device・
  info_done・cpu・memory・link・disk・gpu・sample_done、u64 は high と low の 2 つの uint）、device の kind（GPU・disk・link）、period の範囲
  250〜10000（既定 1000）。流量の約束を header の comment に書いた。
- compositor `userland/desktop/wayland/sysmon.c`（新）: monitor object の作成（`zwl_sysmon_create`、manager の version 2 だけ）、request
  （destroy・ack・set_period）、`zwl_sysmon_tick`（250 ms ごとに object の数と最短の period を数え、1 つ目で専用の thread を起こし、最後が消えたら
  止める（終わったのを見てから join、event loop は sample を待たない））、thread は libkeiland-backend の `kl_backend_monitor_sample` を period ごと、
  info を 5 sample ごと（generation が変われば新しい serial）に取り、lock の下に最新を置く。event loop は新しい sample を各 object に送る:
  info を聞いていなければ info、前の sample を ack していて、client の送信の queue（`output_bytes`）に 1 sample 分の余裕がある時だけ
  sample の全部（丸ごとか無し）。`zwl.h` に kind `ZWL_SYSTEM_MONITOR` と object の `monitor_period`・`monitor_waiting`・`monitor_info`、
  `protocol.c` の振り分け、`system.c` の capability（version 2 の manager に）・`get_monitor`・tick・close。3 つの Makefile。
- libkeiland: `keiland.h` に `KL_SYSTEM_HAS_MONITOR`、`struct kl_monitor_info`・`struct kl_monitor_frame`（CPU の全体と core ごとの share、
  memory、link・disk の率と和、disk の平均 latency、GPU の share・周波数・温度・電力、valid）、`kl_system_monitor_open/take/info/close`、
  `KEILAND_VERSION 23`。`system/system-monitor.c`（新、event を pending の raw sample に、sample_done で current にして前との率の frame を作り
  ack）、`system/system-monitor-rate.c`（新、Wayland を知らない率の計算: id で対応、新しい機器と巻き戻った counter はその frame の率なし）、
  `system.c`（manager の version を global の版と 2 の小さい方で bind、capability、`system_monitor_make`）、`system-protocol.c`
  （`get_monitor` "2nu"、`kl_system_monitor_v1_interface`）、`settings.c` は version 1 で bind（settings に v2 は要らない）、`exports.map`、3 つの Makefile。
- 試験: `plan/tools/keiland-linux/lib-smoke.c` の版の確かめを `>= 22` に（23 で FAIL しないように）。`userland/tests/keiland-system` に
  `monitor` の command（250 ms で info と frame を出す）。`plan/ws134/tests/host/rate-test.c`（新、`run.sh` に追加: CPU の share と巻き戻り、
  link の id の対応・新しい link・巻き戻り、disk の率・平均 latency・busy・全体の latency、GPU の share）。`plan/ws134/tests/monitor-system-p012.sh`
  （新、guest で zdesktop と probe: capability 0x20、info（4 CPU・host・nvme0n1・link・GPU なし）、4 秒で 10 frame 以上・0.25 秒おき・valid
  0x1f、busy loop で CPU 20% 以上、disk の読み 1 MB/s 以上、sampling の開始と停止の log、ERROR なし）。config-amd64-monitor.mk に keiland-system。

## 確かめ

- build: zedBSD の libkeiland.so・compositor・keiland-system（-Werror）exit 0、warning 0。Linux の libkeiland.so と compositor（gcc）exit 0、
  host の clang の -fsyntax-only（sysmon.c・system-monitor.c・system-monitor-rate.c・system.c）warning 0。keiland-os-boundary の checker PASS
  （B2 は zedBSD と Linux の binary）。style-check の新しい違反 0（system.c の既存の 4 件は前から）。
- host 試験: `plan/ws134/tests/host/run.sh` → monitor-host・monitor-interact・monitor-rate PASS。
- QEMU（T に依頼）: `monitor-system-p012.sh`。FreeBSD の build（backend-test.sh）も依頼。結果は未着。
