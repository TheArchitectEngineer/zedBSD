<!-- awesome-plan project=zedbsd record=ws037-p004 -->

# ws037-p004: GSP の起動（P0〜P3・P7）と方式 B（VFIO）の試験の手順

Status: in-progress（2026-10-14 P1。4a〜4e を実装、build と host 試験まで。方式 B の R0・R1 PASS、R2 は P7 で FAIL、R3 は P2b で止まった（T2）。原因の解析と直しを commit `1c1732db7`。再試験で R2 PASS、R3 は libos の init で止まった。nouveau との照合で RMARGS の region の大きさの違いを直した（`714ab8477`）。R3・R4 の再試験は T2 待ち。下の「R2・R3 の結果への対応」と「再試験の R3（libos で停止）と nouveau との照合」）
Disposition: normal
Parent: [WS037](../ws.md)
Queue: Q1 の割当（2026-10-14「p004 を 4a から順に実装して進めること」）
依存: [p003](../phase003/phase.md) の骨格（commit `8a52c965d`）の main への統合、firmware の package `nvidia-gsp-firmware`（commit `dfa4a2592`）。試験は host 10.0.30.5 の Linux（Debian 13）と VFIO の準備（Q1、[host.md](../host.md)）。
実行者: phase-runner（high）

設計の正は [nvrtx-design.md](../nvrtx-design.md) の 2・3・5・6・10 節。この phase.md はそれを実装の単位と試験の手順に落とした物。手順の値の出典は改名の後の作業の文書 `plan/ws037/temp/rename/renamed/gsp-boot.md`（commit しない）。

## 0. ベータ2（10/16 夕）との関係

既定は `CONFIG_DRIVER_PCI_NVRTX=n`。有効でも `nvrtx.gsp=1` が無ければ N1 で止まる（p003 のまま）。下の段は **前から順に、どこで切っても build と既定の動作が変わらない**単位に分ける。間に合わない段は入れない（drop）。各段は `nvrtx.stop=<段>` で手前で止められる。

| 単位 | 中身 | 実機 | 落とし方 |
| --- | --- | --- | --- |
| 4a | host だけの部品: firmware の file の検査（bin の header・ELF の section）、radix3 の page table、queue の配置と要素の枠（checksum・折り返し）、WPR の配置の計算、RPC の header | 不要（host 試験） | 入れても呼ばれない（`nvrtx.gsp=1` の時だけ） |
| 4b | P0: root の mount の後の worker、firmware の読み込み（`/lib/firmware/nvidia/tu102/`、28.5 MB）、DMA の確保、VBIOS の読みと FWSEC の取り出し、配置の計算。**GPU には書かない**（VBIOS の読みの窓の切り替えを除く、下の 2） | 方式 B | `nvrtx.stop=P1a` |
| 4c | P1a・P1b: devinit の完了待ち（読むだけ）、WPR2 の確認、FWSEC-FRTS を GSP の falcon で | 方式 B | `nvrtx.stop=P1b` |
| 4d | P2a・P2b: GSP を RISC-V の起動へ、libos の args、booter_load を SEC2 で、RISC-V の確認、START_OK の待ちと CPU sequencer | 方式 B | `nvrtx.stop=P2a` |
| 4e | P3 の最小: static info、client・device・subdevice。P7: unload → FWSEC-SB → booter_unload、shutdown・reboot で | 方式 B | `nvrtx.stop=P3` |

4b 以降で attach は `nvrtx.gsp=1` かつ TU10x（`gsp_tu102`）の時だけ bind する（今は常に ENODEV）。bind した device は `suspend` で EBUSY を返し、system の suspend を断る（GSP の suspend は範囲の外）。`shutdown` で P7 を流す。

## 1. 部品と file（`src/drivers/gpu/nvrtx/`）

| file | 中身 | host 試験 |
| --- | --- | --- |
| `fw.c` | firmware の file の読み込み（i915 の `firmware.c` と同じく root の mount を待つが、上限は 32 MiB）。file の先頭の 6 語の header（magic 0x10de・ver 1）と HS の header の範囲の検査。GSP-RM の ELF64 の section header の走査で `.fwimage` と `.fwsignature_tu10x` を名前で探す（中身は解析しない） | 有（作った header の file と、build の中の実物の file の header だけ） |
| `layout.c` | VRAM の大きさ（0x100ce0 の値から）と VGA の workspace から WPR2・heap・ELF・bootloader・FRTS の番地を出す純粋な関数。heap の大きさの式 | 有（8 GiB で heap 105 MiB・WPR2 135 MiB、[design](../nvrtx-design.md) 10 節 13） |
| `radix3.c` | 4 KiB の DMA の page の集まりから 3 段の表を作る（lvl0 1 page・lvl1 1 page・lvl2 は page ごとの番地）。zedBSD の DMA の vector は 64 KiB までなので、28.5 MB は 64 KiB の chunk の集まりで持つ | 有（chunk の番地の並びから表の中身） |
| `queue.c` | 共有 memory の queue（PTE の配列 4 KiB、cmdq 256 KiB、msgq 256 KiB、slot 4 KiB × 63）、要素の header（48 byte）と RPC の header（32 byte）、checksum、折り返し、16 slot までの分割。pointer の置き場所の交差（cmdq の rx に CPU の読み pointer） | 有（模擬の相手で送受信、折り返し、分割、checksum） |
| `rpc.c` | RPC の担当の worker。投入は直列、応答は **sequence で照合**（function の番号だけにしない、design 2 節 6）。event（0x1000 以上）の振り分け、CPU sequencer の 9 つの opcode | 有（queue の模擬の上で） |
| `falcon.c` | falcon の reset・IMEM/DMEM の PIO・boot・halt 待ち（GSP 0x110000、SEC2 0x840000、SEC2 は PMC の enable も） | 無（実機） |
| `fwsec.c` | VBIOS の BIT の 'p' token → PMU の表 → type 0x85 → v2 の desc、DMEM の interface の patch（FRTS 0x15・SB 0x19）、`acr/bl.bin` を bootloader に | 一部（VBIOS の image の解析は作った image で） |
| `gsp.c` | P0〜P3・P7 の順の本体と段の印 | 無（実機） |

