# WS279 Pi 5 の GPU（V3D 7.1）と display（HVS6）の初期化と submit の手順

- 版 1（2026-10-21 P2、ws279-p001、q1544）。code は書いていない。Fable の design-reviewer は使わない（2026-10-21 ユーザー「Pi5の対応は、Fableによるレビューは不要です。Opus 5.5で対応しましょう。」）。
- 依頼の元: 2026-10-21 ユーザー「GPUは別途Mesaの初期化手順を正確にまとめてから実装しましょう。」
- 実機・QEMU での観測は**一つもしていない**。下の値は source と dtb を読んだ結果。実機で確かめる点は §8 に分けて書く。
- license（[Guardrail](../guardrail.md) の「GPL にも広げる」2026-10-15、範囲は全部の WS）: GPL の file（Linux の v3d・vc4・pmdomain、Raspberry Pi の iommu・firmware KMS）の register の offset・bit・表の値は data として使い、block ごとに出典（file と行）を書く。code・comment・構造・名前は写さない。packet の形と QPU の命令の形は MIT の Mesa から取る。この文書の文は全て自分で書いた（写しは無い）。register の名前はこの文書の中だけの呼び名で、zedBSD の code では独自の名前にする。

## 0. 資料

| 記号 | 資料 | 版 | license | 置き場所（読むだけ） |
| --- | --- | --- | --- | --- |
| L | Linux mainline `drivers/gpu/drm/v3d/`・`vc4/`・`drivers/pmdomain/bcm/bcm2835-power.c`・`drivers/clk/bcm/clk-raspberrypi.c`・`include/soc/bcm2835/raspberrypi-firmware.h` | v6.19（05f7e89ab9731565d8a62e3b5d1ec206485eeb0b） | GPL-2.0 | p2 の worktree の `plan/ws141/temp/linux/`（WS141 の作業の tree、commit しない） |
| LD | Linux mainline `arch/arm64/boot/dts/broadcom/bcm2712.dtsi` | v6.19 | GPL-2.0 / MIT（dts） | scratchpad に取得（下の R の行に hash） |
| R | Raspberry Pi の Linux `drivers/iommu/bcm2712-iommu.c`・`drivers/gpu/drm/vc4/vc4_firmware_kms.c`・`include/soc/bcm2835/raspberrypi-firmware.h`・`drivers/gpu/drm/v3d/v3d_drv.c` | rpi-6.12.y（2026-10-21 に branch の先頭 58b28eeef87abfb49e52591ceae6af89a29242aa から取得） | GPL-2.0 | scratchpad（SHA-256 は下） |
| M | Mesa `src/broadcom/`（common・cle・qpu・compiler・vulkan・simulator） | 25.3.6（06f9e28304d5d3f109c33535c1c25b9df5769af2） | MIT（`cle/v3d_packet.xml` は file に注記なし、WS141 の `mesa-lic.tsv` で UNKNOWN、Mesa 全体は MIT） | `plan/ws141/temp/mesa/` |
| F | firmware の dtb `bcm2712-rpi-5-b.dtb`（C1）・`bcm2712d0-rpi-5-b.dtb`（D0）を `dtc` で dts に戻した物 | main の `vendor/raspberrypi-firmware/boot/`（kernel の git hash 89586905b8603e545cce9089a81f5f35d65bc998） | GPL-2.0（dtb） | 下の F の行 |
| Z | zedBSD の Pi 4 の driver `src/drivers/gpu/bcm2711/`（56,705 行）と WS141 の記録 | main 2026fac56 | Zlib | |

SHA-256（主な file、先頭 16 桁。L・M は `plan/ws141/temp/` の tree、R は取得した file、F は main の `vendor/raspberrypi-firmware/boot/`）:

```
L dd7312fb769242f7 v3d/v3d_drv.c
L 83a23f52de2d2011 v3d/v3d_gem.c
L 2bdf8ba31b1120f3 v3d/v3d_irq.c
L cad07c01f0a10414 v3d/v3d_mmu.c
L ab374d39a47af85b v3d/v3d_regs.h
L 2afa8f9cfc1bee21 v3d/v3d_sched.c
L 89fb981c35aee12d vc4/vc4_hvs.c
L 72abdef454d6c12e vc4/vc4_plane.c
L dc529da1453b9b64 vc4/vc4_crtc.c
L 550f2517ce8371ab vc4/vc4_regs.h
L 53ba4530e8e90a92 pmdomain/bcm/bcm2835-power.c
R 9c846e11acb7619a bcm2712.dtsi
R cb51903188484c9c bcm2712-iommu.c
R 76c4212ccf59dfca vc4_firmware_kms.c
R 4eef64f4ba698648 raspberrypi-firmware.h
R e41f7fb105983588 rpi-v3d_drv.c
M b13f995b1a4b606a cle/v3d_packet.xml
M abf436006dd3bed5 qpu/qpu_pack.c
M 4b1b90e3bb8ea614 qpu/qpu_instr.h
M f61745f35e5b34c7 qpu/qpu_instr.c
M 36b5b286e461e89d common/v3d_device_info.c
M 27f8e9adbd7710eb common/v3d_device_info.h
M dbd6959e3b10053f common/v3d_util.c
M fa4722f6346ba507 common/v3d_tfu.h
M d5debdf303653222 compiler/nir_to_vir.c
M e282c7ae7a5cc231 compiler/qpu_schedule.c
M ff2493df97d6191e compiler/vir_register_allocate.c
M 4102b7282bcc1647 compiler/qpu_validate.c
M 8e314e2c49360193 vulkan/v3dvx_cmd_buffer.c
M 3c5e22ad6e816443 vulkan/v3dvx_queue.c
M ffaf743923992b5b vulkan/v3dvx_meta_common.c
M eaf3c553f3fcd8e2 vulkan/v3dv_cmd_buffer.c
F 40a2fbe9c29e8b9a bcm2712-rpi-5-b.dtb
F 03d80fc8196bfd11 bcm2712d0-rpi-5-b.dtb
```

（R の行の `bcm2712.dtsi` は LD、`rpi-v3d_drv.c` は R の `v3d_drv.c` を手元で改名した物。R の `v3d_regs.h` は L と同じ hash だった。全桁は `sha256sum` で取り直せる。p002 の最初に全桁を `plan/ws279/tests/` に TSV で置く。）

---

## 1. V3D 7.1 の初期化

### 1.1 device tree の node（F、Pi 5 の firmware の dtb。C1 と D0 で V3D・HVS の node は同じ: `diff` で一致を確かめた）

| node | 値（F） | CPU から見た address（計算） |
| --- | --- | --- |
| `/axi/v3d@2000000`、`compatible = "brcm,2712-v3d"`、`status = "disabled"` | reg: hub 0x10_0200_0000 + 0x4000、core0 0x10_0200_8000 + 0x6000、sms 0x10_0203_0800 + 0x700（reg-names "hub","core0","sms"） | `/axi` の ranges・dma-ranges は 0x10_0000_0000〜を同じ番地に写す → そのまま |
| | `power-domains = <&pm 1>`、`resets = <&pm 0>`、`clocks = <&firmware_clocks 5>`、名前の property は綴りが `clocks-names`（`clock-names` ではない） | |
| | `interrupts = <SPI 250>, <SPI 249>`（GIC の INTID 282・281）。順は binding の通り 0 = hub、1 = core（L `v3d_irq.c` 262〜297 行で index 1 を core、0 を hub として取る） | Pi 4 は 1 本を共用、Pi 5 は 2 本 |
| `/soc/watchdog@7d200000`、`compatible = "brcm,bcm2712-pm"` | reg 0x308、reg-names "pm" だけ（asb・rpivid_asb は無い）、clocks 無し | soc の ranges（子 0 → 親 0x10_0000_0000）で 0x10_7d20_0000 |
| `/soc/firmware/clocks`（`raspberrypi,firmware-clocks`） | mailbox は `/soc/mailbox@7c013880`（0x10_7c01_3880、SPI 33） | |

- zedBSD は firmware の dtb の node を status に関わらず読む（Z `fdt-util.c` 12〜14 行の方針）。V3D の node は disabled のままでよい。
- mainline の bcm2712.dtsi（LD）には v3d の node が無く（`grep` で無いことを確かめた）、hvs に `iommus` も無い。zedBSD が使うのは F（firmware の dtb）なので、F の値に従う。
- 名前は driver が clock を index 0 で取ればよい（L `v3d_drv.c` 360 行は名前なしで最初の clock を取る）。

### 1.2 電源と reset（L `bcm2835-power.c`、PM block を ARM から直接書く。firmware の mailbox の power domain は使わない）

PM の register は Pi 4 と同じく書く値に password 0x5a00_0000 を OR する（L 102・110〜111 行）。

| 項目 | Pi 4（Z `v3d-power.c` 17〜25 行と同じ値） | Pi 5（L 82・87・380〜390・449〜458 行） |
| --- | --- | --- |
| V3D の domain の register | PM + 0x10c（GRAFX） | PM + 0x304（2712 の GRAFX） |
| reset を解く bit | bit 6 | bit 6（同じ） |
| ASB（AXI の bridge）の停止・再開 | rpivid_asb + 0x08（slave）・0x0c（master）、REQ_STOP bit 0、ACK bit 1、1 µs | **無し**（Pi 5 の pm には asb の reg が無く、L 385〜387 行で ASB の手順を飛ばす） |
| domain の clock | firmware の clock 5 を 1 µs だけ on → off してから bit 6 を立て、また on | pm の node に clocks が無いので、domain の側の clock の操作は無い（L 516 行で optional の clock が NULL） |

