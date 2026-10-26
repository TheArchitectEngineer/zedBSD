# WS298 の設計: Intel VMD の driver（pci-vmd）

- Phase: [ws298-p001](phase001/phase.md)（Queue q1613、担当 P1）。版 1: 2026-10-27。
- 親: [WS298](ws.md)。関係: q1612（Latitude 5330 の NVMe に zedBSD の区画を作り、USB 無しで起動する）。
- この文書は設計だけ。source は変えていない。

## 0. 出典・読んだ物・確認の水準

| 読んだ物 | 版 | 使い方 |
| --- | --- | --- |
| FreeBSD `sys/dev/vmd/vmd.c`・`vmd.h`（BSD-2-Clause） | src-15.0（`/home/awe/freebsd/port-analysis/src-15.0/`） | **主な出典**。register の offset・bit、bus の範囲、窓（MEMBAR1 と MEMBAR2+0x2000）、MSI の address の作り方、domain の番号の選び方 |
| Linux `drivers/pci/controller/vmd.c`（GPL-2.0） | 6.12.5（`/home/awe/visionfive-linux/`、4cecf169f38e） | 照合と手順の参照だけ（設定空間の write の後の read-back、設定空間の access の直列化、bridge の窓を閉じる値）。code・名前・構造は写さない（[Guardrail](../guardrail.md) の「MIT・GPL の出典のデータの引用」） |
| PCI Local Bus Specification 3.0、PCI-to-PCI Bridge Architecture Specification 1.2、PCI Express Base Specification（ECAM の address の形、type 1 header の bus 番号・窓） | 公開の仕様 | header の offset の値 |
| zedBSD `include/drivers/pci/pci.h`、`src/drivers/pci/pci.c`（bus の作成・scan・probe、`message_register`・`establish_msix`・`map_msix_entry`）、`pci-pcat.c`（`pcat_map_bar`・`pcat_allocate_irqs`）、`pci-plda.c`（`host_msi_establish`・`host_msi_disestablish` の drain）、`pci-ecam-fdt.c`（`host_map_bar`）、`pci-window.c`・`pci-window.h`、`src/drivers/platform/rpi5/rp1-usb.c`（driver が作る root bus）、`src/kern/platform/pcat.c`（driver の登録・`probe_fixed_disks`）、`platform/amd64/vmunix.mk`・`Makefile` の CONFIG | main 84727824f | 載せ方 |
| 5330 の Debian の `lspci -nn`・`/sys/block/nvme0n1` の link・`efibootmgr -v`（2026-10-27 06:4x、P1 が ssh で読んだ） | — | 実機の構成（§2.2） |
| 素の 5330 の zedBSD の kernel log `build/hw5330/logs/p4-q1606-tools-move-dmesg.txt`（P4 の worktree） | — | 今の zedBSD が NVMe を見ていない証拠（`vfs: native boot disk=none physical disks=1`、PCI は segment 0000 の ECAM だけ） |

確認の水準: 上の source は全部 cat・grep で読んだ。Intel の VMD の register の公開の文書は**未確認**（探していない。§11 の U9）。5330 の VMD の BAR の大きさ・MSI-X の数・VMCAP/VMCONFIG の値は**未読**（5330 は P3 が q1612 で使用中のため、§2.3 の読み取りを p002 の最初に行う）。QEMU に VMD の model は無いので、QEMU の確認は無い。

## 1. 責務と境界

### 1.1 この driver がする事

1. PCI の function 8086:467f（Alder Lake の VMD、class 0x0104）に付く。
2. VMD の CFGBAR（BAR0）を ECAM として、VMD が隠している root port とその下の function の設定空間を読み書きする。
3. VMD の下の bus 番号・bridge の窓・endpoint の BAR を、MEMBAR1（BAR2）と MEMBAR2（BAR4、先頭 8 KiB を除く）の中に自分で割り当てる（§5）。
4. VMD の下の bus を zedBSD の PCI core に新しい root bus（segment 0xffff）として見せ、`drv_pci_bus_scan_tree()` で列挙させる。既存の pci-nvme の driver がその下の NVMe に付く。
5. 子の MSI・MSI-X を VMD の MSI-X の 1 つの entry へ付け替え（remap）、その entry の割り込みで子の handler を呼ぶ（§6）。

