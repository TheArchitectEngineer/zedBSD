<!-- awesome-plan project=zedbsd record=ws039-p002 -->

# ws039-p002: 骨格と認識（段 A0・A1・A1v、読むだけ）

Status: in-progress（2026-10-16 P3、q1211 / q1211-i02。実機（10.0.10.3、USB の zedBSD）で A0・A1・A1v ok、2026-10-16 08:28。merge と clearance は Q1 の判定待ち）
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

## 実機の 1 回目と A1 の直し（2026-10-16、q1211-i02）

**1 回目**（Q1 が 08:1x に流した。cold の電源の入れ直しの後、`Boot0001 zedBSD USB` を `efibootmgr --create-only` で足し（BootOrder には入れていない、ユーザーの許可済み）、`hw-run.sh boot 0001`。zedBSD は DHCP で 10.0.30.5、Debian は 10.0.10.3）: A0 ok、**A1 fail: `loader framebuffer 640x480 is not the timing's 1920x1080`**、`amdrdnactl status` は `/dev/amdrdna0: No such file or directory`。証拠 `build/tmp/amdrdna-hw-1/`（worktree の中、commit しない）。

原因:
- image の boot の行に `video=640x480`（text の console の起動で builder が足す）があり、loader が GOP を 640x480 にした。GOP は monitor の timing（1920x1080）を変えず、640x480 の surface を DSCL の scaler で拡大して出していた。A1 は loader の大きさを OTG の active の大きさと比べていたので落ちた。比べるべきは HUBP の viewport（plane が fetch する surface の部分）。
- control の node が無いのは、A1 の失敗で attach が function を firmware に返していたため（design §3.1 の「attach しない」）。

直し:
- `picture.c`（新、host でも動く）: `amdrdna_picture_check`。loader の大きさを HUBP の viewport と比べ（viewport の始点が 0,0 でなければ ENOTSUP、大きさが違えば EINVAL）、形を whole（viewport = active）・placed（拡大なしで中に置く）・scaled（拡大）に分ける。DSCL の RECOUT（出力の矩形）が active の中か、DSCL の mode・ratio・AUTOCAL が viewport と矩形に合うかは **note だけ**（合格を決めない）。
- `readout.c`: HUBP の viewport の始点、DSCL の mode・RECOUT の始点と大きさ・水平と垂直の ratio・AUTOCAL の mode を読む（`readout_scaler`）。`A1 scaler` の行を出し、`A1 ok` の行は `640x480 on 1920x1080 scaled` の形にした。GOP の予約は pitch × (timing・viewport・loader の高さの最大)。
- `dcn-registers.c`: DSCL の 6 個（SCL_MODE・H/V の ratio・RECOUT_START・RECOUT_SIZE・AUTOCAL）。DPP 4・5 は別の位置なので DPP_CONTROL からの距離（0x27・0x2c・0x30・0x3d・0x3e・0x38、6 pipe で同じ）で持つ。出典は block ごと（`dcn_2_0_0_offset.h`・`sh_mask.h`、`dcn10_dpp_dscl.c`）。
- `amdrdna.c`: A1 が失敗しても function を保ち（`failed_stage = A1`、以後の段は走らない、ADVANCE は EIO）、control の node を出す（status が読める）。その function は suspend を拒まない（段を何も走らせていないので）。design §3.1 の A1 の行を直した。
- UAPI（`include/uapi/amdrdna.h`）: `gop_source_width`・`gop_source_height`・`gop_shape`（`AMDRDNA_GOP_SHAPE_WHOLE/PLACED/SCALED`）を reserved から取った（struct の大きさは同じ）。`amdrdnactl status` の gop の行に出す。
- `tests/hw-run.sh` の collect: `amdrdnactl vbios` の 1 行の報告が `vbios.rom` の先頭に混ざっていた（33 byte）ので stdout を捨てた。