手順（L から読んだ順、自分の言葉で）:

1. **親の domain（GRAFX）**: L は全ての board で GRAFX → V3D の親子を登録し（L 622〜631 行）、親の power on は PM + 0x10c の POWUP（bit 0）が立っていれば何もしない（L 231〜233 行）。2712 で PM + 0x10c が何を意味するかは L に書かれていない（**未確認**）。zedBSD は Pi 5 で PM + 0x10c に**書かず**、読んだ値を印に出すだけにする（firmware が電源を入れていると仮定。§8 U1）。
2. **V3D の domain の on**: PM + 0x304 を読み、bit 6 を立てて書く（password 付き）。
3. **reset**（hang の後）: L の reset は domain の off → on（L 545〜573 行）。Pi 5 では PM + 0x304 の bit 6 を下ろして書き、また立てて書く。ASB が無いので、memory への道を止める手順は無い。止まらない AXI の転送が残る危険は §8 U2。
4. **電源を切る**（shutdown・suspend の時だけ。今は使わない）: SMS を power off にしてから（§1.4）、bit 6 を下ろす。

### 1.3 clock（firmware の mailbox、Pi 4 と同じ）

- V3D の clock は firmware の clock ID 5（L `raspberrypi-firmware.h` 140〜156 行の並び）。tag は Pi 4 の WS141 の文書と同じ（GET_CLOCK_STATE 0x00030001、SET_CLOCK_STATE 0x00038001、GET/SET_CLOCK_RATE 0x00030002/0x00038002、GET_MAX 0x00030004、GET_MIN 0x00030007）。
- mainline は V3D の clock を登録の時に最大に上げる（L `clk-raspberrypi.c` 137〜140 行の maximize）。Raspberry Pi の版は加えて、probe で最大の rate を読み、暇な時に下げる（R `v3d_drv.c` 424〜450・478 行）。zedBSD は最初は SET_CLOCK_STATE(5, on) と、GET_MAX で読んだ rate への SET_CLOCK_RATE だけにする（Pi 4 の p010 の core clock の扱いと同じ方針）。
- HVS の clock は firmware の clock 4（core、"core"）と 16（disp、"disp"）（F の hvs の node の clocks）。display は firmware が既に動かしているので、zedBSD は rate を変えない。
- Pi 5 の mailbox は bus の address の alias を付けない（WS278 design §1 の表）。

### 1.4 SMS（7.1 で新しく増えた block、L `v3d_drv.c` 290〜318 行、`v3d_gem.c` 108〜122 行、`v3d_regs.h` 518〜542 行）

SMS は V3D の電源と isolation の状態機械。register は sms の窓の中:

| 名前（この文書の呼び名） | offset | 内容 |
| --- | --- | --- |
| REE_CS | 0x000 | 状態 [3:0]（書くと状態の要求）、他の bit は下 |
| TEE_CS | 0x400 | bit 30 = power off の要求、bit 29 = power off の解除、状態 [3:0] |
| 状態の値 | — | 0x0 = idle、0xa = reset のための isolation 中、0xb = reset 中、0xc = power off のための isolation 中、0xd = power off |

（他の bit: 31 interrupt、28 lock、27 lock の解除、26〜19 は mode の出入り、[16:10] SEQ_PC、[9:8] hub と core の状態、[7:6] new mode、[5:4] old mode。L `v3d_regs.h` 520〜542 行。zedBSD は状態 [3:0] と bit 30・29 だけを使う。）

起こし方（probe の中で clock を on にした直後、V3D の register を初めて読む前）:

1. TEE_CS に bit 29（power off の解除）だけを書く。
2. TEE_CS の状態 [3:0] が 0（idle）になるまで待つ（L は最大 100 ms）。
3. SMS の reset: REE_CS に状態 4 を書く。
4. REE_CS の状態が 0xa でも 0xb でもなくなるまで待つ（最大 100 ms）。

hang の reset（§2.6）でも 3〜4 を PM の reset の前に行う（L `v3d_gem.c` 125〜150 行の順: 割り込みを止める → SMS の reset → PM の reset → 不変の状態を書き直す → page table を書き直す → 割り込みを戻す）。

電源を切る時は TEE_CS に bit 30 を書き、状態が 0xd になるまで待つ（L `v3d_drv.c` 306〜318 行）。

### 1.5 識別（L `v3d_drv.c` 375〜395 行、M `common/v3d_device_info.c` 69〜104 行）

| 読む register | offset | 7.1 で期待する値 | 使い道 |
| --- | --- | --- | --- |
| hub MMU_DEBUG_INFO | hub + 0x1238 | PA の幅 = 30 + [11:8]、VA の幅 = 30 + [7:4] | DMA の幅、GPU の VA の幅（Pi 4 の p009 で直した VA 幅の扱いと同じ） |
| hub IDENT1 | hub + 0x00c | TVER [3:0] = 7、REV [7:4] = 1 → 版 71。NCORES [11:8] = 1 | 版の確認（dtb の compatible と一致すること）、core の数 |
| hub IDENT3 | hub + 0x014 | IPREV [15:8] = 「7.1.x の x」、[23:16] = compat_rev（M は rev と compat_rev として読む） | **rev で振る舞いが変わる**（下の表） |
| core IDENT0 | core + 0x000 | VER [31:24] = 7 | M はここと IDENT1 から版を作る |
| core IDENT1 | core + 0x004 | VPM_SIZE [31:28]（×8 KiB）、NSLC [7:4]、QUPS [11:8]、REV [3:0] = 1 | QPU の数 = NSLC × QUPS |

rev による差（M）:

| rev | 差 | 出典 |
| --- | --- | --- |
| < 5 | Texture Shader State に R/B swap の bit が無く、array stride が bit 32 から | M `cle/v3d_packet.xml` 1633 行〜の comment、`vulkan/v3dv_private.h` 2368〜2373 行（関数は 2369 行） |
| ≥ 5 | 上の bit 32 が R/B swap、array stride は bit 33 から 24 bit | 同上 |
| < 6 | CSD の CFG4（batch の数）は「数 − 1」を書く | M `vulkan/v3dv_cmd_buffer.c` 4244〜4250 行、L `v3d_sched.c` 455〜459 行 |
| ≥ 6 | CFG4 は数そのもの | 同上 |
| ≥ 7（または 6 で compat_rev ≥ 4） | 入力の unpack の MAX0 が使える | M `common/v3d_device_info.h` 100〜106 行 |
| ≥ 10（2712 D0） | GL Shader State Record が draw index と base vertex の入った別の形（全 field の位置が変わる） | M `cle/v3d_packet.xml` 1314 行〜、`vulkan/v3dvx_pipeline.c` 412〜420 行、`common/v3d_device_info.h` 88〜91 行 |

ユーザーの Pi 5 は 2 GB 版で D0 の基板（WS278 design §1）。D0 の V3D は 7.1.10（M の comment）なので、**draw index の形の shader state record が必須**、CFG4 は数そのもの、R/B swap の bit あり。C1 の rev は M に書かれていない（**未確認**、実機の IDENT3 で読む）。

版ごとの CL の定数（M `common/v3d_device_info.c` 77〜88 行）:

| | 4.2 | 7.1 |
| --- | --- | --- |
| clipper の XY の単位 | 1/256 pixel | 1/64 pixel |
| CLE の先読み（CL の終わりの後に余白として取る byte） | 256 | 1024 |
| CL の buffer の最小 | 4096 | 16384 |
| 描画先（RT）の最大の数 | 4 | 8（M `common/v3d_limits.h` 44 行） |

### 1.6 MMU と page table（L `v3d_mmu.c` 25〜80 行、`v3d_gem.c` 263〜310 行）

- 形式は 4.2 と**同じ**: 1 段の表、4 KiB の page ごとに 32 bit の PTE、4 GiB の VA で 4 MiB の表。PTE: bit 31 superpage（1 MiB）、bit 30 bigpage（64 KiB）、bit 29 書ける、bit 28 有効、[23:0] PFN（物理 36 bit まで）。Z `mmu.c` 25〜38 行の値と一致する。Pi 5 の RAM（2〜16 GiB）は 36 bit に収まる。
- V3D から見た address: `/axi` の dma-ranges が同じ番地なので、PTE の PFN は CPU の物理 address >> 12。
- 設定の順（4.2 と同じ register）: PT_PA_BASE（hub + 0x1204）に表の物理 >> 12 → MMU_CTL（hub + 0x1200）に enable と、PTE 無効・書き込み違反・cap 超えの abort と割り込み → ILLEGAL_ADDR（hub + 0x1230）に scratch page >> 12 と bit 31 → MMUC_CONTROL（hub + 0x1000）に enable → flush（MMUC に flush と enable を書き FLUSHING bit 2 が下りるまで、次に MMU_CTL に TLB_CLEAR bit 2 を足して TLB_CLEARING bit 7 が下りるまで、各 100 ms）。
- **7.1 の差は MMUC の CLEAR の bit だけ**: 4.2 は bit 3、7.1 は bit 11（L `v3d_regs.h` 155 行）。L は CLEAR を使っていない。
- page 0 は割り当てない（0 は hardware で「無し」の意味）。Pi 4 と同じ。