### 1.2 しない事（境界）

- BIOS の設定（RAID On）を変えない。VMD の bypass（remap を切る）は 467f に無い（FreeBSD・Linux とも 467f に bypass の flag を付けていない）ので使わない。
- 電源の管理: Linux の BIOS_PM_QUIRK（子の LTR を 0x1003 にし ASPM を点ける）はしない。S0 idle（ws052）で VMD の domain を D3 にする経路は扱わない（§7.3）。
- 子の INTx は無い（VMD は legacy の割り込みを通さない）。
- hot plug・AER・DPC・secondary bus reset はしない。
- HAL の API（`include/hal/hal.h`）は変えない。PCI core（`pci.c`・`pci.h`）も変えない（§3 の通り既存の hook で足りる）。

## 2. hardware の事実

### 2.1 VMD の register（VMD の function 自身の設定空間）

| 値 | 意味 | 出典 |
| --- | --- | --- |
| BAR0（0x10） | CFGBAR: 下の domain の ECAM。大きさ ≫ 20 が bus の数 | FreeBSD vmd.c（`vmd_attach` の bus_end の計算）、Linux vmd.c（`VMD_CFGBAR`） |
| BAR2（0x18） | MEMBAR1: 下の device の memory の窓（全体） | FreeBSD vmd.c（`rman_manage_region` の 1 つめ）、Linux（`VMD_MEMBAR1`） |
| BAR4（0x20） | MEMBAR2: 窓。**先頭 0x2000 は VMD 自身の物**で窓に使わない | FreeBSD vmd.c（`+ 0x2000`）、Linux（`membar2_offset = 0x2000`） |
| 0x40（16 bit） | VMCAP。bit 0 = bus の範囲の制限がある | FreeBSD（`VMD_CAP`・`VMD_BUS_RESTRICT`）、Linux（`PCI_REG_VMCAP`・`BUS_RESTRICT_CAP`） |
| 0x44（16 bit） | VMCONFIG。bit 1 = MSI の remap を**切る**（bypass）、bit 9:8 = bus の開始（0→0、1→128、2→224、3→不明で失敗） | FreeBSD（`VMD_CONFIG`・`VMD_BYPASS_MSI`・`VMD_BUS_START`）、Linux（`PCI_REG_VMCONFIG`・`VMCONFIG_MSI_REMAP`・`BUS_RESTRICT_CFG`） |
| 467f の性質 | bus の制限あり、MSI-X の entry 0 は子の remap に使わない（first vector = 1）、bypass は無い | FreeBSD（`vmd_devs` の 467f の行: `BUS_RESTRICT \| VECTOR_OFFSET`）、Linux（`VMD_FEATS_CLIENT` の `VMD_FEAT_OFFSET_FIRST_VECTOR`） |
| 子の MSI の address・data | address = 0xfee00000 \| (VMD の MSI-X の entry の番号 ≪ 12)、上位 32 bit = 0、data = 0 | FreeBSD（`vmd_map_msi`）、Linux（`vmd_compose_msi_msg`: destid の bit 7:0 に entry の番号） |
| CFGBAR の address | (bus − bus_start) ≪ 20 \| device ≪ 15 \| function ≪ 12 \| offset。範囲の外の bus は 0xffffffff を返し、write は捨てる | FreeBSD（`vmd_read_config`・`vmd_write_config`）。ECAM の形は PCIe Base Spec |
| 設定空間の write | VMD は設定の write を posted の memory write に変える。write の後に同じ所を読み、完了させる | Linux（`vmd_pci_write` の手順、Procedure referenced） |
| 設定空間の access | 一部の版で並行の access が CPU を止めうる。lock で直列にする | Linux（`vmd_pci_read` の前の comment の手順、Procedure referenced） |
| domain（segment）の番号 | FreeBSD は PCI_DOMAINMAX − unit（0xffff から下へ） | FreeBSD（`vmd_attach` の `psc.domain`） |