struct の形（WPR meta 256 byte、libos の args、RMARGS の r570 の形（`bDmemStack = 1`）、要素と RPC の header、system info）は自分で書く（判断の項目 2）。大きさと offset は host 試験の static assert で事実の値と合わせる（design 10 節 13）。名前は改名の後の名前（`temp/rename/rename-map.tsv`）。

## 2. 段ごとの具体（自分の言葉、値は事実）

- **P0（書かない）**: firmware 5 つを読み、header と section を検査。DMA の確保（ELF の image の chunk、radix3、signature 256 byte 整列、bootloader、WPR meta、libos の args の page、log 3 つ（各 64 KiB）、RMARGS、queue 516 KiB）。全部を GSP が動く間ずっと保つ（design 2 節 5）。VBIOS: PRAMIN（0x625f04 が有効で VRAM を指す時、0x1700 に窓を書いて 0x700000 から読む）→ だめなら PROM（PCI の config の 0x50 の bit 0 で ROM の shadow を切り、BAR0 の 0x300000 から読み、戻す）。**この 2 つは窓の切り替えの書き込みで、読み終えたら元に戻す**。image の検査（0xAA55・PCIR）、type 0xE0 の image の読み替え、FWSEC の desc の版が 2 であること（3 なら止まる）。FWSEC-FRTS と FWSEC-SB の ucode を作る。WPR の配置を計算。system info と registry の RPC を cmdq に積む（GSP はまだ読まない）。
- **P1a（読む）**: 0x118128 の bit 0 と 0x118234 の bits 7:0 = 0xFF を最大約 4 s 待つ。
- **P1b**: WPR2 の上限（0x1fa828）が 0 でなければ止まる（前の起動の残り）。FWSEC-FRTS を GSP の falcon で（bootloader 経由、FRTS の領域 1 MiB）。成功の判定は 0x001438 の bits 31:16 = 0 と WPR2 の下限・上限が 0 でないこと。
- **P2a**: GSP の falcon を reset、GSP の MAILBOX0/1 に libos の args の番地、booter_load を SEC2 で（署名は SEC2+0x408 の bit 20 で選ぶ、MAILBOX0/1 に WPR meta の番地）、SEC2 の MAILBOX0 = 0、GSP+0x080 に bootloader の app の版。
- **P2b**: 0x111240 の bit 0（RISC-V が走る）、msgq を poll して START_OK（0x1001）を数秒待つ。間の CPU sequencer（0x1002）を実行。log の put の値を印に出す（中身は解読しない）。
- **P3**: static info（65）、client（0x0）→ device（0x80）→ subdevice（0x2080）。handle は kernel が決める。
- **P7**: unload（47、0・0・0）→ GSP の MAILBOX0 = 0x80000000 を最大 2 s → GSP の falcon の reset → FWSEC-SB（0x001454 の bits 15:0 = 0）→ booter_unload（WPR2 の上限が 0 なら飛ばす、MAILBOX0/1 = 0xff・0xff）→ WPR2 の上限 = 0。失敗の扱いは design 3.1 の表（自動の再試行なし）。
- **段の印**: 各段の begin・ok・fail を `nvrtx_stage_mark` で。方式 B では console は QEMU の仮想の VGA（primary）にあるので、GSP の起動で RTX の画面が変わっても印は残る。text console の切り離し（判断の項目 12、pcat の口）は方式 B では要らず、方式 A の前（p004 の後）に作る（Q1 の許可、2026-10-14）。

## 3. 方式 B（VFIO）の試験の手順（T1 が流す、host の準備は Q1）

### 3.1 host（10.0.30.5、Debian 13、headless）— Q1 が用意

- kernel の command line に `intel_iommu=on iommu=pt`。RTX 2070 の全 function（video・audio・USB・UCSI、`lspci -nn -d 10de:` で数える）を `vfio-pci` に（`/etc/modprobe.d/` で `options vfio-pci ids=…`、`nouveau`・`nvidia` を blacklist）。IOMMU の group に他の device が無いこと。
- QEMU・OVMF（Debian の物）。centris と同じく `/tmp/nvrtx-hw.lock` で同時に 1 つ。強制終了の時は host の reboot（ssh）で WPR2 を消す（Q1 の回答 A）。FLR・bus reset で WPR2 が消えるかを各 run の最後に記録する。
- host.md の「調べて埋める」の表を埋める（PCI の address と ID、group、FLR、`reset_method`、BAR の大きさ、VBIOS の hash（sysfs の rom、BLOB は repository に入れない））。

### 3.2 QEMU の command line の案

```sh
qemu-system-x86_64 -machine q35,accel=kvm -cpu host -smp 4 -m 8G \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=$RUN/OVMF_VARS_4M.fd \
  -device VGA,id=console \
  -device pcie-root-port,id=rp1,chassis=1,slot=1 \
  -device vfio-pci,host=0000:01:00.0,bus=rp1,addr=0.0,multifunction=on,rombar=0 \
  -device vfio-pci,host=0000:01:00.1,bus=rp1,addr=0.1 \
  (USB・UCSI の function も同じ形で 0.2・0.3) \
  -drive file=$IMAGE,format=raw,if=virtio \
  -qmp unix:$RUN/qmp.sock,server=on,wait=off -display none -vnc none \
  -serial unix:$RUN/serial.sock,server=on,wait=off
```