### 1.7 不変の状態と cache（L `v3d_gem.c` 20〜37・183〜260 行、M `simulator/v3dx_simulator.c` 78〜140 行）

- 初期化と reset の後: core の L2TFLSTA（core + 0x034）= 0、L2TFLEND（core + 0x038）= 0xffff_ffff（L2T の flush を常に全体に）。MISCCFG（4.1 未満だけ）は書かない。Pi 4 と同じ。
- job の前の無効化（外から内へ）: L3（GCA）は 4.1 以上で無い → 何もしない、L2C は 3.3 以上で何もしない、L2T を FLUSH（L2TCACTL = L2TFLS bit 0 | FLM=0）、slice の cache を全部無効化（SLCACTL = 0x0f0f_0f0f）。**7.1 でも同じ register と値**（L は版で分けていない、M の simulator も 7.1 で同じ処理）。
- CSD の後と、cache の clean を頼まれた job の後: L2TCACTL に TMUWCF（bit 8）を書いて下りるまで → L2TFLS | FLM=2（CLEAN）を書いて L2TFLS が下りるまで（各 100 ms）。7.1 でも同じ。
- **注意（未確認）**: 7.1 の L2TCACTL・SLCACTL の bit の位置が 4.2 と同じかは、L と M（公開の部分）が版で分けていないことからの推論。M の simulator が include する 7.1 の register の header（`libs/core/v3d/registers/7.1.7.0/v3d.h`）は公開されていない。実機で clean の待ちが終わることで確かめる（§8 U3）。

### 1.8 割り込み（L `v3d_irq.c`）

| | 4.2 | 7.1 |
| --- | --- | --- |
| line | 1 本（hub と core の共用、Pi 4 の SPI 74） | 2 本（hub SPI 250、core SPI 249） |
| core の INT_STS（core + 0x050）で使う bit | OUTOMEM b2、FLDONE b1（bin の終わり）、FRDONE b0（render の終わり）、CSDDONE **b7**、GMPV b5 | OUTOMEM b2、FLDONE b1、FRDONE b0、CSDDONE **b6**（L `v3d_regs.h` 283 行）。core の GMPV は無い |
| hub の INT_STS（hub + 0x050）で使う bit | MMU_WRV b5、MMU_PTI b4、MMU_CAP b3、TFUC b1 | 同じ＋GMPV **b6**（L `v3d_regs.h` 68 行） |
| 性能 counter の bit（使わない） | PCTR b6 | PCTR b5 |

初期化の順（L `v3d_irq.c` 249〜307 行）: core の INT_CLR（core + 0x058）と hub の INT_CLR（hub + 0x058）に上の bit を書いて残りを消す → line を登録 → MSK_CLR（+0x064）で上の bit の mask を外す（`v3d_irq_enable`）。

MMU の違反の時の AXI の ID の読み方（log のため、L `v3d_irq.c` 190〜215 行の表の値）: 7.1 は VIO_ID の下 7 bit、範囲 0x00〜0x2f L2T、0x30〜0x37 CLE、0x38 PTB、0x39 PSE、0x3a CSD、0x40〜0x5f TLB、0x60〜0x6f MMU、0x7c〜0x7d TFU、0x7f GMP。違反の address は VIO_ADDR（hub + 0x1234）を va 幅 − 32 だけ左へずらす（4.2 と同じ）。

### 1.9 初期化の全体の順（zedBSD の段の印の案）

Pi 4 と同じく、firmware の framebuffer に段の印を出して実機で進める（WS141 の進め方）。

| 段 | すること | 印に出す値 |
| --- | --- | --- |
| V0 | dtb から node を見つけ、hub・core0・sms・pm の窓を map。IRQ 2 本を登録（mask のまま）。clock 5 の状態と最大の rate を読む | 窓の物理 address、INTID、clock の状態と rate、PM + 0x10c・0x304 の値 |
| V1 | clock を on・最大へ。PM + 0x304 の bit 6 を立てる。SMS を idle に（§1.4 の 1〜4） | TEE_CS・REE_CS の値と待った時間 |
| V2 | 識別（§1.5）: MMU_DEBUG_INFO、IDENT0/1/3、core IDENT1 | 版・rev・compat_rev・QPU の数・VPM・VA/PA 幅 |
| V3 | scratch page と 4 MiB の表を確保（連続、uncached か clean）、不変の状態（§1.7）、MMU（§1.6） | MMU_CTL の値、flush が終わった事 |
| V4 | 割り込み（§1.8） | 残っていた INT_STS |
| V5 | noop の bin/render job（§2.2）を投げ、FLDONE と FRDONE を待つ | CT0CA/CT1CA、BFC/RFC、かかった時間 |
| V6 | TFU の写し（§2.4）、CSD の空の dispatch（§2.5） | TFU_CS、CSD_STATUS |

---

## 2. job の submit と完了

### 2.1 CL の register（7.1 で offset は 4.2 と同じ。L `v3d_regs.h` 292〜346 行は版で分けていない）

CT0（bin）と CT1（render）の QBA（+0x160/+0x164）・QEA（+0x168/+0x16c、書くと動く）、CT0QMA/QMS（+0x170/+0x174、tile alloc の address と大きさ）、CT0QTS（+0x15c、bit 1 enable | tile state の address）、PTB の BPOS（+0x30c、overflow の大きさ）、BFC/RFC（+0x134/+0x138、終わった frame の数）、CT0CA/CT1CA・CT0RA/CT1RA（hang の判定）。core の窓は Pi 5 で 0x6000 byte（Pi 4 は 0x4000）。

### 2.2 bin と render の job（L `v3d_sched.c` 226〜333 行、M `vulkan/v3dvx_queue.c` 30〜131 行の noop job、`v3dvx_cmd_buffer.c` 50〜110・1010〜1200 行）

kernel の側の順は 4.2 と**同じ**:

- bin: cache の無効化（§1.7）→ PTB_BPOS = 0（前の job の overflow を使わない）→ QMA/QMS（tile alloc）→ QTS（tile state、enable 付き）→ QBA → QEA（ここで動く）。終わりは core の FLDONE。OUTOMEM が来たら 256 KiB の overflow を確保して PTB_BPOA/BPOS に書く（L `v3d_irq.c` 42〜100 行、Z の今の扱いと同じ）。
- render: cache の無効化 → CT1QBA → CT1QEA。終わりは FRDONE。QCFG は書かない。

**CL の中身（packet）が 7.1 で変わる**。M `cle/v3d_packet.xml` の版の印（`max_ver="42"` と `min_ver="71"`）が付いた物が全部の差で、zedBSD の encoder が触る物は次の通り:

| packet・struct（code） | 4.2 | 7.1 | zedBSD への影響 |
| --- | --- | --- | --- |
| Tile Binning Mode Cfg（120） | 幅・高さ − 1、double buffer、MSAA、最大の BPP、RT の数 − 1、tile alloc の block の大きさ | 幅・高さ − 1、**tile の幅と高さを log2 で明示**（0=8、1=16、2=32、3=64 pixel）、tile alloc の block の大きさ。BPP・RT の数・MSAA は無い | Z `native-pass.c` 186〜193 行の bin の前置きの byte を版ごとに |
| Tile Rendering Mode Cfg（121）Common（sub-id 0） | BPP の最大、内部の depth の型、early Z、幅・高さ、RT の数 | **tile の幅・高さの log2**、early-Z の無効と方向、depth buffer の無効、double buffer、MSAA、幅・高さ、RT の数 − 1 | 書き直し |
| 121 Color（4.2 の sub-id 1、全 RT の型を 1 つに） | あり | **無い**。代わりに RT ごとの Render Target Part1（sub-id 2）: clear color の下 32 bit、内部の型と clamp（新しい enum、例 8 bit unorm = 8）、内部の BPP、**tile buffer の中の stride（128 bit 単位、2 行分）と base（512 bit 単位）** | RT ごとに tile buffer の配置を driver が計算する（M `v3dvx_cmd_buffer.c` 1141〜1158 行の計算: stride = tile 幅 × BPP の word 数 / 2、次の RT の base は tile 高さ × stride / 8 だけ進める）。使わない RT 番号は stride = 1 で印を付ける |
| 121 ZS Clear Values | sub-id 2 | sub-id 1（中身は同じ: Z の float、stencil 8 bit） | sub-id の番号が違う |
| 121 Clear Colors Part1〜3（4.2 の sub-id 3〜5） | clear color を 3 つに分け、Part3 に UIF の padded height と raster の stride | Render Target Part2（sub-id 3、mid 40 bit）・Part3（sub-id 4、top 56 bit）。**UIF の padded height の field は無い** | 64・128 bpp の時だけ Part2/3 |
| Clear Tile Buffers（25） | Z/S と全 RT の clear の bit | **Clear Render Targets**（field 無し） | |
| Cfg Bits（96） | early Z の bit あり、line の rasterization 2 bit | early Z の bit は無い（Common に移った）、Z の clipping mode・Z clamp・depth bounds の bit が増えた | |
| Blend Cfg（84） | RT mask 4 bit | RT mask 8 bit | |
| Clipper XY Scaling（110） | 1/256 pixel の半幅・半高さ | **1/64 pixel** | viewport の計算の単位（Z `native-viewport.c`） |
| Clipper Z no guardband（112） | 無い | 新しい | 使わなくてよい（任意） |
| Depth Bounds Test Limits（93） | 無い | 新しい | 使わない |
| GL Shader State Record（struct） | 既定の属性値の address あり、VS・CS の VPM の入出力の分離の bit | 既定の属性値の address は**無い**、分離の bit は無い、FEP の depth を遅らせない bit が増えた、各 shader の address の位置が 32 bit 前へ | 書き直し。rev ≥ 10 は Draw Index の形（§1.5） |
| TMU Config Parameter 2（struct） | | 書き込みの変換・DIM の問い合わせの bit が増えた | 書き直し |
| Texture Shader State（struct） | | Cb・Cr の base（26 bit、手で shift）、chroma の offset、R/B swap（rev ≥ 5）、array stride 24 bit、sRGB の bit が transfer func の 3 bit に、flip の bit の位置 | 書き直し |
| Store/Load Tile Buffer General（29/30） | RT 0〜3 | RT 0〜7（store の値が増えただけ） | 同じ形 |
| Output Image Format の enum | | 46〜63 が増えた（使わない） | |