### 2.2 5330 で分かっている事（Debian の lspci、2026-10-27）

- `0000:00:0e.0 [8086:467f]` RAID bus controller（VMD）、`0000:00:06.0 [8086:09ab]` RST VMD Managed Controller（system peripheral）。
- NVMe: KIOXIA BG5（DRAM-less）`1e0f:000c`、Linux の名前で `10000:e1:00.0`、その上の root port は `10000:e0:06.0`。bus 224（0xe0）から始まるので VMCONFIG の bit 9:8 は 2 のはず。
- UEFI の entry `UEFI RST KBG50ZNS256G`（firmware は RST の UEFI driver で NVMe を見る）。
- 素の 5330 の zedBSD は segment 0000 だけを見て、NVMe を見ていない（§0）。

### 2.3 p002 の最初に読む事（読むだけ、5330 の Debian で）

`sudo lspci -vvv -xxxx -s 00:0e.0`・`-s 10000:e0:06.0`・`-s 10000:e1:00.0` と `/sys/bus/pci/devices/0000:00:0e.0/resource`。確かめる事: CFGBAR・MEMBAR1・MEMBAR2 の address と大きさと 64/32 bit・prefetchable、MEMBAR1 が 4 GiB の下か、VMD の MSI-X の entry の数と table の BAR・offset、VMCAP・VMCONFIG の値（`-xxxx` の 0x40・0x44）、root port と NVMe の BAR と MSI-X の entry の数。§5 の窓の選び方の前提（4 GiB の下に 1 MiB 以上の窓がある）をここで確かめる。前提が崩れたら p002 を止めて設計を直す。

## 3. zedBSD の PCI の枠組みへの載せ方

PCI core は host bridge ごとに `struct drv_pci_bus_ops` を取る（`pci.h`）。VMD は「PCI の function の driver が作る host bridge」として、既存の hook だけで載る:

| hook | VMD の実装 |
| --- | --- |
| `config_space_size` | 4096 |
| `config_read` / `config_write` | CFGBAR の MMIO（§4）。write の後に read-back。VMD ごとの spinlock（interrupts off）で直列 |
| `map_bar` / `unmap_bar` | 子の BAR が MEMBAR の窓の中にあることを確かめ、`kern_device_map(..., KERN_DEVICE_UNCACHED)`（`pci-ecam-fdt.c` の `host_map_bar` と同じ形）。bus address = CPU の物理 address（窓の translation は無い） |
| `allocate_irqs` | MSI-X・MSI: その capability があれば 1 つの記録を返す（`pcat_allocate_irqs` と同じ形）。INTx: ENOTSUP |
| `msi_establish` / `msi_disestablish` | §6 |
| `intx_*` | 無し（4 つとも NULL） |
| `defer_unassigned` | 0（scan の前に全部の BAR を割り当て済み） |

root bus は `drv_pci_bus_create_root(segment, bus_start, &ops, vmd, dma, &root)` で作り、`drv_pci_bus_scan_tree(root)` で列挙する（`rp1-usb.c` の `publish_bus` と同じ順）。