- 仮想の VGA を先に置いて primary にする。`rombar=0` で OVMF に RTX の option ROM を走らせない（GOP を 1 つにする）。Turing の devinit は電源投入の時に GPU の中の firmware が済ませる（P1a で確かめる）ので、option ROM が無くても P1a は通る見込み（**未確認**）。通らなければ `rombar=1`（OVMF が RTX の GOP も作る）を試し、loader が仮想の VGA を選ぶことを確かめる。
- PROM の読みは BAR0 の 0x300000（VFIO の BAR0 の直の map）。QEMU は BAR0 の 0x88000〜の config の写しを横取りするので、ROM の shadow の切り替えは PCI の config の 0x50 で行う（2 節）。それでも VBIOS が読めない時の代わりは、host の sysfs の rom を `romfile=` で渡し guest の PCI の ROM の BAR から読む道（**未確認**）。
- 4 GiB を超える BAR（BAR1 256 MiB・BAR3 32 MiB は 64 bit）が OVMF で割り当てられること（`-fw_cfg name=opt/ovmf/X-PciMmio64Mb,string=65536` が要るかもしれない）。

### 3.3 guest の image

- config: `plan/ws037/tests/config-amd64-nvrtx.mk`（作る）= `include config/ci/config-amd64.mk` に `CONFIG_DRIVER_PCI_NVRTX := y`、`ZEDBSD_USER_PROGRAMS += nvidia-gsp-firmware`、`ZEDBSD_GRAPHICAL_BOOT := n`、`ZEDBSD_GRAPHICAL_LOGIN := n`、`ZEDBSD_BOOT_KERNEL_MESSAGES := y`、`ZEDBSD_BOOT_EXTRA_LINES := nvrtx.gsp=1 nvrtx.stop=<段>`。firmware の license は `yes | make firmware-licenses`（T1 の image の作り方の規則、AGENTS.md の「試験の image の作り方」）。
- 段ごとに image を作り直さず、boot の語は image の boot の config の最後の行なので、T1 は段の語だけを変えた image を作る（`BUILD` を段ごとに分ける）。

### 3.4 何を見るか（判定）

| run | boot の語 | 合格 |
| --- | --- | --- |
| R0 | （nvrtx.gsp なし） | 仮想の VGA の console に `nvrtx: N0 ok chip=166 TU106` と `N1 gop … is not on this GPU`、login prompt（boot-test と同じ画面の撮り方） |
| R1 | `nvrtx.gsp=1 nvrtx.stop=P1a` | P0 の ok（firmware の大きさ・FWSEC の desc の版・WPR の配置の行）、login prompt |
| R2 | `nvrtx.gsp=1 nvrtx.stop=P2a` | P1a・P1b の ok（WPR2 の下限・上限）、`P7 left: region … without a started GSP; reboot the host before the next run`（2026-10-14 に変更、下の「R2・R3 の結果への対応」）、login prompt。**R2 の後は host を reboot** |
| R3 | `nvrtx.gsp=1 nvrtx.stop=P3` | P2b の ok（`P2b ok: GSP-RM is up after N ms`）、続けて `P3 skipped`・`P7 begin: unload`・`P7 begin: FWSEC-SB`・`P7 booter_unload`・`P7 ok: region torn down`（P7 は boot の中で worker が流す） |
| R4 | `nvrtx.gsp=1` | P3 の ok（client・device・subdevice）、`poweroff` の後の P7 の ok |

- 判定は QMP の `screendump` の PNG（仮想の VGA の console）と、serial（`plan/tools/guest/serial.py`）での対話（`dmesg | grep nvrtx` を打って画面で読む）。serial の log を読んで判定しない（AGENTS.md）。
- 各 run の最後に guest の中で `poweroff`（P7 が走る）→ QEMU の終了を待つ。QEMU を強制で殺した時と P7 が失敗した時は、host で WPR2 が残るので、次の run の前に Q1 が host を reboot する。
- 固まった時は gdbstub（`-S -gdb tcp::<port>`）・QMP の `info registers` で止まった所を見る（2026-09-23 の解析の規則）。boot の log を取り直さない。

## 4. 受け入れ

- 4a: host 試験が PASS（struct の大きさ、WPR の配置の 8 GiB の例、radix3、queue の送受信・折り返し・分割・checksum、firmware の header）。amd64 の CI の config で y・n とも build warning 0、n で動作が変わらない。
- 4b〜4e: 方式 B の R1〜R4 の合格（PNG）。実機の証拠と QEMU の証拠は分けて書く（方式 B は「QEMU の guest に渡した実機の GPU」と書く）。
- GPL の source の名前・comment が code に無い（`temp/rename/rename-map.tsv` の旧名で `git grep` が 0）。

## 5. 危険と未確認（design 10 節に加えて）

1. VFIO の下で `rombar=0` の時に GFW の devinit が済んでいるか（P1a）。
2. VFIO の下の VBIOS の読み（PRAMIN の窓、PROM の shadow の切り替え）。
3. GSP-RM が IOMMU の下の DMA の番地（guest の物理 → host の IOMMU）で動くこと（nouveau の注記では GSP-RM が自分で変換を扱う）。
4. FLR が無い場合の host の reboot の要否（Q1 が run ごとに記録）。
5. 570.144 の struct と nouveau の 535 の混在（design 10 節 6）。host 試験で大きさを合わせても、中身の意味は実機でだけ分かる。

## 結果（2026-10-14、P1）

実装は全部 `CONFIG_DRIVER_PCI_NVRTX=y` かつ boot の語 `nvrtx.gsp=1` かつ TU10x の時だけ動く。既定（n）の kernel は nvrtx の symbol 0（`build/ws037-k-n`、rc 0・warning 0・check PASS）。`nvrtx.gsp=1` の無い y の kernel は p003 と同じ（N0・N1 の後 ENODEV）。

