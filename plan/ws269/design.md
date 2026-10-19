<!-- awesome-plan project=zedbsd record=ws269-design -->

# WS269 設計: VisionFive 2（JH7110）の PowerVR BXE-4-32 の driver

- 版: 1（2026-10-19、P2、ws269-p001、q1316-i01）。コードは書いていない。design-reviewer 待ち。
- 範囲: kernel の driver の構成、firmware、userland の Vulkan の道、実装の Phase の分割、ユーザーの判断（D1〜D6）。
- 印: **事実**（出典あり）、**推測**（出典なし、確かめる Phase を書く）、**未確認**。

## 0. 出典

| # | 資料 | URL / 場所 | 何に使ったか |
| --- | --- | --- | --- |
| S1 | JH7110 TRM Preliminary V2（WS256 で SHA256 固定済み、`design-peripherals.md` §0） | https://doc-en.rvspace.org/JH7110/PDF/JH7110_TRM_StarFive_Preliminary_V2.pdf | GPU の address（0x1800_0000〜0x180F_FFFF、1 MiB）、GPU timer（0x130C_0000）、割り込み（`gpu_os_irq` global_interrupts[77]、`gpu_pow_gpio_irq` [78]）、clock（SYS CRG offset 0x0c `clk_gpu_root` の mux、0xb4 `clk_gpu_core` 分周 3〜7 既定 3、0xb8〜 core_clk・sys_clk・clk_apb・rtc_toggle・noc_bus_gpu_axi の gate）、reset（SYS の rstn 21 `gpu_rstn_apb`・22 `gpu_rstn_doma`・27 `noc_bus_reset_gpu_axi`）、電源（§3: dom_gpu_default・dom_gpu_a・dom_gpu_b。B は A の中の **MIPS core** が制御する Active Power Management。「GPU does not support DVFS」）、PMU（0x1703_0000 の `gpua_power_mode`、hard event 7 = GPU event） |
| S2 | JH7110 Datasheet（同） | 同じ WS256 の資料 | 「IMG BXE-4-32 MC1、最大 600 MHz（既定 400 MHz）」、Vulkan 1.2・GLES 3.2・OpenCL 3.0（vendor の closed driver での話）、「Dedicated processor for B-Series core firmware execution」、最大 64 GB の仮想 address、SLC、TFBC |
| S3 | Linux v6.12 の binding header（GPL-2.0 OR MIT、値だけ） | `starfive,jh7110-crg.h`（SYSCLK_GPU_ROOT 3、GPU_CORE 45、GPU_CORE_CLK 46、GPU_SYS_CLK 47、GPU_APB 48、GPU_RTC_TOGGLE 49、NOC_BUS_GPU_AXI 50。SYSRST_GPU_APB 21、GPU_DOMA 22、NOC_BUS_GPU_AXI 27）、`starfive,jh7110-pmu.h`（JH7110_PD_GPUA 2） | 番号 |
| S4 | Linux v6.12 `jh7110.dtsi` | WS256 の固定の写し | **gpu の node は無い**（`pwrc: power-controller@17030000`、compatible `starfive,jh7110-pmu` はある）。mainline の DT だけでは GPU を見つけられない |
| S5 | Mesa の PowerVR の文書 | https://docs.mesa3d.org/drivers/powervr.html | **BXE-4-32 は BVNC 36.50.54.182 で「unsupported and not under active development」**。device info と firmware は「community の要望で」置かれ、動作の保証は無い。非 conformant の GPU は `PVR_I_WANT_A_BROKEN_VULKAN_DRIVER=1` が要る。active な対象は AXE-1-16M・BXM-4-64・BXS-4-64 だけ。firmware の repo は https://gitlab.freedesktop.org/imagination/linux-firmware |
| S6 | Imagination の linux-firmware の commit「powervr: update firmware for 1.0.OS@6645434 release」 | https://gitlab.freedesktop.org/imagination/linux-firmware/-/commit/efbebc90f25adb2b2e1499e3cc24ea3f3c3e4f4c | `powervr/rogue_36.50.54.182_v1.fw` が Imagination の tree にある（検索の要約で確認。page の本体と LICENSE の名前は Anubis で読めず**未確認**） |
| S7 | Linux の drm/imagination の patch の議論（2024-02、DRM_POWERVR の依存） | https://lkml.iu.edu/hypermail/linux/kernel/2402.2/00212.html ほか同じ thread | Imagination が G6110・GX6250・GX6650・**BXE-4-32**・BXS-4-64 の firmware を持ち、StarFive を含む platform に載っている、という発言 |
| S8 | drm/imagination の generic compatible（2025-04、Matt Coster v6 04/18）、TH1520 の binding（2025-05）、BXS-4-64 MC1 の series | https://lkml.rescloud.iu.edu/2504.1/04951.html、https://lists.infradead.org/pipermail/linux-riscv/2025-May/071304.html、https://lkml.iu.edu/hypermail/linux/kernel/2503.3/04256.html | mainline の match は `img,img-rogue` と型の compatible（`img,img-axe-1-16m`、`img,img-bxm-4-64` など）。**JH7110・BXE の binding は見つからなかった（未確認、無いと推測）** |
| S9 | Pine64 forum（Star64 = JH7110） | https://forum.pine64.org/showthread.php?tid=18301 | vendor の driver は `rgx.fw.36.50.54.182`（vendor の名前）を読む。mainline の名前 `rogue_<BVNC>_v1.fw` と違う |
| S10 | Imagination forum「Mesa for VisionFive2」 | https://forums.imgtec.com/t/mesa-for-visionfive2/3822 | VF2 の BXE-4-32 の open な driver について Imagination は「更新は無い」と答えた（日付は未確認） |
| S11 | 既存の zedBSD | `include/drivers/gpu/gpu.h`（`struct drv_gpu_ops`）、`include/uapi/gpu*.h`（`/dev/gpu0` の ioctl 'G'）、`src/drivers/gpu/gpu.c`、`src/drivers/gpu/bcm2711/`（V3D: Venus の decode → native の job、自前の QPU の compiler）、`src/drivers/gpu/amdrdna/compiler/`（gfx10）、`src/drivers/gpu/compiler/`（共有の SPIR-V → scalar IR）、`userland/desktop/libvulkan/`（Venus の wire を `/dev/gpu*` に送る唯一の Vulkan） | 合わせる形 |
| S12 | WS256 | `plan/ws256/design-peripherals.md` §3.2（CRG・reset・SYSCON は `src/drivers/platform/visionfive2/vf2-crg.c` 他、「各 Phase は表に自分の行を足す」「使わない clock・PD に触らない」）、`plan/ws256/phase013/phase.md`（p013 cleared: CRG の表に GPU の行は無い、**PMU の driver は無い**） | 電源・clock・reset の基盤 |

