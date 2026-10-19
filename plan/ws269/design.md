<!-- awesome-plan project=zedbsd record=ws269-design -->

# WS269 設計: VisionFive 2（JH7110）の PowerVR BXE-4-32 の driver

- 版: 2（2026-10-19、P2、ws269-p001、q1319-i01）。版 1（q1316-i01）を [design-review-1.md](design-review-1.md) に合わせて直した。対応の表は末尾の §10。コードは書いていない。
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
| S9a | vendor の JH7110 の glue（Dual MIT/GPLv2） | `drivers/gpu/drm/img/img-rogue/services/system/rogue/sf_7110/sysconfig.c`・`sysconfig.h` | cache の扱い: snoop の mode を **EMULATED**（179-183 行。CPU_ONLY は `SUPPORT_AXI_ACE_TEST` の時だけ）、host の cache の保守は全部 ccache の FLUSH64 の range（186-222 行）、DMA の mask は **32 bit**（425 行）。clock は `clk_bv` を 594 MHz、BIN2 の板（`/opp-table-0/opp-1250000000` がある板）は 396 MHz に set_rate し（`sysconfig.h:88-89`、`sysconfig.c:303-305・319`）、その値を FW の core clock に渡す（503 行）。順は apb・rtc の clock → rate → core・sys の clock → reset apb → doma の解除（316-325 行）、axi の clock は runtime resume で別に（224-229 行）。PMU の hw event の mask を resume で 0、suspend で全部（226・236 行）。IRQ 82 =「77 + 5」（`sysconfig.h:51`） |
| S9b | vendor の DT | `arch/riscv/boot/dts/starfive/jh7110.dtsi:1403-1422` | compatible `img-gpu`、reg `0x18000000/0x100000` と `0x130C000/0x10000`、clocks GPU_CORE・GPU_APB・GPU_RTC_TOGGLE・GPU_CORE_CLK・GPU_SYS_CLK・NOC_BUS_GPU_AXI（names clk_bv・clk_apb・clk_rtc・clk_core・clk_sys・clk_axi）、resets GPU_APB・GPU_DOMA だけ（**27 の NOC_BUS_GPU_AXI の reset には触らない**）、power-domains PD_GPUA、interrupts 82、`current-clock = <8000000>`、status disabled |
| S9c | vendor の core の定義 | `hwdefs/rogue/km/cores/rgxcore_km_36.50.54.182.h:60-61`、`hwdefs/rogue/km/configs/rgxconfig_km_36.V.54.182.h` | BRN 63553・71317。feature: AXI_ACELITE（57 行）、FBCDC 50・architecture 7（62-64 行）、MIPS（71 行）、NUM_CLUSTERS 1、PHYS_BUS_WIDTH 36、SLC 16 KiB、VIRTUAL_ADDRESS_SPACE_BITS 40 |
| S9d | vendor の PMU と ccache | `drivers/pmdomain/starfive/jh71xx-pmu.c:88`、`drivers/cache/sifive_ccache.c:234` | hw event の mask、FLUSH64 の range（手順の参照先） |
| S10 | firmware の header（レビュー 1 が header の 4 KiB と device info だけを読んだ。逆アセンブルなし） | S6 の file | firmware processor MIPS、BRN 63553・71242、feature は BXM-4-64 と同じで大きさだけ違う（COMMON_STORE 16384/19456、ISP の tile 4/6、MAX_PARTITIONS 10/12、IPP pipe 4/6）、BRN44079 が無い。**BRN の組が S9c（71317）と違う**: FW の版と DDK の版の違いと推測、p002 で照合 |
| S11 | Mesa `src/imagination/`（レビュー 1） | Mesa main | `common/device_info/bxe-4-32.h` がある。compiler は `pco`（MIT）。encoding は `pco/pco_isa.py`、control stream は `csbgen` の XML（license の行は未確認） |
| S12 | 既存の zedBSD | `include/drivers/gpu/gpu.h`、`src/drivers/gpu/`（bcm2711・amdrdna・compiler・venus）、`userland/desktop/libvulkan/`、`userland/desktop/libkeiland-backend-zedbsd/scanout-zedbsd.c`、`src/hal/riscv64/space.c:1778-1780`・`page.c`、`src/drivers/generic/dma.c`、`src/hal/riscv64/bsp-visionfive2/board.c`（ccache の FLUSH64） | 合わせる形と、今ある制限 |
| S13 | WS256 | `plan/ws256/design-peripherals.md` §2.4（bus master ごとの coherency の確かめ）・§3.2、ws.md の p011・p013・p015・p020・p022・p023 | 電源・clock・reset・DMA の基盤と実機の確かめ |