- **入れ子の scan**: VMD の attach は pcat の bus 0 の scan の途中（`drv_pci_bus_scan` → `drv_pci_device_probe` → attach）で呼ばれる。`drv_pci_bus_create_root` は新しい bus を `root_buses` の先頭に足すだけで、外側の `drv_pci_scan_all` の for は今の bus の `next` を辿るので新しい bus を二度 scan しない。core に lock は無く、boot の scan は 1 本の thread（確かめた: `pci.c` 295〜560 行）。
- **driver の登録の順**: VMD の attach の中で子が probe されるので、pci-nvme（と AHCI）の driver は VMD より先に登録されている必要がある。`src/kern/platform/pcat.c` で `drv_pci_nvme_driver_register()`・AHCI の後に `drv_pci_vmd_driver_register()` を置く。
- **disk の probe**: NVMe の namespace は scan の後の `probe_fixed_disks()`（`drv_pci_nvme_probe_namespaces()`）で読まれる。VMD の下の NVMe も scan の中で attach 済みなので、そのまま拾われる見込み（p002 で `physical disks=2` を確かめる）。
- **shutdown**: `drv_pci_shutdown()` は `root_buses` の順に回る。VMD の bus は先頭なので NVMe の shutdown が VMD より先。VMD 自身の shutdown は何もしない。
- **DMA**: 子の DMA は VMD を通って host の memory へそのまま届く（translation 無し）。root bus の dma は VMD の function の `drv_pci_device_dma()` をそのまま使う。
- **segment**: 0xffff − instance（instance は 0 から、最大 4）。PC の ACPI の `_SEG` がこの高さを使う例は知られていない。名前は `ffff:e1:00.0` の形になる。kernel の disk の層は segment を使わない（`src/kern/disk.c` に segment の参照が無い）。

## 4. 設定空間の access

- offset = ((bus − bus_start) ≪ 20) + (device ≪ 15) + (function ≪ 12) + reg。bus が [bus_start, bus_end] の外、または offset + width が CFGBAR の大きさを超える時: read は 0xffffffff（width に合わせて切る）、write は捨てて EINVAL。
- bus_end = min(255, bus_start + (CFGBAR の大きさ ≫ 20) − 1)。CFGBAR が 1 MiB 未満なら attach を失敗にする（Linux は 1 MiB 未満で拒む）。
- 1・2・4 byte の access。write の後に同じ address を同じ幅で読む（posted の完了）。
- lock: VMD ごとの `struct spinlock` を `spin_lock_irqsave` で。scan の前の割り当て（§5）も同じ access の関数を通す。

## 5. bus 番号と BAR の割り当て（scan の前に VMD の driver が行う）

firmware（RST の UEFI driver）の割り当てを当てにせず、毎回同じ規則で割り当てる。理由: firmware が残した bus 番号が bus_start の基準か分からない、USB から起動した時に firmware が VMD の domain を設定するか分からない、毎回同じ結果なら試験と log で確かめやすい。firmware が残した値は割り当ての前に log に書く（解析用）。

### 5.1 順

1. **見つける**: bus_start の 32 slot × function（header type の bit 7 で多機能）を CFGBAR で読む。vendor が 0xffff なら無い。
2. **止める**: 見つけた function の command（0x04）の memory・I/O・bus master を落とす（BAR を書き換える間に decode・DMA させない）。
3. **root port の窓を閉じる**: type 1 header（header type の bit 6:0 = 1、class 0x0604）の I/O・memory・prefetchable の窓を「base > limit」にする。値: I/O base/limit（0x1c）= 0x00f0、I/O の上位（0x30）= 0x0000ffff を一度書いてから 0、memory base/limit（0x20）= 0x0000fff0、prefetchable base/limit（0x24）= 0x0000fff0、上位 base（0x28）= 0xffffffff、上位 limit（0x2c）= 0。Procedure referenced: Linux vmd.c（`vmd_domain_reset`）。offset は PCI-to-PCI Bridge Spec 1.2。
4. **bus 番号**: root port を見つけた順に、primary（0x18）= bus_start、secondary（0x19）= subordinate（0x1a）= 次の空き番号（bus_start + 1 から）。番号が bus_end を超えたらその root port は使わない（log）。
5. **下を見つける**: 各 root port の secondary の bus の function を列挙する。**下に type 1 header（switch）があればその root port は扱わない**（log して窓を閉じたまま。深さ 1 だけを扱う、§11 の U4）。
6. **BAR の大きさ**: 下の endpoint の BAR0〜5 を、全部 1 を書いて読み戻す標準の手順で測る（64 bit の BAR は 2 つの register）。I/O の BAR は割り当てない（VMD は I/O を通さない。FreeBSD の `vmd_get_rman` は I/O に NULL）。ROM の BAR は触らない。
7. **root port の窓を置く**: root port ごとに、下の memory の BAR の大きさを、BAR の大きさ（最小 4 KiB）に揃えて足した量を 2 の冪（最小 1 MiB）に丸めた大きさの窓を、`drv_pci_window_place()`（`pci-window.c`）で置く。windows = { MEMBAR1 全体、MEMBAR2 の +0x2000 から終わりまで }、busy = それまでに置いた窓、`wide = false`（bridge の memory の窓は 32 bit なので 4 GiB の下だけ）。置けなければその root port は使わない（log）。
8. **BAR を置く**: 窓の中に、大きい BAR から順に `drv_pci_window_place()`（windows = その窓、busy = 置いた BAR、`wide = false`）で置き、BAR に書く（64 bit の BAR の上位は 0）。prefetchable の BAR も non-prefetchable の窓に置く（prefetchable の窓は使わない。どちらに置いても正しい）。
9. **窓を開ける**: root port の memory base/limit（0x20/0x22）に窓を書き（1 MiB 単位、上位 12 bit）、command の memory と bus master を立てる。endpoint の command は落としたまま（driver が `drv_pci_device_enable()` で立てる）。