host（実機の証拠ではない）:
- `plan/ws039/tests/amdrdna-host-test.sh`: PASS（PASS の行 64、そのうち足した case `picture.*` 14 個）: whole（1 回目の前の Debian の dump の形）、scaler 未設定の whole、scaled（640x480 → 1920x1080、Linux の書き方の ratio）、縦横比を保つ scaled（1440x1080 @240,0）、AUTOCAL の autoscale（ratio 1.0 のまま）と手動の ratio 1.0 の note、AUTOCAL の autocenter、中央の placed、ratio・矩形の note、loader と viewport の大きさの違い（EINVAL）、loader 無し、viewport の移動（ENOTSUP）、空の viewport。DCN の register は 46 種 × 6 = 276 個が Linux の header と一致、監査 PASS（allow list に一般語 8 個: bottom・difference・horizontal・inside・ratio・right・vertical・lines）。
- build: `make -j16 ZEDBSD_CONFIG=plan/ws039/tests/config.mk BUILD=build/p3/usb AMDRDNA_RUN=A1v build/p3/usb/vmunix build/p3/usb/bin/amdrdnactl` rc 0、warning 0、kernel include check PASS、amd64 vmunix check PASS。`git diff --check` OK。style-check の違反は前からの log の引数の条件演算子 3 個だけ。

**実機の 2 回目・3 回目**（P3、電源の入れ直しなし。Debian から USB の sda2 の `vmunix` を差し替え（前の物は Debian の `~/ws039/vmunix.hw1`、SHA-256 6d5fc8c0…）→ `hw-run.sh boot 0001` → `AMDRDNA_ZED=root@10.0.30.5 hw-run.sh collect`）:
- 2 回目（08:25、vmunix 78013626…）: A0・A1・A1v ok。ただし `A1 scaler dpp=0 mode=1 out=0,0 1920x1080 ratio=0x1000000/0x1000000` で ratio が 1.0 のまま拡大していたので「ratio が合わない」の note が出た → GOP は DSCL の AUTOCAL（hardware が ratio を自分で出す）を使っていると見て AUTOCAL を読むようにした。
- **3 回目（08:28、vmunix 0835d308…、証拠 `build/tmp/amdrdna-hw-3/`）**:
  - `A0 ok gc=10.1.10 navi10 vram=6128MiB bar0=256MiB rebar=0x7000/0x840 posted=1 sos=0 smu=0x0 grbm=0x00003028 fb=0x8000000000 rtdb=0`、blocks 39（psp 11.0.0 smu 11.0.0 sdma 5.0.0 ih 5.0.0 nbio 2.3.0 hdp 5.0.0 mmhub 2.0.0 dcn 2.0.2）、shape se=2 sa=2 cu/sa=10 rb/se=8 l2=16 wave=32
  - `A1 scaler dpp=0 mode=1 auto=1 out=0,0 1920x1080 ratio=0x1000000/0x1000000 viewport=0,0 640x480 active=1920x1080`（note 無し）
  - `A1 ok otg=0 opp=0 mpcc=0 hubp=0 dig=4 mode=DVI 640x480 on 1920x1080 scaled fb=+0x0 pitch=0x280 stride=640 fmt=8 lit=1`、`A1 clk dispclk_cntl=0x4f194f4f pixel_rate=0x00000000 dto=0x00000000/0x29b92700 underflow=1 lock=0`、`A1 vram cpu=0-256MiB gpu=256-6127MiB kept=3`（top 0x17ef00000-0x17f000000、gop 0x0-0x2a5000、train 0x0-0x2000000）
  - `control device /dev/amdrdna0`、`kept navi10; stages up to A1v run now`
  - `A1v ok vbios=VFCT 57856B fwinfo=3.3 caps=0x1 train=0 sclk=300MHz mclk=875MHz pptid=0 scratch=0x4c fwtop=0K`、tables fwrsv=2.1 pp=12.0 rev1 482B shutdown=118C dpm=4.5、cmds post=0x930c enc=0xab64 pclk=0x950c pg=0x9944 crtc=0xa222 dceclk=0xc1c6 tx=0xae78 aux=0xbab2
  - `amdrdnactl status`（新しい amdrdnactl を `/tmp` に送って実行）: `stage=A1v target=A1v failed=none flags=0x0000019d`、`gop otg=0 hubp=0 dig=4 kind=2 640x480 on 1920x1080 shape=2 pitch=0x280 format=8 vram=+0x0 on_gpu=1`、`vbios source=1 length=57856`

