<!-- awesome-plan project=zedbsd record=ws039-p002 -->

# ws039-p002: 骨格と認識（段 A0・A1・A1v、読むだけ）

Status: in-progress（2026-10-16 P3、q1211 / q1211-i01。code・host 試験・USB の image まで済み、実機の電源の入れ直しと USB の差し込み待ち）
Disposition: normal
Parent: [WS039](../ws.md)
実行者: P3（worktree `/home/awe/zedBSD-worktrees/p3`、branch `agent/p3-ws039`、base main `202d4ed03`）

## 範囲（[design.md](../design.md) §16 の p002 の行）

骨格（PCI の attach、BAR の map）、RDNA1〜3 の device id の表、IP discovery の parser、resizable BAR の capability と BAR0 の大きさの読み（log だけ）、GOP の readout と clock の読み、VBIOS（VFCT か ROM の BAR）と data の表の parser、PSP の runtime DB の parser、VRAM の配置、段の印、control の node（`/dev/amdrdna0`、UAPI、`amdrdnactl`、開発の間だけ）、boot の parameter、独自の名前の register の表、監査の script、host 試験 (b)(f)(g)(i)。**実機: A0・A1・A1v の ok**。

承認: 2026-10-15 夜 ユーザー「AMDに集中しましょう。N=1にします。」と design §15 の回答（[ws.md](../ws.md) の表）。

## 最初にしたこと: design.md の §15 #7・#11 の反映（2026-10-16）

- #7「まず256MBで実装します。」: §1.2 の BAR0、§2 の lock の表、§3 の A2、§5.2、§5.5（冒頭の注）、§12 の boot の parameter（`amdrdna.window` を消した）、§12.5 の UEFI の設定、§14 の 10、§15 #7、§16 の p002・p003 を、BAR0 の 256 MiB の窓（2 つの pool）だけの実装に直した。A0 は ReBAR の capability を読んで log に出すだけ。
- #11「開発の間だけ」: §3.2 に「control の node は `CONFIG_AMDRDNA_CONTROL`（kernel-options.list の bool、開発の間の既定 y）が y の時だけ作る。完成したら既定を n にするか消す。n の時は `amdrdna.auto` だけで進む」を足し、§15 #11 に回答を書いた。

## 実機の dump（2026-10-16 00:05〜00:20、10.0.10.3 の Debian 13、Linux 6.12.111）

道具: [tests/hw-reglist.py](../tests/hw-reglist.py)（Linux の header から dump する register の表を作る、host）、[tests/hw-dump.py](../tests/hw-dump.py)（Debian の root で BAR5 を mmap して読む。書くのは間接の窓の index の 2 つだけで、元の値に戻す）。出力は `plan/ws039/temp/dump/`（commit しない。VBIOS の image を含むので BLOB は tree に入れない）。

1. **cold（GOP の状態のまま）**: Debian の起動で amdgpu は自動で load されたが firmware が無く early_init で失敗していた（PSP は起動していない）。その状態で `cold/` を取った（register 451 個、config 空間、discovery の表 10 KiB、runtime DB の位置の 4 KiB、ROM の BAR 100 KiB、ACPI の VFCT 57988 byte）。
2. **loaded**: firmware（linux-firmware 20260410 の navi10_*）を `~/amdfw` に置き、実行時の `/sys/module/firmware_class/parameters/path` で与えて amdgpu を load し直した（永続の設定は変えない）。`dmesg-load.txt`・`loaded-info*.txt`（firmware の版、IP の版、hwmon）・`loaded/`（同じ dump）。**この後は PSP の SOS が生きているので、zedBSD の試験の前に電源の入れ直しが要る**（Q1 に連絡済み）。

### 分かった値（A0・A1・A1v の期待値）