確かめられなかった事（WebFetch が 429/403/Anubis で止まった）: mainline の `drivers/gpu/drm/imagination/` の各 file の SPDX（GPL-2.0-only か GPL-2.0-only OR MIT か）、MIPS の firmware processor の対応（`pvr_fw_mips.c` の有無）、Mesa `src/imagination/` の license、`LICENSE.powervr` の文面と kernel.org の linux-firmware に 36.50.54.182 が入っているか、vendor の DT の gpu node の値（IRQ の番号、clock-names）。**p002 で全部を取得して SHA を固定し、この表を確定する**。

## 1. 対応の状況のまとめ（受け入れ 1）

| 層 | BXE-4-32（36.50.54.182）の状況 | 根拠 |
| --- | --- | --- |
| silicon の資料 | register の仕様は非公開。TRM は SoC 側（address・clock・reset・PMU・IRQ）だけ | S1 |
| Linux の kernel driver（mainline） | AXE-1-16M・BXS-4-64・BXM-4-64 は binding がある。JH7110・BXE の binding・platform の data は**見つからない（未確認）**。firmware processor は B-series の BXE では **MIPS**（S1 §3 の「MIPS core」。META・RISC-V ではない）と推測、mainline の driver が MIPS を扱えるかは**未確認** | S5、S8、S1 |
| firmware | Imagination の tree に `rogue_36.50.54.182_v1.fw`（mainline の driver の形式）。vendor の BSP は `rgx.fw.36.50.54.182`（vendor の driver の形式、mainline と互換か**未確認**、互換でないと推測） | S6、S9 |
| Mesa（powervr の Vulkan） | device info はあるが **unsupported、active な開発なし、非 conformant** | S5 |
| vendor の closed driver | StarFive の BSP の DDK（Linux 5.15 系）。closed の userland、zedBSD には使えない | S10、S9 |