変わらない物（版の印が無い）: Number of Layers、Flush VCD Cache、Start Tile Binning、Flush、Supertile Coordinates、Tile Coordinates（Implicit）、End of Loads、End of Tile Marker、Branch to Implicit Tile List、Return、Multicore Rendering Supertile Cfg・Tile List Set Base、Tile List Initial Block Size、Start Address of Generic Tile List、End of Rendering、Viewport Offset、Clip Window、Clipper Z、Vertex Array Primitives・Indexed Primitive List、Flat Shade・Centroid・Non-perspective の flag、Color Write Masks、Blend Constant Color、Sample State、Occlusion Query Counter、Sampler State。

最小の job（V5 の noop、M `v3dvx_queue.c` 30〜131 行の 7.1 の分岐の順）:

- BCL: Number of Layers(1) → Tile Binning Mode Cfg（幅 1、高さ 1、tile 64×64 = log2 3/3）→ Flush VCD Cache → Start Tile Binning → Flush（binning の flush）。
- RCL: Rendering Mode Cfg Common（early-Z 無効、1×1、RT 1、tile 64×64）→ Render Target Part1（BPP 32、型 8、stride 1 = 使わない）→ ZS Clear Values（1.0、0）→ Tile List Initial Block Size（自動の連鎖、64 B）→ Multicore Rendering Tile List Set Base（tile alloc）→ Multicore Rendering Supertile Cfg（全部 1）→ 間接の list（Implicit Tile Coords → End of Loads → Branch to Implicit Tile List → Store General(None) → End of Tile → Return）の Start Address of Generic Tile List → Supertile Coordinates(0,0) → End of Rendering。
- 4.2 との差は tile の大きさを Binning と Common に明示する所と、Color の packet が Render Target Part1 になる所だけ。

tile の大きさの選び方（M `common/v3d_util.c` 99〜200 行）: 7.1 は TLB が color 16 KiB・depth 16 KiB・補助の depth 8 KiB で、全 RT の BPP の合計（byte、MSAA なら ×4）と depth 4 byte（MSAA なら ×4）が収まる最大を 64×64、64×32、32×32、32×16、16×16、16×8、8×8 の順に探す（depth が補助の 8 KiB に収まれば color に 32 KiB まで使える）。double buffer なら 1 段小さく。4.2 は RT の数・最大の BPP・MSAA から表を引く。Keiland の合成（RGBA8 1 枚、depth 無し）は 7.1 で 64×64（4 byte × 4096 = 16 KiB）になる見込み（計算、未確認）。

### 2.3 CL の buffer の決まり（M）

- 7.1 は CLE が 1024 byte 先読みするので、CL の buffer の終わりの後 1024 byte も GPU の VA に map しておく（4.2 は 256）。最小 16 KiB。守らないと「MMU error from client CLE」になる（M `common/v3d_device_info.h` 64〜75 行の comment の内容）。
- TFU は 64 byte 先読みする（`V3D_TFU_READAHEAD_SIZE`、同 77〜80 行）。TFU が読む buffer は 64 byte 余分に map する（4.2 と同じ）。

### 2.4 TFU（L `v3d_sched.c` 334〜379 行、`v3d_regs.h` 99〜147 行、M `common/v3d_tfu.h`、`vulkan/v3dvx_meta_common.c` 928〜1033 行）

register は hub の中で **0x400 → 0x700 に移った**。

| | 4.2 | 7.1 |
| --- | --- | --- |
| CS / SU / ICFG | 0x400 / 0x404 / 0x408 | 0x700 / 0x704 / 0x708 |
| IIA / ICA / IIS / IUA | 0x40c / 0x410 / 0x414 / 0x418 | 0x70c / 0x710 / 0x714 / 0x718 |
| IOC（出力の形式・stride・mipmap の数） | 無い（IOA の下位と ICFG に混ざる） | **0x71c（新しい）** |
| IOA / IOS | 0x41c / 0x420 | 0x720 / 0x724 |
| COEF0〜3 | 0x424〜0x430（COEF0 の USECOEF の時だけ 1〜3 を書く） | 0x728〜0x734（**常に 4 つとも書く**） |
| CRC | 0x434 | 無い |

順: IIA → IIS → ICA → IUA → IOA → IOC（7.1 だけ）→ IOS → COEF0 → COEF1〜3 → ICFG に IOC（完了で割り込み、bit 0）を足して書く（ここで動く）。完了は hub の TFUC。

field の位置（M `common/v3d_tfu.h` 25〜67 行）:

| field | 4.2 | 7.1 |
| --- | --- | --- |
| 出力の tiling（LT=3、UBLINEAR 1/2 列=4/5、UIF=6、UIF+XOR=7） | IOA の [5:3] | IOC の [14:12]（shift 12） |
| level 0 を書かない | IOA の bit 0 | IOC の bit 0 |
| 出力の stride（UIF は UIF block の高さ、raster は pixel） | ICFG の OPAD（4.2 は padding の数） | IOC の [31:16]（shift 16）に stride そのもの |
| mipmap の数 | ICFG の shift 5 | IOC の shift 4 |
| 入力の形式（raster=0、SAND128/256=1/2、LT=11、UBLINEAR=12/13、UIF=14/15） | ICFG の shift 18 | ICFG の shift 23 |
| texture の型 | ICFG の shift 9 | ICFG の shift 16 |

TFU は raster の入力を UIF に並べ替えられる（4.2 と同じ）。Keiland の窓の TFU（Pi 4 の WS141 p013・WS211）はこの表で 7.1 に移せる。Pi 5 には H.264 の hardware の decoder が無いので、Pi 4 の video（WS236、vchiq）の YUV の TFU の経路は Pi 5 では使わない。

### 2.5 CSD（compute、L `v3d_sched.c` 381〜428 行、`v3d_regs.h` 413〜476 行、M `common/v3d_csd.h`、`v3dv_cmd_buffer.c` 4236〜4255 行）

| | 4.2 | 7.1 |
| --- | --- | --- |
| STATUS | core + 0x900 | core + 0x900（同じ） |
| QUEUED_CFG0〜6 | 0x904〜0x91c | 0x930〜0x948 |
| QUEUED_CFG7 | 無い | 0x94c（L は 0 を書く） |
| CURRENT_CFG0〜6・7 | 0x920〜0x938 | 0x958〜0x970・0x974 |
| CURRENT_ID0/1 | 0x93c / 0x940 | 0x978 / 0x97c |
| 完了の割り込み | core の bit 7 | core の bit 6 |

順: cache の無効化 → CFG1〜6 → CFG7 = 0（7.1）→ CFG0（ここで動く）。完了は CSDDONE。その後に cache の clean（§1.7）を必ず行ってから fence を返す（L は CSD の job の後に clean の job を続ける）。CFG4 の batch の数の「− 1」の有無は rev で変わる（§1.5）。CFG0〜2 の workgroup の数は [31:16]、CFG3 の supergroup の値の位置は 4.2 と同じ（M `common/v3d_csd.h`）。

### 2.6 完了の待ち、hang、reset（L `v3d_sched.c` 725〜866 行、`v3d_gem.c` 124〜150 行）

- 完了: 割り込み（FLDONE・FRDONE・TFUC・CSDDONE）で fence を返す。Z の今の作り（IRQ は印を立てるだけ、worker が退役させる。Z `v3d-job.c`・`v3d-hardware.c`）をそのまま使う。
- hang の判定（Linux は 500 ms ごとに見る）: bin/render は CTnCA と CTnRA が前と同じなら hang、進んでいれば待ち続ける。CSD は CURRENT_CFG4（残りの batch）が減っていなければ hang。TFU は期限で hang。7.1 で違うのは CSD の CURRENT_CFG4 の offset（0x968）だけ。
- reset の順（7.1）: ERR_STAT（core + 0xf20）を log → 割り込みを止める → SMS の reset（§1.4 の 3〜4）→ PM の reset（§1.2 の 3）→ 不変の状態（§1.7）→ page table の register を書き直す（§1.6）→ 割り込みの残りを消して戻す。Pi 4 では Z が SMS の段を持たない（4.2 に SMS は無い）。