| 項目 | 値 | 出所 |
| --- | --- | --- |
| PCI | 1002:731f rev 0xca、subsystem 1da2:e411、03:00.0。上に Navi10 の PCIe switch（01:00.0 upstream 1002:1478、02:00.0 downstream 1002:1479）、root port 00:01.0 | lspci |
| BAR | BAR0 256 MiB @0x4000000000（64 bit pref）、BAR2 2 MiB @0x4010000000、BAR4 I/O 256、BAR5 512 KiB @0x80000000、ROM 128 KiB @0x80080000 | lspci |
| ReBAR | capability @0x200、BAR0: 256 MiB・512 MiB・1 GiB、今 256 MiB。BAR2: 2 MiB〜256 MiB | lspci（Linux の amdgpu は窓を置き直して BAR0 を 8 GiB にした） |
| VRAM | MEMSIZE = 0x17f0 = **6128 MiB**（6 GiB − 16 MiB）、192 bit GDDR6 | cold の register、dmesg |
| FB | GCMC/MMMC の FB_LOCATION_BASE 0x8000・TOP 0x817f、FB_OFFSET 0 → MC 0x8000000000〜0x817EFFFFFF | cold |
| post | BIOS_SCRATCH_7 = 0x0070020a（bit 9 = 0x200 が立つ = posted）、BIOS_SCRATCH_6 = 0（accelerated mode でない） | cold |
| PSP | C2PMSG_33 = 0x80000000（bit 31）、C2PMSG_35 = 0x80000000（bootloader ready）、C2PMSG_81 = 0（SOS 無し） | cold |
| SMU | MP1 の FIRMWARE_FLAGS（SMN 0x03B10024）= 0（cold）、1（loaded） | cold・loaded |
| GRBM | GRBM_STATUS 0x00003028、CP_ME_CNTL 0x15000000（halt）、CP_MEC_CNTL 0x50000000 | cold |
| discovery | signature 0x28211407、版 1.1、binary の大きさ 0x444。IP の表 @0x3C "IPDS" 版 2・0x328 byte、die 1 つ、IP 39 個。版: GC 10.1.10、MP0 11.0.0、MP1 11.0.0、SDMA0 5.0.0、OSSSYS 5.0.0、NBIF 2.3.0、HDP 5.0.0、MMHUB 2.0.0、**DMU（DCN）2.0.2**、THM 11.0.0、SMUIO 11.0.0。base（GC 0x1260/0xA000、MP0・MP1 0x16000、DMU 0x12/0xC0/0x34C0/0x9000、NBIF 0/0x14/0xD20/0x10400）は navi10 の静的な header と一致 | cold の discovery.bin、loaded の sysfs |
| runtime DB | VRAM の上端 − 1 MiB に cookie 0x0ED5 が無い（中身は不定）= runtime DB 無し | cold |
| GOP | OTG0 だけ MASTER_EN。H_TOTAL 0x897（2200）、H blank 0x840/0xc0、V_TOTAL 0x464（1125）、V blank 0x461/0x29 → **1920x1080**。ODM0 → OPP0、MPC_OUT0 → MPCC0、TOP_SEL 0 → DPP0/HUBP0、HUBP0 の VTG_SEL 0。format 8、SW_MODE 0（linear）、PITCH の register 0x780、address HIGH 0x80 LOW 0 = MC 0x8000000000 = VRAM の offset 0。VGA の mode 0（6 つとも）、MASTER_UPDATE_LOCK 0 | cold |
| GOP の pitch | register 0x780 = 1920、Linux の efifb の linelength は 7680 byte = 1920 画素。Linux の DC はこの欄に pitch − 1 を書く（`dcn20_hubp.c` の `hubp2_program_size`）が、GOP は pitch そのものを書いている。A1 は register と loader の stride の両方を log に出し、一致の判定は VRAM の offset で行う（pitch は GOP の register を保つので A6 で変えない） | cold、dmesg |
| 出力 | DIG4（PHY E）だけ BE_EN、BE_CNTL 0x50021000: FE_SOURCE_SELECT 0x10（FE E）、**DIG_MODE 2 = DVI**（DP ではない。monitor は HDMI の connector に DVI の mode で点けている見込み）、HPD_SELECT 5。DIG4 の FE_CNTL の SOURCE_SELECT 0 = OTG0 | cold |
| clock | DP_DTO0_PHASE 0（DP でないので DTO は使っていない）、MODULO 0x29b92700（700 MHz = DPREFCLK）、DENTIST_DISPCLK_CNTL 0x4f194f4f、OTG0_PIXEL_RATE_CNTL 0 | cold |
| VFCT | 長さ 0xe284、VBIOSImageOffset 0x4C、image header: bus 3、device 0、function 0、1002:731f、ImageLength 0xe200（57856 byte）、先頭 0x55AA | cold の vfct.bin |
| VBIOS | `113-5E4111U-X4I` | dmesg |
| 温度 | THM の CG_MULT_THERMAL_STATUS = 0x5a2d（bit 8:0 = 45、bit 17:9 = 45）、hwmon の edge 43 ℃ | cold・loaded |
| firmware の版（loaded） | SOS 0x00100d50、ASD 0x21000109、SMC 42.64.0、ME 0x65、PFP 0xa0、CE 0x26、MEC/MEC2 0x9c（feature 35）、RLC 0x80、SDMA0/1 0x23（feature 50）、TMR 9 MiB @0x817d000000、kiq は mec 2 pipe 1 q 0 | loaded |

## 実装（2026-10-16 P3、実機前）

| commit | 中身 |
| --- | --- |
| `0adf5cc85` | design.md の #7・#11、この phase.md、`tests/hw-reglist.py`・`tests/hw-dump.py` |
| `a76de00fe` | ACPI の口: `include/drivers/acpi/acpi.h` の `drv_acpi_table_lookup` と `src/drivers/acpi/acpi-kern.c` の実装（[proposed](../proposed/acpi-table-lookup.diff) と同じ。Q1 了承、2026-10-16） |
| `3b3726a4f` | driver（`src/drivers/gpu/amdrdna/` の 14 file）、`include/uapi/amdrdna.h`、`include/drivers/pci/pci-amdrdna.h`、`userland/tests/amdrdnactl/`、build の行（`Makefile` の `CONFIG_AMDRDNA_CONTROL`、`config/kernel-options.list`、`platform/amd64/vmunix.mk`）、host 試験と監査（`tests/amdrdna-host-test.sh` ほか）、`tests/config.mk` |
| `bddc728fd` | `src/kern/platform/pcat.c` の登録の 3 か所（Q1 が所有に加えた） |