| 単位 | commit | 中身 |
| --- | --- | --- |
| 4a | `59cd3b4b9` | `nvrtx-gsp.h`・`wire.c`・`image.c`・`layout.c`・`radix3.c`・`queue.c`（hardware に触れない部品） |
| R0 の config | `0ea563474` | `plan/ws037/tests/config-amd64-nvrtx.mk`（`NVRTX_RUN=R0〜R4`） |
| 4b | `6229ea0b1` | attach が bind して worker を起こす（suspend は EBUSY）。`file.c`（root の mount を待ち、GSP の image は section の表だけ読んで `.fwimage` を 1 MiB の DMA の chunk へ直に）、`memory.c`、`rom.c`（PRAMIN、だめなら PROM。窓の切り替えだけ書いてすぐ戻す）、`vbios.c`（image の連鎖・BIT・falcon の表・FWSEC v2・command 欄）、`gsp.c`（P0） |
| 4c | `0a8e41fbd` | `falcon.c`（reset・PIO・boot）、`boot.c`（FWSEC を GSP の falcon で `acr/bl.bin` 経由、booter を SEC2 で、region の解除）。P0 に device info の表から SEC2 の reset bit。P1a（devinit の完了待ち）、P1b（WPR2 が既にあれば書かずに止まる、bus master、FWSEC-FRTS、0x1438 と WPR2 で判定） |
| 4d | `09d88f135` | `records.c`（system info 928 byte・registry 3 項目・unload・sequencer の解読）、`rpc.c`（送信と doorbell、sequence での照合、sequencer の実行）。P2a（system info と registry を積む → GSP の falcon を RISC-V の mode へ → libos の番地 → booter_load → app の版 → RISC-V の確認）、P2b（init done を 10 s 待つ） |
| 4e | `82b183ee7` | P3（static info 1656 byte を読み、名前・VRAM・BAR1/BAR2 の根を印に。client・device・subdevice を確保）。P7（unload の call → GSP の mailbox 0x80000000 → GSP の falcon の reset → FWSEC-SB → booter_unload → WPR2 の上限 0） |

- 終わり方: この版は P4 が無いので、worker は届いた段の後に必ず P7（GSP が走っていれば unload と reset、region を張っていれば SB と booter_unload）を流し、DMA の memory を返して終わる。falcon が halt しない・GSP が止まらない時は memory を返さず（`FAILED_HOLD`）、detach も EBUSY（電源断まで）。自動の再試行はしない。
- struct の大きさと offset（system info 928・static info 1656・RM の確保の引数 120/56/4・WPR の記録 256・RMARGS 72・queue の header）は、参照の header を temp で compile して出した値（`plan/ws037/temp/offsets/`、commit しない）を事実として使い、zedBSD の code では byte の offset で 1 欄ずつ書く（構造体を写さない）。
- host 試験: `plan/ws037/tests/nvrtx-gsp-host-test.sh` PASS（124 項目、実物の firmware 5 file を含む）、`nvrtx-identify-host-test.sh` PASS（14 case と device info の表）。amd64 の y の kernel rc 0・warning 0・check PASS（+27 KiB）。
- 未実施: 実機（方式 B の VFIO、方式 A）での全段。方式 B の R0〜R4 を T1 に依頼する（3.4 節）。R4 の合格は「P3 ok: client, device and subdevice」と「P7 ok: region torn down」。
- 範囲の外のまま: P4 以降（VRAM の allocator・MMU・channel）、display（P8）、text console の切り離し（方式 A の前）、suspend、reboot・shutdown の P7（今は worker が必ず止めて終わるので要らない）。

## R2・R3 の結果への対応（2026-10-14、P1）

T2 の結果（[results-20261014-nvrtx-vfio](../../agents/T2/results-20261014-nvrtx-vfio.md)、QEMU の guest に VFIO で渡した実機の RTX 2070）: R0・R1 PASS。R2 は `P1b ok: region 01ffe000-01ffee00` の後 `P7 begin: FWSEC-SB` → `P7 booter_unload` → `booter: mailbox 00000029 000000ff` → `P7 fail: region still up to 01ffee00`。R3（host の warm reboot の後）は `P1b ok` → `P2a ok: RISC-V running` → `P2b begin: waiting for init done` のまま 5 分以上 ok も fail も出ず、guest は idle（RIP は HLT）。**host の warm reboot で WPR2 は消えた**（R3 の P1b が ok）。

### R2 の P7 の原因（MIT の事実から）

- nouveau（MIT）の TU102 の停止（`tu102_gsp_fini`）は、GSP-RM が動いている時（`gsp->running`）だけ呼ばれ、順は unload の RPC → GSP の MAILBOX0 が 0x80000000 → GSP の falcon の reset → FWSEC-SB（0x1454 の下位 16 bit = 0 を確認）→ booter_unload（MAILBOX0/1 = 0xff・0xff、WPR2 の上限が 0 なら飛ばす）。FRTS の後に GSP の起動が失敗した時に WPR2 を解く道は無い。
- open-gpu-kernel-modules 570.144（MIT の `kernel_gsp_tu102.c`・`kernel_gsp_booter_tu102.c`・`kernel_gsp.c`、temp に取得、commit しない）も同じ: FWSEC-SB と booter_unload（`kgspTeardown_TU102`）は `kgspUnloadRm` の unload の RPC の後だけ。boot の失敗の時は teardown せず、次の boot は WPR2 が既にあると「GPU は悪い状態で reset が要るかもしれない」として断る。
- よって **booter_unload は booter_load と GSP-RM が作った状態を解く物で、FWSEC-FRTS だけが張った region には使えない**（我々の R2 の順は両 driver の道の外）。mailbox0 の 0x29 は booter の誤りの番号で、意味は MIT の source に無い（booter は閉じた firmware、vendor の driver も数値を出すだけ）。mailbox1 の 0xff は渡した値がそのまま残った物。
- SEC2 の reset と falcon の準備の誤りではない: R3 で同じ `nvrtx_booter_run`（SEC2 の reset・PMC・IMEM/DMEM・署名の patch）の booter_load が mailbox 0 で成功した。FWSEC-SB は result 0（`P7 FWSEC-SB error` の行が無い）。