### 0.2 参照の規則（Guardrail の適用）

- register の offset・bit、FWIF の struct の layout（offset・大きさ・enum の値）、BRN の番号、clock の rate、IRQ の番号は**値**として使い、使う block の直前の複数行の comment に `Value(s) referenced: <版> <path> (<表・define の名前>)` を書く。FWIF の layout は firmware との ABI の値として扱う。
- 手順（電源・clock・reset の順、MIPS の起動、cache の保守の仕方）は `Procedure referenced: <版> <path> (<関数の名前>)`。
- code・comment・構造・symbol の名前は写さない（MIT・Dual の file でも）。zedBSD の名前は独自に付ける。file の上部に出典・notice を書かない。
- firmware は逆アセンブルしない（S7）。FW の挙動は FW の trace の buffer、FWIF の応答、register の値だけで調べる。
- phase.md の受け入れ 4「GPL の source は手順の参照だけ」は Guardrail（値の引用も可）より狭い。この設計は Guardrail に合わせる。phase.md の文面の揃え方は Q1 に依頼する（§9）。

### 0.3 まだ確かめていない事（p002 で固定する）

- 最新 mainline の imagination の tag（FWIF の値を取る版）と、S6 の FW の版の対。
- Mesa の `bxe-4-32.h`・`pco_isa.py`・csbgen の XML の版と license の行。
- S10 と S9c の BRN の違い。
- vendor の DT の 2 つ目の reg `0x130C000`（GPU timer 0x130C_0000 の書き間違いに見える、推測）と FW の SOC_TIMER の関係。

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

- **発見（D2）**: mainline の DT に gpu の node が無い（S4）。推奨は `visionfive2.c` の組み込みの記述: reg 0x1800_0000/1 MiB、IRQ 82（S9a、S9b、WS256 の n + 5 の規則と一致）、PD_GPUA、clock・reset。DT に `img,img-rogue` 系の node があればそちらを優先する。
- **CRG の API**: DT の node が無くても `struct drv_vf2_clock`・`drv_vf2_reset` を `{crg, index}` で直接作れる（`vf2-crg.h`）。clock の表に GPU の行（S3 の 3・45〜50）と**親の行**（gpu_root の mux の親の PLL、gpu_sys_clk の親 ISP_AXI、gpu_rtc_toggle の親 OSC と分周、noc_bus_gpu_axi の親 gpu_core、apb の親 APB_BUS）を足す。足さないと rate を計算できない（レビュー M9、mainline の `clk-starfive-jh7110-sys.c` の定義）。
- **順（必須）**: PD_GPUA on → clock（apb・rtc → gpu_core の rate → core・sys → axi）→ reset の解除（apb → doma）。reset の解除は status を待つので、PD と clock が先でないと待ちが終わらない。reset 27（NOC_BUS_GPU_AXI）には触らない（S9b）。Procedure referenced: S9a の enable の関数。
- **rate**: gpu_core を 396 MHz に設定して始める（S9a の BIN2 の値、どの板でも安全と推測）。594 MHz への引き上げは p005 で FW が起きた後に試す。FW に渡す core clock は `drv_vf2_clock_get_rate` で**実際に読んだ値**。rtc の分周は vendor の DT の `current-clock = <8000000>`（8 MHz）を目安に OSC（24 MHz）÷ 3 とし、p004 で TRM と照合する（推測）。
- **PMU**（新規 `vf2-pmu.c`）: PD_GPUA だけを software の turn-on で入れる。他の PD に触らない。hw event の mask は GPU の event（hard event 7、S1）だけを扱い、vendor の「全部 0 / 全部 1」は写さない。
- **power-domain b**: mainline の binding は B-series に PD「a」「b」を要求するが、JH7110 では b を FW の MIPS が管理する（S1）。host は b を持たない前提とし、mainline の手順で b を触る所は飛ばす（未確認、p005 で FW の応答で確かめる）。
- **Active Power Management**: 最初は FW の APM・idle の要求を無効にする（FW の init の設定で）。有効にするのは p006 の後の任意の作業。