結論: 誰も BXE-4-32 を open な stack で「動く」と言っていない。mainline の kernel の手順と Mesa の USC の encoding は**参照できる最も近い公開の資料**だが、BXE 固有の quirk（S5 の「core-specific features and workarounds are likely missing」）は自分で見つける事になる。WS269 は**研究的で失敗し得る WS** として扱う（D1）。

## 2. 既存の zedBSD の GPU の形（合わせる対象）

- userland の Vulkan は `userland/desktop/libvulkan/` 1 つ。標準の Vulkan を Venus の wire に変え、`/dev/gpu*` の普通の session・resource・map・submit・display の ioctl（`include/uapi/gpu.h` 他）に流す。shader は SPIR-V のまま kernel へ送る。
- kernel の各 driver（`bcm2711` の V3D、`amdrdna`）は `struct drv_gpu_ops`（`open`・`resource_create`・`blob_create`・`command`・`present`・`resource_map`、任意の `display`・`share`・`commands`・`scanout`・`jobs`・`recovery`）を実装し、Venus の command を decode して native の job に落とす。shader は `src/drivers/gpu/compiler/`（SPIR-V → scalar SSA IR、`optimize`）を通し、各 backend（V3D の QPU、gfx10）が自分の ISA に下ろす。
- 新しい UAPI は作らない。PowerVR も同じ `drv_gpu_ops` の backend として `/dev/gpu0` に出る。**HAL の変更は要らない**（周辺の電源・clock は HAL に入れない、S12）。`plan/ws269/proposed/` は空。

## 3. kernel の driver の構成（受け入れ 2）

置き場所 `src/drivers/gpu/powervr/`（SoC の glue は `src/drivers/platform/visionfive2/` 側に分ける）。

### 3.1 発見と電源・clock・reset

- **DT の node**: mainline の DT に gpu の node が無い（S4）。WS256 の方針は「loader で DTB を差し替えない」。案は D2。推奨: `visionfive2.c` が SoC の compatible（`starfive,jh7110`）を見て、組み込みの記述（reg 0x1800_0000 / 1 MiB、IRQ、S3 の clock・reset の番号、PD_GPUA）で powervr を attach する。DT に `img,img-rogue` の node があればそちらを優先する（将来の mainline の DT に追随）。IRQ の PLIC の番号は TRM の global_interrupts[77] から求め（推測: PLIC の番号は global の番号 + 5 で 82。WS256 の他の device の対応で確かめる）、p002 で vendor の DT と照合する。
- **PMU**（新規 `vf2-pmu.c`、WS256 に無い）: 0x1703_0000 の PMU で PD_GPUA を入れる（software の turn-on の手順と `gpua_power_mode`、hard event の mask は TRM §3 の表から）。**他の PD に触らない**（PD_VOUT を落とすと GOP の画が消える、S12 の方針）。dom_gpu_b は firmware の MIPS が管理する（S1）ので host は触らない。
- **clock・reset**: `vf2-crg.c` の表に GPU の行を足す（SYS 3 gpu_root mux、45 gpu_core 分周、46〜50 の gate、reset 21・22・27）。順序は「PD_GPUA on → clock の gate を開く → reset 解除（apb → doma → noc axi）」を推奨とし、正確な順は p002 で mainline の手順（TH1520 の power sequencing の series が参考になる、S8）と TRM を照らして決める。rate は既定の 400 MHz（S2）のまま、set_rate はしない（DVFS 無し、S1）。
- **coherency**: GPU の bus が coherent か未確認。`drv_fdt_dma_coherent(…, VF2_DMA_DEFAULT_COHERENT)` の既定に従い、非 coherent の時に備えて CPU の書いた command buffer・page table は ccache の flush を通す道を最初から持つ（WS256 p023 の dma の道を使う）。