---

## 3. QPU の 7.1 の差（compiler が要る変更）

出典は全て M（`qpu/qpu_instr.h`・`qpu/qpu_pack.c`・`qpu/qpu_instr.c`・`compiler/*.c`、MIT）。

### 3.1 命令の形（M `qpu/qpu_pack.c` 46〜110 行の field の位置）

64 bit の ALU 命令の field:

| bit | 4.2 | 7.1 |
| --- | --- | --- |
| [63:58] | mul の op | 同じ |
| [57:53] | signal | 同じ（ただし表が違う、下） |
| [52:46] | cond・flag | 同じ |
| [45]・[44] | mul・add の書き先が magic か | 同じ |
| [43:38]・[37:32] | mul・add の書き先 | 同じ |
| [31:24] | add の op | 同じ（op の番号の表は違う） |
| [23:21]・[20:18] | mul の入力 b・a の **mux**（r0〜r5・A・B） | [23:18] = **raddr_c**（mul の a） |
| [17:15]・[14:12] | add の入力 b・a の mux | [17:12] = **raddr_d**（mul の b） |
| [11:6]・[5:0] | raddr_a・raddr_b（2 つの読み口を 4 つの入力で分け合う） | raddr_a（add の a）・raddr_b（add の b） |

要点:

- **accumulator（r0〜r5）が無い**（M `common/v3d_device_info.c` 74 行 `has_accumulators = ver < 71`）。入力は全て register file（rf0〜rf63）か small immediate。add と mul が別々に 2 つずつ読み口を持つので、4.2 の「2 つの読み口を 4 つの入力で分け合う」制約が無くなる。
- small immediate は 4 つの読み口のどれにも置ける（signal の SMIMM_A/B/C/D）。ただし 1 命令に 1 つだけ、add の側は a か b、mul の側は c か d（M `compiler/qpu_schedule.c` 986〜1011 行）。
- 書き先の magic の番号: 0〜4（r0〜r4）は予約、5 は QUAD（4.2 では r5）、19〜24（RECIP・RSQRT・EXP・LOG・SIN・RSQRT2 の SFU）は予約、55 は REP（4.2 では R5REP）（M `qpu/qpu_instr.h` 94〜137 行）。TMU・TLB・VPM・SYNC・UNIFA などの他の magic の番号は同じ。

### 3.2 signal（M `qpu/qpu_pack.c` 131〜198 行の 2 つの表）

7.1 の表（番号 → 意味）: 0 なし、1 thrsw、2 ldunif、3 thrsw+ldunif、4 ldtmu、5 thrsw+ldtmu、6 ldtmu+ldunif、7 thrsw+ldtmu+ldunif、8 ldvary、9 thrsw+ldvary、10 ldvary+ldunif、11 thrsw+ldvary+ldunif、12 ldunifrf、13 thrsw+ldunifrf、14 SMIMM_A、15 SMIMM_B、16 ldtlb、17 ldtlbu、18 wrtmuc、19 thrsw+wrtmuc、20 ldvary+wrtmuc、21 thrsw+ldvary+wrtmuc、22 ucb、23 予約、24 ldunifa、25 ldunifarf、26 ldtmu+wrtmuc、27 thrsw+ldtmu+wrtmuc、28〜29 予約、30 SMIMM_C、31 SMIMM_D。4.2 の表とは番号の割り当てが違う（4.2 には rotate・SMIMM_B の組があり、SMIMM_A/C/D は無い）。

暗黙の書き先: 7.1 の ldunif・ldunifa・ldvary は **rf0 に暗黙に書く**（M `qpu/qpu_instr.c` 1004〜1014 行。4.2 では ldunif は r5、ldvary は r3 と r5、ldtmu は r4）。ldtmu は 7.1 では signal の書き先（sig_addr）に書く。

### 3.3 op（M `qpu/qpu_pack.c` 585〜745 行の v3d71 の add、747〜800 行の mul の表）

- **SFU は add の op**になった（op 188 の raddr_b の値 32〜37 = RECIP・RSQRT・EXP・LOG・SIN・RSQRT2）。4.2 は magic の書き先に書いて 2 命令後に r4 から読む。7.1 は op の結果を普通の書き先に書き、**次の命令が読むまでに 2 命令の間を空ける**（M `compiler/qpu_schedule.c` 1848〜1849 行の latency 2）。SFU を同時に 1 つだけ使う決まりは同じ。
- MOV・FMOV が add にも mul にもある（add の op 249、mul の op 14 の raddr の値で選ぶ）。4.2 は mul の MOV と、add の OR で写していた（Z `qpu.h` 115〜124 行の EITHER の扱い）。
- 新しい op: ROTQ・ROT・SHUFFLE（252〜254、4.2 の rotate の signal と r5 の broadcast の代わり）、BALLOT・BCASTF・ALLEQ・ALLFEQ（subgroup）、V10PACK・V11FPACK、mul の FTOUNORM16・FTOSNORM16・VFTOUNORM8 など（M `nir_to_vir.c` 1782〜1812 行の 7.1 だけの op）。
- 入力の unpack に SAT（[0,1] に飽和）・NSAT（[−1,1]）・MAX0 が増えた（M `qpu/qpu_instr.h` 301〜305 行）。NIR の fsat を FMOV の unpack にできる（M `nir_to_vir.c` 1802〜1806 行）。MAX0 は rev による（§1.5）。
- op の番号の表は 4.2 と 7.1 で違う（同じ op でも番号と、1 入力の op を選ぶ field が mux から raddr に変わる）。encoder は版ごとの表を持つ。

### 3.4 shader の入口の payload（M `nir_to_vir.c` 4751〜4795 行、`vir_register_allocate.c` 1215〜1250 行）

| | 4.2 | 7.1 |
| --- | --- | --- |
| fragment の W | rf0 | **rf3** |
| fragment の centroid W | rf1 | rf1 |
| fragment の Z | rf2 | rf2 |
| compute の payload 0 / 1 | rf0 / rf2 | **rf3** / rf2 |
| varying の C 係数 | r5（ldvary が 1 命令遅れて書く） | **rf0**（同じく 1 命令遅れて書く） |

varying の補間（M `nir_to_vir.c` 1041〜1110 行）: ldvary で係数を受け、W を掛けて C を足す形は同じで、C の置き場所だけが r5 → rf0 に変わる。

### 3.5 schedule の決まり（M `compiler/qpu_schedule.c`・`qpu_validate.c`）

- ldvary の後の rf0 の衝突: ldvary は rf0 を 1 命令遅れて書くので、ldvary の直後の命令は rf0 を書いてはいけず、ldvary の次の命令で rf0 を読んではいけない（qpu_schedule.c 600〜613・690〜700 行）。
- thrsw（thread の切り替え）と終わり（thrend）:
  - 7.1 では ldvary を thrsw の 3 つの遅延の slot に置けるが、**2 番目の slot だけは不可**（qpu_validate.c 258〜265 行、qpu_schedule.c 1528〜1537 行）。4.2 は全部の slot で不可。
  - thrend の命令は add・mul で register file に書いてはいけない。thrend の後（遅延の slot）は rf2・rf3 に書いてはいけない（fragment の setup が上書きする）（qpu_validate.c 329〜374 行、qpu_schedule.c 1996〜2030 行）。4.2 は遅延の slot で register file に一切書けない。
  - 暗黙の rf0 の書き込みと、同じ時の他の signal による register file の書き込みが重なる時（「rf0 の flop の衝突」）は、thrsw の最後の遅延の slot に register file を書く signal を置かない（qpu_schedule.c 1669〜1720・2088 行）。
  - SFU の命令は thrend の最後の 2 つの slot に置かない（7.1 は slot 2、qpu_schedule.c 2058〜2068 行）。
- accumulator が thrsw で消える、という 4.2 の制約（Z `shader-schedule.c` が扱う物）は無くなる。
- register の割り当て: 物理 register は 64 個を thread の数で分けるのは同じ（訂正 2026-10-21 p004: 4 thread なら rf0〜rf31 の 32 個。M `vir_register_allocate.c` 1055〜1072 行は thread の index ごとに 64 を半分にし、4.x 以降は 2 thread が index 0）。7.1 は rf0 を ldunif のためになるべく空けておく（同 926〜1010 行）。最後の 3 命令が使えない register がある（同 977〜990 行、版で違う。7.1 は rf2・rf3 が thrend の後で壊れる）。

### 3.6 TMU・TLB・VPM

- TMU の設定の書き込み（TMUC・TMUS・TMUD など magic）と texture の state の参照の形は同じ。7.1 の TMU は border color を常に 32 bit float で受けて自分で変換する（M `vulkan/v3dvx_device.c` 52〜160 行の border color の encode、137 行からが 4.2 だけの変換）。
- 4.2 では ldtmu と SFU・TLB・VPM の同時使用の制限の数え方に ldtmu が入るが、7.1 では入らない（qpu_validate.c 287〜298 行）。
- TLB への書き込み（色・Z）の命令の形は版で分かれていない（M の compiler の TLB の書き込みに版の分岐は無い。§8 U5 で実機で確かめる）。