### 5.2 純な関数に分ける

5.1 の 1〜9 は `pci-vmd-plan.c` の純な関数にし、設定空間の access を関数の表（read・write の callback）で受ける。kernel では §4 の access を渡し、host 試験では配列の上の模型を渡す（§9.1）。hardware を触る部分（MMIO・割り込み・PCI core の呼び出し）は `pci-vmd.c` に置く。

## 6. 割り込み

### 6.1 VMD 自身の MSI-X

- attach で `drv_pci_device_allocate_irqs(vmd, DRV_PCI_IRQ_ALLOW_MSIX, 1, 1, &irq, &n)` で記録を 1 つ得て、`irq.index` を first vector（467f は 1）に変えてから `drv_pci_device_establish_irq()` で handler を付ける。core の `establish_msix` は `irq->index` の entry の table を map して書く（`pci.c` 1870 行・3523 行）。entry 0 は使わない（467f は entry 0 を子の remap に使わない、§2.1）。
- MSI-X が無い、または entry が 2 つ未満なら attach を失敗にする（remap ができないので子は割り込みを持てない）。
- 子の remap を使うので VMCONFIG の bit 1（bypass）を 0 にする（FreeBSD `vmd_set_msi_bypass(dev, false)`、Linux `vmd_set_msi_remapping(vmd, true)` と同じ向き）。
- 子は全部この 1 つの entry を共有する（§11 の U2）。FreeBSD は entry ごとに利用者を数えて最も空いた entry を選び、Linux は NVMe だけを空いた entry に分ける。5330 の下は NVMe が 1 つ（の見込み）なので 1 つで足りる。

### 6.2 子の message（`msi_establish` / `msi_disestablish`）

- 表: 子の message の slot を固定の数（16）持つ。各 slot に run・argument・in_flight。
- `msi_establish`: 空いた slot（run が NULL で in_flight が 0）を取り、run・argument を入れる。handle = slot の番号、address = 0xfee00000 | (first vector ≪ 12)、data = 0。
- VMD の entry の handler: lock の下で使用中の slot の in_flight を上げて run を取り出し、lock を外して各 run を呼び、lock の下で in_flight を下げる。どれかが仕事を見つけたら 1 を返す。end of interrupt は core（VMD の MSI-X の dispatch）が出す。
- `msi_disestablish`: slot を空け、in_flight が 0 になるまで 1 ms ずつ最大 1 秒待つ。戻らなければ slot を元に戻し EBUSY（`pci-plda.c` の `host_msi_disestablish` と同じ契約、`pci.h` の msi_establish の comment）。
- 子の MSI（MSI-X でない）も同じ address・data になる。FreeBSD は MSI を 1 つに限る（`vmd_max_msi = 1`）。zedBSD の allocate_irqs も 1 つだけを返す。