### 3.2 core の識別と firmware の起動

1. register の窓を map し、CORE_ID（BVNC）を読む。36.50.54.182 以外なら attach しない（log に BVNC を出す）。register の offset は mainline の `pvr_rogue_cr_defs.h` 相当から手順として参照（code を写さない。license は p002 で確定、D4）。
2. firmware の file を `/lib/firmware/powervr/rogue_36.50.54.182_v1.fw` から読む（新しい firmware の package `userland/firmware/powervr/`、§4）。header（形式の版、section の表）を検査する parser を host 試験付きで書く。
3. firmware 用の GPU の MMU の page table（FW の heap）と、MIPS の firmware processor 用の address 変換（MIPS の TLB/page table）を作り、code・data・初期の config（FWIF の init の構造）を置く。
4. MIPS を reset から出し、firmware の初期化の完了（FW の init の flag）を待つ（上限付き、timeout は ETIMEDOUT と log）。
5. health check の command を KCCB に送り、FWCCB の応答と割り込み（`gpu_os_irq`）を確かめる。

ここまでが「GPU を起こす」（ws.md 完了の条件 1）。QEMU に PowerVR は無いので**実機だけ**で確かめる。

### 3.3 MMU・memory・command の queue

- **GPU の MMU**: Rogue の 3 段の page table（PC・PD・PT）、context ごとに 1 つ。session（`drv_gpu_ops.open`）ごとに FW の memory context を作る。`resource_create`・`blob_create` は system の memory（4 KiB の page、非連続可）を GPU の VA に map。`resource_map` は CPU 側の map（非 coherent なら write-combine 相当の属性、WS256 の memory 属性の扱いに従う。未確認、p005）。
- **queue**: FW の CCB（client circular buffer）を context ごとに geometry（TA）・fragment（3D）・compute・transfer で持ち、kick を KCCB に積む。完了は FW の sync（UFO）を `gpu-fence` の fence に結ぶ。`jobs`・`recovery` の ops: FW の hang の通知で context を止め、session に EIO を返す。全体の reset は PD_GPUA の off/on と firmware の再起動。
- **Tile の memory**: TA が使う parameter buffer（freelist）を context ごとに確保し、FW の grow の要求（freelist の拡張）に応える。
- **display**: VF2 の display（DC8200）は GPU と別で WS256 p011 の boot framebuffer（`riscv64.framebuffer`）だけ。`present` は render 結果を boot framebuffer に transfer の job か CPU の copy で写す。自前の display の driver ができたら `scanout` の ops に替える（WS270 と調整、D5）。

### 3.4 UAPI

新規なし。`/dev/gpu0` の既存の ioctl（GET_INFO・RESOURCE_*・BLOB_CREATE・COMMAND・SUBMIT・FENCE_*・JOB_*）で足りる。capset は Venus の版を返す（bcm2711 と同じ）。

## 4. firmware の入手と license