### R3 の P2b の原因の一部と、切り分けの印

- **10 s で timeout しない理由**: 待ちの loop が時間を測らず「1 回 10 µs」と数えていた。`kern_usleep_range` は次の scheduler の tick（amd64 は 1000 Hz）で起きるので、1 回が 1 ms 以上かかり、10 s の待ちは 1000 s 以上になる（5 分では終わらない）。全ての待ち（`nvrtx_poll`、RPC の送受信、P1a、P5 の fence）を単調な時計（`nvrtx_now_us`、`kern_rtc_read_counter`）で測る形に直した。
- **init done が来ない理由**は未確定（この版で切り分ける）。直しの前の版でも、message が 1 つでも来ていれば印（sequencer は無印で実行、他は `rpc: message …`）か fail が出るので、5 分の間に status の queue には何も来なかった見込みが高い（sequencer の poll の中で長く止まっていた可能性も残る、下の印で分かる）。
- 足した印:
  - `P2a memory: libos X rmargs Y queues Z+81000` と `P2a memory: logs A B C, 10000 bytes each`（DMA の番地 = guest の物理番地。vIOMMU は無い）。
  - `P2b <t> ms: cmd put P got G; msg size S put P got G`（t = 200・1000・3000・10000）: cmd の put は我々、got は GSP が読んだ所（GSP が system info と registry を読めば 2 以上）。msg の size は GSP が status の queue の header を書くと 0x40000 になる（0 なら GSP-RM は queue の初期化まで来ていない）。
  - `P2b <t> ms: mbox M0 M1 riscv R boot B; logs L0 L1 L2`: GSP の falcon の MAILBOX0/1、RISC-V の状態（0x111240 の bit 0）、secure scratch 14（0x1180f8 の bit 26 が boot の引き渡しの完了）、3 つの log の put pointer（0 なら libos は log を書いていない）。
  - `rpc: sequencer of N bytes`（CPU sequencer を受けた時）、`rpc: firmware error K: text`（0x1006 の誤りの報告の種類と文）。
  - fail の時は `P2b fail (E): memory kept for a dump; reboot the host` を出し、**memory を返さず P7 も流さない**（log と queue を外から dump するため。どちらにしても WPR2 は host の reboot まで残る）。
- 判定は今までどおり画面の PNG。加えて fail の時は T2 が QEMU の monitor/QMP の `pmemsave` で上の番地を dump する（下の手順）。log の中身は GSP-RM の ELF の `.fwlogging` の書式で P1 が host で解読する（serial/console log ではない）。

### P7 の直し（commit `1c1732db7`）

- region の teardown（FWSEC-SB → booter_unload）は **走っていた GSP を unload して止めた後だけ**。GSP を起こしていない時（stop=P2a、P1b・P2a の失敗）は何もせず、`P7 left: region L-H without a started GSP; reboot the host before the next run` を出して memory を返す。次の run の前に host の reboot（warm で足りる、R3 で確認）。
- R3 以降の P7 の順は nouveau・ogkm と同じ（unload の RPC → MAILBOX0 0x80000000 → GSP の falcon の reset → FWSEC-SB → SEC2 の reset → booter_unload 0xff・0xff → WPR2 の上限 0）。

### 検証（P1、host）

- `make -j16 ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/ws037-k-y CONFIG_DRIVER_PCI_NVRTX=y vmunix`: rc 0、warning 0、`amd64 vmunix check: PASS`。`git diff --check` 0。
- `plan/ws037/tests/nvrtx-gsp-host-test.sh`: PASS 146 項目（この worktree に firmware の cache が無く、実物の file の 19 項目は skip。変えた file は host 試験の対象外の hardware の部分）。
- 既定（n）の kernel は再 build していない（変えた file は全て `CONFIG_DRIVER_PCI_NVRTX=y` の時だけ compile される nvrtx の file）。
- GPL の旧名（`temp/rename/rename-map.tsv`）が足した行に 0。

### T2 への手順（方式 B、R2・R3・R4 の再試験）

image は 3.3 節のまま（`config-amd64-nvrtx.mk`、`NVRTX_RUN=R2`・`R3`・`R4`、main の `1c1732db7` 以降）。各 run の前に host の WPR2 が無いこと（前の run が `P7 left`・`P2b fail`・`P7 fail` なら host を reboot）。QEMU に vIOMMU を足さない（印の番地が guest の物理番地であるため）。

| run | 合格の印（`dmesg \| grep nvrtx` の画面） | 後始末 |
| --- | --- | --- |
| R2 | `P1a ok`、`P1b ok: region 01ffe000-01ffee00`、`P2a skipped`、`P7 left: region 01ffe000-01ffee00 without a started GSP; reboot the host before the next run`、login prompt | host を reboot |
| R3 | `P2a memory:` 2 行、`P2a ok: RISC-V running`、`P2b 200 ms:`（等）の行、`P2b ok: GSP-RM is up after N ms`、`P3 skipped`、`P7 begin: unload`、`P7 begin: FWSEC-SB`、`P7 booter_unload`、`P7 ok: region torn down`、login prompt | 合格なら reboot 不要（次の R4 の P1b が ok なら確認になる） |
| R4 | R3 に加えて `P3 static: …` 2 行、`P3 ok: client, device and subdevice`、`P4 skipped`、`P7 ok: region torn down` | 同上 |