### 3.2 CPU と GPU の memory の共有（レビュー B1）

**事実**: JH7110 で BXE を動かす唯一の driver は GPU を非 coherent（snoop の emulation、全部の保守を ccache の FLUSH64）・DMA 32 bit として扱う（S9a）。core は AXI_ACELITE の feature を持つが（S9c）、vendor はそれを試験の build でしか使わない。zedBSD は VF2 の既定を coherent にしており（`VF2_DMA_DEFAULT_COHERENT 1`）、riscv64 の uncached の写像は kernel（`page.c`）も user（`space.c:1778-1780`）も UNSUPPORTED、非 coherent の DMA の確保は uncached の写像が無いと ENOTSUP（`dma.c`）。

設計:

1. **GPU は非 coherent を既定**。組み込みの記述は DT を持たないので `drv_fdt_dma_coherent` の既定に頼らず、記述に「非 coherent」を明示する。coherent と確かめられた時だけ切り替える。
2. **DMA の address は 32 bit**（DDR の下の範囲）。GPU の物理 bus は 36 bit（S9c）だが、広げるのは確かめた後。
3. **種類ごとの共有の仕方**:

| memory | 使い方 | 共有の仕方 |
| --- | --- | --- |
| FW の code・data（MIPS の page table で写す） | CPU が一度書き、FW が読む | 書いた後に FLUSH64 の range |
| KCCB・FWCCB・FW の init・trace の buffer など、CPU と FW が両方書く構造 | 頻繁な往復 | **uncached の別名**（WS256 の予備 (i)、0x4_0000_0000 + pa の窓）で CPU が読み書き。予備 (i) が無い間は「kick の前に FLUSH64、読む前ごとに FLUSH64」（invalidate は S9a と同じく FLUSH64 で代える。ccache の FLUSH64 は書き戻しと追い出しを兼ねる） |
| GPU の page table（context ごと） | CPU が書き、GPU が読む | 書いた後に FLUSH64、その後に GPU の MMU の cache の invalidate を FW に要求 |
| user の blob（Vulkan の memory） | アプリが読み書き | D7 |

4. **確かめ（陽性の対照の形、WS256 §2.4）**: p005 の FW の起動は、最初から上の表の安全な道（uncached の別名か FLUSH64）で行う。FW が起きた後、p006 の最初で「CPU が cacheable で古い模様を cache に持ったまま、GPU（FW の sync の書き込み）が DRAM に書く → CPU が別名で読む値と cacheable で読む値を比べる」を 8 回流し、coherent か非 coherent かを決める。陽性の対照は「FLUSH64 を省いた時に古い値が見える事」。
5. **依存**: WS256 p023（DMAC の coherency）の結果に関係なく、予備 (i) の uncached の別名を GPU のために作る。これは `src/hal/riscv64/`（page.c の kernel の uncached の写像、bsp-visionfive2 の別名の窓）の実装で、§3.6 の分析のとおり hal.h は変えない。p004 の範囲に入れる（WS256 p023 が先に作れば使う）。
6. **D1 の試行の数え方**: 4 の前提（共有の道が正しい事）が成り立った後の試行だけを数える。p005 では、FW の構造を全部 uncached の別名で置いた状態を「共有の道が正しい」とみなす（別名の写像そのものは p004 の host 試験と、CPU の書き込みが別名の読みで DRAM の値として見える事の実機の確かめで保証する）。