driver の file: `amdrdna.c`（PCI の登録・attach・段の機械・bring-up thread・shutdown）、`stage.c`（段の印・boot の parameter `amdrdna.off=1`・`amdrdna.auto=`）、`mmio.c`（register、間接の register の窓、間接の VRAM の窓）、`identify.c`（A0）、`readout.c`（A1、VRAM の配置）、`vbios.c`（A1v）、`control.c`（`/dev/amdrdna0`、`CONFIG_AMDRDNA_CONTROL`）、host でも動く読み手: `block-table.c`（IP discovery）、`boot-database.c`（PSP の runtime DB）、`vbios-table.c`（VFCT と AtomBIOS の data・command の表）、`layout.c`、`products.c`（RDNA1〜3 の device id、GC の版 → chip）、`dcn-registers.c`（DCN の register の表）、`bytes.c`、header `amdrdna-regs.h`・`amdrdna-hw.h`・`amdrdna-private.h`。

設計からの変更・決めたこと:
- ioctl の group は `'R'`（`'A'` は audio が使っている）。
- VBIOS の flash の読みは PCI の expansion ROM の BAR ではなく SMUIO の ROM の register（Linux の soc15 の道、PCI の層の変更不要）。
- A1 の「firmware の package の有無」は attach の時に root が無いので p004（firmware の load）に回した。
- A0 で secure OS が生きていれば（C2PMSG_81 ≠ 0）`A0 hold` の行を出し、A1・A1v だけ走らせ、A1v より先の要求は EBUSY。
- 段は失敗の後に自動で再試行しない（以後の ADVANCE は EIO）。kept の function は detach を拒み（control の node が record を持つ）、suspend を拒む。
- GOP の範囲は pitch の register と loader の stride の広い方 × 高さで予約する（pitch の register が pitch か pitch − 1 か、GOP と DC で違うため）。

確認（host、実機の証拠ではない）:
- `make -j16 BUILD=build/p3/amd64 ZEDBSD_CONFIG=config/ci/config-amd64.mk build/p3/amd64/vmunix`、同じく `ZEDBSD_CONFIG=plan/ws039/tests/config.mk` の `vmunix` と `bin/amdrdnactl`: rc 0、warning 0、kernel include check PASS、amd64 vmunix check PASS。
- `CONFIG_DRIVER_PCI_AMDRDNA=n`（`config/ci/config-amd64-server.mk`、`BUILD=build/p3/amd64-noamd`）の vmunix: rc 0、warning 0、PASS、amdrdna の object は作られない。
- `plan/ws039/tests/amdrdna-host-test.sh`: PASS。合成の case（block table 10、boot DB 5、VBIOS 12、layout 5、device id 6）、dump の実物（`temp/dump/cold`: discovery の表が Linux の sysfs と一致（39 entry、GC 10.1.10、DMU 2.0.2 …）、runtime DB 無し、VFCT の image（offset 0x68、0xe200 byte）の表が Python の読みと一致、flash の image は同じ所に同じ表）、DCN の register 40 種 × 6 instance = 240 個が Linux の `dcn_2_0_0_offset.h` と一致、監査 (g) `audit-symbols.sh` PASS（出典の header の識別子と交差するのは allow list の一般語 139 個だけ、hex の block は全部 `Value(s) referenced:` 付き）。
- `plan/tools/style-check.py`: 違反は log の引数の短い条件演算子だけ。

## 状態と次

- [x] 骨格・device id の表・discovery・BAR0・GOP の readout・VBIOS・runtime DB・VRAM の配置・段の印・control の node・boot の parameter・register の表
- [x] 監査の script と host 試験 (b)(f)(g)(i)
- [x] build（warning 0）
- [x] USB の image（`plan/tools/guest/test-image.sh plan/ws039/tests/config.mk build/p3/usb AMDRDNA_RUN=A1v`、tree 073d1a384、rc 0、`check-amd64-native-image` OK、`build/p3/usb/hdd-image.img` 2485125120 byte、SHA-256 `b36df12fa60ffe236829b082251bbb3e0458edc2bf7367de1f5d880353f62805`。Debian の `~/ws039/hdd-image.img` に写して一致を確かめた。warning は外部の package（openssl など）と perl の locale だけ）
- [x] 実機の操作の道具 [tests/hw-run.sh](../tests/hw-run.sh)（write: USB へ dd、boot: BootNext と reboot、collect: zedBSD の SSH で status・dmesg・VBIOS を取る、debian: 戻る）。実機ではまだ流していない
- [ ] 待ち（2026-10-16 00:57 に Q1 へ依頼、01:34 まで返事なし）: ユーザーが 10.0.10.3 の電源を入れ直し（amdgpu の load の後の cold）、2.5 GB 以上の USB を挿す。UEFI に USB の entry が無ければ `efibootmgr --create` の可否も Q1 経由で確かめる
- [ ] 実機（電源の入れ直し・USB の差し込みはユーザー、Q1 経由）: A0・A1・A1v の ok