- **R3 で `P2b fail` が出たら**（10 s 余りで出る）: 画面の `P2b … ms` の 8 行と `P2a memory:` の 2 行を PNG で撮る。guest を止める前に、QEMU の monitor（HMP）で `pmemsave <番地> <大きさ> <file>` を 5 回: logs の 3 つ（各 `0x10000`）、queues（`0x81000`）、libos（`0x1000`）。QMP なら `{"execute":"pmemsave","arguments":{"val":<番地>,"size":<大きさ>,"filename":"<file>"}}`。file を centris に持ち帰り、path を Q1 経由で P1 へ。その後 QEMU を止め、host を reboot。R4 は流さない。
- `P2b … ms` の行が 1 行も出ず固まった時は、gdbstub か QMP の `info registers` で guest の止まった所を見る（boot の log は取り直さない）。
- 所要の目安: R2・R3・R4 で image の作り直し 3 つ、run 3 回（各 3〜4 分）と reboot 1〜2 回。

## 再試験の R3（libos で停止）と nouveau との照合（2026-10-14、P1）

T2 の再試験（[results-20261014-nvrtx-retest](../../agents/T2/results-20261014-nvrtx-retest.md)、QEMU の guest に VFIO で渡した実機）: R2 PASS（`P7 left`）。R3 は `P2a ok: RISC-V running` の後、200・1000・3000・10000 ms のどれも
`cmd put 2 got 0; msg size 0 put 0 got 0`、`mbox 000000ca 00000000 riscv 00000035 boot 11000000`、10 s で `P2b fail (42)`（timeout の直しは効いた）。dump: T2 の `build/ws037-nvrtx/dump3/`。

### dump の解読（host で、P1）

- **libos の args の page**（7fe00000）: 4 つの region（LOGINIT 7fe01000+0x10000、LOGINTR 7fe11000+0x10000、LOGRM 7fe21000+0x10000、RMARGS 7fe31000+**0x48**）、kind 1・loc 1。
- **rmargs**: queues 7f800000、PTE 0x81（129）、cmd 0x1000、stat 0x41000、bDmemStack 1。nouveau の r570 と同じ。
- **LOGINIT**（log0）: put = 0x16（22 語）。GSP は PTE の並びの上に log を書いた（= **GSP は system memory を DMA で読み書きできている**。vIOMMU 無しの VFIO で guest の物理 = IOVA の前提は正しい）。22 語は 7 つの record（先頭の語は metadata の番地 0x5bb9xxx、次は時刻、残りが引数）:
  1. `0x61800001fe2c0000` = FB の 0x1fe2c0000（我々の配置の ELF の場所）
  2. `0x61800001f7900000`・`0x6900000` = FB の 0x1f7900000 から 105 MiB（我々の heap、P0 の行と一致）
  3. `0x61800001fe180000`・`0x80000`（heap の上端の 512 KiB）
  4. 引数なし
  5. `0x10e`
  6. `0x5b42000`・**`0x48`** = libos が RMARGS の region を ELF の `.section_task_rm_args_buffer_instance`（0x5b42000、**0x1000**）へ map した記録。大きさは我々が渡した 0x48
  7. 引数なし → ここで止まった（MAILBOX0 = 0xca）
- LOGINTR・LOGRM は PTE の並びのまま（put 0）= RM の task は始まっていない。cmdq の got 0、msgq の header 0 も同じ意味。
- WPR の配置（record 1〜3）・radix3（booter_load が成功し libos が WPR の image から走った）・libos の args の DMA は正しく読めている。
- `mbox 0xca`・`riscv 0x35`・`boot 0x11000000` の意味は MIT の source に無い（libos の停止の番号と RISC-V の状態の bit）。0x1180f8 の bit 26（boot の stage 3 の引き渡しの完了、ogkm の `dev_gc6_island_addendum.h`）は 0。MAILBOX0 は我々が書いた libos の番地（7fe00000）が GSP に 0xca で上書きされた。

### nouveau（r535 の道、TU10x、r570 の値）との照合

照合に使った file は全て MIT（`subdev/gsp/tu102.c`・`base.c`・`fwsec.c`、`rm/r535/gsp.c`・`rpc.c`、`rm/r570/gsp.c`・`rm.c`・`nvrm/gsp.h`、`falcon/base.c`・`fw.c`・`gm200.c`・`gp102.c`、`engine/sec2/tu102.c`）と open-gpu-kernel-modules 570.144 の MIT の `kernel_gsp_tu102.c`・`kernel_gsp.c`。GPL の部分は使っていない。

