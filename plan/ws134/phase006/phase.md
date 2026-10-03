<!-- awesome-plan project=zedbsd record=ws134-p006 -->
# ws134-p006: K2 kernel の disk ごとの統計 `hw.diskstats`

Status: cleared（q661、2026-10-04。Q1 判定）
Disposition: normal
Parent: [WS134](../ws.md)
設計: [design.md](../design.md) §1.2 の K2
依存: なし（HAL の API は変えない。時刻は既存の `kern_rtc_read_counter`、無ければ tick）

## 範囲

物理の whole disk ごとの読み書きの回数・byte・時間、busy の時間、出ている request の数を kernel が数え、sysctl `hw.diskstats` で出す。
sysctl の CLI の表示。monitor の Disk の本物の値（p008）の出どころ。

## 実装（commit fa6c742・755ae25）

- `include/uapi/sysctl.h`: `HW_DISKSTATS 7`、`DISK_STATS_VERSION 1`、kind（OTHER 1・NVME 2・USB 3・UAS 4・IDE 5・SDMMC 6・SCSI 7）、flags
  （READ_ONLY・REMOVABLE）、`struct disk_stats_header`（version・struct_size・element_size・count・generation・reserved、24 byte）と
  `struct disk_stats_entry`（name[32]・kind・flags・id・generation・read/write の ops・bytes・ns・busy_ns・inflight・reserved、120 byte）、
  ILP32・LP64 の `_Static_assert`。
- `include/kern/disk.h`・`src/kern/disk.c`: `struct disk` に統計の field（registry の lock で守る。d_inflight を変える所で変える）、
  `struct bio` に `b_submitted_ns`。`bio_admit` の inflight++ の後に `disk_stats_start`（提出の時刻、0→1 で busy の始まり）、
  `bio_complete` と `bio_submit` の拒否の inflight-- の後に `disk_stats_end`（1→0 で busy の和、完了した read/write の回数・byte・時間。
  flush は数えない、拒否は busy だけ）。`disk_set_stats_kind`（driver が disk_create の前に呼ぶ。kind の無い disk、つまり partition・
  loop・ufs の内部の disk は数えず出さない）。`disk_stats_copy`（registry の lock の下で header と kind のある `d_parent == NULL` の disk を
  一度に書く）。id は `d_dev`（作られた順、再利用しない）、generation は kind のある disk の出入りで 1 増える全体の数（disk は現れた時の値）。
  時刻は `kern_rtc_read_counter`（秒と余りに分けて ns）、無ければ `sched_ticks`。
- driver の kind: pci-nvme（NVME）、usb-storage（USB）、usb-uas-disk（UAS）、pcat-ide・pc98-ide・sun4u-cmd646（IDE）、rpi4-sdhci（SDMMC）、
  x68k-spc-disk（SCSI）。
- `src/kern/sysctl.c`: leaf `hw.diskstats`、`sysctl_diskstats`。
- `userland/base/sysctl/main.c`: `show_diskstats`（`hw.diskstats: generation= disks=` と disk ごとの行）、`sysctl -a` にも。
  可変長の値の取得を `fetch_value`（長さ → 読み、増えた時は取り直す）にまとめ、`show_cputimes` も使う。

## 確かめ

- build: kernel は amd64（monitor の config）・pcat（i386）・rpi4（arm64）・pc98 で exit 0、warning 0。sysctl の CLI exit 0。
  sun4u・x68k の kernel は未 build（driver に 2 行ずつ）。
- style-check: 変えた所の新しい違反 0（disk.c の既存の違反の数は変わらない）。
- 試験の判定の Python は host で作った読みで確かめた。
- QEMU（T に依頼）: `plan/ws134/tests/diskstats-p006.sh`（NVMe の boot disk の nvme0n1 が kind 2、partition は出ない、raw の 32 MiB の読みで
  read の ops・bytes（8 MiB 以上）・時間・busy が増え平均 latency が 1 us〜1 s、32 MiB の書き込みと sync で write が 8 MiB 以上、QMP で USB の
  stick を挿すと kind 3/4 の新しい disk と別の id・generation の増加、その読みが stick に数わる、抜くと消えて generation がまた増える）。結果は未着。
- 実機は未実施（NVMe・USB は 5330）。

## 結果（Q1、2026-10-04、T1-065、QEMU Venus KVM、NVMe、main a326b91）

cleared。`diskstats-p006: PASS`: nvme0n1（NVMe、partition は数えない）、読み 32 MiB で ops・bytes・時間・busy が増え、書き込みで write が増える。QMP の usb-storage の hotplug で sda（kind 3、別の id）が現れ generation 1→2・読みを計上、device_del で消え 2→3。sun4u・x68k の kernel は CI の config が無く未 build。
