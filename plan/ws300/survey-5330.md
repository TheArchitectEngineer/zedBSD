<!-- awesome-plan project=zedbsd record=ws300-survey-5330 -->

# WS300 の調べ: 5330 の Debian の読みと firmware の表（2026-10-29 P5、q1666-i03）

WS300 p001（調査と設計）の入力。設計ではない。条件: `uname` で Debian（chaos、Linux 6.19.13）を確かめ、centris の `/tmp/i915-hw.lock` を取った。読みだけ（再起動・書き・package の導入・s2idle はしていない）。
NVMe は nvme-cli が無いので、python の Linux の admin の passthrough の ioctl で Identify（06h）と Get Features（0Ah）だけを送った（読みの command）。PCI は `sudo lspci -vvv`・`-xxxx`・`setpci` の読み、pmc_core は debugfs の読み。
生の記録は `scratchpad/ws300-5330/`（`nvme.txt`・`pci.txt`、git に入れない）。firmware の表は git の `plan/ws049/tests/latitude5330/dsdt.dat`（BIOS 1.31.1）の逆アセンブル（scratchpad の `ws303-acpi/dsdt.dsl`）。

## 1. NVMe（10000:e1:00.0、KIOXIA BG5 KBG50ZNS256G、fw 11200109、NVMe 1.4）

| 項目 | 値 |
| --- | --- |
| RTD3R・RTD3E | 100000 µs・500000 µs |
| NPSS・APSTA | 4（PS0〜PS4）・1（APST 有り） |
| HMPRE・HMMIN | 16384・3584（4 KiB の単位、64 MiB・14 MiB）。**DRAM の無い SSD で HMB を使う** |
| WCTEMP・CCTEMP | 356 K・358 K |
| PS0〜PS2 | 4.0 W・2.5 W・1.3 W、操作の状態、ENLAT・EXLAT 1 µs |
| PS3 | 0.05 W、非操作、ENLAT 800 µs・EXLAT 1000 µs |
| PS4 | 0.003 W、非操作、ENLAT 3000 µs・EXLAT 44900 µs |
| Feature 02h（Power Management） | 0（PS0） |
| Feature 0Ch（APST） | 有効（1）。Linux の表: PS0〜PS2 → PS3 を 100 ms、PS3 → PS4 を 2000 ms |
| Feature 0Dh（HMB） | 1（Linux が HMB を渡している） |
| Feature 10h（HCTM） | TMT1 354 K・TMT2 356 K |
| Feature 11h（NOPSC） | 1 |
| Linux の parameter | `apst_primary_timeout_ms` 100、`apst_secondary_timeout_ms` 2000、`apst_primary_latency_tol_us` 15000、`apst_secondary_latency_tol_us` 100000、`default_ps_max_latency_us` 100000 |

## 2. PCIe の link（VMD の下の root port 10000:e0:06.0 と NVMe）

| 項目 | root port（8086:464D、CPU の PEG の x4） | NVMe |
| --- | --- | --- |
| LnkCap | 16 GT/s x4、ASPM L1、L1 の exit < 16 µs、ClockPM 無し | 16 GT/s x4、ASPM L1、L1 の exit < 32 µs、ClockPM 無し |
| LnkCtl | **ASPM Disabled**、CommClk+ | **ASPM Disabled**、CommClk+ |
| L1SS の cap | PCI-PM L1.2・L1.1、ASPM L1.2・L1.1、CMRT 110 µs、T_POWER_ON 500 µs | PCI-PM L1.2・L1.1、ASPM L1.2（**ASPM L1.1 無し**）、CMRT 60 µs、T_POWER_ON 150 µs |
| L1SS の ctl | 全部無効、T_CommonMode 110 µs、LTR_L1.2_THRESHOLD 616448 ns、T_PwrOn 500 µs | 全部無効、LTR_L1.2_THRESHOLD 616448 ns、T_PwrOn 500 µs |
| LTR | DevCap2 LTR+、DevCtl2 LTR+ | DevCtl2 LTR+、LTR の cap の max snoop 3145728 ns（Fable の調べの「max snoop が 0 なら 0x1003」の値と同じ） |
| PM | D3hot・D3cold から PME | PME 無し |