### 3.3 attach の 2 段と firmware を読む時期（レビュー M8）

- VF2 の attach は `probe_soc` の中で root の mount の前に走る。既存の amdrdna・i915 は thread の中で root を上限付きで待って FW を読む（`amdrdna/firmware.c:39-57`、`i915/firmware.c:11-17`）。
- 早い段（attach）: PD・clock・reset、register の窓、CORE_ID の BVNC を読む。36.50.54.182 以外なら attach しない。ここでは render の node を出さない。
- 遅い段（worker の thread）: root を上限付き（例 60 s）で待ち、`/lib/firmware/powervr/rogue_36.50.54.182_v1.fw` を読み、FW を起こし、成功したら render の node を登録する。失敗は ETIMEDOUT・ENOENT を画面の console に 1 行で出し、node を出さない。
- initrd が root の時: FW を initrd の `/lib/firmware/powervr/` に入れる（firmware の package を選んだ時に initrd にも入る既存の流儀か、p003 で確かめる）。
- Keiland の起動時の probe との競合: display の node（§3.5）は早い段で出す。render の node は遅れて出る。libvulkan は render の node が無い時に失敗するので、Keiland の起動は render の node を上限付きで待つ（WS270 と調整、p010）。

### 3.4 core の識別と firmware の起動

1. CORE_ID の BVNC を読む（Value(s) referenced: mainline の `pvr_rogue_cr_defs.h`）。
2. FW の file を検査する parser（header、section の表、**device info の BRN・feature の表**）を host 試験付きで書く。BRN・feature は FW の file から取り、表は手で持たない。
3. **MIPS の場合、kernel 用の GPU の VM context は作らない**。FW の memory は MIPS の page table で写し（共有の仕方は §3.2）、boot data に register の物理 address を入れる（Procedure referenced: mainline の `pvr_fw_mips.c`、`pvr_vm_mips.c`）。
4. MIPS を reset から出し、FW の init の完了を上限付きで待つ。
5. health の kick を KCCB に送り、FWCCB の応答と IRQ 82 を確かめる。

QEMU に PowerVR は無いので実機だけで確かめる。判定は §7。

### 3.5 MMU・memory・command の queue・表示