| 段 | nouveau | nvrtx | 判定 |
| --- | --- | --- | --- |
| VRAM の大きさ | 0x100ce0 の lmag・lsc、bit 30 で 15/16 | 同じ | 同じ |
| VGA workspace | 0x625f04 の bit 3、無効なら FB−1 MiB、下すぎれば FB−128 KiB | 同じ | 同じ |
| WPR の配置 | FRTS 1 MiB を VGA の 128 KiB 揃えの下、boot 4 KiB 揃え、ELF 64 KiB 揃え、heap = 0 + 8 MiB + align(96 KiB×GiB) + align(96 MiB)（r570 の libos2）、WPR = heap − meta の 1 MiB 揃え、非 WPR heap 1 MiB | 同じ（105 MiB、dump の record 2 と一致） | 同じ |
| WPR meta | magic・revision・radix3・ELF の大きさ・bootloader（番地・大きさ・code/data/manifest）・signature（0x1000）・rsvd = 非 WPR heap・各 offset・`gspFwWprEnd` = VGA の 128 KiB 揃え・fbSize・VGA・bootCount 0・verified 0 | 同じ（host 試験の値） | 同じ |
| radix3 | lvl0 → lvl1 → lvl2 → image の page | 同じ | 同じ（booter_load 成功） |
| libos の args の page | 0x1000、LOGINIT・LOGINTR・LOGRM 各 0x10000（先頭 8 byte の後に PTE）、RMARGS、kind 1・loc 1、id8 の詰め方 | RMARGS の大きさが **0x48** | **違う → 0x1000 に合わせた** |
| rmargs の中身 | r570: 共有 memory・PTE 数・cmd/stat の offset、sr 0、bDmemStack 1 | 同じ（dump） | 同じ |
| queue の初期の header | tx: version 0・size 0x40000・msgSize 0x1000・msgCount 63・writePtr 0・flags 1・rxHdrOff 32・entryOff 0x1000 | 同じ | 同じ |
| system info | BAR0/1/3 の番地、BDF、maxUserVa、config の写し 0x88000+0x1000、PCI の ID、ACPI（handle があれば DOD・JT・CAPS を埋め bValid 1）、bIsPrimary、bPreserve 0 | ACPI の欄は 0（bValid 0） | 違い（RM の段の物、libos では読まない。動いた後に見る） |
| registry | RMSecBusResetEnable・RMForcePcieConfigSave・RMDevidCheckIgnore = 1 | 同じ | 同じ |
| system info・registry の投入の時期 | oneinit（FRTS の前）に cmdq へ、doorbell 0x110c00 = 0 | P2a（FRTS の後、GSP の reset の前）に cmdq へ、doorbell | 順の違い。どちらも後に GSP の falcon の reset が来るので GSP が見る物は同じ（P0 で GPU に書かない設計のまま） |
| FWSEC-FRTS | falcon の reset（disable: 0x048・0x014、reset_eng 0x3c0 の pulse・scrub、enable: scrub 待ち・0x084 = boot0）→ 0x624 bit 7・0x10c = 0 → bl を IMEM の上端・desc を DMEM → ctx 0x600 → 起動・halt 待ち 2 s → 0x1438 の上位 | 同じ | 同じ（R2・R3 で ok） |
| GSP を RISC-V へ | reset_eng（0x3c0 の pulse 10 µs、0x040 を触って 0x10c の bit 1・2 が 0 まで） | 同じ | 同じ |
| GSP の MAILBOX0/1 | libos の args の番地 | 同じ | 同じ |
| booter_load | SEC2 の reset（PMC 込み）→ 0x624・0x10c → os code・app（secure）・os data を PIO → MAILBOX0/1 = WPR meta → 0x104 = os code の offset → 0x100 = 2 → halt 待ち → MAILBOX0 = 0 | 同じ | 同じ（R3 で ok） |
| 起動の後 | GSP+0x080 = bootloader の app の版、0x111240 bit 0 を確認、INIT_DONE を poll（その間の sequencer を実行） | 同じ | 同じ |

**直し（commit `714ab8477`）**: RMARGS を 1 page（0x1000）で確保し、libos の args にも 0x1000 で名指す（nouveau と ogkm の `kgspSetupLibosInitArgs` と同じ。ELF の受け側の section も 0x1000）。host 試験に「RMARGS の region が 0x1000」を足した（167 項目 PASS、実物の firmware の file を含む）。印の行は 79 桁の枠に収まるように分けた: `P2b <t> ms: mbox …`・`P2b <t> ms: log put L0 L1 L2` を別の行に、`P7 left: region L-H, no GSP started; reboot the host`、`P6 channel error` と `P0 fail` を短く、firmware の誤りの文は 40 文字まで。

- 検証: y の kernel（`build/ws037-k-y`）rc 0・warning 0・`amd64 vmunix check: PASS`、`git diff --check` 0、GPL の旧名 0。
- 見立て: libos は RMARGS を自分の 0x1000 の section へ page 単位で map するので、0x48 の region を受けて止まった（record 6 の直後で停止）。**未確認**（libos の停止の番号 0xca の意味は公開されていない）。直らない時は system info の ACPI の欄を次に疑う（RM の段に入ってからの物なので、LOGRM に書き込みがあるかで分かる）。

### T2 への手順（R3・R4 の再試験、main の `714ab8477` 以降）

前と同じ（3.3 節の image、`NVRTX_RUN=R3`・`R4`、各 run の前に host の reboot と `prepare.sh --check`）。

| run | 合格の印 | 後始末 |
| --- | --- | --- |
| R3 | `P2a ok: RISC-V running`、`P2b 200 ms: cmd put 2 got 2; msg size 40000 …`（GSP が cmdq を読み msgq を作る）、`P2b ok: GSP-RM is up after N ms`、`P3 skipped`、`P7 begin: unload`、`P7 begin: FWSEC-SB`、`P7 booter_unload`、`P7 ok: region torn down`、login prompt | 合格なら続けて R4（reboot は要らない見込み。P1b が ok なら確認になる） |
| R4 | R3 に加えて `P3 static:` 2 行、`P3 ok: client, device and subdevice`、`P4 skipped`、`P7 ok: region torn down` | — |

- R3 が `P2b fail` の時: 画面の `P2b … ms` の行（今は 1 時点 3 行、`log put` の行を含む）を PNG に撮り、前回と同じ 6 つの `pmemsave`（logs 3 つ、queues、libos、rmargs）。番地は `P2a memory:` の行から。その後 QEMU を止め host を reboot。R4 は流さない。
- `log put` の 1 つ目が 0x16 のまま（前回と同じ所で停止）なら、dump の log0 の record を P1 が前回と比べる。LOGRM の put が 0 でなくなれば RM の task まで進んだ印。

## R3 の 3 回目（CPU sequencer の opcode 8）と nouveau に合わせた sequencer（2026-10-14、P1）