- Linux の ASPM の policy は `default`（firmware の設定を保つ）。VMD の下の link は firmware が設定しない（隠れた root port）ので、Debian でも ASPM・L1SS は切れたまま。
- VMD（00:0e.0、8086:467F）: VMCAP 0x40 = 0x0001（bus の制限）、VMCONFIG 0x44 = 0xFE01（[9:8] = 2 → bus の始まり 224 = 0xE0、bit 1 の MSI の bypass は 0）、0x70 = 0。BAR0 0x52000004、BAR2 0x72000000、BAR4 0x55100004。VMD の PM は D3hot を持ち PME 無し。

## 3. PMC（`/sys/kernel/debug/pmc_core`）

- `package_cstate_show`: **Package C2 だけが数え、C3〜C10 は 0**。`slp_s0_residency_usec` 0、`substate_residencies` の S0i2.0 は 0。→ Debian でもこの 5330 は動作中の idle で PC3 より深くに入っていない（原因は NVMe の link だけとは限らない。i915・USB などの他の妨げもありうる、未確認）。
- `ltr_show`: XHCI の snoop 409600 ns、ISH 10485760 ns、AGGREGATED_SYSTEM の snoop 800063488 ns。VMD の下の NVMe は PMC の port の一覧に名が無い（CPU の PEG の root port は PCH の SOUTHPORT でない）。
- `/sys/power/mem_sleep` は `[s2idle]` だけ。

## 4. firmware の表（DSDT、BIOS 1.31.1）

- `\_SB.PC00.VMD0`（`_ADR` 0x000E0000）: `VDSD == 1` の時 `_DSD` の `StorageD3Enable` = 1（UUID 5025030F-842F-4AB4-A561-99A5189762D0）。`_PS0`・`_PS3` は `VMS0` が 0 でない時に `ISAT` を 0・1 にするだけ。`_S3D`・`_S4D` は 3。
- VMD0 の子に隠れた root port ごとの device がある。CPU の x4 の port は `\_SB.PC00.VMD0.PEG0`（`RPD` 6、`_ADR` 0x8030FFFF、`RSPT` 2）で、D3cold の method `VPOF`（`D3CV` の確かめ → `RL23` で link を L2/L3 へ → `VDOF` で電源を切る、`ID3C` = 1）と `VPON`（`VDON` → `RLA` → 100 ms → `ID3C` = 0）、`VR3D` を持つ。これらを呼ぶ所は DSDT の中に無い（`grep VPOF` が定義だけ）→ OS の VMD の driver（Windows の RST）が呼ぶ前提と見られる（推定）。
- PCH の root port（`RP01` など）の下の device には `STD3 != 0` の時の `StorageD3Enable` の `_DSD` がある（12745 行ほか）。5330 の NVMe は VMD の下なので VMD0 の `_DSD` が効く見込み。`VDSD`・`STD3`・`VMS0` は GNVS の値で、今の値は未読（zedBSD の AML で読める）。

## 5. zedBSD の code の事実（worktree p5-design-c、main d94ceac00）

- `src/drivers/pci/pci-vmd.c:422〜435` の `vmd_suspend` は常に EBUSY（隠れた root port に driver が無く bus の番号と窓を D3 の間に保てない、と comment）。`.resume` は無い。driver は VMD の後ろの bus を自分で番号付けし窓を置く（file の先頭の comment）。
- `src/drivers/pci/pci-power.c`: driver の付いた function を深い順に suspend（driver の suspend → config の保存（header・PM・MSI・MSI-X・PCIe の control・LTR・L1SS）→ D3hot → platform）、resume は逆。driver の無い function（VMD の後ろの root port）は触らない（`suspend_entry` 822〜、file の先頭の comment）。`drv_pci_device_keep_d0` で D0 のまま眠らせる口がある（525〜）。
- NVMe の suspend・APST・HMB の事実は [ws.md](ws.md) の「今の事実」のまま（2026-10-29 に読み直していない）。