- **GPU の MMU**: Rogue の 3 段の page table、session ごとに FW の memory context。VA は 40 bit（S9c）、物理は 32 bit に限る（§3.2）。
- **queue**: context ごとに geometry・fragment・compute・transfer の CCB、kick は KCCB。完了は FW の sync を `gpu-fence` に結ぶ。
- **parameter buffer**: context ごとの freelist、FW の grow の要求に応える。
- **recovery（レビュー m7）**: `drv_gpu_recovery_ops` の契約（stop_begin・poll・fault・reset・isolate、`gpu.h:59-88`）に当てる: stop_begin = 新しい kick を止める、poll = FW の hang・fault の通知と health の kick の応答、fault = FW の fault の情報を context に結ぶ、reset = PD_GPUA の off/on と FW の再起動（遅い段をもう一度）、isolate = fault の context の session に EIO。
- **IRQ と lock**: 上半分は IRQ の status を読んで clear し、下半分（worker）に FWCCB の処理を任せる。lock の順は「device → context → CCB」。上半分は lock を取らない。
- **圧縮（レビュー m14）**: FBCDC（TFBC）は最初は使わない（S9c で feature はある）。
- **表示（レビュー M1）**: VF2 の display（DC8200）は WS256 p011 の boot framebuffer だけ。**display の node を別に作る**: boot framebuffer を `drv_gpu_display_ops`（query・mode・claim・release・present・wait）で出す display の device（kernel の console からの引き継ぎ、Keiland の release で console に戻す）。render の node（powervr）は `share`（export・import）を持ち、display の node は `scanout` の import で render の buffer を受ける。present は CPU の copy（非 coherent なので copy の前に render の buffer を FLUSH64）で boot framebuffer に写す。順と role は **gpu0 = display（早い段）、gpu1 = render（遅い段）**（Pi 4 と同じ分け方）。自前の DC8200 の driver ができたら display の node だけを置き換える。

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
| p002 | 資料の固定（コードなし）: mainline の tag、Mesa の `bxe-4-32.h`・`pco_isa.py`・csbgen、FW と LICENSE の mirror、§0.3 の未確認、BRN の照合 | p001 の確認 | 4 | 文書 |
| p003 | firmware の package（mirror から、sha256 の検証、license・WHENCE・manifest、initrd への入れ方） | p002、D3 | 4 | build、package の host 試験 |
| p004 | `vf2-pmu.c`（PD_GPUA）、`vf2-crg.c` の GPU と親の行、組み込みの記述（D2、非 coherent・32 bit）、**riscv64 の uncached の別名の写像**（kernel・user、§3.6）、attach の早い段（BVNC） | WS256 p013・p015・p022、p002 | 8〜12 | host 試験（PMU・CRG の値、別名の写像の PTE）、build warning 0、実機: 画面の console に PD_GPUA on・rate・BVNC・別名の読みの確かめの行 |
| p005 | 遅い段: root を待つ worker、FW の parser（host 試験）、MIPS の page table、FW の起動、health の kick、IRQ | p003、p004 | 16〜24 | host 試験（parser・page table）、実機: 画面に FW の応答の行 |
| p006 | coherency の確かめ（§3.2 の 4）、GPU の MMU（context）、resource・blob・map（D7）、CCB・kick・fence、null の kick、recovery、render の node の登録、`share` | p005、D7 | 16〜24 | 実機: coherency の判定の行、`userland/tests/gpu-*` の資源・fence の試験の PASS の行を画面で |
| p007 | display の node（boot framebuffer の `drv_gpu_display_ops`、console の引き継ぎ、`scanout` の import、CPU の copy の present）。GPU の core と独立なので p004 の後なら p005・p006 と並行できる | WS256 p011 の実機、p004 | 8〜12 | 実機: 試験の program が claim して色を出し、release で console に戻るのをユーザーが目で見る |
| p008 | Rogue の USC・PDS の encoder（共有の IR から）、kernel の中の試験の道（compute の kernel を直接 kick して buffer を読み返す、Venus を通さない） | p006 | 40〜60 | host 試験（encoding の golden を `pco_isa.py` の値から）、実機: 自己試験の PASS の行 |
| p009 | 最小の Venus の decode（instance・device・memory・buffer・compute の pipeline・dispatch） | p008 | 30〜50 | 実機: compute の Vulkan の試験の PASS の行 |
| p010 | graphics: TA・3D の job、vertex・fragment、transfer、render pass、WSI の display の node への受け渡し、Keiland の起動の待ち（§3.3） | p009、p007 | 60〜100 | 実機: `vkdemo`・`venus-frame` の画をユーザーが目で見る |
| p011 | Keiland の要る範囲の残り（WS270 と合わせる） | p010 | 30〜50 | 実機: Keiland の desktop をユーザーが目で見る |
| p012 | 規約の全文の見直し | p003〜p011 | 4〜6 | build warning 0、host 試験 |

順の要: p004 は WS256 の p013（cleared）・p022（CRG の実機）・p015（rootfs からの起動の実機）の後。p007 は WS256 p011 の実機の後。p005 で FW が起きなければ D1。

## 7. 実機の判定の方法（レビュー M3）