照合（上の「分かった値」の Debian の dump と）:
- 一致: PCI の id・rev・subsystem、BAR0 256 MiB @0x4000000000・BAR2 2 MiB・BAR5 512 KiB、ReBAR（256 MiB・512 MiB・1 GiB、今 256 MiB）、VRAM 6128 MiB、FB 0x8000000000、posted（BIOS_SCRATCH_7 の bit 9。値は 0x0012020a で Debian の 0x0070020a と上位が違う）、C2PMSG_81 = 0・C2PMSG_35 = 0x80000000、MP1 の FIRMWARE_FLAGS 0、GRBM_STATUS 0x00003028、discovery の 39 個と各版、runtime DB 無し、OTG0 → ODM0 → OPP0 → MPCC0 → HUBP0、DIG4（DVI、FE E）、format 8、surface の VRAM の offset 0、DENTIST 0x4f194f4f、DP_DTO0 の MODULO 0x29b92700、PIXEL_RATE_CNTL 0、VFCT の image 57856 byte（hw-3 の `vbios.rom` の先頭の 33 byte（amdrdnactl の報告の行）を除いた中身が Debian の `vfct.bin` の image と byte ごと一致）。
- 違う（mode の違いで説明がつく）: pitch の register 0x280（Debian の起動は 1920x1080 の GOP で 0x780）、viewport 640x480（同 1920x1080）、DSCL が拡大（mode 1、AUTOCAL 1）。

残り（この Phase の中では直さない、記録だけ）:
- HUBP0 の underflow の bit が 1（3 回とも。Debian の dump の時は 0）。拡大の GOP の状態の sticky な印と見る（見込み）。A6（surface の替え）の前に意味を確かめる。
- 拡大の GOP では A6 の client の surface の大きさは viewport（640x480）・pitch 0x280 になる（design §8.3「pitch・幅・高さは GOP と同じ」）。1920x1080 で出すには image の `video=640x480` を外すか A10〜（mode set）。A6 の前に決める（Q1・ユーザーの判断）。
- 「A1 が失敗しても control の node を出す」道は build と code の読みだけで、実機では通していない（3 回目は A1 ok）。

## 状態と次

- [x] 骨格・device id の表・discovery・BAR0・GOP の readout・VBIOS・runtime DB・VRAM の配置・段の印・control の node・boot の parameter・register の表
- [x] 監査の script と host 試験 (b)(f)(g)(i)
- [x] build（warning 0）
- [x] USB の image（`plan/tools/guest/test-image.sh plan/ws039/tests/config.mk build/p3/usb AMDRDNA_RUN=A1v`、tree 073d1a384、rc 0、`check-amd64-native-image` OK、`build/p3/usb/hdd-image.img` 2485125120 byte、SHA-256 `b36df12fa60ffe236829b082251bbb3e0458edc2bf7367de1f5d880353f62805`。Debian の `~/ws039/hdd-image.img` に写して一致を確かめた。warning は外部の package（openssl など）と perl の locale だけ）
- [x] 実機の操作の道具 [tests/hw-run.sh](../tests/hw-run.sh)（write: USB へ dd、boot: BootNext と reboot、collect: zedBSD の SSH で status・dmesg・VBIOS を取る、debian: 戻る）。実機ではまだ流していない
- [x] 電源の入れ直し・USB の差し込み・`Boot0001` の追加（ユーザー・Q1、2026-10-16 08:1x）
- [x] 実機の 1 回目（Q1）: A0 ok、A1 fail（loader の 640x480 と timing の 1920x1080）→ A1 を viewport と scaler で読むように直し、A1 の失敗でも control の node を出すようにした
- [x] 実機の 3 回目（P3、2026-10-16 08:28）: **A0・A1・A1v ok**、dump と照合済み
- [ ] Q1: merge と clearance の判定。次の段（A2、p003）の前に、拡大の GOP のままか `video=640x480` を外すかを決める