### 3.7 zedBSD の compiler への影響

Z の構成: 共通の SPIR-V の frontend と IR（`src/drivers/gpu/compiler/`）→ bcm2711 の lowering（`shader.c`・`shader-analyze.c`・`shader-lower.c`・`shader-output.c`）→ scheduler と register の割り当て（`shader-schedule.c` 2,360 行、accumulator と r4/r5 の暗黙の書き込みを前提）→ encoder（`qpu.c`・`qpu.h`、4.2 の mux と signal の表）。

| 部分 | 7.1 で | 見込み |
| --- | --- | --- |
| 共通の frontend・IR・最適化 | そのまま | 0 |
| lowering（scalar の op、varying、TMU、TLB、VPM の epilogue） | 大半は共有。SFU の op 化、fsat の unpack、payload の register（rf3）、C 係数の rf0、MOV の扱いを版で分ける | 中 |
| scheduler・register の割り当て | 7.1 用を新しく（accumulator 無し、4 つの読み口、small immediate の置き場所、rf0 の暗黙の書き込み、ldvary の遅れ、SFU の latency 2、thrsw/thrend の 7.1 の決まり） | 大（4.2 の 2,360 行と同じ程度） |
| encoder | 7.1 用を新しく（field の位置、signal の表、op の番号の表、raddr） | 中（4.2 の 871 行と同じ程度） |
| 固定の shader（`meta/texture.*.spvasm`、`texture-words.inc`） | 訂正（2026-10-21 p004）: `texture-words.inc` は SPIR-V の word で実行時に compile するので版に依らず共有。7.1 の compiler で compile できる事を試験で確かめる | 0 |

---

## 4. HVS6 の display

### 4.1 部品（F・LD。C1 と D0 で node は同じ）

| 部品 | node | CPU の address | 割り込み |
| --- | --- | --- | --- |
| HVS6 | `/hvs@107c580000`、`brcm,bcm2712-hvs`、reg 0x1a000 byte、`iommus = <&iommu4>`（F だけ、LD には無い）、clocks firmware 4（core）・16（disp） | 0x10_7c58_0000 | `disp_intr`（L2 の intc）の 2・9・16 = channel 0・1・2 の end of frame |
| pixelvalve 0 / 1 | `brcm,bcm2712-pixelvalve0/1` | 0x10_7c41_0000 / 0x10_7c41_1000（各 0x100） | GIC SPI 101 / 110 |
| HDMI 0 / 1 | `brcm,bcm2712-hdmi0/1`（hdmi・dvp・phy・rm・packet・metadata・csc・cec・hd の 9 窓） | 0x10_7c70_1400 / 0x10_7c70_6400 | HPD は aon_intr の L2 |
| disp_intr | `brcm,bcm2711-l2-intc` | 0x10_7c50_2000 | GIC SPI 97 |
| MOP・MOPLET（writeback） | | 0x10_7c50_0000・0x10_7c50_1000 | disp_intr の 1・0 |
| IOMMU4 | `/axi/iommu@5200`、`brcm,bcm2712-iommu`、TLB の cache は `iommuc@5b00` | 0x10_0000_5200 | |

出力の対応（L `vc4_crtc.c` 1309〜1333 行、`vc4_hvs.c` 595〜612 行）: HDMI0 ← PV0 ← HVS channel 0、HDMI1 ← PV1 ← HVS channel 1（channel 1 の mux で選ぶ）、channel 2 は writeback 用。PV は 1 clock に 1 pixel、FIFO 64。

### 4.2 C0 と D0 の違い（L `vc4_hvs.c` 1650〜1660 行、`vc4_regs.h` 543〜741 行）

HVS の VERSION（+0x000）の [7:0] が 0x53 なら C0、0x54 なら D0。**ユーザーの D0 の基板は D0 の配置**。共通の register は同じ番地、channel ごとの register が違う:

| register（この文書の呼び名） | C0 | D0 |
| --- | --- | --- |
| VERSION / CXM_SIZE（dlist の memory の大きさ）/ LBM_SIZE / UBM_SIZE / COBA_SIZE / COB_SIZE | 0x000 / 0x004 / 0x008 / 0x00c / 0x010 / 0x014 | 同じ |
| CONTROL（bit 31 HVS の enable、[22:18] prefetch の行、[7:4] 最大の要求の数） | 0x020 | 0x020 |
| PRI_MAP0/1 | 0x0b8 / 0x0bc | 0x038 / 0x03c |
| channel n の CTRL0 | 0x030 + 0x20·n | 0x100 + 0x40·n |
| CTRL1（bit 8 背景の塗り、bit 0 interlace） | 0x034 + 0x20·n | 0x104 + 0x40·n |
| BGND | 0x038 + 0x20·n | 0x108 + 0x40·n（BGND0）、0x10c（BGND1） |
| LPTRS（[11:0] = 表示する dlist の先頭の word の番号） | 0x03c + 0x20·n | 0x110 + 0x40·n |
| COB（[31:16] top、[15:0] base） | 0x040 + 0x20·n | 0x114 + 0x40·n |
| STATUS（bit 22 空、[21:16] frame の数、[14:13] mode 0=無効 1=初期化 2=動作 3=EOF、[12:0] 今の行） | 0x044 + 0x20·n | 0x118 + 0x40·n |
| DL（[11:0] = hardware が今使っている dlist の先頭） | 0x048 + 0x20·n | 0x11c + 0x40·n |

CTRL0 の bit（両方同じ）: 31 enable、30 reset、[28:16] 幅 − 1、15 oneshot、[12:0] 行の数 − 1。

dlist の memory は HVS の窓の 0x4000 から（L `vc4_hvs.c` 1717〜1720 行で 5 世代以上は 0x4000）。大きさは CXM_SIZE（word）。先頭の 32 word は bootloader（firmware）の物で触らない（L `vc4_regs.h` 1056 行、`vc4_hvs.c` 1219〜1237 行）。

### 4.3 HVS の初期化（L `vc4_hvs.c` 1478〜1532・1534〜1630 行。zedBSD は firmware の表示を引き継ぐので、多くは書かない）

L が全部を初期化する時の順: CONTROL = enable | prefetch 8 行 | 要求 15 → PRI_MAP0/1 = 全て 1 → CSC の係数（C0 と D0 で register が違う）→ COB（channel の出力の buffer、1 行 3840 pixel × 4 行ずつ、channel 2・1・0 の順に下から割り当て）。

zedBSD の方針（Pi 4 の R0 と同じ考え、[GPU の driver の scanout の規則](../guardrail.md) 2026-10-04）: firmware が HDMI に出している channel だけを使い、**HVS の CONTROL・COB・CSC・PV・HDMI は firmware のまま書かない**。zedBSD が書くのは dlist の memory（bootloader の 32 word より後）と、使う channel の LPTRS と CTRL1 の背景の bit だけ。

### 4.4 dlist の 1 枚の plane（L `vc4_plane.c` 1736〜2080 行、`vc4_regs.h` 1350〜1407 行。拡大縮小なし・RGB・raster の場合）

| word | 中身 |
| --- | --- |
| CTL0 | bit 30 valid、[29:24] 次の要素までの word の数（L は plane の word の数を後で入れる）、[22:20] address の形（0 linear、1 SAND128、2 SAND256、3 MAP8、4 UIF）、[19:18] alpha mask（D0 は固定の alpha を 3 で表す）、bit 15 unity（拡大縮小なし）、[14:13] RGBA の並び、[10:8]・[7:5] scaler の mode、[4:0] pixel の形式 |
| POS0 | [28:16] 画面の y、bit 15 左右反転、[12:0] 画面の x |
| CTL2 | [31:30] alpha の mode（C0。D0 は CTL0 の alpha mask に移った）、bit 29 premultiplied、bit 28 plane の alpha を混ぜる、[15:4] plane の alpha（12 bit）、CSC（D0 は bit 19 と [22:20]） |
| POS1 | 拡大縮小がある時だけ: [28:16] 出力の行 − 1、[12:0] 出力の幅 − 1 |
| POS2 | [28:16] 元の行 − 1、[12:0] 元の幅 − 1 |
| POS3 | context（L は 0xc0c0c0c0 を置く） |
| PTR0 | bit 31 上下反転、[28:16] UPM の base（UBM の word 単位）、[14:10] UPM の handle − 1、[9:8] UPM の buffer の行（0=2、1=4、2=8、3=16）、[7:0] **address の上位 8 bit**（40 bit の address） |
| PTR1 | address の下位 32 bit |
| PTR2 | [16:0] pitch（byte） |
| （拡大縮小・YUV の時だけ LBM の address と係数の word） | |

list の終わりは END の word（bit 31）。