- 入手先: Imagination の linux-firmware の tree（S6）か、kernel.org の linux-firmware（入っているか未確認）。既存の流儀（`userland/firmware/README.md`: 固定の revision から取得、byte を検証、`/lib/firmware` に license・WHENCE・manifest と一緒に置く、`awemorris/zedBSD-firmware` の mirror）に合わせ、新しい entry `userland/firmware/powervr/`（package `powervr-firmware`）を作る。
- license: linux-firmware の PowerVR は `LICENSE.powervr`（再配布可、改変不可と推測。**文面は未確認**、p002 で読む）。image に入れる既定（VF2 の config に既定で入れるか、同意の道）は D3。
- vendor の BSP の `rgx.fw.*` は使わない（形式が mainline と違うと推測、license も別）。

## 5. userland の Vulkan の道（受け入れ 2）

| 案 | 中身 | 利点 | 欠点 |
| --- | --- | --- | --- |
| **A（推奨）** zedBSD の形 | libvulkan（Venus）はそのまま。kernel の powervr が Venus を decode し、共有の IR から **Rogue の USC の backend**（と PDS の小さな program）を自前で書く。bcm2711 と同じ構造 | 既存の architecture・UAPI・libvulkan・Keiland をそのまま使える。apps に新しい library が要らない | 最大の仕事。USC の encoding の公開の資料は Mesa の `src/imagination/` の code だけ（手順として参照、D4）。BXE の quirk を自分で見つける |
| B Mesa の powervr を移植 | Mesa（meson・python・NIR）を `userland/packages/` に入れ、Linux の pvr の DRM の UAPI を zedBSD に足し、libvulkan の代わりの ICD にする | Mesa の compiler と Vulkan の実装を得る | 新しい UAPI（DRM の ioctl の互換）、Vulkan の実装が 2 つになり Keiland・libvulkan の前提が壊れる、toolchain の依存（Q1 の許可）。しかも BXE-4-32 は Mesa で **unsupported**（S5）で、動く保証が無い |
| C GPU を使わない | WS270 の Keiland を boot framebuffer と CPU の描画で動かし、WS269 は firmware の起動（§3.2）までで止める | 小さく確実 | ws.md の完了の条件 2 を満たさない（範囲の変更はユーザーの判断） |

推奨は **A**。ただし Keiland の WS270 を WS269 の完成に縛らないよう、WS270 は先に C の道（framebuffer）で始める事を D5 で提案する。A の途中で BXE の firmware が起きない・job が通らない場合の打ち切りの線を D1 で決める。

## 6. 実装の Phase の分割（受け入れ 3）

LW の見込みは粗い（8 LW ≈ 実時間 2〜3 h）。QEMU に PowerVR は無いので T1 の QEMU の試験は「他の platform の回帰」と「VF2 の image の build」だけ、GPU の動作は**実機（VF2）**で確かめる。

| Phase | 中身 | 依存 | LW | 確かめ |
| --- | --- | --- | --- | --- |
| p002 | 資料の固定（コードなし）: Linux の `drivers/gpu/drm/imagination/`・Mesa `src/imagination/`・firmware・`LICENSE.powervr`・vendor の DT の gpu node を取得し SHA を固定、SPDX を確定、§0 の未確認を埋める。MIPS の firmware processor の手順の範囲を決める | p001 の design-reviewer | 4 | 文書 |
| p003 | firmware の package `userland/firmware/powervr/`（license・WHENCE・manifest、firmware の menu と `config/drivers/firmware.list`） | p002、D3 | 4 | build、package の host 試験 |
| p004 | `vf2-pmu.c`（PD_GPUA だけ）、`vf2-crg.c` に GPU の行、`visionfive2.c` の組み込みの GPU の記述（D2） | WS256 p013・p015、p002 | 6 | host 試験（PMU・CRG の register の値）、build warning 0、実機: boot log に PD_GPUA on・rate 400 MHz |
| p005 | powervr の probe: CORE_ID の BVNC、firmware の parser（host 試験）、FW の heap の MMU、MIPS の起動、health check | p003、p004 | 16 | host 試験（parser・page table）、**実機: boot log に BVNC 36.50.54.182 と FW の応答** |
| p006 | GPU の MMU（context）、resource・blob・map、CCB・kick・fence・recovery、`/dev/gpu0` の登録、transfer の job で present | p005 | 16 | 実機: `userland/tests/gpu-*` の資源・fence の試験、transfer で boot framebuffer に色 |
| p007 | Rogue の USC と PDS の backend（共有の IR から）。まず compute（clear・copy） | p006、D4 | 24〜40 | host 試験（encoding の golden）、実機: compute の結果の読み返し |
| p008 | Venus の decode → TA/3D の job（bcm2711 の形）。vertex・fragment、Keiland の要る範囲（WS270 と合わせる） | p007 | 24〜40 | 実機: `vkdemo`・`venus-frame` の pixel、Keiland の画面（ユーザーの目視） |
| p009 | 規約の全文の見直し（conformance） | p003〜p008 | 4 | build warning 0、host 試験 |