T2（[results-20261014-nvrtx-r3-retest2](../../agents/T2/results-20261014-nvrtx-r3-retest2.md)、QEMU の guest に VFIO で渡した実機）: RMARGS の直しが効き、200 ms で `cmd put 2 got 2; msg size 40000`（GSP-RM が動き、system info と registry を読み、status の queue を作った）。727 ms に message 0x1020（1212 byte）と CPU sequencer（2000 byte）が来て、`sequencer: opcode 8 not supported` → `P2b fail (21)`。その時 `mbox 0 0`・`riscv 0`（RISC-V が止まっている）・`boot 13300000`。

### dump4 の解読（P1、host）

- 0x1020 は `GSP_POST_NOCAT_RECORD`（nouveau の r570 の msgfn の表）。nouveau は r570 で受けて捨てる（`drop_post_nocat_record`）。
- sequencer（bufferSizeDWord 16354、cmdIndex 490）の中身: `POLL 0x110040 が 0x80000000`（GSP の停止の待ち）→ `WRITE 0x110040 = 0` → `CORE_RESET` → GSP の falcon の DMEM（0x1101c0/c4）と IMEM（0x110180/84/88）へ小さな falcon の program を書く → `WRITE 0x110040 = 0xfe`・`0x110104 = 0xfd00` → `CORE_START` → `CORE_WAIT_HALT` → `WRITE 0x110004 = 0` → `STORE 0x110040 → 0`・`STORE 0x110044 → 1` → **`CORE_RESUME`**（opcode 8）。GSP-RM が自分を止め、GSP の falcon で別の program を走らせ、SEC2 の booter で GSP-RM を再開させる Turing の起動の手順。

### nouveau（MIT の `rm/r535/gsp.c` の `r535_gsp_msg_run_cpu_sequencer`、`falcon/v1.c`、`engine/sec2/tu102.c`）との照合と直し（commit `870aecf93`）

| opcode | nouveau | nvrtx（直しの後） |
| --- | --- | --- |
| 0 REG_WRITE | wr32(addr, val) | 同じ |
| 1 REG_MODIFY | mask(addr, mask, val) | 同じ |
| 2 REG_POLL | rd32(addr) を 1 回、その後 timeout（0 なら 4 s）まで (rd32 & mask) == val を poll、失敗は無視 | 1 回目の読みを足した（前は無かった） |
| 3 DELAY_US | udelay(val) | 同じ（usleep） |
| 4 REG_STORE | regSaveArea[index] = rd32(addr) | 同じ |
| 5 CORE_RESET | GSP の falcon の reset（disable・enable）、0x624 bit 7、0x10c = 0 | 同じ |
| 6 CORE_START | 0x100 の bit 6 なら 0x130 = 2、でなければ 0x100 = 2 | 同じ（`nvrtx_falcon_start` に分けた） |
| 7 CORE_WAIT_FOR_HALT | 0x100 の bit 4 を 2 s | 同じ |
| 8 CORE_RESUME | GSP の reset_eng（RISC-V へ）→ GSP の 0x040/0x044 = libos の args の番地 → **SEC2 を start**（v1 の start: SEC2 の 0x100 の bit 6 なら 0x130、でなければ 0x100 = 2。P2a の booter_load が載ったまま）→ 0x1180f8 の bit 26 を 2 s → SEC2 の 0x040 が 0 → GSP の 0x080 = app の版 → 0x111240 の bit 0 | **足した**（`rpc_core_resume`）。印 `sequencer: core resume`、失敗の時は `resume not handed over (boot …)`・`resume booter …`・`resume left RISC-V stopped` |

ほかに nouveau に合わせた所:

- RPC の番号: nouveau は返事を待つ call に 0 から順に番号を振る（system info・registry は 0 で番号を使わない）。我々は 1 からだった → 0 から。
- 返事の照合: nouveau は function だけで照合する。我々は function と番号 → function だけ（番号が違えば `rpc N: reply sequence …` の印だけ出す。design 2.6 の番号の照合は動いた後に戻す）。
- event の扱い: nouveau が handler 無しで捨てる `UCODE_LIBOS_PRINT`（0x100c）・`PERF_BRIDGELESS_INFO_UPDATE`（0x100f）・`GSP_POST_NOCAT_RECORD`（0x1020）は印を出さずに捨てる。

検証: y の kernel rc 0・warning 0・`amd64 vmunix check: PASS`、host 試験 167 項目 PASS（実物の firmware の file を含む。sequencer の decode の opcode 8 は引数 0 語で前から通る）、`git diff --check` 0、GPL の旧名 0。CORE_RESUME の実際の動きは実機でだけ分かる（未実施）。

### T2 への手順（R3・R4、main の `870aecf93` 以降）

前と同じ image の作り方（`NVRTX_RUN=R3`・`R4`）。各 run の前に host の reboot と `prepare.sh --check`。

| run | 合格の印 | 後始末 |
| --- | --- | --- |
| R3 | `P2b 200 ms: cmd put 2 got 2; msg size 40000 …`、`rpc: sequencer of 2000 bytes`、`sequencer: core resume`、（`resume …` の失敗の行が無い）、`P2b ok: GSP-RM is up after N ms`、`P3 skipped`、`P7 begin: unload`、`P7 begin: FWSEC-SB`、`P7 booter_unload`、`P7 ok: region torn down`、login prompt | 合格なら続けて R4 |
| R4 | R3 に加えて `P3 static:` 2 行、`P3 ok: client, device and subdevice`、`P4 skipped`、`P7 ok: region torn down` | — |

- R3 が `P2b fail` の時: 画面の全部の nvrtx の行（`sequencer: resume …` の行を含む）を PNG に、前回と同じ 6 つの `pmemsave`、QEMU を止め host を reboot。R4 は流さない。
- R3 が P2b ok で P7 が fail の時（`P7 unload call`・`P7 fail …`）: 行を PNG に撮り、host を reboot。R4 は流さない。
