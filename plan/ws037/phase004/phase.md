<!-- awesome-plan project=zedbsd record=ws037-p004 -->

# ws037-p004: GSP の起動（P0〜P3・P7）と方式 B（VFIO）の試験の手順

Status: in-progress（2026-10-14 P1。4a〜4e を実装、build と host 試験まで。実機（方式 B）は T1 待ち）
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
| R2 | `nvrtx.gsp=1 nvrtx.stop=P2a` | P1a・P1b の ok（WPR2 の下限・上限）、P7 の一部（FWSEC-SB・booter_unload で WPR2 の上限が 0 に戻る、guest の shutdown の後）、login prompt |
| R3 | `nvrtx.gsp=1 nvrtx.stop=P3` | P2b の ok（START_OK）、guest の `poweroff` の後の P7 の ok |
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