- 2026-10-19 のユーザーの決定（master.md の decisions-log の #10「SD boot, framebufferで目視」、#15「画面が出るまで待つ（serial の対話の例外は作らない）」）と AGENTS.md の「serial・console の log で判定しない」に従い、**判定は VF2 の framebuffer の画面をユーザーが目で見る**。SSH は今の例外（Linux・FreeBSD の guest）に無いので使わない。
- driver の段の結果は、画面の console に短い 1 行（段の名前と ok / 失敗の理由）で出す。試験の program（`userland/tests/gpu-*`、自己試験）は PASS / FAIL の要約の 1 行を画面に出す。ユーザーは USB keyboard（WS256 p020）で login して試験を打つか、image の起動の script が流す。
- 依存: WS256 p011（framebuffer の実機の確認）。
- ユーザーの手の要る試験は Q1 の「ユーザーが選べる試験の一覧」に載せ、ユーザーに今の試験を頼まない。

## 8. ユーザーの判断（D1〜D3、D5〜D7。D4 は Guardrail で決まっているので外した）

| # | 問い | 選択肢 | 推奨 |
| --- | --- | --- | --- |
| D1 | FW が起きない時の打ち切りの線 | (a) 共有の道が正しいと分かった後（§3.2 の 6）、p005 の FW の起動に **16 LW** を使い、それでも health の kick が通らなければ止めて報告。使う診断は FW の trace の buffer、FWIF の応答、MIPS・GPU の register、BVNC・BRN の照合、clock の rate（396 MHz）だけで、FW の逆アセンブルはしない / (b) 上限なしで続ける / (c) WS269 を p005 までに縮める | (a) |
| D2 | GPU の発見 | (a) `visionfive2.c` の組み込みの記述（DT の node があれば優先） / (b) DT の overlay・別の DTB（WS256 の「DTB を差し替えない」に反する） / (c) vendor の DT | (a) |
| D3 | `powervr-firmware` を VF2 の image に既定で入れるか | (a) VF2 の config で既定 y、build の時に `LICENSE.powervr` の同意 / (b) 既定 n、menu で選ぶ | (a)（S7 の文面を確かめた。逆アセンブルの禁止は README に書く） |
| D5 | WS270（VF2 の Keiland）との関係（Keiland は GPU の Vulkan 無しで描けない、§2） | (a) WS270 は p010 まで画面なしの立ち上げ（service・seat・input・display の node の claim）だけ進め、描画は p010 の後 / (b) WS270 を p010 まで待つ / (c) CPU の Vulkan（lavapipe 相当）か CPU の描画の backend を別の WS で作る（大きい） | (a)（WS270 と Q1 が調整） |
| D6 | userland の Vulkan の道 | §5 の A / B / C | A |
| D7 | user の blob の HOST_COHERENT の memory（Vulkan の必須の memory type）をどう出すか | (a) riscv64 の HAL の実装で HAL_SPACE_NOCACHE を uncached の別名で出し、HOST_VISIBLE\|HOST_COHERENT を uncached で出す（CPU の読みは遅い。HOST_CACHED の type は submit の時の FLUSH64 と別に出す） / (b) uncached を作らず、全部を cacheable にして submit・map の時に FLUSH64 し、HOST_COHERENT を名乗る（仕様に反し、アプリの非同期の読み書きで壊れ得る） | (a)（hal.h は変えない、§3.6） |

## 9. 危険と未確認、Q1 への依頼

- 危険: cache の扱いを誤ると「FW が起きない」に見える（§3.2 で先に潰す）。USC の backend と Venus の decode の規模（p008〜p011 で 160〜260 LW）。power-domain b の扱い（§3.1）。BRN の違い（S10 と S9c）。
- 未確認: §0.3、rtc の分周、594 MHz で安定するか、GPU の bus が本当に非 coherent か（p006 で確かめる）、TQ（transfer）が USC を要するか（Mesa の構造からの推測。p010 に置いたので順は崩れない）。
- Q1 への依頼: phase.md の受け入れ 4 の文面を Guardrail に揃えるか（§0.2）。WS270 との D5 の調整。ws.md の依存の行は本 Phase で直した（WS256 p011・p013・p015・p020・p022・p023）。

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