順の要: p004 は WS256 の p013（cleared）・p015（rootfs からの起動）の実機の確認が済んでから。p005 で firmware が起きなければ D1 の打ち切りに従う。

## 7. ユーザーの判断（D1〜D6）

| # | 問い | 選択肢 | 推奨 |
| --- | --- | --- | --- |
| D1 | BXE-4-32 は Mesa でも unsupported（S5）。どこまで試みるか、打ち切りの線 | (a) p005（FW の起動）で 2 回の試行で起きなければ止めて報告 / (b) 起きるまで続ける / (c) WS269 を p005 までに縮める | (a) |
| D2 | mainline の DT に gpu の node が無い。GPU をどう見つけるか | (a) `visionfive2.c` に組み込みの記述（DT の node があれば優先） / (b) ESP に DT の overlay・別の DTB を置く（WS256 の「DTB を差し替えない」に反する） / (c) vendor の DT を使う | (a) |
| D3 | `powervr-firmware` を VF2 の image に既定で入れるか | (a) VF2 の config で既定 y、build の時に license の同意 / (b) 既定 n、menu で選ぶ | (a)（i915・amd-navi と同じ扱いになるよう、`LICENSE.powervr` の文面を p002 で確かめてから） |
| D4 | USC の encoding・FW の interface の公開の資料は Mesa（MIT と推測）と Linux（GPL-2.0-only OR MIT と推測）の code だけ。参照の扱い | (a) 2026-10-19 の許可（GPL は手順の参照だけ）に従い、MIT の部分も手順・値の参照だけで写さない / (b) MIT の file は notice を付けて値の表（register の定義・FWIF の構造）を取り込んでよい | (b) を MIT と確定できた header（register・FWIF の定義）に限って。確定できない物は (a)。p002 で SPDX を確かめてから決める |
| D5 | WS270（VF2 の Keiland）を WS269 に縛るか | (a) WS270 は先に boot framebuffer（GPU 無し）で始め、WS269 の p008 の後に GPU へ / (b) WS269 の p008 まで WS270 を待つ | (a)（Keiland が GPU 無しで描けるかは WS270 で確かめる、未確認） |
| D6 | userland の Vulkan の道 | §5 の A / B / C | A |

## 8. 危険と未確認

- BXE の MIPS の firmware processor を mainline の手順で起こせるか（最大の危険、p005）。
- GPU の bus の coherency、GPU の VA の幅（S2 は 64 GB）と JH7110 の DRAM の address。
- IRQ の PLIC の番号（推測 82）、PMU の turn-on の手順と待ちの上限、reset の順。
- Keiland の要る Vulkan の範囲（WS270 と未調整）。
- 試験: QEMU で GPU を確かめる手は無い。実機の確認はユーザーの手の要る場面（画面の目視）がある。