- **UPM（unified pixel memory）が plane ごとに要る**（4.2・HVS5 には無い）: plane ごとに handle（1〜32、L は ida で配る）と UBM の中の領域を確保し、PTR0 に base・handle・行の数を入れる（L `vc4_plane.c` 978〜1055 行）。領域の大きさ = (pitch + 62) / 32 × 32 byte × 行の数（2 行なら 2）を UBM の word に揃えた物（L 816〜831 行）。UBM の大きさは UBM_SIZE で読める。firmware が使っている handle と領域は firmware の dlist の PTR0 から分かるので、それと重ならないように取る（§8 U7）。
- pixel の形式（4:0）と RGBA の並びは HVS5 の値と同じ表（L の `hvs_format` の hvs と pixel_order_hvs5）。Pi 4 の Z `display-flip.c` が使う ARGB8888 の値をそのまま使える見込み（**未確認**、実機で色を見る）。
- address は 40 bit。zedBSD の buffer は物理で連続（Z `buffer.c`）なので、物理の address をそのまま入れる。Pi 4 の「1 GiB の下」の制約（Z `display-flip.h` 52〜55 行）は Pi 5 で無くなる。

### 4.5 scanout の切り替えと vblank

- 表示の切り替え: 新しい dlist を dlist の memory に書き終えてから、channel の LPTRS に先頭の word の番号を書く。hardware は frame の始まりで取り込む。取り込まれたかは DL の [11:0] が新しい先頭になった事で知る（L `vc4_hvs.c` 835〜855 行、`vc4_crtc.c` 835〜845 行）。Pi 4 の「3 つの slot（採用・公開済み未採用・置き換え可）」の方式（Z `display-flip.h` 18〜22 行）がそのまま使える。
- vblank: **pixelvalve の割り込み（GIC SPI 101 / 110）を使う**。PV の register（INTEN・INTSTAT など）は Pi 4 と同じ（L `vc4_crtc.c` は 2712 で PV の V_CONTROL の ODD_TIMING と PIPE_INIT_CTRL を足すだけ、どちらも mode set の時だけ）。mainline は HVS6 の end of frame の割り込みを使っていない（L `vc4_hvs.c` 1742〜1746 行で 6 世代は HVS の IRQ を登録しない）。HVS の EOF は L2 の intc の先にあり、使うには L2 の intc の driver が要るので使わない。
- 背景の塗り: 一番下の plane が alpha を持つ時は CTRL1 の bit 8 を立てる（L `vc4_hvs.c` 999〜1011 行）。

### 4.6 firmware の framebuffer からの引き継ぎ（手順の案）

1. P0 読み出し: VERSION（C0/D0）、CONTROL、CXM_SIZE・UBM_SIZE、channel 0〜2 の CTRL0・CTRL1・LPTRS・STATUS・DL、firmware の dlist（LPTRS の先頭から END まで）、IOMMU4 の CTRL（+0x00）・BYPASS_END（+0x20）・ADDR_CAP（+0x14）・DEBUG_INFO（+0x38）（R `bcm2712-iommu.c` 22〜85 行の offset）。印に出す。
2. firmware の出力の channel を、CTRL0 の enable と STATUS の mode = 2 から決める（HDMI0 なら channel 0）。zedBSD の規則で、それ以外の channel は触らない。
3. firmware の dlist の PTR0/PTR1 から framebuffer の address を読み、mailbox の framebuffer の address（WS278 の `rpi5.framebuffer.*`）と一致を確かめる。一致すれば IOMMU4 は素通し（下）と見なせる。
4. 自分の dlist（console の plane 1 枚、firmware と同じ framebuffer、自分の UPM）を bootloader の 32 word の後に作り、LPTRS を切り替え、DL で取り込みを確かめる（R0 相当）。
5. 以後は Pi 4 と同じ段（overlay・cursor・primary の zero copy、3 slot の flip）。

IOMMU4: R `bcm2712-iommu.c` 147〜235 行では、Linux の driver は IOMMU を有効にする時、40 GiB（`DEFAULT_APERTURE_BASE`、同 115 行）より下の address を素通しにする bypass を設定する。Pi 5 の RAM は 16 GiB までなので、IOMMU が無効でも、Linux の設定で有効でも、物理 address の plane は表示できる。firmware が起動時に IOMMU4 をどう設定しているかは**未確認**（P0 で読む、§8 U6）。zedBSD は IOMMU4 を書かない。

### 4.7 zero copy の scanout の条件（Pi 5）

| 条件 | 値 |
| --- | --- |
| buffer | 物理で連続（zedBSD の buffer）、40 bit の address、IOMMU は使わない |
| 形式 | RGB の linear（まず ARGB8888/XRGB8888）。UIF（address の形 4）も HVS6 は読める（L `vc4_plane.c` の tiling の分岐）。Pi 4 の WS141 p015 の UIF の設計を Pi 5 でも使える見込み |
| pitch | 17 bit の byte の pitch |
| 大きさ | 拡大縮小なし（unity）なら UPM だけ、拡大縮小ありなら LBM と係数（後の Phase） |
| 寿命 | dlist が名前を持つ間は buffer を変えない・解放しない（DL で取り込みの後に前の buffer を返す） |
| cache | GPU の書き込みは V3D の L2T の clean の後、CPU の書き込みは cache の clean の後（Pi 4 と同じ） |

### 4.8 もう 1 つの道: firmware KMS（R `vc4_firmware_kms.c`）

Raspberry Pi の版には、HVS を firmware に mailbox で操作させる driver がある（`raspberrypi,rpi-firmware-kms-2712`、F に node があり disabled）。plane は mailbox の SET_PLANE（tag 0x00048015、R `raspberrypi-firmware.h` 156 行）に 32 bit の DMA address・大きさ・pitch・形式・位置・alpha・layer を渡す（R 64〜96 行の形）。vblank は firmware が SMI の register 経由の割り込み（F の `firmwarekms` の interrupts、intc@7d503000 の 19）で知らせる。register の知識が少なくて済むが、flip ごとに mailbox の往復が要り、address は 32 bit。zedBSD の推しは直接の HVS6（Pi 4 と同じ作り、速い）。§9 の D2。

### 4.9 HDMI

firmware が HDMI0 の mode を設定して出している。zedBSD の規則（firmware の出力先以外で scanout を始めない）で、最初は HDMI・PV を触らない。mode の変更・hotplug・HDMI1 は後の Phase（L `vc4_hdmi.c` 3335〜3390 行の 2712 の variant、`vc4_hdmi_phy.c` の 2712 の PHY）。

---

## 5. Pi 4 の driver との共有と分離

方針: Pi 4 の kernel の object を変えない（WS278 design §2 の「rpi4 の object は前処理の後に変わらない」と同じ）。board は compile 時に選ぶ（`ZEDBSD_BOARD := rpi5`）ので、版で違う部分は**別の file** にして board の source の一覧で選ぶ。共有の file の中の `#if` は最小にする。

| 層 | Z の file（行） | Pi 5 で | 理由 |
| --- | --- | --- | --- |
| Vulkan の実行器（transport・object・record・descriptor・pipeline の記録・queue・sync・memory・resource） | `vulkan-*.c`（約 25,000 行、`vulkan-native-*` と `vulkan-video.c` を除く） | **共有** | 版に依らない |
| 実行の worker・buffer・share・VA の管理・job の退役 | `render-worker.c`・`render-device.c`・`render-runtime.c`・`buffer.c`・`share.c`・`v3d-memory.c`・`v3d-job.c`・`mmu.c` | **共有**（job の起動の register の offset だけ版の表から） | MMU の形と job の流れは同じ（§1.6・§2） |
| 電源・clock・識別・cache・IRQ | `v3d-power.c`・`v3d-hardware.c`・`v3d.c`・`firmware.c`・`v3d-diagnostic.c` | **分ける**（7.1 用: PM 0x304・SMS・IRQ 2 本・bit の違い・TFU/CSD の offset） | §1 |
| CL の packet の encoder | `native-pass.c`・`native-bin.c`・`native-shader.c`・`native-texture.c`・`native-viewport.c`・`native-colour.c`・`native-storage.c`・`cl.c`・`list.c`、`vulkan-native-*.c` の packet の部分 | **分ける**（7.1 の encoder）。packet を使う側（pass の組み立て、upload）は共有 | §2.2 の表 |
| TFU | `v3d-tfu-probe.c`、TFU の job の組み立て | 7.1 の register の表で分ける | §2.4 |
| compiler（frontend・IR） | `src/drivers/gpu/compiler/` | **共有** | |
| compiler（lowering） | `shader.c`・`shader-analyze.c`・`shader-lower.c`・`shader-output.c` | 共有を基本に、版の差（§3.7）を版の file に | |
| compiler（scheduler・register の割り当て・encoder） | `shader-schedule.c`・`qpu.c`・`qpu.h` | **新しく 7.1 用** | §3.1〜3.5 |
| 固定の shader の機械語 | `meta/` | 7.1 用を作る | |
| display の lease・scanout の hold・GPU の interface | `display-device.c`・`attach.c`・`stage.c`・`fdt-util.c` | **共有**（board の node の名前だけ） | |
| display の hardware（HVS・PV・HDMI の program、flip、IRQ） | `display-flip.c`・`display-program.c`・`display-start.c`・`display-execute.c`・`display-irq.c`・`display.c`・`readout.c` | **分ける**（HVS6 の register・dlist・UPM・40 bit・C0/D0） | §4 |
| video の decode | `vulkan-video.c`（vchiq・MMAL） | **Pi 5 では入れない**（H.264 の hardware が無い） | |