## 7. 寿命

### 7.1 attach

1. `drv_pci_device_enable_memory()`・`drv_pci_device_set_bus_master()`（FreeBSD `pci_enable_busmaster`）。
2. BAR0・2・4 を `drv_pci_device_bar()` で読み、CFGBAR を `drv_pci_device_map_bar()` で map。MEMBAR1・2 は map しない（範囲を窓として使うだけ）。
3. VMCAP・VMCONFIG を読み bus_start・bus_end を決める。VMCONFIG の bit 9:8 が 3 なら失敗。
4. log: `vmd: 0000:00:0e.0 8086:467f buses e0-ff cfgbar ... membar1 ... membar2 ... vmcap .... vmconfig .... msix N`。
5. MSI-X の entry を付け（§6.1）、bypass を切る。
6. 割り当て（§5）。root port ごとに log: `vmd: root port ffff:e0:06.0 bus e1 window ...-...` と BAR ごとに `vmd: ffff:e1:00.0 BAR0 ... size ...`。firmware が残していた値も 1 行。
7. root bus を作り scan（§3）。
8. 失敗は途中までを戻す（MSI-X の handler を外す、CFGBAR を unmap）。root bus を作った後の失敗は無い（scan の結果は問わない）。

### 7.2 detach・shutdown

- detach: 子が付いている間は EBUSY。boot の後に VMD を外す経路は無いので、子の detach から順に戻す一般の手順は作らない。
- shutdown: 何もしない（子の shutdown は core が先に呼ぶ）。

### 7.3 suspend・resume（S0 idle）

VMD の domain の root port は zedBSD の PCI core から見ると driver の無い bridge で、D3 からの戻りで窓・bus 番号が消えると子が見えなくなる。p002 では VMD の `suspend` が EBUSY を返して S0 idle に入らないようにする（`drv_pci_suspend_all` が失敗を返す function を報告する。5330 の S0 idle は p002 の範囲の外）。§11 の U7。

## 8. file と結線

| file | 中身 |
| --- | --- |
| `include/drivers/pci/pci-vmd.h` | `drv_pci_vmd_driver_register()`、plan の型（host 試験も使う） |
| `src/drivers/pci/pci-vmd.c` | driver（match・attach・bus の ops・割り込み） |
| `src/drivers/pci/pci-vmd-plan.c` | §5 の純な割り当て（hardware に触らない、host 試験がそのまま compile する） |
| `platform/amd64/vmunix.mk` | `CONFIG_DRIVER_PCI_VMD` が y の時に 2 つの .c を足す（`pci-window.c` は既に入っている） |
| `Makefile` | `CONFIG_DRIVER_PCI_VMD ?= $(if $(filter amd64,$(ZEDBSD_PLATFORM)),y,n)` と `-DCONFIG_DRIVER_PCI_VMD=` |
| `src/kern/platform/pcat.c` | NVMe・AHCI の登録の後に `drv_pci_vmd_driver_register()` |
| `plan/ws298/tests/host-vmd-plan.c`・`run-host-vmd.sh` | §9.1 |

規約は [plan/coding-style.md](../coding-style.md) の全文。出典は block ごとに複数行の comment で `Values referenced: FreeBSD 15.0 sys/dev/vmd/vmd.c (...)`・`Procedure referenced: Linux 6.12.5 drivers/pci/controller/vmd.c (...)` の形、file の先頭には書かない（Guardrail）。名前は FreeBSD・Linux の symbol を写さず自分で付ける。

## 9. 確かめ方

### 9.1 host 試験（p002、短い）

配列の上の CFGBAR の模型: bus_start 224、slot 6 に root port（type 1、class 0x0604、窓は firmware の値の入った状態）、その下の bus に NVMe（BAR0 64 bit 16 KiB、全部 1 を書いた時に大きさの mask を返す）。確かめる事:

1. 設定空間の offset の計算と範囲の外（bus 223・256 相当、CFGBAR の終わりを越える access）。
2. VMCONFIG の bit 9:8 の 0・1・2・3 の扱い。
3. 割り当ての結果: root port の bus 番号（e0/e1/e1）、窓（MEMBAR1 の中、1 MiB 揃え、4 GiB の下）、NVMe の BAR0（窓の中、16 KiB 揃え、上位 0）、窓を閉じる値、command の bit。
4. MEMBAR2 の先頭 0x2000 に何も置かない。MEMBAR1 が 4 GiB の上で MEMBAR2 が小さい時に「置けない」を返す。
5. 下に switch がある root port を扱わない。
6. 子の MSI の address が 0xfee01000（first vector 1）、data 0。

### 9.2 build

`make ZEDBSD_CONFIG=plan/tools/hw5330/config-5330.mk BUILD=<自分の dir> disk-image`（または kernel だけの target）で warning 0。

### 9.3 素の 5330（p002 の受け入れ）

q1612 の手順と同じ 5330 の USB（または q1612 が済んでいれば NVMe の区画）から起動し、kernel log で:

- `vmd:` の行（bus の範囲、root port、NVMe の BAR）。
- `nvme:` の attach が segment ffff の function で出る。
- `vfs: native boot disk=... physical disks=2`。
- zedBSD の ssh から NVMe の disk の先頭を読み、GPT の署名（LBA 1 の `EFI PART`）を確かめる（device の名前は p002 で確かめる）。書き込みはしない。

この試験は 5330 を 1 回起動する（`plan/tools/hw5330/5330-run.sh --kernel` で USB の kernel を差し替え、`EXTRA` で読み取りの command）。QEMU の確認は無い（QEMU に VMD の model が無い）。

## 10. p002 の順

1. §2.3 を読む（5330 が空いてから、読むだけ）。前提を確かめる。
2. `pci-vmd-plan.c` と host 試験。
3. `pci-vmd.c`、結線、build。
4. 5330 で 1 回（§9.3）。だめなら kernel log の `vmd:` の行で段を切り分ける（設定空間・割り当て・MSI-X・NVMe の reset）。
5. 記録、commit、Q1 へ。

## 11. 判断の項目（design-reviewer とユーザーへ）

| ID | 項目 | 推し | 他の案 |
| --- | --- | --- | --- |
| U1 | firmware の割り当てを捨てて毎回割り当てる（§5） | 推し: 毎回割り当て（Linux と同じ向き、試験しやすい） | firmware の値が正しければ使う（FreeBSD の向き）。firmware の値の形が分からないので検証の分岐が増える |
| U2 | 子の割り込みは VMD の entry 1 つを共有 | 推し: 1 つ（5330 は NVMe だけ） | entry ごとに利用者を数えて分ける（FreeBSD・Linux）。NVMe が複数の機械で要る |
| U3 | secondary bus reset をしない | 推し: しない（FreeBSD もしない。NVMe の driver が controller を reset する） | Linux は probe で root port の下を reset する（約 1 秒かかる） |
| U4 | root port の下は深さ 1（switch は扱わない） | 推し: 深さ 1 | 再帰で switch も扱う |
| U5 | Linux の BIOS_PM_QUIRK（LTR・ASPM）をしない | 推し: しない（機能に要らない。電力は後） | する |
| U6 | segment を 0xffff − instance | 推し: その通り（FreeBSD と同じ） | 0x1000 から上 |
| U7 | S0 idle の間 VMD は EBUSY で止める | 推し: 止める（5330 の S0 idle は今の範囲の外） | root port の状態を保存・復元する |
| U8 | HAL・PCI core を変えない | 推し: 変えない（hook で足りる） | — |
| U9 | Intel の公開の register の文書を探していない | 推し: FreeBSD（BSD-2）と Linux の照合で足りる、と記録 | Intel の datasheet で確かめる |
