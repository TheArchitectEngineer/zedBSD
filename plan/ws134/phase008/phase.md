<!-- awesome-plan project=zedbsd record=ws134-p008 -->
# ws134-p008: M3a 本物の値（zedBSD の backend の monitor の領域）

Status: in-progress（q662、P2 generation8、2026-10-04。実装・build 済み、T の QEMU の試験待ち）
Disposition: normal
Parent: [WS134](../ws.md)
設計: [design.md](../design.md) §1.3（backend の領域「monitor」）
依存: WS131 p010（統合・cleared）、p005・p006（cleared）、p007（T1-066 PASS）

## 範囲

`libkeiland-backend` に 8 つ目の領域「monitor」を足し、zedBSD の実装（`libkeiland-backend-zedbsd/monitor-zedbsd.c`）で kernel の
counter を読む。compositor の拡張（p012）と app（p013）はまだ使わない。Linux・FreeBSD は p011。

## 実装

- `userland/desktop/libkeiland-backend/keiland-backend.h`: monitor の領域（`kl_backend_monitor_open/info/sample/close`、info（CPU の数・host・
  generation・GPU・disk・link（loopback なし）の id と generation）、sample（CLOCK_MONOTONIC の time、valid の `KL_MONITOR_HAVE_*`、CPU ごとの tick と
  cpu_hz、memory の total・free・cache・reclaimable・swap、link の rx/tx と up、disk の ops・bytes・ns・busy、GPU の busy・周波数・memory・温度・電力）、
  上限 CPU 256・GPU 4・disk 8・link 16、disk の kind の番号（kernel の `DISK_STATS_KIND_*` と同じ））。`struct kl_backend` に触れず、どの thread から
  呼んでもよい（compositor は専用の thread で sample する、design §1.3）。
- `libkeiland-backend-zedbsd/monitor-zedbsd.c`（新）: CPU は `hw.cputimes`、memory は `/dev/system` の `KERN_SYSTEM_GET_VMSTAT`（5 秒に 1 回だけ、
  間の sample は前の値）と `vfs.cache_memory.stats`（cache は全 cache の resident、reclaimable は file data の resident − pending）、swap は vmstat の
  page 数 × 4096、link は network 領域の `kl_backend_network_get_links`（SIOCGIFSTATS、二重に実装しない。id は名前の FNV-1a）、disk は `hw.diskstats`
  （id・generation は kernel の）、GPU は `hw.gputelemetry`（名前は driver の名前。Vulkan の device 名は compositor が p012 で付ける）。可変長の sysctl は
  monitor の buffer に読み、大きくなったら取り直す。info の generation は disk の set の generation に link の id と GPU の数を混ぜた物（変われば読み直す
  合図）。温度・電力は zedBSD に出どころが無く valid に入れない。
- `sources.mk` に追加（compositor に link されるが、p012 までは呼ばれない）。
- 試験の道具 `userland/tests/monitor-probe`（新、試験の image だけ、amd64）: compositor なしで monitor を開き info と sample を `MPROBE` の行で出す。
  `plan/ws134/tests/config-amd64-monitor.mk` に追加。B3（backend を使うのは compositor だけ）の対象は `userland/desktop` なので外れる。
- 試験 `plan/ws134/tests/monitor-backend-p008.sh`（busy loop 1 本と 32 MiB の読みの下で 3 秒の 2 sample: info の CPU 数・host・nvme0n1・link・GPU なし、
  valid 0x1f、tick の和 ±15%、user、memory の大小、disk の読み、link の byte）。

## 確かめ

- build: compositor（`bin/wayland`、monitor-zedbsd.o を含む）と `bin/monitor-probe` が `-Werror` で exit 0、warning 0。style-check 違反 0。
- QEMU（T に依頼）: `monitor-backend-p008.sh`。結果は未着。
- stub の一覧（design.md §1.5）: この Phase は backend だけで、app の画面は p013 まで sim のまま（表は p013 で「本物」に更新する）。

## 制限

- disk の size_bytes は `hw.diskstats` に無く 0（要るなら K2 に足す）。
- GPU の id は `hw.gputelemetry` の並びの番号（driver は GPU を一度だけ登録し外さないので変わらない）。