配置の案（§9 D1）: `src/drivers/gpu/bcm2711/` に版の file（例 `v3d71-*.c`・`hvs6-*.c`・`qpu71.c`）を足し、`CONFIG_DRIVER_BCM2712_GPU` と rpi5 の source の一覧で選ぶ（案 A）。directory の名前は bcm2711 のまま（改名は大きな churn）。案 B は共有の部分を `src/drivers/gpu/vc6/` に移して bcm2711・bcm2712 から使う（Pi 4 の object が変わるので、Pi 4 の回帰が要る）。推しは A。

---

## 6. Phase の分け方

| Phase | 内容 | 依存 | 見込み | 確認 |
| --- | --- | --- | --- | --- |
| p002 | V3D 7.1 の骨格: `CONFIG_DRIVER_BCM2712_GPU`、rpi5 の attach、dtb の node、窓の map、PM 0x304・clock・SMS・識別・MMU・IRQ 2 本（§1、段 V0〜V4 の印）。定数の表（GPL の値の出典を block ごとに）。register の出典の TSV | p001、WS278 p002（実機で起動） | 4〜6 LW | build（warning 0）、rpi4 の kernel.elf が不変、host の試験（Pi 5 の 2 つの dtb で node を読む）。実機: 印の写真（V0〜V4） |
| p003 | job の submit: 7.1 の packet の encoder の最小（Binning・Rendering Mode Common・RT Part1・ZS clear・supertile・store）、noop job（V5）、clear の job、TFU（§2.4）、CSD の空の dispatch（§2.5）、cache の clean、hang の判定と reset（SMS → PM）| p002 | 6〜8 LW | host: packet の byte を Mesa の XML から作った期待値と照合（WS141 の「固定 XML 照合」と同じ形）。実機: V5・V6 の印、clear した buffer を framebuffer に写す |
| p004 | QPU 7.1 の encoder と scheduler・register の割り当て（§3）。固定の shader の機械語を 7.1 で | p001（host だけで進められる。p002・p003 と並べられる） | 10〜14 LW | host: encoder の word を Mesa の表（`qpu_pack.c` の v3d71 の表）から作った期待値と照合、scheduler の決まり（§3.5）を検査する試験、共通の compiler の試験の 7.1 の版 |
| p005 | 描画の経路: 7.1 の shader state record（rev ≥ 10 は draw index の形）・texture state・TMU cfg・clipper 1/64・cfg bits・blend、tile の大きさの選び方（§2.2）、Vulkan の実行器の native の部分を 7.1 に。最初の三角形と texture の quad | p003・p004 | 8〜10 LW | 実機: 三角形・texture の quad を framebuffer に写した写真 |
| p006 | HVS6 の display: P0 の読み出し、引き継ぎ（§4.6）、UPM、primary・overlay・cursor の plane、PV の vblank、3 slot の flip、zero copy | p002（V3D と独立に並べられる） | 8〜10 LW | 実機: P0 の値の写真、自分の dlist で console が出る事、cursor の動き |
| p007 | `drv_gpu_interface` と Keiland の graphical login（compositor は libvulkan だけ） | p005・p006、WS278 p004（USB keyboard） | 4〜6 LW | 実機: Keiland の login の画面、keyboard で login |
| p008 | 速さ（clock の最大、tile の大きさ、TFU の窓）、Pi 4 の WS141 p013 の項目の 7.1 の版 | p007 | 4〜6 LW | 実機の frame の時間 |
| p009 | 規約の全文の見直しと license・類似の監査（GPL の値の出典の block、Mesa の file ごとの license） | p002〜p008 | 3〜4 LW | build・host 試験、監査の結果 |
| （後で） | HDMI の mode の変更・hotplug・HDMI1、HVS の拡大縮小の plane、C1 の基板の確認 | p007 | 未見積 | |

合計の見込み 47〜64 LW（実時間で 12〜20 時間程度、1 LW ≈ 15〜25 分の較正で）。最も重いのは p004（compiler）。p004 と p006 は p002・p003 と並べられる。

---

## 7. 試験の方法

QEMU に Pi 5 の machine は無い（WS278 design §5）。V3D・HVS の emulation も無い。

- **host（担当が流す、短い物）**: build（warning 0）、rpi4 の kernel.elf が変わらない事、Pi 5 の C1・D0 の dtb を driver の FDT の parser で読む試験（V3D・PM・HVS・PV・IOMMU4 の node と値）、packet の encoder の照合（Mesa の `v3d_packet.xml` の 71 の定義から作った期待の byte、XML の file は MIT で test の入力に置く。license の扱いは WS141 と同じ）、QPU の encoder の照合（Mesa の 7.1 の表の値から作った期待の word）、scheduler の決まりの検査。
- **実機（Q1 経由、T1/T2 の担当かユーザー）**: ユーザーの Pi 5 2GB（D0）、HDMI0 の monitor、USB keyboard（WS278 p004 の後）、Raspberry Pi Debug Probe（serial の build の時だけ）。段の印（§1.9 の V0〜V6、§4.6 の P0〜）を framebuffer に出し、写真で確かめる。写真は `build/review/` に置いて相対の path で見せる。
- 実機の手順は p002 で `plan/ws279/tests/rpi5-gpu-hw-test.md` に書く（WS278 の `rpi5-hw-test.md` の形）。
- 回帰の試験は細かい修正ごとに回さない（2026-10-03 ユーザー）。実機の試験は意味のまとまり（p003・p005・p006・p007 の終わり）でまとめる。

---

## 8. 未確認の事と危険

調べた事（§1〜§4）と分けて、source から決められなかった事を並べる。全て実機で、段の印で確かめる。

| 記号 | 未確認の事 | 確かめ方・対策 |
| --- | --- | --- |
| U1 | 2712 の PM + 0x10c（GRAFX）の意味と、firmware が V3D の親の電源を入れているか | V0 で PM + 0x10c・0x304 を読んで印に出す。zedBSD は 0x10c に書かない |
| U2 | ASB の無い 2712 で、hang の後の PM の reset が AXI の転送の残りで詰まらないか | reset の前に SMS の reset（L の順）。詰まったら GMP の STOP_REQ（core + 0x604）で AXI を止める手順を足す（L `v3d_gem.c` 47〜57 行、L は reset で `if (false)` にして使っていない） |
| U3 | 7.1 の L2TCACTL・SLCACTL の bit が 4.2 と同じか（L と M が版で分けないことからの推論） | V5・V6 の後の clean の待ちが終わる事、CPU が GPU の結果を読める事 |
| U4 | C1 の基板の V3D の rev（D0 は 7.1.10、M の comment） | V2 の印。C1 の実機は無いかもしれない（WS278 の R5） |
| U5 | 7.1 の TLB の書き込み・VPM の命令の形が 4.2 と同じか（M の compiler に版の分岐が無いことからの推論） | p005 の最初の三角形 |
| U6 | firmware が IOMMU4 をどう設定しているか（無効か、bypass か） | P0 で CTRL・BYPASS_END・ADDR_CAP を読む。firmware の dlist の address と mailbox の framebuffer の address の一致 |
| U7 | firmware が使っている UPM の handle と UBM の領域 | P0 で firmware の dlist の PTR0 を読む。重ならない所を使う |
| U8 | V3D の clock の最大の rate、QPU の数（NSLC × QUPS）、VPM の大きさ | V0・V2 の印 |
| U9 | pixel の形式の番号と RGBA の並びが HVS5 と同じ値で正しい色になるか | p006 の P0 の後、自分の dlist の console の色 |
| U10 | Keiland の合成で 64×64 の tile が選ばれるか（§2.2 の計算） | p005 |
| R1 | QPU の compiler の作り直しが大きく、p004 が延びる | p004 を p002・p003 と並べて先に始める。host の照合を厚くし、実機の試行を減らす |
| R2 | D0 だけで作ると C1 で動かない（HVS の register の配置、V3D の rev） | 版・rev を読んで分ける作りにする。C1 は後で |

---

## 9. ユーザーの判断が要る点

- **D1（配置）**: 7.1・HVS6 の file を今の `src/drivers/gpu/bcm2711/` に足し、rpi5 の build だけが選ぶ（案 A、Pi 4 の object は不変）か、共有の部分を新しい directory に移す（案 B、Pi 4 の回帰が要る）。推し: A。
- **D2（display の道）**: HVS6 を直接書く（§4.3〜4.7、Pi 4 と同じ作り）か、firmware KMS の mailbox（§4.8）か。推し: 直接（flip ごとの mailbox の往復が無い、40 bit の address、Pi 4 の flip の作りをそのまま使える）。
- **D3（対象の基板）**: まず D0（ユーザーの 2GB 版）だけで受け入れ、C1 は実機が手に入った時に確かめる、でよいか。推し: はい（作りは版・rev を読んで分ける）。
- **D4（video）**: Pi 5 には H.264 の hardware の decoder が無いので、Pi 4 の video（WS236）の hardware の経路は Pi 5 では入れない。Pi 5 の video の再生は CPU の decode か HEVC の decoder（別の WS）になる。WS279 の範囲の外としてよいか。推し: 範囲の外。
