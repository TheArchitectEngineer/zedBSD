<!-- awesome-plan project=zedbsd record=ws134-p011 -->
# ws134-p011: M3b Linux・FreeBSD の backend の monitor の領域

Status: in-progress（q662、P2 generation8、2026-10-04。Linux は host で PASS、FreeBSD は T の guest の試験待ち）
Disposition: normal
Parent: [WS134](../ws.md)
設計: [design.md](../design.md) §1.1・§1.3
依存: p008（T1-067 PASS、header の monitor の領域）

## 実装

- `libkeiland-backend-linux/monitor-linux.c`（新、`Makefile.linux` に追加）: CPU は `/proc/stat` の cpuN（user+nice → user、system+irq+softirq →
  system、idle+iowait → idle、steal → other、USER_HZ）、memory は `/proc/meminfo`（cache は Cached+Buffers+SReclaimable、reclaimable は
  MemAvailable − MemFree）、link は network 領域の `kl_backend_network_get_links`（loopback なし、id は名前の FNV-1a）、disk は `/proc/diskstats` の
  `/sys/block` にある whole disk（loop・ram・zram・dm- を除く、kind は nvme の名前・`/usb` の path・mmcblk、時間は ms → ns）、GPU は `/sys/class/drm` の
  cardN（driver の名前、amdgpu の `gpu_busy_percent` を monitor が時間で積んで busy_ns に、`mem_info_vram_*`、i915 の `gt_cur_freq_mhz`・
  `gt_max_freq_mhz`、hwmon の `temp1_input`・`power1_average`）、CPU の温度は thermal zone の x86_pkg_temp。
- `libkeiland-backend-freebsd/monitor-freebsd.c`（新、`Makefile.freebsd` に追加）: CPU は `kern.cp_times`（user+nice、sys+intr、idle、
  `kern.clockrate` の stathz）、memory は `vm.stats.vm` の page 数×`hw.pagesize`（cache は inactive+laundry と `vfs.bufspace`、reclaimable は inactive）、
  swap は `vm.swap_info`、link は network 領域、disk は `kern.devstat.all` の direct access（pass を除く、kind は nvd/nda → NVMe、ada → IDE、
  da → SCSI、mmcsd → SD）、温度は `dev.cpu.0.temperature` か `hw.acpi.thermal.tz0.temperature`。GPU は FreeBSD に読める値が無く出さない。
- 試験 `plan/ws134/tests/monitor-backend-p011.sh linux|freebsd|all`（`userland/tests/monitor-probe/main.c` を各 OS で native に build し、busy loop の下で
  3 秒の 2 sample を判定）。

## 確かめ

- Linux: `keiland-linux.mk` の gcc で object、host の clang で `-fsyntax-only`、warning 0。style-check 違反 0（2 file）。checker（keiland-os-boundary）
  PASS。`monitor-backend-p011.sh linux` → PASS（この host: 64 CPU、nvme0n1・sda・sdb、link 5、valid 0x11f（温度を含む）、tick 19189/19213、user 384）。
- FreeBSD: この host で build できない。T に `monitor-backend-p011.sh freebsd`（WS137 の FreeBSD guest、base の cc で -Werror）と
  `plan/tools/keiland-freebsd/backend-test.sh`（native build、warning 0）を依頼。結果は未着。
- 実機は未実施。

## 結果（T1-068、WS137 の FreeBSD guest、fbeeba4、2026-10-04）

PASS 2/2。`monitor-backend-p011 freebsd: PASS`（CPU 8、disk vtbd0、link vtnet0、valid 0x1f、tick 3052/3048（127 Hz × 8 × 3.00 s）、user 381、
memory total 8317661184、build の warning 0）。`backend-test.sh` rc 0（build・warning 0・install・audit・host-seat・host-session・host-power・
sync-rejected・dmabuf-export-rejected 全 PASS）。証拠 `/home/awe/zedBSD-worktrees/t1/build/t1-068/p011/`・
`/home/awe/zedBSD-worktrees/t1/build/keiland-freebsd/backend-test-068/summary.txt`。
