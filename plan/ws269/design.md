<!-- awesome-plan project=zedbsd record=ws269-design -->

# WS269 設計: VisionFive 2（JH7110）の PowerVR BXE-4-32 の driver

- 版: 3.1（2026-10-19、P2、ws269-p001、q1324-i01）。版 3 を [design-review-3.md](design-review-3.md) に合わせて局所的に直した（対応の表は末尾の §12）。版 3（q1321-i01）は版 2（q1319-i01）を [design-review-2.md](design-review-2.md) に合わせて直した。対応の表は末尾の §11（レビュー 1 への対応は §10）。版 2 は版 1（q1316-i01）を [design-review-1.md](design-review-1.md) に合わせて直した物。コードは書いていない。
- 範囲: kernel の driver の構成、CPU と GPU の memory の共有（coherency）、表示の道、firmware、userland の Vulkan の道、実装の Phase の分割、ユーザーの判断（D1〜D3・D5〜D7、D4 は欠番）。
- 印: **事実**（出典あり）、**推測**（出典なし、確かめる Phase を書く）、**未確認**。
- 出典の規則は [Guardrail](../guardrail.md) の「MIT・GPL の出典のデータの引用」に従う（§0.2）。

## 0. 出典

### 0.1 資料の表

| # | 資料 | 場所 | 何に使ったか |
| --- | --- | --- | --- |
| S1 | JH7110 TRM Preliminary V2（WS256 で SHA256 固定済み、`design-peripherals.md` §0） | https://doc-en.rvspace.org/JH7110/PDF/JH7110_TRM_StarFive_Preliminary_V2.pdf | GPU の address（0x1800_0000、1 MiB）、割り込み（`gpu_os_irq` global_interrupts[77]）、SYS CRG の GPU の clock と reset、電源（dom_gpu_a・dom_gpu_b、B は A の中の MIPS core が制御、「GPU does not support DVFS」）、PMU（0x1703_0000） |
| S2 | JH7110 Datasheet（同） | WS256 の資料 | 「IMG BXE-4-32 MC1、最大 600 MHz」、SLC、TFBC |
| S3 | Linux v6.12 の binding header（GPL-2.0 OR MIT、値だけ） | `starfive,jh7110-crg.h`・`starfive,jh7110-pmu.h` | SYSCLK_GPU_ROOT 3、GPU_CORE 45、GPU_CORE_CLK 46、GPU_SYS_CLK 47、GPU_APB 48、GPU_RTC_TOGGLE 49、NOC_BUS_GPU_AXI 50。SYSRST_GPU_APB 21、GPU_DOMA 22。PD_GPUA 2 |
| S4 | Linux v6.12 mainline の `jh7110.dtsi` | WS256 の固定の写し | **gpu の node は無い**（pwrc の node はある） |
| S5 | Mesa の PowerVR の文書 | https://docs.mesa3d.org/drivers/powervr.html | BXE-4-32（36.50.54.182）は「unsupported」。BXM-4-64 は active な対象 |
| S6 | Imagination の linux-firmware の fork、branch `powervr`（レビュー 1 が GitLab API で確認） | https://gitlab.freedesktop.org/imagination/linux-firmware（branch `powervr`、head 8a58f818、2026-04-15、1.1.OS@6976702） | `powervr/rogue_36.50.54.182_v1.fw`（sha256 59127d3a02e48876310a520b7dc355f7430d3748d2940caaa33f39237d517abb、レビュー 1 が取得）。**kernel.org の linux-firmware には 36.50.54.182 が無い**（33.15.11.3・36.52.104.182・36.53.104.796 だけ）。branch は未 merge・保護なし |
| S7 | `LICENSE.powervr`（同じ fork、レビュー 1 が文面を確認） | 同上 | 改変しない binary の再配布は可。**reverse engineering・decompile・disassemble は禁止**。Imagination が license した製品でだけ使う。特許の許諾は OSI の承認した OS と組み合わせる時だけ |
| S8 | Linux mainline `drivers/gpu/drm/imagination/`（レビュー 1 が `gh api` で読んだ最新）と、vendor の clone の同じ directory（v6.12 相当） | 最新 mainline、`~/visionfive-linux` 4cecf169f38e | SPDX は `GPL-2.0-only OR MIT`（`pvr_fw_mips.c` の 1 行目を clone で確認）。`pvr_fw_mips.c`・`pvr_vm_mips.c`・`pvr_rogue_mips.h` がある（**MIPS の firmware processor を扱う**、clone で確認）。v6.12 の match は `img,img-axe` だけ（clone の `pvr_drv.c:1477`）。最新 mainline では 36.52.104.182（BXM-4-64）が SUPPORTED（レビュー 1、`pvr_device.c:534`）。FW の major 版 1（clone の `pvr_device.c:34`） |
| S9 | StarFive の vendor kernel の clone（2026-10-19 ユーザーの clone、ws.md の資料） | `~/visionfive-linux`、branch `JH7110_VisionFive2_6.12.y_devel` 4cecf169f38e（2025-09-29） | 下の S9a〜S9d |
| S9a | vendor の JH7110 の glue（Dual MIT/GPLv2） | `drivers/gpu/drm/img/img-rogue/services/system/rogue/sf_7110/sysconfig.c`・`sysconfig.h` | cache の扱い: snoop の mode を **EMULATED**（174-183 行。CPU_ONLY は `SUPPORT_AXI_ACE_TEST` の時だけ）、host の cache の保守は全部 ccache の FLUSH64 の range（186-222 行）、DMA の mask は **32 bit**（425 行）。clock は `clk_bv` を 594 MHz、BIN2 の板（`/opp-table-0/opp-1250000000` がある板）は 396 MHz に set_rate し（`sysconfig.h:88-89`、`sysconfig.c:302-303`（opp の判定）・`319`（set_rate））、その値を FW の core clock に渡す（503 行）。順は apb・rtc の clock → rate → core・sys の clock → reset apb → doma の解除（316-325 行）、axi の clock は runtime resume で apb より先に（227 行の axi → 228 行の `u0_img_gpu_enable`）。invalidate は `do_invalid_range`（106-124 行）: 2 MiB の zero page を書いて FLUSH64 し、**L2 を丸ごと追い出す**（`dma_support.c:143`・`rgxlayer_impl.c:1021` が kernel の写像を作る時に呼ぶ）。PMU の hw event の mask を resume で 0、suspend で全部（226・236 行）。IRQ 82 =「77 + 5」（`sysconfig.h:51`） |
| S9b | vendor の DT | `arch/riscv/boot/dts/starfive/jh7110.dtsi:1403-1422` | compatible `img-gpu`、reg `0x18000000/0x100000` と `0x130C000/0x10000`、clocks GPU_CORE・GPU_APB・GPU_RTC_TOGGLE・GPU_CORE_CLK・GPU_SYS_CLK・NOC_BUS_GPU_AXI（names clk_bv・clk_apb・clk_rtc・clk_core・clk_sys・clk_axi）、resets GPU_APB・GPU_DOMA だけ（**27 の NOC_BUS_GPU_AXI の reset には触らない**）、power-domains PD_GPUA、interrupts 82、`current-clock = <8000000>`、status disabled |
| S9c | vendor の core の定義 | `hwdefs/rogue/km/cores/rgxcore_km_36.50.54.182.h:60-61`、`hwdefs/rogue/km/configs/rgxconfig_km_36.V.54.182.h` | BRN 63553・71317。feature: AXI_ACELITE（57 行）、FBCDC 50・architecture 7（62-64 行）、MIPS（72 行）、NUM_CLUSTERS 1、PHYS_BUS_WIDTH 36、SLC 16 KiB、VIRTUAL_ADDRESS_SPACE_BITS 40 |
| S9d | vendor の PMU と ccache | `drivers/pmdomain/starfive/jh71xx-pmu.c:88`、`drivers/cache/sifive_ccache.c:234` | hw event の mask、FLUSH64 の range（手順の参照先） |
| S10 | firmware の header（レビュー 1 が header の 4 KiB と device info だけを読んだ。逆アセンブルなし） | S6 の file | firmware processor MIPS、BRN 63553・71242、feature は BXM-4-64 と同じで大きさだけ違う（COMMON_STORE 16384/19456、ISP の tile 4/6、MAX_PARTITIONS 10/12、IPP pipe 4/6）、BRN44079 が無い。S9c の 71317 が無いのは FW の header が表せない host 側の BRN だから（§0.4 で説明済み、p002 は記録するだけ） |
| S11 | Mesa `src/imagination/`（レビュー 1） | Mesa main | `common/device_info/bxe-4-32.h` がある。compiler は `pco`（MIT）。encoding は `pco/pco_isa.py`、control stream は `csbgen` の XML（license の行は未確認） |
| S12 | 既存の zedBSD | `include/drivers/gpu/gpu.h`、`src/drivers/gpu/`（bcm2711・amdrdna・compiler・venus）、`userland/desktop/libvulkan/`、`userland/desktop/libkeiland-backend-zedbsd/scanout-zedbsd.c`、`src/hal/riscv64/space.c:1778-1780`・`page.c`、`src/drivers/generic/dma.c`、`src/hal/riscv64/bsp-visionfive2/board.c`（ccache の FLUSH64） | 合わせる形と、今ある制限 |
| S9e | vendor の SLC の保守 | `rogue/rgxinit.c:3651`、`rogue/rgxcompute.c:1102` vendor が SLC の flush・invalidate を FW に頼む時期（UFO の解放、compute の flush の要求の時だけ）。**手順の参照だけ**。命令の値は mainline の FWIF から取る（S8b） |
| S8b | mainline の SLC・cache の属性 | `pvr_rogue_fwif.h:1160`（`ROGUE_FWIF_KCCB_CMD_SLCFLUSHINVAL`、送り手は imagination/*.c に 0 件）、`pvr_rogue_mmu_defs.h:100`・`pvr_mmu.c:763-772・2594`（PTE の CC・SLC_BYPASS、CC は常に 0）、`pvr_fw_startstop.c:22-33`（AXI の ACE-Lite の domain の設定）、`pvr_vm_mips.c:175-176`・`pvr_fw.c:295・463-659`（host と共有する FW の object は MIPS で `DEVICE_UNCACHED`） | §3.2 の属性の既定（Value(s)・Procedure referenced） |
| S9f | DDK 1.19 の host 側の BRN | `rogue/rgxinit.c:4413-4421・4912-4920` | BRN71317: GPU の MMU の 1 MiB・2 MiB の page は fault する（最大 256 KiB） |
| S8a | mainline の FW の device info の enum | `drivers/gpu/drm/imagination/pvr_rogue_fwif_dev_info.h:8-19`（最新 mainline も同じ、レビュー 2） | FW の header が表せる BRN は 12 個だけ。**71317 は無い** |
| S13 | WS256 | `plan/ws256/design-peripherals.md` §2.4（bus master ごとの coherency の確かめ）・§3.2、ws.md の p011・p013・p015・p020・p022・p023 | 電源・clock・reset・DMA の基盤と実機の確かめ |

### 0.2 参照の規則（Guardrail の適用）

- register の offset・bit、FWIF の struct の layout（offset・大きさ・enum の値）、BRN の番号、clock の rate、IRQ の番号は**値**として使い、使う block の直前の複数行の comment に `Value(s) referenced: <版> <path> (<表・define の名前>)` を書く。FWIF の layout は firmware との ABI の値として扱う。
- 手順（電源・clock・reset の順、MIPS の起動、cache の保守の仕方）は `Procedure referenced: <版> <path> (<関数の名前>)`。
- code・comment・構造・symbol の名前は写さない（MIT・Dual の file でも）。zedBSD の名前は独自に付ける。file の上部に出典・notice を書かない。
- firmware は逆アセンブルしない（S7）。FW の挙動は FW の trace の buffer、FWIF の応答、register の値だけで調べる。
- phase.md の受け入れ 4 は Q1 が add4ed7d9 で Guardrail（値の引用も可）に揃えた。**解決済み**。

### 0.3 まだ確かめていない事（p002 で固定する）

- 最新 mainline の imagination の tag（FWIF の値を取る版）と、S6 の FW の版の対。
- Mesa の `bxe-4-32.h`・`pco_isa.py`・csbgen の XML の版と license の行。
- S10 と S9c の BRN の違い: 説明は付いた（§0.4）。p002 は版の対を記録するだけ。
- 実機の DTB に GPU の node（vendor の `img-gpu` か mainline の `img,img-rogue` 系）が入っているか。
- vendor の DT の 2 つ目の reg `0x130C000`（GPU timer 0x130C_0000 の書き間違いに見える、推測）と FW の SOC_TIMER の関係。

### 0.4 BRN の組（レビュー 2 Minor 1）

- **事実**: FW の file の device info が表せる BRN は mainline の enum の 12 個だけ（S8a）。71317 はこの enum に無いので、FW の header には原理的に出ない。
- **事実**: 71317 は DDK 1.19 の host 側の BRN で、GPU の MMU の 1 MiB・2 MiB の page が fault する（S9f）。DDK 1.19 の 36.x の core は全部これを持つ。
- **推測**: 71242 は DDK 1.19 の 36.x の定義に無く、後の errata のデータで足された。mainline での効果は `!gpu_multicore_support` の時に SLC_CTRL_MISC の LAZYWB を足す事だけで（`pvr_fw_startstop.c:80`）、BXE の FW の header は GPU_MULTICORE_SUPPORT を持つので適用されない（レビュー 2 の header の再解析）。
- よって組は「FW の header の 63553・71242」と「host 側の 71317」の和。GPU の MMU は 4 KiB の page だけを使う（§3.5）。
- FW の header には DDK 1.19 の config に無い XE_TPU2 もある。feature は DDK の hwdefs から取らず、reset の手順（XE_TPU2 の時の SOFT_RESET2、`pvr_fw_startstop.c:99-135`）は mainline を参照する（§3.4）。

## 1. 対応の状況のまとめ

| 層 | BXE-4-32（36.50.54.182）の状況 | 根拠 |
| --- | --- | --- |
| silicon の資料 | GPU の register の公開の仕様は無い。TRM は SoC 側だけ | S1 |
| 最も近い参照 | **BXM-4-64（36.52.104.182）**: 同じ MIPS の FW processor、同じ feature で大きさだけ違う。最新 mainline で SUPPORTED、Mesa で conformant | S8、S10、S5 |
| Linux mainline | MIPS の FW processor を扱う code がある。JH7110・BXE の binding・platform の data は無い | S8 |
| vendor の kernel driver | DDK 1.19 の kernel 部分（Dual MIT/GPLv2）が**この core を JH7110 で動かしている**。closed なのは userland だけ | S9 |
| firmware | mainline 形式の `rogue_36.50.54.182_v1.fw` は Imagination の fork の未 merge の branch だけ。vendor の `rgx.fw.*` は DDK の形式で使わない | S6 |
| Mesa | device info はあるが unsupported | S5、S11 |

結論: 版 1 が書いたほど研究的ではない。**kernel 側は「最新 mainline の BXM-4-64 の道」を手順の軸にし、JH7110 固有の事（電源・clock・cache）は vendor の glue を手順の参照にする**。危険が大きいのは cache の扱い（§3.2）と、userland の USC の backend の規模（§5）。

## 2. 既存の zedBSD の GPU の形（合わせる対象）

- userland の Vulkan は `userland/desktop/libvulkan/` 1 つ（Venus の wire を `/dev/gpu*` へ）。kernel の backend が Venus を decode して native の job にする（bcm2711・amdrdna）。shader は共有の `src/drivers/gpu/compiler/` の IR から各 ISA へ。
- **表示は `drv_gpu_display_ops`**: Keiland の scanout は `/dev/gpu*` を開き `GPU_DISPLAY_CLAIM`・`PRESENT`・`QUERY` を使う（`scanout-zedbsd.c:45・161・236・322`）。node の role は `GPU_CAP_DISPLAY` と scanout の query から決まり（`gpu.c:4484-4500`）、libvulkan も role で node を選ぶ（`wsi-display-nodes.c:88・592`）。Pi 4 は display の node と render の node を分け、`share`・`scanout` の ops で buffer を渡す（`bcm2711/display-device.c:1557-1559`、`render-device.c:875-877`）。
- compositor は Vulkan だけで描く（`wayland/compose.c:1341・4227`）。CPU の Vulkan は無い。**よって Keiland は GPU の Vulkan 無しでは描けない**（版 1 の D5 (a) の前提は成り立たない）。
- 新しい UAPI は作らない。

## 3. kernel の driver の構成

置き場所: `src/drivers/gpu/powervr/`（GPU の core）、boot framebuffer の display の node（`src/drivers/gpu/` の下、名前は実装で決める）、SoC の glue は `src/drivers/platform/visionfive2/`。

### 3.1 発見・電源・clock・reset

- **発見（D2）**: mainline の DT に gpu の node が無い（S4）。推奨は `visionfive2.c` の組み込みの記述: reg 0x1800_0000/1 MiB、IRQ 82（S9a、S9b、WS256 の n + 5 の規則と一致）、PD_GPUA、clock・reset。DT に node があればそちらを優先する。優先する compatible は mainline 系の `img,img-rogue`・`img,img-bxe-4-32` 系と、vendor の `img-gpu`（S9b。vendor の DT では status disabled なので、status が okay の時だけ）。**DT の道でも coherency は `drv_fdt_dma_coherent(…, 0)` で既定を非 coherent にする**（VF2 の既定 `VF2_DMA_DEFAULT_COHERENT` は 1 なので、それを渡すと `dma-noncoherent` の無い node が coherent に倒れる、`visionfive2.c:86`）。実機の DTB に node があるかは未確認（§0.3）。
- **CRG の API**: DT の node が無くても `struct drv_vf2_clock`・`drv_vf2_reset` を `{crg, index}` で直接作れる（`vf2-crg.h`）。clock の表に GPU の行（S3 の 3・45〜50）と**親の行**（gpu_root の mux の親の PLL、gpu_sys_clk の親 ISP_AXI とその親の ISP_2X の mux（ISP_AXI は ISP_2X の divider、`clk-starfive-jh7110-sys.c:72`）、gpu_rtc_toggle の親 OSC と分周、noc_bus_gpu_axi の親 gpu_core、apb の親 APB_BUS）を足す。足さないと rate を計算できない（レビュー M9、mainline の `clk-starfive-jh7110-sys.c` の定義）。
- **順（必須）**: PD_GPUA on → clock（axi → apb・rtc → gpu_core の rate → core・sys）→ reset の解除（apb → doma）。axi を先にするのは vendor の resume の順（S9a、227 → 228 行）に合わせるためで、apb の register の access が AXI の bus を通る前提と推測する（参照先と順を揃え、ずれの理由を作らない）。reset の解除は status を待つので、PD と clock が先でないと待ちが終わらない。reset 27（NOC_BUS_GPU_AXI）には触らない（S9b）。Procedure referenced: S9a の enable の関数。
- **rate**: gpu_root の mux を PLL2 に固定し、gpu_core を 396 MHz（= 1188 / 3）に設定する。vendor の branch の `jh7110.dtsi:235` は `opp-1250000000` を常に持つので、vendor の kernel は実際にはどの板でも 396 MHz で動く（S9a、`sysconfig.c:302-303`）。**594 MHz には vendor の前例が無いので WS269 の範囲から外し、Future Work の候補にする**（Q1 に登録を依頼、§9）。FW に渡す core clock は `drv_vf2_clock_get_rate` で**実際に読んだ値**。rtc の分周は vendor の DT の `current-clock = <8000000>`（8 MHz）を目安に OSC（24 MHz）÷ 3 とし、p004 で TRM と照合する（推測）。
- **PMU**（新規 `vf2-pmu.c`）: PD_GPUA だけを software の turn-on で入れる。他の PD に触らない。hw event の mask は GPU の event（hard event 7、S1）だけを扱い、vendor の「全部 0 / 全部 1」は写さない。
- **power-domain b**: mainline の binding は B-series に PD「a」「b」を要求するが、JH7110 では b を FW の MIPS が管理する（S1）。host は b を持たない前提とし、mainline の手順で b を触る所は飛ばす（未確認、p005 で FW の応答で確かめる）。
- **attach の失敗の道（レビュー 2 Minor 10）**: 早い段で BVNC が違う・reset の待ちが時間切れの時、遅い段で FW が無い（ENOENT）・root の待ちや FW の起動が時間切れ（ETIMEDOUT）の時は、reset を入れ直し、clock を逆順に落とし、PD_GPUA を off にしてから 1 行を出す。display の node（§3.5）は GPU の core と独立なので残す。
- **Active Power Management**: 最初は FW の APM・idle の要求を無効にする（FW の init の設定で）。有効にするのは p006 の後の任意の作業。

### 3.2 CPU と GPU の memory の共有（レビュー B1）

**事実**: JH7110 で BXE を動かす唯一の driver は GPU を非 coherent（snoop の emulation、全部の保守を ccache の FLUSH64）・DMA 32 bit として扱う（S9a）。core は AXI_ACELITE の feature を持つが（S9c）、vendor はそれを試験の build でしか使わない。zedBSD は VF2 の既定を coherent にしており（`VF2_DMA_DEFAULT_COHERENT 1`）、riscv64 の uncached の写像は kernel（`page.c`）も user（`space.c:1778-1780`）も UNSUPPORTED、非 coherent の DMA の確保は uncached の写像が無いと ENOTSUP（`dma.c`）。

設計:

1. **GPU は非 coherent を既定**。組み込みの記述は DT を持たないので `drv_fdt_dma_coherent` の既定に頼らず、記述に「非 coherent」を明示する。coherent と確かめられた時だけ切り替える。
2. **DMA の address は 32 bit**（DDR の下の範囲）。GPU の物理 bus は 36 bit（S9c）だが、広げるのは確かめた後。
3. **種類ごとの共有の仕方**:

| memory | 使い方 | 共有の仕方 |
| --- | --- | --- |
| FW の code・data（MIPS の page table で写す） | CPU が一度書き、FW が読む | **uncached の別名**で CPU が書く |
| KCCB・FWCCB・FW の init・trace の buffer など、CPU と FW が両方書く構造 | 頻繁な往復 | **uncached の別名**（予備 (i)、0x4_0000_0000 + pa の窓、p004 で作る）で CPU が読み書き |
| GPU の page table（context ごと）、MIPS の page table | CPU が書き、GPU が読む | **uncached の別名**で CPU が書き、その後に GPU の MMU の cache の invalidate を FW に要求 |
| render の target・transfer の結果（GPU が書き CPU が読む） | present の copy（§3.5） | **uncached の別名**（`drv_dma_alloc_coherent` の写像）で CPU が読む。direct map では触らない |
| user の blob（Vulkan の memory） | アプリが読み書き | D7 |

- **GPU に渡す address は常に memory port の PA**（DDR の下の 32 bit）。別名（0x4_0000_0000 + pa）は CPU だけが使う。
- **fallback は p004 の 0a が `ALIAS MISMATCH` の時だけ**: 別名が同じ DRAM を指さないなら、予備 (i) を前提にした §3.2 と D7 (a) は成り立たない。その時は FLUSH64 だけで保つ道を WS256 design-peripherals.md:153 と同じく改めて設計し、p005 に入らず Q1 に返す。
- **CPU の側の invalidate（GPU → CPU の向き）は uncached の別名を原則にする**。vendor は range の FLUSH64 のほかに、kernel の写像を作る時に 2 MiB の zero page で L2 を丸ごと追い出している（S9a の `do_invalid_range`）。range の FLUSH64 だけで L1 や投機の読みが残らないかは未確認（WS256 も inclusive と推測、design-peripherals.md:483）。版 3.1 で D7 (a) から HOST_CACHED を外したので、cacheable の invalidate に頼る所は無い（Future Work の候補、§8 D7）。
- **GPU 側の属性（試験と製品で同じ値、レビュー 3 M2）**: snoop されるかは bus の固定の性質ではなく transaction の属性で決まる。次を mainline と同じ既定に固定し、p006 の試験も製品もこの組で動かす（Value(s)・Procedure referenced: S8b の `pvr_mmu.c`・`pvr_vm_mips.c`・`pvr_fw.c`・`pvr_fw_startstop.c`）。
  - GPU の PTE の CC = 0（mainline と同じ）。
  - SLC_BYPASS: CPU が読む物（FWCCB、render の target）は SLC_BYPASS = 1 を候補にし、p006 で決める（下の SLC）。それ以外は 0。
  - AXI の ACE-Lite の domain の設定: mainline の `pvr_fw_startstop.c` の値のまま。
  - MIPS の写像: host と共有する FW の object は MIPS で uncached（mainline の `DEVICE_UNCACHED` と同じ）。
  - **「coherent」の結論は、試した属性の組にだけ効く**。属性を変える時は確かめを流し直す。
- **GPU の SLC（16 KiB、S9c）**: 「試験で調べる」はやめる（レビュー 3 M2）。mainline の手順（完了の通知の後に host が SLC の flush を送らない。`ROGUE_FWIF_KCCB_CMD_SLCFLUSHINVAL` は enum だけで送り手が無い、S8b）を Procedure referenced として採る。CPU が読む物は上の SLC_BYPASS の候補で守り、p006 の試験は「SLC の flush を入れない形で 8 回とも一致する事」を確かめるだけにする。一致しなければ SLC_BYPASS を CPU が読む物に付けて流し直し、それでも駄目なら Q1 に返す。

4. **確かめ（WS256 design-peripherals.md §2.4 の 0a・0b・1（CPU→device の dirty の line）・6（後始末）と判定の規則を参照する。WS256 との違い: WS256 は INCONCLUSIVE でも主の試験だけで判定する（design-peripherals.md:153）が、WS269 は INCONCLUSIVE の時は切り替えない）**: p005 の FW の起動は、最初から上の表の安全な道（uncached の別名）で行う。試験は **requester の道ごとに名前を付ける**（レビュー 3 M2）: p006 は **MIPS（UFO）の道**だけを確かめ、その結果で切り替えてよいのは MIPS の道（FW の構造）だけ。GPU の data master（TA・3D・compute・TQ、MMU の table walk）の道は、compute の kick ができる p008 で同じ手順を流して確かめる（transfer は p010 なので p006 では使わない、§9）。
   - **0a（p004 で済ませる）**: 別名が同じ DRAM である事。`ALIAS OK` でなければ上の fallback。
   - **0b（陽性の対照。invalidate が効く事の確かめではない）**: dst を cacheable で読んで L2 に clean の line を載せ、別名から違う模様を書き、cacheable で読み直して古い模様が見える事（`CONTROL OK`）。device と独立に CPU だけで stale を作れる事を示す。「FLUSH64 を省いた時に古い値が見える」は非 coherent の観測そのもので対照ではないので使わない。
   - **主の試験**: CPU が dst を cacheable で読んで clean の line を載せた直後に、GPU に src → dst を書かせる（**64 KiB の組を 8 回**、2 MiB の中で 128 KiB ずつずらす。WS256 と同じ大きさで、L2 からの追い出しによる偽の合格を避ける）。道具: p006 は MIPS の FW が書く UFO（4 byte）を 16,384 個並べて 64 KiB を埋める（MIPS の道）。p008 は compute の kernel の書き込み（data master の道）。属性は上の既定の組。**SLC の flush は入れない**。
   - **判定**: **cacheable の view の読みが期待の模様と全部一致する事**。別名の読みは食い違った時の向きの分類だけに使い、判定には使わない。逆向き（CPU が cacheable で書き flush せず、GPU が読む）も 1 回流す。
   - **切り替えの条件**: 8 回とも一致し、かつ 0b が `CONTROL OK` の時だけ、**試した requester の道と属性の組に限って** coherent とみなし既定を切り替えてよい（切り替えは別の Phase で Q1 の承認を得て）。**0b が `CONTROL INCONCLUSIVE` の時は、一致しても切り替えない**（非 coherent の既定のまま）。後始末は WS256 の 6 と同じく範囲を FLUSH64 で捨てる。
5. **依存（Q1 の判断、2026-10-19、q1321）**: 予備 (i) の uncached の別名の写像（実装の設計は WS256 design-peripherals.md §2.4 の予備 (i) の 1〜4 を参照: 4 KiB の段で確保した page の別名だけを写す（2 MiB の leaf では OpenSBI の別名まで写る、WS256 review 3 Major 2）、unmap の時に FLUSH64、別名で LR/SC・AMO を使わない）（`src/hal/riscv64/` の page.c の kernel の uncached の写像、bsp-visionfive2 の別名の窓、space.c の user の NOCACHE）は **WS269 p004 が作って持つ**。WS256 p023 は DMAC の coherency の確かめで要れば WS269 p004 の物を使う（WS256 p023 は予備 (i) を作らない）。§3.6 の分析のとおり hal.h は変えない。
6. **D1 の試行の数え方**: 共有の道が正しい事が成り立った後の試行だけを数える。p005 では、FW の構造を全部 uncached の別名で置き、p004 の 0a が `ALIAS OK` の状態を「共有の道が正しい」とみなす（別名の写像そのものは p004 の host 試験と 0a の実機の確かめで保証する）。

### 3.3 attach の 2 段と firmware を読む時期（レビュー M8）

- VF2 の attach は `probe_soc` の中で root の mount の前に走る。既存の amdrdna・i915 は thread の中で root を上限付きで待って FW を読む（`amdrdna/firmware.c:39-57`、`i915/firmware.c:11-17`）。
- 早い段（attach）: PD・clock・reset、register の窓、CORE_ID の BVNC を読む。36.50.54.182 以外なら attach しない。ここでは render の node を出さない。
- 遅い段（worker の thread）: root を上限付き（例 60 s）で待ち、`/lib/firmware/powervr/rogue_36.50.54.182_v1.fw` を読み、**FW の image を kernel の memory に保持し**（recovery の reset で使う、§3.5）、FW を起こし、成功したら render の node を登録する。失敗は ETIMEDOUT・ENOENT を画面の console に 1 行で出し、node を出さず、§3.1 の失敗の道で電源を落とす。
- 遅い登録の EBUSY: `drv_gpu_register` は他の登録・削除と重なると EBUSY を返す（`gpu.c:604-606`）。worker からの登録は EBUSY の時だけ上限付き（例 100 ms 間隔で 50 回）で再試行し、尽きたら 1 行を出して node を出さず、§3.1 の失敗の道（FW を止め、reset・clock・PD を落とす）を同じく通す。
- initrd が root の時: FW を initrd の `/lib/firmware/powervr/` に入れる（firmware の package を選んだ時に initrd にも入る既存の流儀か、p003 で確かめる）。
- Keiland の起動時の probe との競合: display の node（§3.5）は早い段で出す。render の node は遅れて出る。libvulkan は render の node が無い時に失敗するので、Keiland の起動は render の node を上限付きで待つ（WS270 と調整、p010）。

### 3.4 core の識別と firmware の起動

1. CORE_ID の BVNC を読む（Value(s) referenced: mainline の `pvr_rogue_cr_defs.h`）。
2. FW の file を検査する parser（header、section の表、**device info の BRN・feature の表**）を host 試験付きで書く。BRN・feature は FW の file から取り、表は手で持たない。**例外は host 側の BRN**: FW の header が表せない BRN（mainline の enum の外、S8a）は host の定数として持つ。今は BRN71317 だけ（§0.4、Value referenced: S9f）。feature は DDK 1.19 の hwdefs から取らない（XE_TPU2 のように DDK に無い物がある）。reset の手順は mainline の `pvr_fw_startstop.c` を参照する（Procedure referenced）。
3. **MIPS の場合、kernel 用の GPU の VM context は作らない**。FW の memory は MIPS の page table で写し（共有の仕方は §3.2）、boot data に register の物理 address を入れる（Procedure referenced: mainline の `pvr_fw_mips.c`、`pvr_vm_mips.c`）。
4. MIPS を reset から出し、FW の init の完了を上限付きで待つ。
5. health の kick を KCCB に送り、FWCCB の応答と IRQ 82 を確かめる。

QEMU に PowerVR は無いので実機だけで確かめる。判定は §7。

### 3.5 MMU・memory・command の queue・表示

- **GPU の MMU**: Rogue の 3 段の page table、session ごとに FW の memory context。VA は 40 bit（S9c）、物理は 32 bit に限る（§3.2）。**page は 4 KiB だけを使い、1 MiB・2 MiB の page は使わない**（BRN71317、Value referenced: S9f、§0.4）。
- **queue**: context ごとに geometry・fragment・compute・transfer の CCB、kick は KCCB。完了は FW の sync を `gpu-fence` に結ぶ。
- **parameter buffer**: context ごとの freelist、FW の grow の要求に応える。
- **recovery（レビュー 2 Major 3、`gpu.h:59-88` の契約の文に沿う）**: hang・fault の検出（FW の通知、health の kick の応答の時間切れ）は core に recovery を始めさせる入口で、ops そのものではない。
  - 対象: 1 つの session（open）は geometry・fragment・compute・transfer の複数の FW の context と memory context を持つ。stop_begin・stop_poll・isolate は **その session の全部の FW の context** を対象にする。
  - `stop_begin(ctx)`: その session の新しい kick を止め、FW に全部の FW の context の停止（cleanup）を KCCB で要求する。待たない。
  - `stop_poll(ctx)`: 全部の FW の context の cleanup の完了が FWCCB に届き、その session の posted の descriptor と callback が全部 retire するまで EAGAIN。そこまで済んだ時だけ 0（FW が context の reset を通知して止めたと分かる context もここで 0 を返せる）。FW が応答しない・cleanup が失敗した時は EAGAIN 以外の error を返す。core はまず isolate を呼び、isolate が失敗した時だけ device 全体の fault にする（gpu.c:7216-7238）。
  - `fault(void *, int)`: device 全体に対して冪等。新しい publication（kick・登録）を止め、全部の destroy・close の道を「不確かな DMA の backing を解放せず保持する」状態にする。hardware が止まったとは主張しない。
  - `isolate(ctx)`: core が「stop を確かめられなかった」時に呼ぶ。成功の条件は次の 2 つ: (1) その session の全部の FW の context が到達し得る memory（GPU の page table、CCB、blob の backing）を checked reset まで device の所有として保持できる、(2) FW が他の context を処理し続けている。満たせば callback を error で終え、以後の host の traffic を断り、その後の resource_destroy・close は hardware に触らずに retire する。満たせなければ error を返し、core が fault にする。
  - **health の kick の時間切れ（FW 全体の hang）は session の stop ではない**: backend が fault に当たる処理（kick の停止、backing の保持）をしてから `drv_gpu_report_error` を呼ぶ（gpu.h の `drv_gpu_report_error` の注）。
  - `reset()`: 古い外の所有者が全部 retire した後だけ呼ばれる。PD_GPUA の off/on、clock・reset を入れ直し、**遅い段で保持した FW の image**で FW を再起動し、health の kick が通った時だけ 0（root を待たず file も読まない）。保持していた backing はここで初めて解放する。
- **IRQ と lock**: 上半分は IRQ の status を読んで clear し、下半分（worker）に FWCCB の処理を任せる。lock の順は「device → context → CCB」。上半分は lock を取らない。
- **圧縮（レビュー m14）**: FBCDC（TFBC）は最初は使わない（S9c で feature はある）。
- **表示（レビュー M1）**: VF2 の display（DC8200）は WS256 p011 の boot framebuffer だけ。**display の node を別に作る**: boot framebuffer を `drv_gpu_display_ops`（query・mode・claim・release・present・wait）で出す display の device（kernel の console からの引き継ぎ、Keiland の release で console に戻す）。render の node（powervr）は `share`（export・import）を持ち、display の node は `scanout` の import で render の buffer を受ける。**display の node が render の buffer を読む口（レビュー 3 M1、案 (a)）**: `scanout.import_image` に渡るのは物理の page の配列（`drv_gpu_scanout_backing`）だけで kernel の VA は渡らないので、display の node を **powervr と同じ driver（`src/drivers/gpu/powervr/` の中）に置き**、Pi 4 と同じく driver の中の private な lookup（Procedure referenced: `bcm2711/display-device.c:1789-1815` の `bcm2711_shared_lookup` の形、名前は写さない）で render の buffer の object そのものを retain し、その object が持つ kernel の uncached の VA から読む。借りた page を `hal_pmem_map_uncached` で写し直す道（案 (b)）は採らない（hal.h:653-680 の「caller owns」の外）。HAL の API は変えない。lookup できない buffer（他の driver の物）の import は断る。present は CPU の copy で boot framebuffer に写す（レビュー 2 Major 2）: render の target は `drv_dma_alloc_coherent` で取る uncached の写像（§3.2 の表）で、copy はその写像から読む（hal.h:653-662 の「uncached の写像がある間は direct map で触らない」に従い、FLUSH64 + direct map の読みはしない）。uncached の読みは遅い（1080p で 1 frame 8 MB、未計測）ので、**copy は damage の矩形だけ**にする。boot framebuffer への書き込みの扱い（WS256 p011 の (α)・(β)）は p011 の決定に従う。p010 の後の候補（p011 の範囲で検討）: GPU の transfer で boot framebuffer へ直接書く（条件: framebuffer が GPU の 32 bit の範囲にある事と、DC8200 の読みの coherency）。順と role は **gpu0 = display（早い段）、gpu1 = render（遅い段）**（Pi 4 と同じ分け方）。自前の DC8200 の driver ができたら display の node だけを置き換える。

### 3.6 HAL（レビュー M7）

- 要るもの: (1) kernel の uncached の写像（FW の構造と CCB）、(2) user の uncached の写像（D7 で (a) を選んだ時の HOST_COHERENT の memory）。
- hal.h:750-756 の契約は「通常の user の写像は managed RAM を要する」。HAL_SPACE_NOCACHE の要求で、port が managed RAM の pa を受け取り、PTE には別名の窓の PA（0x4_0000_0000 + pa）を書くのは、呼び手から見て managed RAM の page を uncached に写す事で、契約の文面の内にあると判断する（推測。p004 の実装の review で確かめる）。
- よって **hal.h の差分は要らない**。変えるのは `src/hal/riscv64/page.c`・`space.c`・`bsp-visionfive2/`（別名の窓の対応付け、NOCACHE の受け入れ、unmap と prot の時の扱い）の実装で、承認は要らない（2026-09-25 の規則）。差分の案は無いので `plan/ws269/proposed/` は作らない（版 1 の「空」は誤り。directory は無い）。
- 実装の Phase で hal.h の文面の変更が要ると分かったら、その時に差分の案を `plan/ws269/proposed/` に置いて止まる。

## 4. firmware の入手と license

- 入手: S6 の fork の branch は書き換え得るので、**`awemorris/zedBSD-firmware` の mirror に sha256（S6 の値）で固定**し、そこから取る。FW の版（1.1.OS@6976702）と、FWIF の値を取る Linux の tag を対にして記録する（FW の major 版 1、S8）。
- package: `userland/firmware/powervr/`（`powervr-firmware`）。license・WHENCE・manifest を `/lib/firmware` に置く既存の流儀。
- license: S7 の文面。D3 (a) はこの文面で妥当（レビュー 1）。逆アセンブルの禁止を package の README に書き、調査の手順（§0.2）にも書く。

## 5. userland の Vulkan の道

| 案 | 中身 | 利点 | 欠点 |
| --- | --- | --- | --- |
| **A（推奨）** zedBSD の形 | libvulkan（Venus）のまま。kernel の powervr が Venus を decode し、共有の IR から Rogue の USC と PDS の backend を自前で書く | 既存の UAPI・libvulkan・Keiland をそのまま使う。資料は Mesa の `pco_isa.py`（MIT、encoding を機械で読める形）と csbgen の XML、BXM-4-64 の conformant の道（S11、S5） | 最大の仕事（§6 の LW）。bcm2711 の Venus の decode は 24,329 行、amdrdna は vk 11,385 行 + compiler 9,575 行（レビュー M2） |
| B Mesa の powervr を移植 | Mesa を `userland/packages/` に入れ、Linux の pvr の DRM の UAPI と winsys を zedBSD に足し、ICD にする | Mesa の compiler と Vulkan の実装を得る | **DRM の UAPI と winsys が要る**（新しい UAPI）。Vulkan の実装が 2 つになり Keiland・libvulkan の前提が壊れる。BXE は Mesa で unsupported（S5）。Mesa は userland の package で、AGENTS.md の toolchain ではない（版 1 の誤りを直した） |
| C GPU の描画を使わない | WS269 を FW の起動と display の node までで止める | 小さい | Keiland は Vulkan だけで描くので、**C では Keiland が動かない**（§2）。ws.md の完了の条件 2 を満たさない |

推奨は A。

## 6. 実装の Phase の分割

LW は粗い（8 LW ≈ 実時間 2〜3 h）。p008〜p011 は bcm2711（52,772 行）・amdrdna の規模と比べて見積もった。QEMU の試験は「他の platform の回帰」と「VF2 の image の build」だけ。GPU の動作は実機で、判定は §7。

| Phase | 中身 | 依存 | LW | 確かめ |
| --- | --- | --- | --- | --- |
| p002 | 資料の固定（コードなし）: mainline の tag、Mesa の `bxe-4-32.h`・`pco_isa.py`・csbgen、FW と LICENSE の mirror、§0.3 の未確認、BRN の組の記録（照合ではない、§0.4） | p001 の確認 | 4 | 文書 |
| p003 | firmware の package（mirror から、sha256 の検証、license・WHENCE・manifest、initrd への入れ方） | p002、D3 | 4 | build、package の host 試験 |
| p004 | `vf2-pmu.c`（PD_GPUA）、`vf2-crg.c` の GPU と親の行（ISP_2X を含む）、組み込みの記述（D2、非 coherent・32 bit）、**riscv64 の uncached の別名の写像（予備 (i)、WS269 が持つ、WS256 p023 も使う）**（kernel・user、§3.6）、attach の早い段（BVNC）と失敗の道 | WS256 p013・p015・p022、p002 | 8〜12 | host 試験（PMU・CRG の値、別名の写像の PTE）、build warning 0、実機: 画面の console に PD_GPUA on・rate・BVNC・**0a（`ALIAS OK`）**の行 |
| p005 | 遅い段: root を待つ worker、FW の parser（host 試験）、MIPS の page table、FW の起動、health の kick、IRQ | p003、p004、WS256 p020（判定の操作） | 16〜24 | host 試験（parser・page table）、実機: 画面に FW の応答の行 |
| p006 | coherency の確かめ（§3.2 の 4、0b・**MIPS（UFO）の道**の主の試験、SLC の flush なし、属性は §3.2 の既定）、GPU の MMU（context）、resource・blob・map（D7）、CCB・kick・fence、null の kick、recovery、render の node の登録、`share` | p005、D7 | 16〜24 | 実機: coherency の判定の行、`userland/tests/gpu-*` の資源・fence の試験の PASS の行を画面で |
| p007 | display の node（powervr と同じ driver の中、boot framebuffer の `drv_gpu_display_ops`、console の引き継ぎ、自分の resource での claim・present）と、`scanout` の import（driver の中の private な lookup で render の buffer を retain、uncached の VA からの damage の矩形の CPU の copy、§3.5）。claim・present の部分は p004 の後なら p005・p006 と並行できる。**import の部分は p006（render の `share`）の後** | WS256 p011 の実機、p004、import の部分は p006 | 8〜12 | 実機: 自分の resource で claim して色を出し、release で console に戻るのをユーザーが目で見る。import の試験は p010 |
| p008 | Rogue の USC・PDS の encoder（共有の IR から）、kernel の中の試験の道（compute の kernel を直接 kick して buffer を読み返す、Venus を通さない）、**GPU の data master の道の coherency の確かめ**（§3.2 の 4 と同じ手順） | p006 | 40〜60 | host 試験（encoding の golden を `pco_isa.py` の値から）、実機: 自己試験と data master の道の判定の行 |
| p009 | 最小の Venus の decode（instance・device・memory・buffer・compute の pipeline・dispatch） | p008 | 30〜50 | 実機: compute の Vulkan の試験の PASS の行 |
| p010 | graphics: TA・3D の job、vertex・fragment、transfer、render pass、WSI の display の node への受け渡し（render の buffer の import の試験を含む）、Keiland の起動の待ち（§3.3） | p009、p007 | 60〜100 | 実機: `vkdemo`・`venus-frame` の画をユーザーが目で見る |
| p011 | Keiland が使う Vulkan の機能の不足を driver 側で埋める（WS270 p006 の desktop の起動と目視の前提。2026-10-19 Q1 の判断、WS270 design-review-1 M5） | p010 | 30〜50 | 機能の試験（Keiland が使う Vulkan の機能の試験 program が通る）。desktop の目視は WS270 p006（WS269 p011 → WS270 p006） |
| p012 | 規約の全文の見直し | p003〜p011 | 4〜6 | build warning 0、host 試験 |

順の要: p004 は WS256 の p013（cleared）・p022（CRG の実機）・p015（rootfs からの起動の実機）の後。p007 は WS256 p011 の実機の後。p005 で FW が起きなければ D1。WS256 の p020（USB keyboard）は §7 の判定の操作の依存（p005 以降の実機の試験で、試験の program を打つ時）。WS256 p023 は WS269 の依存ではない（逆に p023 が要れば WS269 p004 の予備 (i) を使う、§3.2 の 5）。
- p002・p004 の phase.md に入れる事（レビュー 2）: p002 は §0.4 の BRN の組と XE_TPU2 の記録、p004 は §3.2 の表と fallback（0a）、§3.1 の clock の順・ISP_2X・PLL2 の固定。
- 各 Phase の phase.md の必須項目（レビュー 3）:
  - p004: 予備 (i) は WS256 §2.4 の予備 (i) の 1〜4 の設計で作る（4 KiB の段で確保した page の別名だけ、unmap で FLUSH64、別名で LR/SC・AMO を使わない）。
  - p005: EBUSY が尽きた時も §3.1 の失敗の道を通す。KCCB の命令の値は mainline の FWIF から（S8b）。
  - p006: requester の道は MIPS（UFO）だけと名付ける。属性（CC=0、SLC_BYPASS の候補、ACE-Lite、MIPS uncached）を試験と製品で同じに。render の target と blob の backing を物理で連続の `drv_dma_alloc_coherent`（`hal_pmem_alloc_limited`）で取るか、page ごとの確保と page ごとの別名にするかを決める（32 bit の範囲で大きな texture は断片化で失敗し得る、GPU には MMU がある。推測）。recovery の契約（§3.5、session の全部の FW の context、isolate の成功の条件、health の時間切れは `drv_gpu_report_error`）。
  - p007: display の node は powervr と同じ driver に置き、import は private な lookup。import の部分は p006 の後。

## 7. 実機の判定の方法（レビュー M3）

- 2026-10-19 のユーザーの決定（master.md の decisions-log の #10「SD boot, framebufferで目視」、#15「画面が出るまで待つ（serial の対話の例外は作らない）」）と AGENTS.md の「serial・console の log で判定しない」に従い、**判定は VF2 の framebuffer の画面をユーザーが目で見る**。SSH は今の例外（Linux・FreeBSD の guest）に無いので使わない。
- driver の段の結果は、画面の console に短い 1 行（段の名前と ok / 失敗の理由）で出す。試験の program（`userland/tests/gpu-*`、自己試験）は PASS / FAIL の要約の 1 行を画面に出す。ユーザーは USB keyboard（WS256 p020）で login して試験を打つか、image の起動の script が流す。
- 依存: WS256 p011（framebuffer の実機の確認）。
- ユーザーの手の要る試験は Q1 の「ユーザーが選べる試験の一覧」に載せ、ユーザーに今の試験を頼まない。

## 8. ユーザーの判断（D1〜D3、D5〜D7。D4 は Guardrail で決まっているので外した）

| # | 問い | 選択肢 | 推奨 |
| --- | --- | --- | --- |
| D1 | FW が起きない時の打ち切りの線 | (a) 共有の道が正しいと分かった後（§3.2 の 6）、**FW を初めて起動した後の debug に 16 LW** を使い（parser・page table の実装と host 試験の LW は数えない。p005 の見積もり 16〜24 LW の全体とは別）、それでも health の kick が通らなければ止めて報告。使う診断は FW の trace の buffer、FWIF の応答、MIPS・GPU の register、BVNC・BRN の照合、clock の rate（396 MHz）だけで、FW の逆アセンブルはしない / (b) 上限なしで続ける / (c) WS269 を p005 までに縮める | (a) |
| D2 | GPU の発見 | (a) `visionfive2.c` の組み込みの記述（DT の node があれば優先、§3.1 の compatible の一覧、DT の道でも非 coherent を既定） / (b) DT の overlay・別の DTB（WS256 の「DTB を差し替えない」に反する） / (c) vendor の DT | (a) |
| D3 | `powervr-firmware` を VF2 の image に既定で入れるか | 既存の流儀（`userland/firmware/README.md`: driver を on にすると menuconfig が firmware を選ぶ `config/drivers/firmware.list`、license は image の build の時に同意）に載せる。(a) powervr の driver を `firmware.list` に載せ、VF2 の config で driver を既定 on にする（結果として FW が入り、build の時に `LICENSE.powervr` の同意） / (b) driver を既定 off、menu で選ぶ | (a)（S7 の文面を確かめた。逆アセンブルの禁止は README に書く） |
| D5 | WS270（VF2 の Keiland）との関係（Keiland は GPU の Vulkan 無しで描けない、§2） | (a) WS270 は p010 まで画面なしの立ち上げ（service・seat・input・display の node の claim）だけ進め、描画は p010 の後 / (b) WS270 を p010 まで待つ / (c) CPU の Vulkan（lavapipe 相当）か CPU の描画の backend を別の WS で作る（大きい） | (a)（WS270 と Q1 が調整） |
| D6 | userland の Vulkan の道 | §5 の A / B / C | A |
| D7 | user の blob の HOST_COHERENT の memory（Vulkan の必須の memory type）をどう出すか | (a) riscv64 の HAL の実装で HAL_SPACE_NOCACHE を uncached の別名で出し、出す type は DEVICE_LOCAL と HOST_VISIBLE\|HOST_COHERENT（uncached）だけ（CPU の読みは遅い）。**HOST_CACHED は出さない**: 既存の libvulkan は HOST_COHERENT の無い type から HOST_VISIBLE・HOST_CACHED を外し（`instance.c:1117-1118`）、確保も断り（`memory.c:1070-1074`）、flush・invalidate は kernel を呼ばない（`memory.c:489-535`）。HOST_CACHED は Future Work の候補（libvulkan に renderer の cache の操作の dispatch が要る、Q1 に登録を依頼）。user の NOCACHE の上でアプリが atomic を使った時の振る舞いは未確認（uncached の別名の上の AMO・LR/SC の PMA）。注: vendor の user の写像は `pgprot_writecombine`（`pmr_os.c:378・388`）で、Svpbmt の無い U74 では cacheable になると推測（未確認）。vendor は実質 (b) に近い道で動いている可能性がある。(a) は Pi 4 の前例（`bcm2711/render-device.c:484`、`gpu.c:3271`）と同じ形だが、uncached の CPU の読みは性能の危険がある / (b) uncached を作らず、全部を cacheable にして submit・map の時に FLUSH64 し、HOST_COHERENT を名乗る（仕様に反し、アプリの非同期の読み書きで壊れ得る） | (a)（hal.h は変えない、§3.6） |

## 9. 危険と未確認、Q1 への依頼

- 危険: cache の扱いを誤ると「FW が起きない」に見える（§3.2 で先に潰す）。USC の backend と Venus の decode の規模（p008〜p011 で 160〜260 LW）。power-domain b の扱い（§3.1）。
- 未確認: §0.3、rtc の分周、GPU の bus が本当に非 coherent か（MIPS の道は p006、data master の道は p008 で確かめる）、JH7110 の interconnect が CC=1 の transaction を snoop するか（CC=0 を使うので確かめない）、FW が完了の時に SLC を書き出すか（mainline の手順からの推測、p006 は SLC の flush なしの一致だけを見る）、uncached の別名の上の AMO・LR/SC、TQ（transfer）が USC を要するか（Mesa の構造からの推測。p010 に置いたので順は崩れない）。
- 危険（版 3 で追加）: present の uncached の読みの帯域（未計測）、range の FLUSH64 の invalidate の確かさ（vendor は L2 を丸ごと追い出す）、FW の書き込みと SLC。
- Q1 への依頼: WS270 との D5 の調整。594 MHz（範囲外）と HOST_CACHED の memory type（D7）を Future Work に登録する事。WS256 の ws.md:53 と design-peripherals.md:357 の予備 (i) の古い写しを直す事（レビュー 3 Minor 7）。受け入れ 4（解決済み、add4ed7d9）。予備 (i) の持ち主は Q1 の判断で WS269 p004（§3.2 の 5）。ws.md の依存の行を §6 に揃えた。

## 10. レビュー 1 への対応

| 指摘 | 対応 | 場所 |
| --- | --- | --- |
| B1 非 coherent | 非 coherent を既定、DMA 32 bit、memory の種類ごとの共有の仕方、陽性の対照の確かめを p006 の最初に、予備 (i) を GPU のために p004 で作る、D1 の数え方 | §3.2、§3.6、§6、D1、D7 |
| M1 表示の道 | display の node（`drv_gpu_display_ops`）を別の Phase p007、gpu0/gpu1 の role、`share`・`scanout`、Keiland は GPU 無しで描けない事実、D5 の作り直し | §2、§3.5、§6 p007、D5 |
| M2 Phase の順 | p006 は null の kick と CPU の copy、compute は kernel の中の試験の道（p008）、Venus の decode を独立の p009、transfer は p010 | §6 |
| M3 判定の方法 | 画面の目視、出力は画面の 1 行、SSH は使わない、WS256 p011 の依存 | §7、§6 |
| M4 D4 | D4 を外し、Guardrail の `Value(s) referenced:` の規則、FWIF の layout は ABI の値、phase.md の受け入れ 4 は Q1 に | §0.2、§8、§9 |
| M5 firmware | fork の branch だけ、mirror に sha256 で固定、FW 版と Linux tag の対、LICENSE の文面、逆アセンブルなし | S6、S7、§4、D1、D3 |
| M6 危険の見積もり | BXM-4-64 を軸、mainline の MIPS の道、vendor の kernel driver は open、D1 を LW と診断の一覧で、`pco_isa.py`・csbgen | §1、§5、D1 |
| M7 HAL | 分析: hal.h は変えず src/hal の実装、要ると分かったら proposed/ | §3.6 |
| M8 FW を読む時期 | attach の 2 段、root を待つ worker、node の登録の時期、initrd | §3.3 |
| M9 clock | 親の行、396 MHz から、実際の rate を FW へ、rtc の分周 | §3.1 |
| m1 IRQ | 82 に確定 | §3.1、S9a・S9b |
| m2 vendor の DT | reg・clock-names・reset の順（27 に触らない）を記録 | S9b、§3.1、§0.3 |
| m3 address の幅 | PHYS 36・VA 40 | S9c、§3.5 |
| m4 FW の memory | MIPS では kernel の VM context なし、MIPS の page table、boot data、BRN・feature は FW の file から | §3.4 |
| m5 電源 | power-domain b、APM は最初は無効、hw event は GPU の event だけ | §3.1 |
| m6 CRG の API | `{crg,index}` の直接の構築、PD → clock → reset が必須 | §3.1 |
| m7 recovery・IRQ | recovery の ops への当て方、上半分・下半分、lock の順 | §3.5 |
| m8 試行の数え方 | LW で数える（16 LW） | D1 |
| m9 B の欠点 | toolchain ではない、DRM の UAPI と winsys | §5 |
| m10 proposed/ | directory は無い、作らない | §3.6 |
| m11 LW | p008〜p011 を規模から見積もり直した | §6 |
| m12 Mesa の資料 | `bxe-4-32.h`・`pco` を p002 の一覧に | S11、§6 p002 |
| m13 p004 の依存 | WS256 p022・p015 を足した、p007 に p011、ws.md の依存も直した | §6、ws.md |
| m14 圧縮 | 最初は使わない | §3.5 |

## 11. レビュー 2 への対応

| 指摘 | 対応 | 場所 |
| --- | --- | --- |
| Major 1 coherency の判定と陽性の対照・SLC | WS256 §2.4 の 0a・0b・判定の規則をそのまま使う。判定は cacheable の読み == 期待値、別名は分類だけ。対照は 0b（CPU だけで stale を作る）。量は 64 KiB × 8 回、読む前に SLC の flush・invalidate の FW の命令。0b が INCONCLUSIVE なら切り替えない | §3.2 の 4、S9e、§6 p006 |
| Major 2 present の道と invalidate・L2 の追い出し | render の target は uncached の写像で取り、copy はそこから（hal.h:653-662 に従う）。damage の矩形だけ。GPU の transfer で直接書く案を p011 の候補に。vendor の `do_invalid_range`（L2 の全体の追い出し）を事実として記録し、GPU → CPU は別名を原則、cacheable の invalidate には 0b 型の確かめ | §3.2 の表と注、§3.5 表示、S9a、D7、§9 |
| Major 3 recovery の ops の契約 | stop_begin = FW の context の cleanup の要求、stop_poll = cleanup の完了と callback の retire、fault = device 全体で publication を止め backing を保持、isolate = context ごとの reset の通知で guilty の backing を保持、reset = 保持した FW の image で再起動 | §3.5 recovery、§3.3 |
| Minor 1 BRN | §0.4 を新設（FW の enum の 12 個、71317 は host 側、71242 は適用されない、XE_TPU2）。host 側の BRN の例外、GPU の MMU は 4 KiB だけ、reset は mainline 参照 | §0.4、S8a・S9f、§3.4 の 2、§3.5 |
| Minor 2 表と 6 の矛盾 | 表を別名に一本化、fallback は 0a の MISMATCH の時だけで、その時は改めて設計して Q1 に返す。GPU には常に PA | §3.2 |
| Minor 3 DT の道の coherency | DT の道でも既定 0、優先する compatible の一覧、実機の DTB は未確認 | §3.1、D2、§0.3 |
| Minor 4 clock | axi を先（vendor の順）と理由、ISP_2X の親の行、PLL2 に固定、594 MHz は Future Work の候補 | §3.1、§9 |
| Minor 5 D1 の数え方 | 「FW を初めて起動した後の debug の LW」と明記 | D1 |
| Minor 6 依存の写し | Q1 の判断: 予備 (i) は WS269 p004 が作って持ち、WS256 p023 は要れば使う。§6 と ws.md を揃えた（p020 は判定の操作の依存、p023 は依存から外した） | §3.2 の 5、§6、ws.md |
| Minor 7 受け入れ 4 | 解決済み（add4ed7d9）と直した | §0.2、§9 |
| Minor 8 D3 | 既存の `firmware.list` の流儀に言い換えた | D3 |
| Minor 9 D7 (b) の注記 | vendor の `pgprot_writecombine` と U74 の推測、uncached の読みの性能の危険 | D7 |
| Minor 10 attach の失敗の道 | reset・clock・PD_GPUA を落とす、display の node は残す | §3.1、§3.3 |
| Minor 11 late register の EBUSY | 上限付きの再試行 | §3.3 |
| Minor 12 行番号 | MIPS 72 行、snoop 174-183 行 | S9c、S9a |

## 12. レビュー 3 への対応

| 指摘 | 対応 | 場所 |
| --- | --- | --- |
| M1 display の node の読み口と p007 の依存 | 案 (a): display の node を powervr と同じ driver に置き、driver の中の private な lookup で render の buffer の object を retain し、その uncached の VA から読む。HAL の API は変えない。p007 の import の部分は p006 に依存、p007 の試験は自分の resource の claim・present、import の試験は p010 | §3.5 表示、§6 p007・p010 |
| M2 試験の requester・属性・SLC | p006 は MIPS（UFO）の道だけと名付け、切り替えも MIPS の道だけ。data master の道は p008。PTE の CC=0・SLC_BYPASS の候補・ACE-Lite・MIPS uncached を試験と製品で同じ値、既定は mainline。結論は試した属性の組にだけ効く。SLC は mainline の手順を Procedure referenced で採り、試験は flush なしで 8 回一致を確かめるだけ | §3.2 の属性・SLC・4、S8b、§6 p006・p008、§9 |
| Minor 1 壊れた表 | D7 の行を表に戻し、箇条を表の後に | §3.2 |
| Minor 2 HOST_CACHED | D7 (a) から外し Future Work の候補。「0b 型の確かめ」の言い方を直した（0b は対照） | D7、§3.2、§9 |
| Minor 3 recovery の細部 | 「isolate を経て fault」、isolate の成功の 2 条件、session の全部の FW の context、health の時間切れは `drv_gpu_report_error`、署名 `fault(void *, int)` | §3.5、§6 の p006 の必須項目 |
| Minor 4 S9e の値の出典 | 値は mainline の FWIF（S8b）、DDK は手順の参照だけ | S9e・S8b、§6 の p005 の必須項目 |
| Minor 5 WS256 との違い | INCONCLUSIVE の規則の違いを明記、WS256 の 1 と 6 を参照 | §3.2 の 4 |
| Minor 6 古い文 | S10・§9・p002 の BRN の照合、594 MHz の未確認を消した。S9a の行番号を 302-303・319 に | S10、S9a、§6 p002、§9 |
| Minor 7 予備 (i) の写しと制約 | WS256 §2.4 の予備 (i) の 1〜4 を参照、user の NOCACHE の atomic を D7 の注に。WS256 の ws.md・design-peripherals.md の古い写しは Q1 に依頼 | §3.2 の 5、D7、§6 の p004 の必須項目、§9 |
| Minor 8 backing の連続性 | p006 の決める事として必須項目に | §6 の p006 の必須項目 |
| Minor 9 p005 の依存 | 表の p005 の行に WS256 p020 | §6 |
| Minor 10 EBUSY が尽きた時 | §3.1 の失敗の道を通す | §3.3、§6 の p005 の必須項目 |

> 2026-10-19 Q1 の追記（WS270 design 改訂 2 §2.2・§2.3）: VF2 の desktop の最初の描画は libvulkan の WSI の copy の道（gpu1 → gpu0 の GPU_RESOURCE_WRITE）。p007 の import と damage は性能のため（実用の fps）で、描画の開始の前提ではない。

> 2026-10-19 Q1 の追記（WS270 design-review-2 N1）: p005 の必須項目に「powervr の遅い段の worker が、render の node（gpu1）を登録するまで `hw.gpu.attaching` を数える」を足す（sessiond は attaching の間は gpu0 があっても待つ、WS270 が持つ）。

> 2026-10-20 Q1 の追記（design-p010.md 版 2、review 1 の Minor 10・M5）: §3.5 の「FW の grow の要求に応える」は D-P1 で置き換える（free list は固定、grow の要求には GROW_UPDATE を delta 0 で返し、RECONSTRUCTION に応える）。§6 の p010 の受け入れは venus-frame を外し、vkdemo と gpu-selftest の clear・copy・draw にする（D-P7、ユーザーに確かめる）。
