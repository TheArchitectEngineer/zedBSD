<!-- awesome-plan project=zedbsd record=ws288-p001-design -->

# WS288 の設計: FPGA の GPU の IP「Amaterrace GPU」と GHCI — ZYBO（XC7Z010）で workflow、本命は KC705（XC7K325T）

2026-10-23 P2（ws288-p001、worktree p2-ws288-p001、基点 main c327d0c73）。親: [WS288](ws.md)。**版 2**（同日: design-reviewer の指摘 38 件（Critical 3・High 15・Medium 14・Low 6）の全部に対応。対応の表は §12。版 1 からの主な変更: detach と reset の順序（§1.3・§6.2）、FENCE の意味（§2.4）、drv_gpu の capability と jobs と capset（§6.4）、window と IRQ の devcfg の事実への修正（§5.1・§6.2）、CP の状態遷移と ring の約束（§2.6・§2.7）、fault は v1 は device 全体（§2.5）、統一 shader の deadlock と ROP の順序（§3.8）、cache の無効化（§2.4 `INVALIDATE`）、見積もりの計算し直しと **7010 では block を分けた bitstream で確かめる**方針（§3.6・R1）、GHCI の標準と vendor の境（§2.1）、KC705 は 1 device（§6.5）、Phase の循環の解消（§8）。）
この Phase は設計だけで、RTL も C も書かない。RTL の置き場所は https://github.com/awemorris/amaterrace-gpu （この Phase では書かない。clone `/home/awe/amaterrace-gpu` は commit 1791b32「Initial」= ユーザーの `README.md` だけ）、計画はこの zedBSD の `plan/ws288/`（2026-10-23 ユーザー「planはこのままzedBSDを使いましょう。」）。FPGA の CPU は https://github.com/awemorris/sukuna-risc.git （2026-10-23 ユーザー「mc64をsukuna-riscに改名しました。」）。

**ユーザーの仕様（amaterrace-gpu の README.md、1791b32、2026-10-23「概略を書きました。書き足してOKです。」）が上位**。この設計はそれに従う: block の名前と略号 **GHCI**（この project が提案する、Vulkan 互換の GPU のための標準の HCI）、**DC**（Display Controller）、**CP**（Command Processor）、**BLT**（Blitter）、**VF**（Vertex Fetcher）、**RX**（Rasterizer）、**SU**（Shader Unit）、**TU**（Texture Unit）、**ROP**（Buffer Writer）、**MC**（Memory Controller）。全体は `CPU -- (AXI4) -- CP`、fill/copy は `CP → BLT → MC`、primitive は `CP → VF → SU → RX → SU → ROP → MC`（TU は 2 段目の SU から）。shader は「Warp: 32-lane scalar with 16-bit packed」、register file は未定（「xxx」→ §3.5 と U12 で提案、[proposed/README-additions.md](proposed/README-additions.md) に README への書き足しの案）。

**到達目標**: 上の block を、ZYBO で小さな端から端までの道（CP + BLT の fill/copy を `fpgareg` で動かし、次に DC）から積み上げ、KC705 で SU を複数並べて zedBSD の Vulkan（libvulkan → kernel の executor）から使えるようにする。zedBSD の側の driver は **GHCI の汎用の driver**（Amaterrace に固有なのは shader の ISA の backend だけ）。

## 0. 出典と確認の水準

読んだ物（この checkout、main c327d0c73）:

- zedBSD の GPU の枠組み: `include/drivers/gpu/gpu.h`（`drv_gpu_ops`、interface 版 9、optional の表の契約）、`src/drivers/gpu/gpu.c`（版 2 で読んだ: 669〜686 行 = `/dev/gpuN` は **空いている最小の番号**、1024〜1045 行 = `GPU_CAP_JOB` は `GPU_CAP_NOTIFICATION` と `jobs` の reserve/commit/cancel が揃わないと `EINVAL`、`NOTIFICATION` は `commands` の submit/drain が要る、775〜830 行 = `drv_gpu_unregister` は **`online = 0`・`recovery->fault(ENODEV)`・`report_error(ENODEV)`・node の削除まで済ませてから** session が残れば `EBUSY` を返す）、`gpu-display.h`・`gpu-share.h`・`gpu-scanout.h`（`import_image`・`query_device`・`constraints`、「同じ device の scanout は普通の share の import で足りる」）、`include/uapi/gpu.h`・`gpu-op.h`・`gpu-job.h`・`gpu-fence.h`。
- libvulkan（版 2 で読んだ）: `context.c` 124 行（`CAPSET|BLOB|TRANSFER|COMMAND|MAPPING` が必須）、142〜170 行（capset id 4、≥ 156 byte、wire 1、XML の版、timelines ≠ 0）、172〜200 行（160 byte の vendor の magic と flag: `OPAQUE|STRICT_QUEUE|QUIESCE`）、`instance.c` 732〜734 行・`device.c` 279〜281 行（`strict_queue`・`native_quiescence`・`GPU_CAP_JOB|JOB_CAPACITY` が無いと device を作らない）。native の driver の capset の作り方は `src/drivers/gpu/powervr/render-runtime.c` の `runtime_capset`（175 行）。
- 既存の driver の形: `src/drivers/gpu/bcm2711/`（`vk/` 26k 行、`compiler/` 11k、`display/` 13k = display だけの別の drv_gpu device、static で kernel の寿命）、`powervr/`（`vk/` 9k、`compiler/` 14k）、`nvrtx/`（PCI の attach）、`venus/`（8k）、共有の `src/drivers/gpu/compiler/ir.h`（scalar の SSA、if-converted、Boolean は 32 bit の all-ones/zero。op の一覧は §6.6 の表）。
- kernel の API（版 2）: `include/kern/pmem.h`（`kern_pmem_alloc_limited`（連続の run。「末尾」は保証しない）、`kern_pmem_map_uncached(run)`、`kern_device_map`）、`include/kern/irq.h`（`kern_irq_register`・`kern_irq_register_msi`）。driver は `hal_*` を直接呼ばない。
- WS286 の板の事実と driver: [plan/ws286/design.md](../ws286/design.md) §1.1、§3.11〜§3.12（PS の DMA は CPU の cache と coherent でない、L2 off）、§5.6・§5.10。`include/uapi/fpga.h`、`src/drivers/platform/zybo/zynq-devcfg.c`（版 2 で確かめた: `WINDOW_PHYS 0x43c00000`・`WINDOW_BYTES 0x10000`・`PL_IRQ 61` は **固定**、`fpga.window=` の boot parameter は無い、attach の時から IRQ 61 の handler を持つ（284 行））、`platform/armv7/zybo/pl/plio/plio.v`・`plio.xdc`。
- KC705 の workflow: [phase001-kc705-bringup.md](phase001-kc705-bringup.md)、`platform/fpga/kc705/bringup/bringup.v`、`plan/tools/kc705/vivado-build.tcl`。
- Vivado 2026.1 の board store（KC705 1.7）: DDR3（MIG の preset `mig.prj`: 64 bit、800 MHz = 1250 ps、PHY 4:1、ui_clk 200 MHz）、PCIe x1〜x8、sys_diff_clock 200 MHz、LED・button・DIP・UART・IIC・flash（linear/SPI）。**HDMI は board file に無い**（UG810 未読、未確認）。
- 部品の規模（Vivado の `get_parts`、実測）: xc7z010clg400-1 LUT 17,600・FF 35,200・BRAM36 60・DSP48E1 80、speed -1。xc7k325tffg900-2 LUT 203,800・FF 407,600・BRAM36 445・DSP 840・GTX 16、speed -2。
- host の道具（実測）: iverilog 12.0、Vivado 2026.1（`xsim` 有り）、openFPGALoader 0.13.1、openocd 0.12.0、openXC7。**無い物**: verilator、cocotb。

確認の水準の印: 「実測」= この checkout か host で確かめた。「推定」= 経験則の見積もりで、p002 以降の Vivado の `report_utilization` で置き換える。「未確認」= 文書を読めていない（UG810、UG585 の HP port の細部、DS187、AMD の IP の EULA、XDMA の細部）。

## 1. 全体の構成と clock・reset

### 1.1 block と流れ（README の図に従う）

```
   CPU -- (AXI4) -- CP                      ※ ZYBO の GP0 は AXI3。boards/zybo の wrapper が単発の AXI4-Lite に直す

   Fill/Copy:   CP --> BLT --> MC
   Primitive:   CP --> VF --> SU --> RX --> SU --> ROP --> MC
                                              |
                                              +--> TU --> MC
   Compute:     CP --> SU --> MC
   Display:     DC --> MC（KC705）/ 自分の AXI（ZYBO の HP1）

   +------------------------------------------------------------------+
   | atg_top（GPU の IP）                                                |
   |  atg_cp   GHCI の register、ring、state、fence、IRQ、DBG            |
   |  atg_blt  2D の fill / copy / tile                                 |
   |  atg_vf   index と頂点の読み、primitive の組み立て                  |
   |  atg_su[N] shader unit（vertex も fragment も compute もここ）      |
   |  atg_rx   clip・viewport・edge function・quad の走査               |
   |  atg_tu   sampler と texture cache                                 |
   |  atg_rop  depth・stencil・blend・書き戻し（Buffer Writer）           |
   |  atg_mc   内部の memory bus（AMB）の arbiter、aperture、AXI master  |
   +------------------------------------------------------------------+
   +------------------------------------------------------------------+
   | atg_dc（DC の IP、別の top。7010 では GPU と同時に載せない）         |
   +------------------------------------------------------------------+
```

- GPU（`atg_top`）と DC（`atg_dc`）は別の IP、別の top。7010 には同時に載せなくてよい（2026-10-23 ユーザー）。ZYBO では別々の bitstream（§8）。KC705 では 1 つの bitstream に両方を載せ、MC の口を共有し（§4.2）、zedBSD からは **1 つの GHCI の device**（display の ops 付き、§6.5）に見える。
- block の間の内部の memory の口は 1 つの自前の bus「AMB」（§4.1）。外の AXI は MC の `am_axi_master` 1 つだけが話す。block は AXI を知らない。
- primitive の流れで SU は 2 回通る（頂点の warp と fragment の warp）。同じ SU が両方を走らせ、RX は頂点の出力を post-transform の buffer（PT）から取って fragment の warp を作る（deadlock の回避は §3.8）。TU は fragment の warp（と compute）から使う。
- 性能の counter と debug の register は小さく（§2.2）。DBG の ADDR/DATA で CPU が AMB を通して DDR を読み書きできるので、**最初の milestone は kernel の driver 無しに `fpgareg` だけで BLT の結果を確かめられる**（§8 p003）。
- **7010 では全 block が 1 つの bitstream に入らない見込み**（§3.6）。ZYBO は block を分けた bitstream（CP+BLT、DC、CP+RX+ROP、CP+SU の compute）で workflow と各 block を確かめ、全体の結合は Verilator と KC705 で確かめる（R1・U17）。

### 1.2 名前と parameter

- RTL の module の接頭辞: `atg_`（Amaterrace GPU の block: `atg_cp`・`atg_blt`・`atg_vf`・`atg_su`・`atg_rx`・`atg_tu`・`atg_rop`・`atg_mc`・`atg_dc`）、共通の部品は `am_`（`am_afifo`、`am_sync`、`am_axi_master`、`am_axi_regs`、`am_jtag_regs`）。
- GHCI の ID（§2.2）: magic `"GHCI"`、vendor `"AMAT"`（Amaterrace）、device `"ATG1"`（この GPU の 1 代目）、ISA `"ATS1"`（Amaterrace Shader ISA 1）。
- 主な parameter（top で与える。board ごとの値は §3.7 の表）: `NSU`（SU の数）、`NPHYS`（SU の物理の lane の数: 8/16/32。**warp は常に 32 lane**（README）で、`32/NPHYS` cycle で 1 命令を流す、§3.1）、`NWARPS`（常駐の warp の数）、`NREGS`（thread ごとの register の数、U12）、`LINE_BYTES`（AMB の line: 32 か 64）、`AMB_W`（AMB の beat の幅、64〜512）、`AXI3`（1 で AXI3 の master）、`TU_BILINEAR`、`ROP_STENCIL`、`PACKED`（16 bit packed の演算器）、`ADDR_W` = 32。

### 1.3 clock・reset・quiesce

| 領域 | ZYBO | KC705 | 中身 |
| --- | --- | --- | --- |
| `gclk`（GPU の core、register、AMB の client 側） | FCLK_CLK0 = 100 MHz（plio と同じ。`ps7_init` の DIV0 = 10、WS286 §1.2） | SYSCLK 200 MHz → MMCME2 → 100 MHz（最初は ZYBO と同じ値で timing の目標を 1 つにする。閉じたら 150 MHz を試す、§9 R3） | CP・BLT・VF・RX・SU・TU・ROP・MC の arbiter |
| `mclk`（AXI の master 側） | = `gclk`（HP0 の `S_AXI_HP0_ACLK` に FCLK_CLK0 を返す。同期なので CDC 無し） | MIG の `ui_clk` = 200 MHz | `am_axi_master` と AMB の master 側の FIFO。`gclk` ≠ `mclk` の時は `am_afifo`（gray の pointer の非同期 FIFO、自前）で要求と応答を渡す |
| `pclk`（pixel） | FCLK_CLK0 → MMCME2 → 25.2 MHz（640×480。100 MHz から 25.175 ちょうどは作れない: 25.2 は VGA の許容の中（0.1%）、**実機の monitor で確かめる**）/ 74.25 MHz（720p） | SYSCLK → MMCME2 → 同上 | DC の timing generator と出力。v1 は **2 mode だけ**（1 つの MMCM の 4 出力: 25.2・126・74.25・371.25。BUFGMUX で対を切り替え。800×600・1024×768 は後に DRP の再設定で足す、§5.1） |
| `pclk5`（TMDS の serial） | pclk × 5（OSERDESE2 の DDR で 10 bit/pixel） | KC705 の HDMI は ADV7511 の parallel（§5.3、未確認）なので無し | DC の TMDS の出力だけ |
| `hclk`（host link） | 無し | PCIe の `user_clk`（Gen2 x4 で 125 MHz の見込み。§4.3 で決める） | PCIe の bridge の AXI 側。register は `am_afifo` 経由で `gclk` へ、DMA は AMB の master 側へ |
| `tck` | JTAG | JTAG | KC705 の debug の bridge（§4.4）。`am_sync` の 2 段の同期で `gclk` へ |

- reset は領域ごとに **同期・active low**（`rst_n`）。入口は ZYBO が `FCLK_RESET0_N`（devcfg の `FPGA_ENABLE` が `FPGA_RST_CTRL` を解除すると **上がる**、WS286 §5.6）、KC705 が `!mmcm_locked | mig_ui_clk_sync_rst | cpu_reset` の OR の反転。各領域の reset の解除は `am_sync` で同期する。
- **reset の領域は 3 つ**: (a) GPU の core（CP の制御、BLT、VF、RX、SU、TU、ROP）、(b) MC と `am_axi_master`（AXI の outstanding を持つ）、(c) DC。`CTRL.RESET`（§2.2）は **(a) だけ**を reset し、(b) と (c) には触らない。MC は core の reset の後に「GPU の client の待ち行列を捨てる」`flush` を受ける（応答待ちの transaction は応答が来るまで保つ）。DC は自分の `CTRL.RESET` を持つ。
- **quiesce（AXI を静める）の定義**: `CTRL.HALT`（§2.6）→ MC が GPU の client からの **新しい AR/AW の発行を止め**、発行済みの W の beat を出し切り、全部の B/R の応答を待つ。応答は AXI の約束で必ず返るので有限（返らない AXI は壊れた system で、`WDT_CYCLES` の 4 倍を待って `STATUS.AXI_STUCK` を立てる。その時 hardware は何もできず、driver は PL の書き直し（ZYBO）か PCIe の reset（KC705）に上げる）。`STATUS.AXI_IDLE` は **GPU の client の分だけ**を数える（KC705 の DC は常に読んでいるので含めない）。
- **`CTRL.RESET` の順序は hardware が守る**: `RESET` を書くと CP は `HALT` → quiesce → `AXI_IDLE` を待ってから (a) を 1 cycle reset し、MC を flush し、`RESET` の bit を落とす。watchdog（§2.5）が切れた時も同じ順（FAULT を立て、HALT し、quiesce を待ってから driver の `RESET` を待つ。**hardware は自分では reset しない**）。AXI の途中で reset する道は無い。

## 2. GHCI（GPU Host Controller Interface）: CP の register、command、fence、割り込み、状態遷移、ring

### 2.1 GHCI が決める物と決めない物（AHCI・xHCI の類推）

- **2026-10-23 ユーザーの決定（command と ISA、版 3 で §2.4・§3 を書き直す）**:「コマンドは、現在のカーネルのコマンド（Venus由来）を使いましょう。ISAはRISC-Vベースでカスタムしましょう。RISC-V仕様に則る必要はないです。ベースにしつつ、他のGPUの平均を取って、実装しやすくよく使われるものを、我々の命令形式にフィットさせて取り込んでいきましょう。」
  - command: GHCI の ring の command は、zedBSD の kernel の今の command（`include/uapi/gpu-op.h` の Kei GPU command protocol 版 1、番号は Venus の wire format 1 と同じ。zedBSD だけの物は `GPU_OP_OWN_FIRST` から）を使う。版 2 の独自の opcode（§2.4）は置き換える。CP がこの Vulkan の command の流れを解く（firmware か hardware かは版 3 で決める）。
  - ISA: RISC-V を基にした独自の shader の ISA（GHCI の標準の ISA）。RISC-V の仕様に従う必要は無い。他の GPU（NVIDIA・AMD・Intel・Arm・PowerVR など）で共通によく使われ、実装しやすい物を、我々の命令の形式に合わせて取り込む。版 2 の "ATS1"（64 bit の固定長）は置き換える。

- **2026-10-23 ユーザーの決定（版 2 の「shader の ISA は決めない」を改める）**:「GHCIは標準のシェーダーISAを定義し、ベンダ独自のシェーダISAはオプションで利用可能。標準ISAの実装は、このGPUではハードウェアでそのまま実装されるが、コマンドプロセッサがJITコンパイルしてネイティブISAに変換してもOK。」
  - GHCI は **標準の shader の ISA**（命令・符号化・binary の形・実行の model のうち driver から見える所）を決める。GHCI の GPU は全部この標準の ISA の program を受ける。
  - vendor 独自の ISA は **任意**。`ISA_ID` などで標準の ISA に加えて持てる（driver は使ってもよい）。
  - 2026-10-23 ユーザー（続けて）「ネイティブISAを直接送信も可能。」→ driver は標準の ISA の代わりに、その GPU の native（vendor）の ISA の program を直接送ってもよい（CP の JIT を通らない）。program ごとにどちらの ISA かを示す（`LOAD_PROGRAM` の flag か ISA の ID、p002 の ghci.md で決める）。
  - 標準の ISA の実装の仕方は自由: **Amaterrace は hardware がそのまま実行する**（native = 標準）。他の実装は **CP が受け取った標準の ISA を JIT して自分の native の ISA に変えてよい**（driver からは見えない）。
  - これで汎用の driver（zedBSD の `src/drivers/gpu/ghci/`）は ISA の backend を 1 つ（標準の ISA）持てば、どの GHCI の GPU でも shader まで動かせる。下の「決めない」の shader の ISA の行と、§3 の "ATS1" の扱い（標準の ISA の名前と符号化にするか、Amaterrace の native を標準として採るか）、§6.6 の `isa/ats1/` の名前は、p002 の `docs/ghci.md` の草稿と p008 で直す（Amaterrace の native の ISA を GHCI の標準の ISA として書く案が自然）。

- **決める（公開・版付き・vendor に依らない。amaterrace-gpu の `docs/ghci.md` が正本、GHCI 1.0）**: (a) Host Control Register（HCR）の配置と意味（§2.2）、(b) extended capability の list（DC・debug・vendor の register の見つけ方）、(c) command ring と packet の封筒と標準の opcode（§2.4）、(d) state register file のうち **Vulkan の固定機能に当たる標準の領域**（§2.3）、(e) fence・割り込み・fault・watchdog・aperture・quiesce・reset の約束（§2.5〜§2.7）、(f) memory の model（32 bit の GPU address、aperture、将来の MMU の capability）、(g) CP の状態遷移（§2.6）と ring の約束（§2.7）。
- **決めない（vendor、`VENDOR`・`DEVICE`・`ISA` の ID で識別）**: shader の ISA と binary の形と **shader の実行の model**（register の番号、constant bank、I$、warp の数。§3 は Amaterrace の "ATS1" の文書 `docs/ats1.md` の内容で、GHCI は「属性 k は ISA の入力 slot k」「fragment の入力は ISA が決める」「program の base と word 数」「constant の領域 k」とだけ言う）、texture の tile の並び（capability で「linear だけ」「4×4 tile」を名乗る）、vendor の opcode（0x80〜0xFF）、vendor の state（0x1E0〜0x1FF）、vendor の register（extended capability `VEND`。Amaterrace は NWARPS・NREGS・NPHYS・PACKED をここに置き、ISA の backend が読む）。
- 汎用の driver（zedBSD の `src/drivers/gpu/ghci/`、§6）は HCR・ring・fence・割り込み・memory・標準の state・標準の opcode・DC の capability を扱い、**ISA の ID で compiler の backend を選ぶ**（最初は "ATS1" だけ）。ISA の backend が無い GPU は BLT と DC だけで attach する（display + 2D の device として使える。Vulkan は出さない）。**「汎用」の意味はここまで**: host interface・BLT・DC は汎用、shader は ISA の backend が要る（README-additions の文もこの形）。
- 版: HCR の `GHCI_VERSION` の major が変わる時だけ互換を壊す。minor の追加は末尾の register と新しい capability と opcode だけ。zedBSD の driver は major 1 だけを受ける。R19: v1 は Amaterrace が実装する物だけを標準に書く。

### 2.2 Host Control Register（HCR、64 KiB の窓の中の offset、32 bit、little endian）

窓: ZYBO の GPU の bitstream は GP0 `0x43C00000`（devcfg の固定の窓、64 KiB、IRQ_F2P[0] = GIC 61）、ZYBO の DC だけの bitstream も **同じ窓の offset 0**（DC だけの GHCI、§5.1）と IRQ_F2P[0]。KC705 は PCIe の BAR0（**128 KiB**: +0x00000 HCR、+0x10000 DC の register）と JTAG。slave は **17 bit** を解き、範囲の外は 0 を返す（64 KiB の alias で二重に見えない）。

| offset | 名前 | R/W | 中身 |
| --- | --- | --- | --- |
| 0x000 | GHCI_MAGIC | R | `0x47484349`（"GHCI"） |
| 0x004 | GHCI_VERSION | R | [31:16] major（1）、[15:0] minor（0） |
| 0x008 | VENDOR_ID | R | FourCC。Amaterrace は `0x414D4154`（"AMAT"） |
| 0x00C | DEVICE_ID | R | FourCC。`0x41544731`（"ATG1"）。DC だけの instance は `"ATDC"` |
| 0x010 | ISA_ID | R | FourCC。`0x41545331`（"ATS1"）。0 = shader 無し |
| 0x014 | DEVICE_VERSION | R | vendor の実装の版（major.minor） |
| 0x018 | EXTCAP | R | extended capability の list の先頭の offset（byte、0 = 無し）。各 entry は 2 word: word0 `{[7:0] id, [15:8] cap の版, [31:16] 次の entry までの dword 数（この entry からの相対、0 = 終わり）}`、word1 = cap の register の offset（byte、窓の先頭から）。id: 1 `DISP`（DC、§5）、2 `DBG`（JTAG の bridge の有無）、3 `PERF`、4 `VEND`（vendor の register）、5 `MMU`（将来） |
| 0x01C | GCLK_HZ | R | core の clock。driver が timeout を秒に直す |
| 0x020 | CAPS | R | [0] CP と ring が有る、[1] BLT、[2] shader（`ISA_ID` ≠ 0）、[3] DC がこの窓に有る（`DISP` の entry と対）、[7:4] log2(warp の lane 数)（Amaterrace は 5）、[8] TU bilinear、[9] ROP stencil、[10] `WAIT_VSYNC` 可、[11] texture の tile（0 linear、1 4×4）、[15:12] 標準の state の版、[16] MMU、[23:20] log2(memory の line の byte)（5 = 32、6 = 64） |
| 0x024 | CAPS2 | R | [7:0] queue（ring）の数（v1 は 1）、[15:8] shared memory KiB、[23:16] constant の領域の word/16、[27:24] texture の slot の数/4（v1 は 4 = 16 slot）、[31:28] 頂点の属性の数/4（4 = 16） |
| 0x028 | SCRATCH | RW | 32 bit（疎通） |
| 0x02C | COUNTER | R | `gclk` の数（free running） |
| 0x030 | CTRL | RW | [0] RUN、[1] RESET（W1S、§1.3 の順で self-clear）、[2] HALT、[3] STEP（HALTED で 1 command）、[4] WDT_EN。状態遷移は §2.6 |
| 0x034 | STATUS | R | [0] CP busy、[1] BLT busy、[2] VF busy、[3] RX busy、[4] ROP busy、[5] TU busy、[6] SU busy（OR）、[8] AXI_IDLE（GPU の client の outstanding が 0）、[9] FAULT、[10] HALTED、[11] RING_EMPTY、[12] MEM_READY（KC705: MIG の calibration 完了。ZYBO: 1）、[13] AXI_STUCK、[14] QUIESCED（新しい発行を止めている）、[19:16] 状態（§2.6 の番号） |
| 0x038 | IRQ_STATUS | R/W1C | [0] FENCE、[1] FAULT、[2] RING_EMPTY、[3] WATCHDOG、[4] DBG_DONE、[5] SOFT（IRQ_SET で立てる）、[6] HALTED（HALT の完了） |
| 0x03C | IRQ_ENABLE | RW | [6:0]。IRQ の線 = OR(IRQ_STATUS & IRQ_ENABLE)（level、plio と同じ約束） |
| 0x040 | IRQ_SET | W1S | [5] |
| 0x044 | WDT_CYCLES | RW | 1 command の上限の cycle 数（既定 2^28 ≈ 2.7 s @ 100 MHz） |
| 0x050 | RING_BASE | RW | ring の GPU address（32 byte 揃え）。RUNNING では書けない（無視、FAULT 種別 7） |
| 0x054 | RING_SIZE | RW | log2(byte)（10〜20）。同上 |
| 0x058 | RING_WPTR | RW | dword の index（ring の中、mod size）。書くと doorbell（§2.7） |
| 0x05C | RING_RPTR | R | CP が **解き終えて block に渡した**（parse 済みの）dword の index。非同期の block の完了は別（`WAIT_IDLE`・`FENCE`） |
| 0x060 | APERTURE_BASE | RW | GPU が触ってよい address の範囲の先頭（32 byte 揃え）。**全部の AMB の要求がここで検査される**。RUNNING では書けない |
| 0x064 | APERTURE_LIMIT | RW | 範囲の終わり（含まない） |
| 0x068 | FAULT_ADDR | R | 最後の fault の address |
| 0x06C | FAULT_INFO | R | [7:0] client の番号、[8] write、[15:12] 種別（1 aperture、2 未知の opcode、3 ring の誤り、4 watchdog、5 AXI の SLVERR/DECERR、6 RUN 中の KICK、7 RUNNING での禁止の書き込み）、[23:16] 種別 1〜5 の時の block、[31:24] JOB_TAG（§2.4: fault した仕事の tag。CP が検出した時はその command の tag、非同期の時は block が持つ tag） |
| 0x070 | FENCE_LAST | R/W | 最後に書いた fence の値。**reset の後に driver が「最後に完了した値」を書き戻す**（§2.5） |
| 0x080〜0x08C | PERF0〜3 | R | busy の cycle の合計（標準の意味は「busy の cycle」4 本まで。Amaterrace: SU、RX、TU、MC）。`PERF_CTRL` 0x090 [0] で clear |
| 0x0A0 | DBG_ADDR | RW | AMB を通した memory の読み書きの address（4 byte 揃え、aperture の検査を受ける）。書くと read を始める（非同期） |
| 0x0A4 | DBG_DATA | RW | 読み: `DBG_STATUS.busy` が 0 の時の最後の read の値（slave は **待たせない**）。書き: `DBG_ADDR` へ write を始める |
| 0x0A8 | DBG_STATUS | R | [0] busy、[1] error（aperture の外）。完了で `IRQ_STATUS.DBG_DONE` |
| 0x0F00 | KICK | W | [0] BLIT、[1] DRAW、[2] DISPATCH（STATE の値で直接 kick。ring 無しの最初の試験と debug 用。IDLE と HALTED でだけ受ける。RUNNING では FAULT 種別 6） |
| 0x1000〜0x17FC | STATE[0..511] | RW | state register file の alias（§2.3）。IDLE・HALTED でだけ書ける（RUNNING は無視して種別 7）。ring の `SET_REG` と同じ場所 |
| 0x100〜 | EXTCAP の entry | R | `DISP`（KC705: +0x10000 を指す。ZYBO の DC だけの bitstream: +0x0200）、`DBG`、`PERF`（0x080）、`VEND`（0x0C0: Amaterrace の NWARPS・NREGS・NPHYS・PACKED・PT の大きさ） |
| 他 | — | R | 0（書き込みは無視、応答は OKAY） |

### 2.3 state register file（STATE[0..511]。0x000〜0x1DF は GHCI の標準、0x1E0〜0x1FF は vendor）

| index | block | 中身（32 bit の word。float は IEEE の bit） |
| --- | --- | --- |
| 0x000〜0x01F | BLT | 0 OP（[1:0] 0 FILL、1 COPY、2 TILE（linear → vendor の tile）、3 UNTILE。[4] blend over。[9:8] bpp（0 = 32、1 = 16）。[12] COPY の向き）、1 DST_BASE、2 DST_STRIDE（byte）、3 SRC_BASE、4 SRC_STRIDE、5 DST_XY、6 SRC_XY、7 SIZE、8 COLOR、9 CLIP_MIN、10 CLIP_MAX |
| 0x020〜0x04F | VF | 0 TOPOLOGY（[2:0] 0 points、1 lines、2 line strip、3 triangles、4 triangle strip、5 triangle fan）、1 INDEX_BASE、2 INDEX_FORMAT（0 無し、1 u16、2 u32）、3 FIRST（first vertex / first index）、4 COUNT、5 PRIM_RESTART（[0]）、6 INSTANCE_COUNT、7 FIRST_INSTANCE、8 VERTEX_OFFSET（base vertex、signed）、9〜15 reserved、16+2k ATTR[k]_BASE、17+2k ATTR[k]_DESC（[15:0] stride、[19:16] format（0 R32F、1 RG32F、2 RGB32F、3 RGBA32F、4 RGBA8_UNORM、5 RGBA8_UINT、6 RG16F、7 RGBA16F、8 R32_UINT、9 RG32_UINT）、[20] per-instance、[31:24] instance の divisor）、k < 16。**属性 k の行き先は ISA の入力 slot k**（Amaterrace: r_k） |
| 0x050〜0x06F | RX | 0〜5 VIEWPORT（float: x_scale、y_scale、x_off、y_off、z_scale、z_off）、6 SCISSOR_MIN、7 SCISSOR_MAX、8 CULL（[1:0] none/front/back、[2] front = CCW）、9 DEPTH_CLAMP、10 RAST_MODE、11 VARYING_COUNT（0〜32 word）、12 DEBUG_COLOR（shader 無しの定数色、p007）、13 PROVOKING（[0] 0 = first、1 = last） |
| 0x070〜0x08F | ROP | 0 RT_BASE、1 RT_STRIDE、2 RT_FORMAT（0 BGRA8、1 RGBA8、2 RGB565）、3 RT_SIZE、4 DS_BASE、5 DS_STRIDE、6 DEPTH（[0] test、[1] write、[4:2] func、[5] D24S8、[6] D32F）、7 STENCIL_FRONT、8 STENCIL_OPS、9 STENCIL_BACK、10 BLEND（版 1 と同じ bit）、11 BLEND_CONST、12 WRITE_MASK |
| 0x090〜0x18F | TU（**16 slot × 16 word**） | 16s+0 BASE（level 0）、+1 SIZE（[15:0] w、[31:16] h）、+2 LEVELS（[3:0] mip の数 ≤ 13）、+3 SAMPLER（[1:0] wrap u、[3:2] wrap v、[4] mag linear、[5] min linear、[6] mip linear、[7] compare、[10:8] compare func、[12] format）、+4〜+15 MIP_OFFSET[1..12]（level 0 からの byte の offset。driver が計算） |
| 0x190〜0x1AF | program | 0 VS_BASE、1 VS_WORDS、2 FS_BASE、3 FS_WORDS、4 CS_BASE、5 CS_WORDS、6 CONST_BASE、7 CONST_WORDS（constant の領域 0。ISA が意味を決める）、8 LDS_BYTES、9 WG_SIZE（[7:0] x、[15:8] y、[23:16] z）、10 VS_OUT_WORDS（position の後の varying の word 数） |
| 0x1B0〜0x1DF | reserved | 0 |
| 0x1E0〜0x1FF | vendor | Amaterrace v1 は 0x1FF を DBG_MARK に使う |

### 2.4 command（ring の中、32 bit の dword の列。標準の opcode は 0x00〜0x7F、vendor は 0x80〜0xFF）

```
header: [31:24] opcode  [23:16] flags  [15:8] JOB_TAG  [7:0] n = payload の dword 数（≤ 255）
```

| opcode | 名前 | payload | 意味 |
| --- | --- | --- | --- |
| 0x00 | NOP | n 個（読み飛ばす） | ring の端の padding（§2.7） |
| 0x01 | SET_REG | (index, value) × n/2 | STATE[index] = value。index ≥ 512 は FAULT 種別 3 |
| 0x02 | BLIT | 0 | STATE の BLT の範囲で 1 回。flags[0] = 1 なら終わりまで CP が待つ |
| 0x03 | DRAW | 0 | VF を kick。開始の時に D$ と T$ を無効化（下） |
| 0x04 | DISPATCH | 3（gx、gy、gz） | compute。同上 |
| 0x05 | FENCE | 2（addr、value） | **完全な fence**: (1) 先行する全部の command（非同期の BLIT/DRAW/DISPATCH を含む）が完了し、ROP の write-combine を flush し、全部の書き込みの BRESP が返った後に、(2) `value` を `addr`（4 byte 揃え、aperture の中）に書き、(3) **その書き込みの BRESP の後に** `FENCE_LAST` を更新し、flags[0] = 1 なら `IRQ_STATUS.FENCE`。軽い fence は v1 に無い |
| 0x06 | WAIT_IDLE | 0 | flags = 待つ block の mask（0 は全部）。block が idle（ROP は cache が空）になるまで CP が止まる |
| 0x07 | LOAD_CONST | 3（領域 k、addr、words） | constant の領域 k に DMA。実行中の warp が無い時だけ（CP が `WAIT_IDLE(SU)` を暗黙に挟む） |
| 0x08 | LOAD_PROGRAM | 1（stage） | 「stage の program が変わった」の印: I$ を無効化 |
| 0x09 | WAIT_FENCE | 2（addr、value） | memory の `addr` の word が `(int32)(mem − value) ≥ 0` になるまで poll（64 cycle ごと）。watchdog の対象 |
| 0x0A | PERF_RESET | 0 | |
| 0x0B | WAIT_VSYNC | 0 | CAPS[10] の時だけ。DC の vsync の pulse を待つ |
| 0x0C | INVALIDATE | 0 | flags の mask: [0] D$、[1] T$、[2] constant、[3] I$。DRAW/DISPATCH の開始時に [1:0] を暗黙に行う（direct map の valid の flash clear、1 cycle）。CPU・BLT・他の SU が書いた後に明示に使う |
| 0x80 | DRAW_RAW（vendor、Amaterrace） | 9（3 頂点 × x, y, z の float） | VF と SU 無しで RX に 1 つの三角形（p007 の試験と debug） |
| 0x81 | DBG_MARK（vendor） | 1 | STATE[0x1FF] = payload |
| 他 | — | — | FAULT（種別 2）、CP は FAULTED |

- JOB_TAG: driver が仕事（session・submit）ごとに付ける 8 bit。BLIT/DRAW/DISPATCH を受けた block は tag を持ち、非同期の fault の `FAULT_INFO[31:24]` に写す。v1 の回復は device 全体（§2.5）だが、tag で「どの session の仕事で落ちたか」を log に出せる。
- 並列性: v1 は 1 本の ring と 1 つの queue（`CAPS2[7:0]` = 1、`gpu_command_submit.timeline` は 0 だけ）。BLT と DRAW の重なりは `WAIT_IDLE` を挟まない限り起きる（順序の規則は §6.4）。

### 2.5 fence・割り込み・失敗

- **fence の値**は 32 bit で driver が単調に増やす。wrap は `(int32)(a − b)` で比べる（`WAIT_FENCE` も同じ）。fence の書き込み先は driver が GPU の memory の中に取った「fence page」（4 KiB、ZYBO は uncached の alias で CPU が読む、§6.3）。**reset の後**: driver は fence page の word と `FENCE_LAST` に「最後に完了した値」を書き戻してから `RUN` にする（比較が狂わない）。driver は 2^31 を超える前に（例 2^30 ごと）全部の pending を待って値を折り返す。
- IRQ: ZYBO は `IRQ_F2P[0]`（GIC 61、devcfg の固定）。GPU の bitstream では GPU、DC だけの bitstream では DC がこれを使う。KC705 は PCIe の MSI（§4.3 の表: user IRQ 0 = GPU、1 = DC。XDMA の channel の IRQ は別の vector）、JTAG の時は poll。level で、W1C の約束は plio と同じ。driver の handler は mask → 読んで W1C → unmask。
- **失敗と回復（v1 は device 全体）**: (1) aperture の外・AXI の error・watchdog・RUN 中の禁止の操作 → `FAULT`、CP は FAULTED、`HALT` と quiesce を自動で始める（§1.3）。進行中の block は `HALT` を見て止まる。driver は `FAULT_INFO`（block・tag）を log し、**`drv_gpu_report_error` → `recovery.fault` → `recovery.reset`**（`CTRL.RESET`、ring と fence page を作り直し、全 session の未完了の fence を error で完了）。「他の session は続ける」は **CP が parse の時に検出した誤り（種別 2・3・7）で、まだ何も block に渡していない時だけ**（その submit の session に `drv_gpu_report_session_error`、CP は `STEP` で次の header へ）。SU/ROP に入っていた他の session の仕事を黙って失う道は無い。(2) **watchdog**: 1 つの command が `WDT_CYCLES` を超える → FAULT 種別 4。driver 側も `execution_timeout_ns` で二重に見る。(3) `AXI_STUCK`: hardware では直らず、driver は device を失った物として扱う（`report_error`、ZYBO は `fpgaload` で書き直し、KC705 は PCIe の reset）。
- **何が起きないか**: GPU は aperture の外を決して読み書きしない。v1 は session ごとの隔離は無い（1 つの aperture を全 session が共有、R7）。

### 2.6 CP の状態遷移

| 状態（`STATUS[19:16]`） | 入る条件 | 受ける物 | 出る遷移 |
| --- | --- | --- | --- |
| 0 IDLE | reset の後、`RUN` = 0 | HCR の全部の書き込み、`KICK`、STATE の alias、`DBG` | `RUN` ← 1 → RUNNING |
| 1 RUNNING | `RUN` = 1 | `RING_WPTR`（doorbell）、`IRQ_*`、`SCRATCH`、`DBG`、`HALT`、`RESET`。**`RING_BASE/SIZE`・`APERTURE_*`・STATE の alias・`KICK` は無視して FAULT 種別 6/7** | `HALT` ← 1 → HALTING; FAULT → FAULTED; `RUN` ← 0 は今の command の後に IDLE（doorbell は保つ） |
| 2 HALTING | `HALT` ← 1 か FAULT | 読みだけ | 今の command（block の非同期の仕事を含む）が終わるか fault で止まったら quiesce → HALTED（`IRQ_STATUS.HALTED`） |
| 3 HALTED | HALTING の後 | IDLE と同じ + `STEP`（1 command）、`HALT` ← 0 で RUNNING へ | `RESET` → RESETTING |
| 4 FAULTED | FAULT | HALTING と同じ（自動で quiesce まで進む）。`STATUS.FAULT` = 1 | `RESET` だけ → RESETTING |
| 5 RESETTING | `RESET` ← 1（どの状態からでも。RUNNING からは HALTING を経る） | 読みだけ | `AXI_IDLE` → core の reset、MC の flush → IDLE。`RESET` の bit が落ちる |

- reset で **残る** register: `APERTURE_*`、`RING_BASE/SIZE`、`IRQ_ENABLE`、`WDT_CYCLES`、`SCRATCH`、STATE。**消える**: `RING_WPTR/RPTR`（0）、`IRQ_STATUS`、`FAULT_*`、`FENCE_LAST`（driver が書き戻す）、`CTRL.RUN`、block の内部（cache、PT、warp）。driver は reset の後に ring を先頭から使い直す。

### 2.7 ring の約束

- ring は `RING_SIZE` の byte の、dword の環。`WPTR`・`RPTR` は dword の index（0 ≤ x < size/4）。空: `WPTR == RPTR`。満: `(WPTR + 1) mod n == RPTR`（**1 dword を空ける**）。driver の空きは `(RPTR − WPTR − 1) mod n`。
- packet は環の端を跨がない: 残りに入らない時は driver が `NOP`（n = 残り − 1）で端まで埋めて先頭へ。CP は header の `n` が `WPTR` を越える時（driver の誤り）を FAULT 種別 3 とする（待たない。driver は **packet 全部を書いてから** `WPTR` を進める）。
- doorbell の前の CPU 側の順序: ZYBO は uncached（Normal non-cacheable）の書き込みの後、device の register の書き込みの前に **`dsb st`**（`kern_` の mmio の write が barrier を含むかは p006 で確かめ、含まなければ driver が明示に置く）。KC705 は BAR1（AXI-MM の posted write）の後に **BAR1 の最後の word を読み戻してから** BAR0 の doorbell を書く（posted write の追い越しを防ぐ）。
- CP は doorbell のたびに `RPTR` の line を **読み直す**（先読みした古い line を捨てる）。CP の先読みは `WPTR` まで。
- ring が満ちた時: driver は submit ごとに fence を打つので、最も古い未完了の fence の IRQ を待って空きを得る（`RING_EMPTY` の IRQ は使わない）。
- `RPTR` は「parse して block に渡した」位置。driver は `RPTR` で ring の再利用を判断してよい（packet の中身は CP が block に写し終えているので、`RPTR` より前は書き換えてよい。block が参照する memory（頂点、texture）は fence で守る）。

## 3. SU（Shader Unit）と Amaterrace の ISA "ATS1" の最初の案（vendor の文書 `docs/ats1.md` の内容）

### 3.1 全体

- **warp は 32 lane の scalar、16 bit packed 付き**（README）: 1 thread = 32 bit の scalar の register の列。各 lane は 32 bit の演算と、1 word を 2 つの 16 bit（fp16・int16）として扱う packed の演算（`*2` の命令、§3.4）を持つ。Vulkan の subgroup の幅 = 32。
- **物理の lane は parameter `NPHYS`（8/16/32）**: warp の 32 lane を `32/NPHYS` cycle で流す（NVIDIA の 32 の warp を 16 幅の SIMD で 2 cycle、AMD の 64 の wave を 16 幅で 4 cycle と同じ考え）。ISA・compiler・driver・API から見える幅は常に 32。**同じ binary が NPHYS に依らず動く**（p008 の受け入れ: NPHYS 8/16/32 で model と同じ結果）。
- **SM/CU に当たる 1 つの単位**: NPHYS の SIMD の datapath、`NWARPS` の常駐の warp を round-robin で発行する scheduler（1 warp の 1 命令が `32/NPHYS` cycle を占める）、BRAM の register file、小さな I$・D$、workgroup の shared memory（LDS）、constant bank、post-transform の buffer（PT）、primitive table。workgroup は 1 unit の中（`maxComputeWorkGroupInvocations` = 32 × NWARPS）。
- **exec mask は compiler が管理する**: warp ごとの `EXEC`（32 bit）を `EXEC_*` 命令で立て下げし、hardware は再収束を扱わない。共有の IR が if-converted で Boolean を 32 bit の all-ones/zero で持つ（`ir.h`）ので、ほとんどの分岐は `SELECT` で消え、`EXEC` が要るのは loop と「副作用の有る命令」だけ。**`KILL` の sticky bit は STG・ATOM・EXP・TEX の副作用を自動で抑える**（killed の lane は書かない。EXEC は変えない）。
- **待ちは小さな hardware の scoreboard で**（2026-10-23 ユーザー「数は少なくていいので、scoreboardを実装できないでしょうか。性能に大きく影響しますので」、版 2 の「register ごとの scoreboard を持たない」を改める）: warp ごとに、結果がまだ戻っていない長い命令（memory、texture、超越関数、除算）の宛先の register を数個（案: 4〜8 entry、p008 で BRAM・LUT を実測して決める）だけ覚え、発行の時に source・dst がそれに当たる命令だけを止める（RAW・WAW）。当たらない独立の命令は待たずに発行し、scheduler は止まった warp を飛ばして他の warp を発行する。entry が埋まった時は、次の長い命令を entry が空くまで止める。`WAIT cm, ct, cx`（class ごとの counter）は store の完了や memory の順序の待ちのために残し、compiler は値の待ちに WAIT を入れなくてよい。具体の entry 数・比べ方・I$ との関係は p008 の設計で決める（`docs/ats1.md` に反映）。
- 精度: fp32 は IEEE の形、丸めは最近偶数、denormal は flush。fp16 の packed も同じ規則。超越は 2 ulp 程度。整数は 32 bit（packed は 16 × 2）。

### 3.2 register と operand の符号化（8 bit）

| 符号 | 意味 |
| --- | --- |
| 0x00〜0x3F | r0〜r63（thread ごとの 32 bit。実際の数 `NREGS` は `VEND` の register。NREGS = 32 の構成では 0x20〜0x3F は FAULT） |
| 0x40〜0x7F | c0〜c63（constant bank の先頭 64 word。64 以降は `LDC`） |
| 0x80 | zero、0x81 one（1.0f）、0x82 one_i、0x83 LANE_ID、0x84 WARP_ID、0x85 EXEC、0x86〜0x88 WG_ID_X/Y/Z、0x89〜0x8B LOCAL_ID_X/Y/Z、0x8C LOCAL_INDEX、0x8D PRIM_ID、0x8E FRONT_FACING、0x8F NUM_WG_X、0x90 ONE_H2、0x91 KILLED（自分の lane の discard の bit を Boolean に） |
| 0x92〜0xFF | reserved |

- Boolean は IR と同じ「all ones = true、0 = false」の 32 bit で普通の register に置く。比較命令は Boolean を dst に書く。predicate register は持たない。

### 3.3 命令の形（64 bit 固定）

```
[63:56] opcode   [55:48] dst   [47:40] src0   [39:32] src1   [31:24] src2
[23:20] mod（[20] neg src0、[21] abs src0、[22] neg src1、[23] abs src1）
[19:16] sub（副種別: 比較の条件、atomic の種類、EXP の対象、packed の half の選択など）
[15:0]  imm16（memory の offset、EXP の index、分岐の相対の word 数（signed）、TEX の slot と level）
MOVI / 32 bit の即値を持つ命令: [31:0] = imm32
```

### 3.4 命令の一覧（v1）

| 群 | 命令 | 意味・latency |
| --- | --- | --- |
| 移動 | `MOV`、`MOVI d, imm32`、`SEL d, cond, a, b`、`LDC d, [a + imm16]`（constant bank を index で） | 1 |
| fp32 | `FADD`、`FSUB`、`FMUL`、`FFMA d, a, b, c`、`FMIN`、`FMAX`、`FFLOOR`、`FFRACT`、`FTRUNC`、`FROUND`、`F2I`（sub: s/u）、`I2F`（sub: s/u） | DSP48E1 の pipeline で 4〜6 |
| 16 bit packed | `FADD2`、`FMUL2`、`FFMA2`、`FMIN2`、`FMAX2`、`IADD2`、`ISUB2`、`IMUL2`、`FCMP2`（half ごとの 0xFFFF/0）、`SWAPH`。**乗算は half ごとに DSP 1 本（DSP48E1 の 25×18 に独立な 2 つの 11×11 は詰められない）= lane あたり DSP 2**。`PACKED = 0` の構成ではこれらは FAULT | 4〜6 |
| pack | `PACKH d, a, b`（2 つの fp32 → half × 2）、`UNPACKH d, a`（sub: low/high → fp32）。**`PACKED` に依らず必須**（IR の `PACK_HALF`/`UNPACK_HALF` の行き先。LUT の変換器） | 2 |
| 比較 | `FCMP d, a, b`（sub: lt/ge/eq/ne、NaN は IR の規則）、`ICMP`（sub: eq/ne/slt/sge/ult/uge） | 2 |
| 超越 | `RCP`、`RSQ`、`SQRT`、`EXP2`、`LOG2`、`SIN`、`COS` | 共有の 1 本（32 + 8 cycle）。class x |
| 整数 | `IADD`、`ISUB`、`IMUL`、`IAND`、`IOR`、`IXOR`、`INOT`、`SHL`、`SHR`、`ASR`、`IMIN`/`IMAX`（sub: s/u） | 1〜3 |
| 除算 | `UDIV`、`UMOD`、`IDIV`、`IREM` | 共有の 1 本（lane ごとに 34 cycle）。class x |
| 四分木 | `QDX d, a`（lane ^ 1 との差、fine）、`QDY d, a`（lane ^ 2、fine）、`QSWAP d, a`（sub: 0 lane^1、1 lane^2、2 lane^3、3 quad の lane 0 を 4 つに配る）。coarse の微分は `QDX` → `QSWAP sub 3` | 1（quad の 4 lane は常に同じ cycle、NPHYS ≥ 4） |
| memory | `LDG d, [a + imm16]`、`STG [a + imm16], b`、`LDS`/`STS`（shared）、`ATOM d, [a], b`（sub: IR の 11 種、`CMPXCHG` は src2。global は MC の atomic unit で直列に実行し、**この SU の D$ の該当 line を無効化**）、`FENCE`（sub: [0] global（書き込みの BRESP を待つ）、[1] shared、[2] acquire（**D$ を無効化**））、`BAR`（workgroup の全 warp の到達） | LDG は D$ hit で 4、miss で AMB の latency。class m |
| texture | `TEXL d..d+3, u, v, lod`（slot = imm16[4:0]）、`TEXB d..d+3, u, v, bias`（lod は quad の微分から TU が計算 + bias。`SAMPLE`・`SAMPLE_BIAS` の行き先。TU が `QDX/QDY` 相当を quad の u・v から取る）、`TEXF d..d+3, x, y`（整数の texel、level = imm16[12:8]）、`TEXSZ d, level`（level の w・h を packed の 16 bit × 2） | TU へ。class t |
| 制御 | `EXEC_SET a`、`EXEC_AND a`、`EXEC_OR a`、`EXEC_SAVE d`、`BRA`、`BRA_ANY`、`BRA_NONE`、`KILL a`（sticky）、`WAIT cm, ct, cx`、`END` | 1〜2 |
| 出力 | `EXP target, a`（target = imm16: 0x000+k POS の k、0x010+4v+c varying v の c、0x100+c COLOR0 の c、0x104 DEPTH。vertex の warp は PT へ、fragment の warp は ROP へ。EXP は非同期で `END` の時に揃う。killed の lane は書かない） | 1 |
| 入力 | VF が vertex の warp の r0.. に属性（slot k = r_k）を、RX が fragment の warp の r0 = b0、r1 = b1、r2 = b2（**perspective 補正済み、元の 3 頂点に対する barycentric**）、r3 = z、r4 = x、r5 = y を書いてから warp を始める。varying の補間は `LDA d, v, k`（自分の lane の primitive の頂点 v（imm16[1:0]）の word k（imm16[7:2]）を primitive table から。word 0〜3 は position（1/w を含む）、4〜 は varying）と `FMUL/FFMA` で compiler が組む。`noperspective` は compiler が `LDA` で 3 頂点の 1/w を取り `lin_i = b_i·w_i / Σ b_j·w_j` を計算する（稀）。flat は provoking vertex = **primitive table の頂点 0**（RX が `PROVOKING` の規則で並べ替える） | LDA 1 |

### 3.5 unit の内部と register file（U12）

- **register file の大きさ** = 32 lane × NREGS × NWARPS × 32 bit × 複製の数。3 つの source を同じ cycle に読むために **同じ内容を 3 つ複製**（各複製は BRAM の 2 port の 1 つを書き込み、1 つを読みに）。lane の bank は物理の lane ごと（NPHYS 個）で、1 つの bank が `32/NPHYS` 個の architectural な lane を持つ。候補:

| 構成 | NREGS | NWARPS | bit（×3 複製） | BRAM36（SU 1 つ、NPHYS 8〜16） | 板での意味 |
| --- | --- | --- | --- | --- | --- |
| A | 32 | 4 | 393 Kbit | **12** | 7010 の推し |
| B | 32 | 8 | 786 Kbit | 24 | 325T の候補（latency を隠す warp が多い） |
| C | 64 | 4 | 786 Kbit | 24 | 325T の推し（compiler の register の圧力が楽、N = 8 で 192 = 43%） |
| D | 64 | 8 | 1.57 Mbit | 48 | 325T で N = 8 だと 384（445 の 86%）。N = 4 なら可 |

  NPHYS = 32 では bank が 1 lane 分（NREGS × NWARPS word = 128〜512 word）で BRAM18 の最小の深さ（512 × 36）を下回り、**BRAM の数が 2 倍になる**（32 bank × 3 複製 = BRAM18 × 96 = BRAM36 × 48）。推し: **7010 は A、325T は C（NPHYS 16）**。複製を 2 つに減らす案（src2 を 1 cycle 遅れて読む）は p008 で LUT と BRAM を測って決める。
- **書き込み port の調停**（複製ごとに 1 本）: 優先は (1) ALU の書き戻し（毎 cycle NPHYS lane）、(2) LDG/LDC/TEX/超越/除算の戻り（戻りの FIFO に貯め、ALU の書き戻しが無い cycle か、issue を 1 cycle 止めて書く）、(3) VF/RX の入力（warp の slot が空の時だけ書くので競合しない）。
- **scheduler**: 毎 cycle、`ready` な warp を round-robin で 1 つ選び、`32/NPHYS` cycle 占める。pipeline は fetch（I$）→ decode → RF 読み（1）→ 実行（1〜6）→ 書き戻し。同じ warp の連続の発行は dst の依存を interlock。
- **I$**: 2 KiB（256 命令、32 byte line の direct map）。**D$**: 1 KiB（32 line、direct map、write-through、valid の flash clear で無効化）。coalescer: 32 lane の address を line ごとにまとめる（`32/NPHYS` cycle に分けて比較）。**LDS**: 4 KiB（BRAM36 × 1）、KC705 は 16 KiB。**constant bank**: 256 word（BRAM18 × 1）。
- **PT（post-transform の buffer）**: 32 頂点 × (4 + 32) word = 1152 word（BRAM36 × 1.3 → 2）。vertex の warp は **PT に 32 頂点分の空きを予約できた時だけ**始まる（§3.8）。
- **primitive table**: NWARPS × 8 primitive × 3 頂点 × 36 word（NWARPS 4 で 3456 word = BRAM36 × 4）。`LDA` は 1 cycle に NPHYS/4 個の primitive を読むので、**table を NPHYS/4 複製**（NPHYS 8 で 2 複製 = BRAM36 × 8、NPHYS 16 で 4 複製 = 16。§3.6 に入れた）。varying を 16 word に絞る ZYBO の構成は半分。
- **超越・除算の unit**: unit に 1 本ずつ。`RCP/RSQ` は table（BRAM18 × 1）+ Newton 1 段（DSP × 2）、`EXP2/LOG2/SIN/COS` は範囲の縮小 + 多項式（DSP × 4）。

### 3.6 大きさ（LUT/FF/BRAM36/DSP）の見積もり — **推定**、内訳から計算し直した（版 2）。実測で置き換える

内訳（LUT）: fp32 の FMA は物理 lane あたり DSP 3 + **LUT 400**（正規化・丸め。版 1 の 250 は楽観。幅は 300〜700）、整数 ALU LUT 150、packed の fp16 LUT +150 + DSP 2。超越 1.5k + DSP 4、除算 0.3k、scheduler・decode・interlock・sequencing 1.8k、D$ と coalescer 1.5k、LDS/const/EXP/LDA/PT の制御 0.6k。固定の block: CP 1.8k、BLT 1.0k、VF 1.5k（instancing 入り 1.7k）、RX 3.3k（DSP 15）、ROP 1.5k（DSP 8。stencil 無しで 1.2k）、TU 2.2k（DSP 12。最近傍だけで 1.4k、DSP 4）、MC 1.5k、regs 0.4k。

| 構成 | 中身 | LUT | BRAM36 | DSP | 7010 の % | 判断 |
| --- | --- | --- | --- | --- | --- | --- |
| 固定の block 全部（KC705 の形） | CP+BLT+VF+RX+ROP+TU+MC+regs | **13.4k** | 12 | 35 | 76% | SU 無しでも 7010 の 3/4 |
| SU、NPHYS 8、A、packed 無し | 8×550 + 1.5k + 0.3k + 1.8k + 1.5k + 0.6k | **10.1k** | 12 RF + 1 I$ + 1 D$ + 1 LDS + 0.5 const + 2 PT + 4 prim(16 varying、2 複製) ≈ 22 | 24 + 4 + 2 = 30 | 57% | — |
| SU、NPHYS 4、NWARPS 2、A' | 4×550 + 1.5k + 0.3k + 1.5k + 1.2k + 0.5k | 7.2k | 6 + 1 + 1 + 1 + 0.5 + 2 + 2 ≈ 14 | 12 + 6 = 18 | 41% | — |
| ZYBO 全部（NPHYS 4、TU 最近傍、stencil 無し） | 13.4k − 0.8k − 0.3k + 7.2k | **19.5k** | 26 | 45 | **111%** | **入らない** |
| **ZYBO bitstream 1「2D」** | CP+BLT+MC+regs | 4.7k | 5 | 0 | 27% | p003・p004 |
| ZYBO bitstream 1 の **実測**（p002、Vivado 2026.1、xc7z010clg400-1、配線後） | CP の stub（ring 無し）+ BLT の骨（32 bpp の FILL/COPY、clip）+ MC（client 2）+ regs | **2.45k**（regs 0.28k、CP 0.60k、BLT 1.39k、MC 0.17k）、FF 2.63k | 0.5（STATE の BRAM18） | 0 | 14% | 100 MHz で WNS +0.473 ns。[phase002](phase002/phase.md) |
| **ZYBO bitstream 2「DC」** | DC（regs、dma、tgen、VGA/TMDS、I2C 無し） | 2.0k | 3 | 0 | 11% | p005 |
| **ZYBO bitstream 3「raster」** | CP+MC+regs+RX+ROP（stencil 無し）+ VF（DRAW_RAW だけなら VF 無し） | 8.2k（VF 入り 9.9k） | 7 | 23 | 47〜56% | p007 |
| **ZYBO bitstream 4「compute」** | CP+MC+regs+SU（NPHYS 8、A） | 13.8k | 27 | 30 | **78%** | p008。閉じなければ NPHYS 4（10.9k、62%） |
| ZYBO「graphics + SU」（NPHYS 4、TU 無し、VF 入り） | 9.9k + 7.2k | 17.1k | 21 | 41 | 97% | **当てにしない**（p009 は Verilator と KC705） |
| KC705 unit（NPHYS 16、C、packed） | 16×700 + 1.5k + 0.3k + 1.8k + 1.5k + 0.6k | 16.9k | 24 RF + 2 + 1 + 4 LDS + 0.5 + 2 + 16 prim（4 複製）≈ 50 | 80 + 6 = 86 | — | — |
| KC705 N=4 | 4 × 16.9k + 13.4k + MIG 10k + PCIe/XDMA 8k + DC 2k + JTAG 0.3k | **101k（50%）** | 200 + 12 + 4 + 16 + 3 = **235（53%）** | 344 + 35 = **379（45%）** | 325T | 最初の目標 |
| KC705 N=8 | 8 × 16.9k + 上 + RX 2 本（+3.3k） | **172k（84%）** | 435（**98%**） | 723（86%） | 325T | **BRAM が限界**: N=8 は RF を B（NREGS 32）か複製 2 にするか、primitive table の複製を減らす（p015 で実測して決める）。N=6 なら LUT 138k（68%）・BRAM 335（75%） |

- 結論（R1・U17）: **7010 では GPU の全 block を 1 つの bitstream に載せない**。block ごとの bitstream で workflow と各 block を板で確かめ、全体の結合は Verilator と KC705。KC705 の本命は **N=4 から始めて N=6〜8 を実測で決める**（README の「複数の SU」は満たす。128 lane の目標は N=8 × 16）。
- p002 の実測（2026-10-23、[phase002](phase002/phase.md)）で直す係数: **BLT は 1.0k → 1.4k**（32 bpp の FILL/COPY と clip だけで 1.39k: 2 line の buffer と word 単位の shifter が大半。16 bpp・blend・tile を足す p003 で 1.8〜2.2k の見込み）、**MC は 2 client で 0.17k**（そのうち AXI master が 0.15k。client の間の mux は合成の最適化で BLT の側に吸収されて分けて測れない。1 client あたり line 幅の wdata の mux が要るので **推定** で 32 byte の line で約 0.1〜0.15k/client、N=8 の 22 client で 2.5〜3.5k。版 2 の 1.5k は少なめの恐れ。p015 で測る）、**regs 0.4k → 0.3k**、**CP は stub で 0.6k**（ring・fence・状態遷移の全部は p003・p004 で測る。1.8k は据え置き）。BRAM は 5 の見込みに対して 0.5（MC と BLT に FIFO の BRAM が無い）。bitstream 1 の全体は 4.7k の見込みに対して 2.45k（CP を 1.8k にしても 3.7k、21%）。SU の係数は p008 の最初の合成で直す。

### 3.7 board ごとの parameter と Vulkan の limit

| | ZYBO（bitstream 4 の SU） | KC705 |
| --- | --- | --- |
| NSU / NPHYS / NWARPS / NREGS | 1 / 8 → 4 / 4 → 2 / 32 | 4 → 6〜8 / 16 / 4 / 64 |
| LINE_BYTES / AMB_W / AXI3 | 32 / 64 / 1 | 64 / 512 / 0 |
| I$ / D$ / T$ / LDS / const / varying | 2 KiB / 1 KiB / —（TU 無し）/ 4 KiB / 256 word / 16 word | 2 KiB / 1 KiB / 4 KiB / 16 KiB / 256 word / 32 word |
| TU_BILINEAR / ROP_STENCIL / PACKED | — / 0 / 0（**U15**: README の「16-bit packed」からの逸脱。`PACKH/UNPACKH` は有る） | 1 / 1 / 1 |
| gclk | 100 MHz | 100 → 150 MHz |

**Vulkan 1.0 の最小の limit と v1 の未達（driver が報告する値。libvulkan の N1・N2 と同じ「規格に合わない点」として記録し、U8 の app が要る物から足す）**:

| limit / 機能 | Vulkan の最小 | v1 | 備考 |
| --- | --- | --- | --- |
| maxVertexInputAttributes | 16 | 16 | 版 2 で 8 → 16 |
| maxVertexOutputComponents | 64 | 128（32 word）KC705 / 64（16 word）ZYBO | |
| maxPerStageDescriptorSamplers / SampledImages | 16 | 16 | 版 2 で 8 → 16 slot |
| maxComputeWorkGroupInvocations | 128 | 128（NWARPS 4）/ 64（NWARPS 2、ZYBO）| ZYBO は未達 |
| maxComputeSharedMemorySize | 16 KiB | 16 KiB / 4 KiB（ZYBO） | ZYBO は未達 |
| mip の段 | 4096 で 13 | 13 | 版 2 で offset を 12 個に |
| instancing（instanceCount・firstInstance・vertexOffset・per-instance） | 必須 | 有り | 版 2 で VF に足した |
| indirect の draw/dispatch | 必須 | **無し** | driver が `ENOTSUP`。v2 で CP に `DRAW_INDIRECT`（memory の引数）を足す |
| cube / 3D / array / gather / compare の texture | 必須（cube・array） | **無し** | compiler が拒む。features で 0 |
| geometry / tessellation | optional | 無し | |
| spill（register の溢れ） | — | **拒む** | NREGS 32 の ZYBO で多い（R8） |
| texture の format | 多数 | RGBA8/BGRA8 だけ | |
| queue | 1 以上 | 1（graphics+compute+transfer の 1 family） | |

### 3.8 統一 shader の deadlock の回避、ROP の順序、clip（版 2 で追加）

- **warp の slot の予約**: SU の NWARPS の slot のうち **少なくとも 1 つは fragment の warp 専用**（`NWARPS_FRAG_RESERVE` = 1、KC705 は 2）。vertex の warp は残りの slot にだけ入る。
- **PT の予約**: VF は vertex の warp を始める前に PT に 32 頂点分の空きを予約する（予約できなければ VF は待つ: RX が PT を消費すれば空く）。RX は PT の三角形を primitive table に写し（clip を含む）、fragment の warp を出し終えたら PT の entry を返す。fragment の warp は ROP に出し終えたら slot を返す。環: VF → SU(vertex) → PT → RX → SU(fragment) → ROP → MC。**ROP は MC の書き込みの受理だけに依存し、MC は ROP を優先する**ので ROP は必ず進む → fragment の slot が空く → RX が進む → PT が空く → vertex の warp が進む。deadlock 無し。
- **頂点の再利用**: v1 は index の再利用の cache を持たない（同じ index は再び shade する）。post-transform cache は p015 の後。
- **ROP の順序**: RX は fragment の warp に **連番**（primitive の順）を付け、ROP は連番の順に退かせる（reorder の scoreboard: NWARPS × NSU entry。早く終わった warp の EXP の data は SU の出力 buffer（warp の slot に付属）に留まり、ROP が受けるまで slot を返さない）。複数の RX（p015）は画面を帯で分け、帯ごとに連番を持つ。
- **clip**: 近平面だけ geometric に clip（1 → 1〜2 三角形）。clip で生まれた頂点の varying は RX が補間して primitive table の新しい entry に書く（VARYING_COUNT 個の lerp、逐次）。遠平面は depth clamp、x・y は guard band（16.8 の固定小数の範囲）。barycentric は **元の 3 頂点に対して**（clip の頂点も元の頂点の線形結合なので、fragment から見れば一貫する）。provoking vertex は `PROVOKING` の規則で primitive table の頂点 0 に置く。

## 4. MC（Memory Controller）の抽象と host link

### 4.1 AMB（Amaterrace memory bus）と `atg_mc`

- **line は `LINE_BYTES`（32 か 64）**: ZYBO は 32（HP の 64 bit × 4 beat）、KC705 は **64**（MIG の AXI4 512 bit = 64 byte × 1 beat、DDR3 の BL8 × 64 bit = 64 byte と同じ。32 では帯域が半分になる）。client 側の口は 1 line 丸ごと: 要求 `{valid, write, addr[31:log2(LINE)], be[LINE−1:0], wdata[8·LINE−1:0], tag[7:0], ready}`、応答 `{valid, tag, rdata, error}`。cache の line と texture の tile（4×4 × 4 byte = 64 byte）は `LINE_BYTES` に合わせる（32 なら tile は 2 line）。atomic は別の小さな口 `{valid, op[3:0], addr[31:2], a, b}` → `{valid, old}`（MC の中の 1 本の read-modify-write の unit で直列に）。
- `atg_mc`: client の数 ≤ **32**（N=8 で CP、BLT、VF、SU × 8 × 2（D$、I$+const）、TU、ROP、DC = 22。`FAULT_INFO` の client は 8 bit）。arbiter は固定の優先の group（1: DC、2: CP の fence・ROP、3: 他は round-robin）。同じ client の要求は順序を守る。応答は tag で返す。**aperture の検査**はここで全 client に対して行い、外れた要求は実行せずに `error` を返して `FAULT` を立てる。quiesce と flush は §1.3。将来の MMU はここに入る。
- `am_axi_master`: AMB の line を AXI の burst にする（AMB_W = 64 なら INCR 4 beat、512 なら 1 beat）。parameter `AXI3`（AxLEN 4 bit、`WID`、burst ≤ 16）。outstanding は read 4・write 4。`SLVERR/DECERR` は `error`。quiesce の「新しい発行を止める・応答を待つ」はここ。
- ZYBO: HP0 は AXI3 64 bit（**HP の AXI3 の WID と 64 bit の WSTRB、FIFO の深さは UG585 を読んで p002 で固定する（未確認）**）。GP0 の slave は plio と同じ。**PS の DDR は CPU の cache と coherent でない**（WS286 §3.12）ので、driver は GPU の memory を uncached で扱う（§6.3）。
- KC705: MIG 7 Series（Vivado の IP、`mig.prj` の preset、AXI4 の slave 512 bit、`ui_clk` 200 MHz、DDR3 1 GB）。`am_axi_master`（AXI3 = 0、512 bit）を MIG の AXI4 に直結。`init_calib_complete` を `STATUS.MEM_READY` に。MIG の設定は TCL の `create_ip` で再生成し、生成物は repository に入れない。

### 4.2 DC の memory の口

- ZYBO: DC は `S_AXI_HP1` を自分の `am_axi_master`（read だけ）で読む。GPU と DC が別の bitstream なので共有の問題は無い。
- KC705: 1 つの `atg_mc` の client（優先の group 1）として読む。line の FIFO（2 行）が有れば優先だけで足りる。quiesce・reset の対象から外す（§1.3）。

### 4.3 KC705 の host link（案と推し）

| 案 | 中身 | 利点 | 欠点・risk |
| --- | --- | --- | --- |
| A. **PCIe の endpoint → x86 の zedBSD** | Vivado の「7 Series Integrated Block for PCI Express」（無償）+ 「DMA/Bridge Subsystem for PCIe（XDMA）」（無償）。BAR0 = HCR + DC（128 KiB、AXI-Lite）、BAR1 = GPU の DDR3 への窓（64 MiB）、DMA は XDMA の H2C/C2H、割り込みは MSI。zedBSD の側は `nvrtx` と同じ `drv_pci_driver_register` + BAR の map + `drv_pci_device_allocate_irqs`。**PCI の照合は class code**（0x038000: display controller、other）+ BAR0 の `GHCI_MAGIC` の読み（vendor/device の組の固定ではない。AHCI の class code の照合と同じ）。Amaterrace の subsystem は "AT"/"G1" | 既存の amd64 の zedBSD と PCI の枠組み。host の CPU が速い | 板を PC の x8/x16 slot に挿す（**U2**）。**FPGA を書き直すと PCIe の link が落ちる** |
| A'. **PCIe + Linux の host + VFIO で QEMU の zedBSD の guest**（**推し**） | 板を Linux の PC に挿し、`vfio-pci` で QEMU の amd64 の zedBSD の guest に渡す。FPGA の書き直し: guest を止め → host で `remove` → JTAG → root port の **secondary bus reset** → `rescan` → guest を起動。**電源投入の時に FPGA が未設定だと BIOS が BAR を割り当てない**（経験則、未確認）ので、(a) KC705 の linear（BPI）flash に bitstream を書いて電源投入で設定する（100 ms 以内の設定の規則）か、(b) host の kernel に `pci=realloc`（と bridge の窓の予約）。手順は `plan/tools/kc705/pcie-reload.sh` に | zedBSD の driver は A と同じ code。rescan と再起動の問題を Linux の host が吸収。T1 の既存の QEMU の運用に載る | VFIO の IOMMU の group の分離（chipset に依る）。DMA の address は guest の物理（IOMMU が翻訳。XDMA は 64 bit の address） |
| B. **soft CPU sukuna-risc + zedBSD の移植** | ユーザーの CPU を 325T に載せ、zedBSD を移植し（RISC-V なら BSP）、GPU を内部の AXI で繋ぐ | 板だけで完結。将来の形 | sukuna-risc の完成と zedBSD の移植が GPU の前に要る。GPU の bringup が CPU に直列 |
| C. **既存の soft RISC-V（VexRiscv 等、Sv32）+ riscv32 の BSP** | MMU 付きの RISC-V の soft core（MIT） | B より早い | 外部の core の導入。遅い CPU で kernel の中の compile が遅い |
| D. **JTAG だけ**（HCR への bridge、§4.4） | OpenOCD の `drscan` で register を読み書き、DDR は `DBG_ADDR/DATA` | PCIe も CPU も無しで MIG・CP・BLT・DC・SU を板で確かめられる。**KC705 の最初の milestone の道** | 遅い。本番の道ではない |

**推し**: **D で KC705 の最初の milestone（MIG + CP + BLT + DC、SU の compute）を通し、本番は A'**。A' の PC はユーザーの判断（**U2**）。B は sukuna-risc の進み次第で後から足す。C は A' が使えない時の予備。MSI の vector の表（A/A'）: XDMA の user IRQ 0 = GPU の IRQ の線、user IRQ 1 = DC、XDMA の channel の IRQ（H2C/C2H）はその後の vector（driver は `drv_pci_device_allocate_irqs` で 4 本を取る。**XDMA の vector の割り当ての規則は PG195 未読で未確認**、p013 で読む）。

### 4.4 KC705 の JTAG の bridge（`am_jtag_regs`、GHCI の `DBG` capability）

- BSCANE2 の USER2（IR 0x03）に 72 bit の DR `{cmd[7:0], addr[31:0], data[31:0]}`: cmd 1 = write、2 = read（次の scan で data が返る）、3 = status。`tck` → `gclk` は `am_sync` の握手。`am_axi_regs` の第 2 の master として多重化。
- host の道具: `plan/tools/kc705/jtag-reg.sh read OFF | write OFF VALUE | mem-read ADDR | mem-write ADDR VALUE`（`DBG_*` の非同期の形: ADDR を書く → STATUS を poll → DATA を読む）。

## 5. DC（Display Controller、`atg_dc`、別の IP）

### 5.1 中身

- `atg_dc_regs`（AXI の slave）: ZYBO の DC だけの bitstream は **devcfg の固定の窓 `0x43C00000` の offset 0 に GHCI の header（0x000〜0x024 の 10 word: `DEVICE_ID` "ATDC"、`ISA_ID` 0、`CAPS` = [3] だけ、`EXTCAP` → `DISP` の entry が +0x200 を指す）** を置き、IRQ_F2P[0]（GIC 61）を使う。汎用の driver は `CAPS[0]` = 0（CP 無し）を見て ring・aperture・`MEM_READY` に触らず、display だけの device として attach する。KC705 は GPU の窓の +0x10000（`DISP` の entry が指す）で、同じ device の display の ops になる（§6.5）。

| offset（DISP の register の先頭から） | 名前 | R/W | 中身 |
| --- | --- | --- | --- |
| 0x00 | DC_ID | R | `0x41544443`（"ATDC"） |
| 0x04 | DC_VERSION | R | |
| 0x08 | DC_CAPS | R | [0] VGA、[1] TMDS、[2] ADV7511、[7:4] 対応する format、[8] cursor（v1 は 0）、[11:9] mode の数（v1 は 2） |
| 0x10 | CTRL | RW | [0] ENABLE、[1] RESET（W1S）、[2] TEST_PATTERN、[5:4] FORMAT（0 BGRA8、1 RGBA8、2 RGB565）、[6] OUTPUT（0 VGA、1 HDMI） |
| 0x14 | STATUS | R | [0] running、[1] underflow（sticky）、[2] idle（ENABLE = 0 で DMA の outstanding が 0）、[15:4] 今の line |
| 0x18 | IRQ_STATUS | R/W1C | [0] VSYNC、[1] UNDERFLOW、[2] FLIP_DONE |
| 0x1C | IRQ_ENABLE | RW | |
| 0x20 | FB_BASE | R | 今 scan している framebuffer |
| 0x24 | FB_NEXT | RW | 次の vsync で取り込む（flip） |
| 0x28 | FB_STRIDE | RW | byte |
| 0x2C | FB_SIZE | RW | [15:0] w、[31:16] h |
| 0x30〜0x4C | H_ACTIVE/H_FP/H_SYNC/H_BP、V_* | RW | timing、`SYNC_POL`（0x50） |
| 0x54 | FRAME_COUNT | R | vsync の数（32 bit、DC の reset で 0。driver は 64 bit の software の counter に積む: display の `events` の sequence は 1 から始めて戻らない約束） |
| 0x58 | PCLK_SEL | RW | [0] 0 = 25.2 MHz（640×480）、1 = 74.25 MHz（720p）。BUFGMUX で `pclk`/`pclk5` の対を切り替え。他の mode は後に MMCM の DRP で |
| 0x60〜0x6C | I2C_CTRL/DATA/STATUS | RW | KC705 の ADV7511 の設定用の小さな I2C master（DC_CAPS[2] の時だけ） |

- `atg_dc_dma`: `FB_BASE` から 1 line ずつ読み、line FIFO（2 line、BRAM36 × 3）に積む。`ENABLE` = 0 で新しい要求を止め、outstanding が 0 で `STATUS.idle`（detach の quiesce、§6.2）。
- `atg_dc_tgen`: `pclk` で h/v の counter、`DE`・`HSYNC`・`VSYNC`。vsync で `FB_NEXT` を取り込み（`FLIP_DONE`）、`FRAME_COUNT`、`IRQ_STATUS.VSYNC`。
- 出力: **ZYBO VGA**（RGB 5-6-5 の抵抗の DAC。pin は Digilent の Zybo-Master.xdc から p005 で Values referenced）。**ZYBO HDMI**（TMDS、OSERDESE2、`TMDS_33`。720p の 742.5 Mb/s は **DS187 未読で未確認**: 640×480 から）。**KC705 HDMI（ADV7511）**: **UG810 未読で未確認**（pin の数、I2C の address、mux）。p012 の前に UG810（**U7**）。

### 5.2 zedBSD の側（§6.5）

- ZYBO（別々の bitstream）: DC は display だけの GHCI の device（`drv_gpu_display_ops` の query/mode/claim/release/present/wait/events、`scanout` の `query_device`/`constraints`/`import_image`（borrowed の physical pages を DMA の到達と cache の属性で検査: ZYBO は `gpu.mem` の領域の中の linear の image だけ受ける））。GPU の device は `share`（`export_resource`/`release`/`import_resource`/`get_scanout_backing`）を持つ。libvulkan の `VK_KHR_display` の WSI がそのまま使える。
- KC705: 1 つの device に display の ops も載せる（same-device scanout は普通の share の import で足りる、`gpu-scanout.h`）。

### 5.3 未確認の事と最初の確かめ

- ZYBO の HDMI の pin と bank、VGA の pin: Digilent の XDC を p005 で読む。KC705 の HDMI: UG810（U7）。
- 最初の確かめは `TEST_PATTERN`（memory を読まない）→ DDR の framebuffer（`DBG_ADDR/DATA` で塗る）。monitor の目視はユーザー（ユーザーの試験の一覧）。

## 6. zedBSD の側: 汎用の GHCI の driver、/dev の node、Vulkan の executor、compiler の backend、試験の道

### 6.1 置き場所と名前

- `src/drivers/gpu/ghci/`（prefix `ghci_`、Zlib、WS288 の範囲。**GHCI の汎用の driver**。Amaterrace に固有なのは `isa/ats1/` だけ）:
  - `ghci.c`（attach の共通: MAGIC/VERSION/CAPS の確かめ、EXTCAP の走査、`drv_gpu_register`、session の数の帳簿）、`zybo-attach.c`（PL の窓、§6.2）、`pci-attach.c`（KC705）、`hcr.h`（§2.2）、`packets.h`（§2.4。amaterrace-gpu の `include/ghci_packets.h` の写し、§7.2）。
  - `cp.c`（ring、doorbell、fence、IRQ の handler、状態遷移の driver 側、watchdog、fault の報告と reset）、`memory.c`（GPU の address の allocator: first-fit の区間の木、session の帳簿、遅延の解放の list と **回収の thread**）、`blit.c`、`native.c`（試験用、§6.4）、`state.c`（標準の state の word の組み立て）、`jobs.c`（§6.4 の jobs の ops）、`capset.c`。
  - `display/`（`display.c`・`display-irq.c`・`scanout.c`・`adv7511.c`）。
  - `isa/ats1/`（§6.6）。backend は `struct ghci_isa_backend {fourcc, compile, release, limits}` の表で `ISA_ID` から選ぶ。
  - `vk/`（§6.7）。
- `/dev/gpuN`: **空いている最小の番号**（`gpu.c` 669〜686 行）なので、driver も道具も `gpu_info.driver_name`（"ghci:AMAT:ATG1"・"ghci-dc:AMAT:ATDC"）で選ぶ。
- ZYBO の PL の memory の pool: `src/drivers/platform/zybo/pl-memory.c`（新規。boot の parameter `gpu.mem=64M` で `kern_pmem_alloc_limited`（連続の run、32 bit）を boot の早い段で取り、`kern_pmem_map_uncached` の alias を持つ。**devcfg の寿命で、GPU の device と DC の device の両方が借りる**。`fpgaload` で block が入れ替わっても pool は残る）。
- userland: 試験 `userland/tests/ghci-blit/`、`userland/tests/ghci-display/`、道具 `userland/base/ghcictl/`（**追加は Q1 に名前の確認**）。boot の parameter は `docs/` の boot parameter の文書に足す。

### 6.2 attach と detach: ZYBO（PL の窓）と KC705（PCI）

- ZYBO の **PL の block の登録**（`zynq-devcfg.c` の変更、WS286 の file だが完了した WS なので WS288 が変える。Q1 に知らせる）: `FPGA_ENABLE` の後に窓の offset 0 の magic を読み、登録された block の driver（`{magic, attach(window_phys, size, irq), detach(), busy()}`）の magic と合えば attach を呼ぶ（`DEVICE_ID` と EXTCAP の形も確かめ、同じ物が 2 回見えたら拒む）。**block の driver が attach している間**: devcfg は IRQ 61 の handler を `kern_irq_unregister` で外して block の driver に渡し、`FPGA_IRQ_WAIT` と `FPGA_REG_WRITE` は `EBUSY`（`FPGA_REG_READ` は可: `fpgareg read` で debug）。detach で戻す。
- **detach の状態機械**（`FPGA_PROGRAM`・`FPGA_DISABLE` の前。`drv_gpu_unregister` が最初の呼び出しで device を offline にする（`gpu.c` 775〜830 行）事を踏まえる）:
  1. devcfg が block の driver の `busy()` を呼ぶ。GPU の driver は自分の session の数（`open`/`close` で数える）が 0 でなければ `EBUSY` を返し、**unregister を呼ばない**。`fpgaload` は「GPU の session を閉じてから」と表示する。
  2. 0 なら GPU の driver は hardware を静める: DC の `ENABLE` = 0 → `STATUS.idle`、CP の `HALT` → `HALTED`・`AXI_IDLE`（§1.3。`AXI_STUCK` なら報告して進む: PL の書き直しが回復の道）。
  3. `drv_gpu_unregister` を呼ぶ。0 なら完了。`EBUSY`（1 と 3 の間に open された）なら device はもう offline なので、**その session が閉じるまで `drv_gpu_unregister` を retry**（devcfg の `FPGA_PROGRAM` は最大 5 s 待ち、超えたら `EBUSY` を返す。device は offline のまま次の retry で回収され、次の attach は新しい登録）。
  4. その後に devcfg が level shifter と reset に触る（outstanding の AXI が無い事を 2 で確かめている）。
  - **JTAG（openFPGALoader）での PL の書き直しは block が attach している間は禁止**（`pl-jtag.sh` は zedBSD を見られないので、運用の規則として `fpgaload --disable` を先に打つ。design の試験の script がこの順を守る）。
- KC705（A'）: PCI の class code 0x038000 で照合し、BAR0 の `GHCI_MAGIC` を読んで確かめる。BAR0 = HCR + DC（128 KiB、`DRV_GPU_MAPPING_DEVICE`）、BAR1 = DDR の窓（64 MiB）。XDMA の channel は `resource_read/write` と ring・program・const の転送に。MSI 4 本。
- attach の順: `GHCI_MAGIC`・`GHCI_VERSION.major`・`CAPS`・`ISA_ID`（backend が有るか）を確かめ → `CAPS[0]` なら `CTRL.RESET` → `MEM_READY` を 1 s 待つ → `APERTURE` → `RING_BASE/SIZE` → fence page と `FENCE_LAST` → `IRQ_ENABLE` → `RUN`。`CAPS[3]` なら display の ops を足す。

### 6.3 memory（所有権・寿命・coherency）

- ZYBO: PL の memory の pool（§6.1）= aperture。CPU は uncached の alias だけで触る（WS286 §3.12）。`resource_map` は `DRV_GPU_MAPPING_UNCACHED_RAM`。**HAL の変更は無い**（`kern_pmem_*` で足りる）。
- KC705: GPU の DDR3（1 GB）が aperture。host からは BAR1 の窓（先頭 64 MiB を host visible の heap、残りを device local として memory type を 2 つ報告）と XDMA。
- 所有: allocation は session が持ち、`close` で全部返す（GPU がまだ参照し得る物は `drain` で待つ）。ring・fence page・staging は device。
- 寿命の規則: 参照を含む command の fence が signal するまで free しない。`resource_destroy` は「最後の fence ≤ 完了した値」を確かめ、違えば遅延の list に積む。**回収は IRQ の handler ではなく回収の thread**（handler は完了した fence の値を更新して thread を起こす。thread が allocator の mutex を取って返し、`drv_gpu_capacity_changed` を呼ぶ）。

### 6.4 drv_gpu との結び（capability・jobs・capset・command の道）

- **capability**（`drv_gpu_register` の検査（`gpu.c` 1024〜1045 行）と libvulkan の要求（`context.c` 124 行、`instance.c` 732 行）を満たす組）: GPU の device は `RESOURCE | CAPSET | BLOB | TRANSFER | COMMAND | MAPPING | NOTIFICATION | FENCE | JOB | JOB_CAPACITY | SHARE | ALLOCATION_SHARE`（KC705 は + `DISPLAY | DISPLAY_EVENTS`）。DC だけの device は `RESOURCE | TRANSFER | MAPPING | BLOB | SHARE | ALLOCATION_SHARE | DISPLAY | DISPLAY_EVENTS`。
- **ops**: `commands.submit/drain`（submit = packet を ring に書いて fence を結ぶ、drain = session の pending の fence を全部待つ）、`jobs.reserve/commit/cancel/capacity`（reserve = ring の空き（packet の最大の大きさ）と fence の番号を確保して `drv_gpu_completion` を持つ、commit = doorbell、cancel = 通常は slot を返す、FAULT の cancel = `report_error` の道、capacity = ring の空きの packet の数）、`recovery.stop_begin/stop_poll/fault/reset`（§2.5）、`share.*`、`display.*`（KC705 と DC）、`scanout.*`（DC）。
- **capset**: `get_capset` は powervr の `runtime_capset`（`render-runtime.c` 175 行）と同じ形: id 4、176 byte、wire 1、XML の版は libvulkan の `VULKAN_PROTOCOL_XML_VERSION`、timelines ≠ 0、160 byte に vendor の magic、164 byte に flag `OPAQUE | STRICT_QUEUE | QUIESCE`（libvulkan が native の device を作る条件）。**libvulkan は変えない**。
- **native の packet（試験用）**: `GPU_OP_GHCI_NATIVE`（`GPU_OP_OWN_FIRST` の後、**0x10100〜。番号は Q1 に確認**）。**build の option `CONFIG_GHCI_NATIVE`（試験の image だけ）と root だけ**に限り、driver は opcode の白表（NOP/SET_REG/BLIT/FENCE/WAIT_IDLE/INVALIDATE）と `SET_REG` の index の白表（BLT の範囲だけ）で検査し、address の word は aperture の中かを見る。それ以外は `EINVAL`。`userland/tests/ghci-blit/` が使う。compositor は使わない。
- 並行性と lock: ring への書き込みは 1 つの mutex（`cp_lock`）。IRQ の handler は spin で `completed_fence` と pending の list を守り、`drv_gpu_complete` は spin の外。
- 失敗: §2.5（v1 は device 全体の fault → reset。parse の誤りだけ session の error）。
- **BLT と ROP の順序（driver の規則）**: render pass の clear（BLT の FILL）の後、DRAW の前に `WAIT_IDLE(BLT)`。copy（BLT）が draw の結果を読む前に `WAIT_IDLE(ROP)`。CPU・BLT が書いた texture・buffer を shader が読む前に `INVALIDATE(D$|T$)`（DRAW/DISPATCH の暗黙の無効化で足りる場合が多い）。pipeline barrier は `WAIT_IDLE` + `FENCE`。

### 6.5 display device

- ZYBO: `display/display.c` が display だけの device を登録（`drv_gpu_unregister` できる寿命。bcm2711 の static の形は取らない）。`query`（1 output、mode は 640×480@60 と 1280×720@60）、`mode`（timing と `PCLK_SEL`）、`claim/release`（lease は 1 つ）、`present`（`FB_NEXT`）、`wait`・`events`（64 bit の software の sequence、VSYNC で進める）。`scanout.import_image` で GPU の device が export した allocation（pool の中の linear の image）を scan に使う。
- KC705: 1 つの device に display の ops（same-device の share の import）。
- cursor・overlay は無し。

### 6.6 compiler の backend（`isa/ats1/`、IR → ATS1）

- 入力は共有の `struct drv_gpu_shader_ir`。SPIR-V の front end は書かない。
- **IR の op の対応表（全部）**:

| IR | ATS1 |
| --- | --- |
| CONST / ICONST / BOOL | `MOVI` |
| LOAD_INPUT（vertex） | 属性の slot k = r_k（VF が書く）。`MOV` |
| LOAD_INPUT（fragment） | `LDA` × 3 + `FMUL` + `FFMA` × 2（flat は `LDA v0`、noperspective は §3.4） |
| STORE_OUTPUT | `EXP` |
| LOAD_PUSH / LOAD_UBO | `c` の operand（64 word まで）か `LDC` |
| FADD FSUB FMUL FNEG RSQ SIN COS RCP SQRT EXP2 LOG2 FABS FLOOR FRACT FMIN FMAX FTRUNC FROUND_EVEN | 同名（FNEG/FABS は mod の bit） |
| FLT FGE FEQ FNEU | `FCMP`（sub） |
| AND OR NOT SELECT | `IAND IOR INOT SEL` |
| KILL | `KILL` |
| IADD ISUB IMUL INEG UDIV UMOD IDIV IREM IAND IOR IXOR INOT SHL SHR ASR | 同名 |
| F2I / I2F（signed, unsigned）、UGE…IEQ INE 等の整数の比較 | `F2I`/`I2F`（sub）、`ICMP`（sub） |
| MOVE / LOOP_BEGIN / LOOP_END | `MOV` / label / `EXEC_SAVE` + 条件 → `EXEC_AND` + `BRA_ANY` + `EXEC_SET` |
| SKIP_BEGIN / SKIP_END（どの channel も使わない区間を飛ばす） | `EXEC_SAVE` + `BRA_NONE` + … + `EXEC_SET` |
| SAMPLE / SAMPLE_BIAS / SAMPLE_LOD | `TEXB`（bias 0 / bias）/ `TEXL` |
| DDX DDX_FINE DDY DDY_FINE | `QDX`/`QDY`（fine）、coarse は `QDX` + `QSWAP sub 3` |
| PACK_HALF / UNPACK_HALF | `PACKH` / `UNPACKH` |
| TEXTURE（LD / RESINFO / SAMPLE* の 2D・array 無し） | `TEXF` / `TEXSZ` / `TEXB`・`TEXL`。cube・3D・array・gather・compare は **拒む**（`ENOTSUP`） |
| LOAD_STORAGE / STORE_STORAGE / ATOMIC / STORAGE_SIZE | `LDG` / `STG` / `ATOM` / driver が constant bank に置く大きさを `LDC` |
| LOAD_SYSTEM | 特殊 operand（0x86〜0x8F） |
| LOAD_SHARED / STORE_SHARED / BARRIER / FENCE | `LDS` / `STS` / `BAR` / `FENCE`（sub） |
| LOAD_VERTEX_INPUT / EMIT_VERTEX / END_PRIMITIVE（geometry） | **拒む**（geometry stage 無し） |

- register の割り当て: linear scan、r0〜r(NREGS−1)。溢れは拒む（`ENOTSUP`）。
- scheduling: 長い命令の結果を使う命令を遠ざけて独立の命令で埋める（scoreboard が止める間を減らす）。`WAIT` は store の完了・memory の順序の時だけ。
- host の試験: `tests/ghci-ats1-compiler-host-test.c`（符号化の往復 + `tests/ats1-model.c`（写し）で実行して CPU の参照と比べる）。

### 6.7 Vulkan の executor（`vk/`）

- libvulkan は変えない。`vk/` は gpu-op の stream を解き、**GHCI の標準の state と opcode だけ**を使う（shader の binary は ISA の backend）。最初の目標は vkdemo の部分集合（§版 1 と同じ: instance/device/queue、memory、buffer・image（2D、RGBA8/BGRA8、D24S8）、shader module、descriptor set（UBO・sampled image・storage）、pipeline、render pass（1 subpass）、command buffer（bind・draw・draw indexed・dispatch・copy・barrier・clear）、fence/semaphore、swapchain は WSI）。
- 大きさ: 8〜12k 行。p011 の LW の不確かさが最も大きい。U8 で部分集合を固定。

### 6.8 試験の道（host → simulation → 板）

| 段 | 道具 | 何を確かめるか | 誰が |
| --- | --- | --- | --- |
| RTL の lint | `verilator --lint-only -Wall`（**U3**）。無ければ `iverilog -Wall` | 0 warning | 担当 |
| block の unit の TB | iverilog（`sim/tb/<block>_tb.v`、`PASS <name>`、`sim/run.sh NAME`） | AXI の slave/master、`am_afifo`、AMB の arbiter と quiesce、CP（**状態遷移、ring の wrap と満、FENCE の完全性（非同期の BLIT の後の fence）、FAULT と reset の順、EXTCAP**）、BLT、RX、ROP（**重なる 2 三角形の blend の順**）、SU（命令ごと、NPHYS 8/16/32 で同じ）、TU | 担当 |
| 全体の simulation | **Verilator**（C++ の harness: DDR の model、HCR、`ghci_packets.h`、ISA の model） | 各 milestone の板の場面（fill → DBG、fence → IRQ、三角形の PNG、compute）、**fault の注入 → reset → 次の仕事が動く**、**ring の wrap の負荷（満ちる・空ける）** | 担当 |
| Vivado の合成 | `vivado-build.sh`（IP 有りは `vivado-build-ip.tcl`）、ZYBO は Vivado（新設）か openXC7 | 実測の使用量、timing、DRC | 担当 |
| 板 ZYBO | `fpgaload` + `fpgareg`（`plan/tools/zybo/ghci-*.sh`）。SSH は WS289 の LAN が有る image なら `deploy.sh`。**生きた serial の判定と担当が JTAG を直に触る例外は WS286 の物（AGENTS.md）で、WS288 への拡張は U16** | p003〜p005・p007・p008 の板の受け入れ、p006 の `ghci-blit`・`ghci-display`、**detach/reattach（session を開いたままの `fpgaload` が EBUSY、閉じた後に通る）** | 担当、画面の目視はユーザー |
| 板 KC705 | `jtag-load.sh` + `jtag-reg.sh` | p012、p014 | 担当 |
| QEMU | GPU は無い。kernel 側の build と boot の回帰（armv7-virt の PNG、driver の無い platform を壊さない） | 各 Phase | T1 |
| VFIO の実機 | A' の QEMU の guest（SSH/QMP の PNG） | p013 以降 | T1 |

QEMU と実機の証拠は分けて書く。やっていない確認は「未実施」と書く。

## 7. repository の分割、license、bitstream の配布

### 7.1 何をどこに置くか

| 置き場所 | 中身 |
| --- | --- |
| **amaterrace-gpu** | `README.md`（ユーザーの物。書き足しの案は [proposed/README-additions.md](proposed/README-additions.md)）、`docs/ghci.md`（**GHCI 1.0 の正本**: §2）、`docs/ats1.md`（§3）、`docs/style.md`、`rtl/common/`、`rtl/gpu/{cp,blt,vf,rx,su,tu,rop,mc}/`、`rtl/dc/`、`include/`（`ghci_regs.vh`・`ats1_isa.vh`、C の `ghci_regs.h`・`ghci_packets.h`・`ats1_isa.h`）、`boards/zybo/`、`boards/kc705/`、`boards/kc705-sukuna/`（将来）、`sim/`、`tools/`、`LICENSE`。Vivado の project・IP の生成物・bitstream は commit しない |
| **zedBSD** | `src/drivers/gpu/ghci/`、`src/drivers/platform/zybo/pl-memory.c` と `zynq-devcfg.c` の変更、`include/uapi/gpu-op.h` の番号、`userland/tests/ghci-*`、`userland/base/ghcictl/`、`tests/ghci-*-host-test.c`・`tests/ats1-model.c`、`plan/tools/zybo/ghci-*.sh`・`plan/tools/kc705/jtag-reg.sh`・`pcie-reload.sh`、`plan/ws288/`。既存の `plio` と `kc705/bringup` は zedBSD に残す |
| **sukuna-risc** | CPU の RTL（WS288 の外） |

### 7.2 共有する header の規則

- `ghci_regs.h`・`ghci_packets.h`・`ats1_isa.h` は amaterrace-gpu が正本。zedBSD は写しを置き、先頭に「copied from amaterrace-gpu <commit>」と SPDX。`plan/tools/ghci-sync.sh` で `diff`（CI では走らせない）。ISA の model も同じ。GHCI の版の定数を header に持ち、driver が attach で比べる。

### 7.3 RTL の規約

- Verilog-2005（iverilog・Vivado・Verilator で通る部分集合）、`default_nettype none`、module 1 つ = file 1 つ、同期 reset、vendor の primitive は `boards/` と `rtl/common/xilinx/` だけ、lint 0 warning、plio.v と同じ形の先頭の comment。amaterrace-gpu の `docs/style.md`。Guardrail には 1 行（Q1）。

### 7.4 license（**U1**）

| 案 | 判断 |
| --- | --- |
| **Zlib**（推し） | zedBSD と同じ。写す header に壁が無い。仕様の文書も Zlib で揃える |
| CERN-OHL-P-2.0 | hardware 向け。写す header の dual の手間 |
| Apache-2.0 / Solderpad 2.1 | 特許の条項。混在の管理 |

- 参照: AMD/Xilinx の公開の文書と Vivado の board file だけ。他の GPU の RTL は読まないし写さない。GPL は参照しない。

### 7.5 AMD の IP を含む bitstream の配布（**U6**）

- MIG・PCIe・XDMA は代えない（ユーザー）。EULA を読むまで public の repository は RTL と TCL だけ、bitstream は release の添付も保留。

## 8. Phase と LW（8 LW ≈ 2〜3 時間。p001 はこの設計。版 2 で p013 の受け入れを縮め、p014 を新設）

| Phase | 内容 | 受け入れ（板で見る物） | 依存 | LW |
| --- | --- | --- | --- | --- |
| p002 | amaterrace-gpu の骨組み、`docs/ghci.md` 1.0 の草稿、`am_axi_regs`・`am_axi_master`（quiesce 付き）・`am_afifo`・`atg_mc`、TB の枠、Verilator の harness の骨、**bitstream 1（CP+MC+BLT）の合成で LUT を実測し §3.6 を直す** | host: TB PASS、lint 0 | p001、U1、U3 | 14 |
| p003 | CP v0（HCR、状態遷移、IRQ、STATE、`KICK`、非同期の `DBG`、EXTCAP）+ BLT + ZYBO の top（PS7、GP0、HP0）→ 板: `fpgareg` で fill/copy → `DBG` で DDR を照合。DDR の領域は DTB の `memory@0` を 448 MiB に | **最初の端から端**: `fpgareg` だけで fill/copy。`IRQ_SET` → `fpgareg wait` | p002 | 12 |
| p004 | ring + fence + 割り込み + fault + reset: `RUN`、ring の約束（§2.7）、完全な FENCE、watchdog、FAULT、`RESET` の順。板: `DBG` で ring を書き doorbell、`fpgareg wait` で FENCE、aperture の外で FAULT → RESET → 次の fill が通る | ring の fill → FENCE → IRQ → 値、**fault → reset → 回復**、wrap の TB | p003 | 12 |
| p005 | DC（bitstream 2）: VGA（先）→ TMDS、`TEST_PATTERN` → DDR の framebuffer。窓の offset 0、IRQ_F2P[0] | color bar の目視、framebuffer の目視、`fpgareg wait` で VSYNC | p002 | 14 |
| p006 | zedBSD: devcfg の block の登録と IRQ の受け渡し、`pl-memory.c`、`ghci.c`・`cp.c`・`memory.c`・`blit.c`・`native.c`・`state.c`・`jobs.c`・`capset.c`、display device と scanout、`ghci-blit`・`ghci-display`、host の試験（packet・allocator・fence の wrap・状態遷移の driver 側） | `ghci-blit` PASS（板）、`ghci-display` の画面（目視）、**detach/reattach の試験**、QEMU armv7-virt の回帰（T1） | p004、p005 | 24 |
| p007 | RX + ROP（bitstream 3、`DRAW_RAW`、`DEBUG_COLOR`、ROP の順序の scoreboard）→ DC | 三角形の画面、depth、重なる blend の順、Verilator の PNG | p004、p005 | 18 |
| p008 | SU v0 と ATS1（bitstream 4「compute」）: `atg_su`（NPHYS の sequencing、RF、書き込み port の調停、I$、D$、LDS、const、超越）、model、`ats1asm.py`、TB、`DISPATCH`。**U12 の確定**。板: vector add を `DBG` で | compute kernel が板で正しい、model と一致（NPHYS 8/16/32） | p004、U12 | 30 |
| p009 | VF + SU の vertex/fragment + PT + primitive table + `LDA` + TU（最近傍 → bilinear）+ deadlock の回避 | textured な三角形（**Verilator の PNG**。板は KC705 の p014） | p007、p008 | 26 |
| p010 | compiler の backend（zedBSD `isa/ats1/`）、host の試験 | host PASS | p008 | 20 |
| p011 | Vulkan の executor `vk/`（vkdemo の部分集合） | vkdemo の 1 frame の readback が参照と一致（**Verilator**。板は p014） | p006、p009、p010 | 48（±50%） |
| p012 | KC705: MIG、MMCM、`LINE_BYTES` 64、JTAG の bridge、CP + BLT + DC（HDMI は U7）、`vivado-build-ip.tcl` | `jtag-reg.sh` で fill → `DBG`、color bar | p004、p005、U7 | 20 |
| p013 | KC705 の host link A': PCIe + XDMA、BAR、MSI、`pci-attach.c`、`pcie-reload.sh`（SBR、rescan、flash か realloc） | **guest の zedBSD から `ghci-blit` PASS**（vkdemo は p014） | p006、p012、U2 | 24（**2026-10-23 後回し**: Future Work fw-kc705-pcie。当面 KC705 は USB JTAG） |
| p014 | KC705 の全 pipeline の bitstream（N=1: VF+SU+RX+TU+ROP、`PACKED`）、vkdemo を板で | **vkdemo が KC705 で参照と一致** | p009、p011、p012（JTAG の bridge。p013 は後回し） | 16 |
| p015 | 複数の SU（N=4 → 6〜8）、work の分配、RX 2 本、150 MHz、PERF、BRAM の実測で N を決める | N=4 で vkdemo、N≥6 で合成が閉じる | p014 | 18 |
| p016 | 規約の全文の見直し（C）、RTL の lint、`docs/ghci.md` 1.0 の確定 | 動作は不変 | p015 | 8 |

合計 304 LW（p011 の幅で 280〜328）。最初の端から端（p002〜p004）は 38 LW、画面まで（p005・p006）で 76 LW。依存の図: p002 → {p003 → p004, p005} → p006; p004 → p007 → p009; p004 → p008 → {p009, p010} → p011; {p004, p005} → p012 → p013 → p014 ← {p009, p011}; p014 → p015 → p016。循環は無い。

**ws.md の「進め方（案）」（DC → BLT → CP の順）からの変更（U14）**: CP + BLT を先にした理由は、`DBG_ADDR/DATA` で DDR を読めるので **kernel の driver も画面も無しに板で結果を照合できる**（DC が先だと framebuffer を CPU で塗る道が無く、目視だけになる）。DC は p005 で p003 と並行できる。

## 9. risk と U 項目

### 9.1 risk

| # | risk | 対処 |
| --- | --- | --- |
| R1 | **7010 に全 block が入らない**（§3.6: 全部で 111%、graphics+SU で 97%） | **block を分けた bitstream**（§3.6 の 1〜4）で板の workflow と各 block を確かめ、結合は Verilator と KC705（U17）。p002・p008 の実測で係数を直す。係数が 20% 良ければ「graphics + SU」を試す |
| R2 | fp32 の pipeline が 100 MHz で閉じない（7010-1） | DSP の pipeline を全段。閉じなければ ZYBO は 50 MHz（`GCLK_HZ`） |
| R3 | KC705 で 100 → 200 MHz（2026-10-23 README のユーザーの目標。150 MHz は版 2 の値） | p015 で。100 MHz でも N=8 で 25.6 GFLOPS |
| R4 | HP の AXI3 の細部（WID、WSTRB、outstanding）の誤り | p002 の TB の protocol checker、p003 の板で `DBG` を先に。UG585 を p002 で読む |
| R5 | PS の DDR の non-coherent で uncached の alias が遅い | ZYBO は workflow の板。cache を使う道は後 |
| R6 | 1 ring・1 queue | v1 の限界。p015 の後に複数 |
| R7 | GPU MMU が無く、session の隔離が無い | v1 は「1 user の板」。MMU は GHCI の capability で将来 |
| R8 | spill 無し・cube/3D 無し・NREGS 32 で shader が通らない | vkdemo から始め、`ENOTSUP` と log。§3.7 の未達の表 |
| R9 | p011 の規模（±50%） | U8 で部分集合を固定、powervr の `vk/` を型に |
| R10 | PCIe の link が FPGA の書き直しで落ちる、BIOS が BAR を割り当てない | A' の `pcie-reload.sh`（SBR、rescan）、flash からの設定か `pci=realloc` |
| R11 | AMD の IP の bitstream の配布 | U6 |
| R12 | KC705 の HDMI が未確認 | U7。p012 は `DBG` で受け入れ |
| R13 | ZYBO の TMDS の速さ | VGA を先に。640×480 から |
| R14 | （版 2 で解消: ZYBO は IRQ 61 だけを使う） | — |
| R15 | （版 2 で解消: 窓は 64 KiB のまま、DC だけの bitstream は offset 0） | — |
| R16 | 共有の header・仕様・RTL・driver の 4 つがずれる | `ghci-sync.sh`、版の定数の比較、p016 で照合 |
| R17 | Verilator が無い（U3 が否） | iverilog の全体の TB（遅い） |
| R18 | 32 lane の warp を NPHYS で分ける sequencing の誤り | NPHYS 8/16/32 で同じ model と比べる（p008） |
| R19 | GHCI の一般化が重くなる | v1 は Amaterrace の形だけを標準に。実行の model は vendor（§2.1） |
| R20 | 統一 shader の deadlock（§3.8）、ROP の順序 | 予約の規則と scoreboard。p007・p009 の TB |
| R21 | 325T の BRAM（N=8 で 98%） | p015 で RF の複製 2 か NREGS 32 か primitive table の複製の削減を実測で選ぶ。N=6 を許容 |
| R22 | detach の競合で device が offline のまま残る（§6.2 の 3） | retry と 5 s の上限。次の attach は新しい登録 |

### 9.2 U 項目（ユーザーの判断。各々に推し）

2026-10-23 ユーザー（U2）: PCIe は「フリーズしたときホストPCの再起動は必要ですか？」等の問いの後「了解です。安定してきたらPCIeにしましょう。PCをSwitchbotで電源ON/OFFはできますが、BIOSが停電後の自動電源ONができるかは不明です。」→ U2 は「当面 KC705 は USB JTAG だけ（構成も操作も JTAG の bridge）。GPU が安定したら PCIe（Alveo 型: USB JTAG で構成し PCIe で Linux から。flash の基本の bitstream か部分再構成の固定の領域で link を保つ）」。p013 は Future Work fw-kc705-pcie。README の目標「Synthesis goal is 200MHz on KC705 board and 800MHz for ASIC processes.」（ユーザー、c840a0a）により KC705 の gclk の目標は 200 MHz（p015、R3 の 150 MHz を改める）。


2026-10-23 ユーザー「U項目は推奨で承認します。」→ U1・U3〜U17 は推しで決定。U2 は同時の問い「KC705はUSB JTAGで運用できませんか？」への答えの後に確定（保留）。


| # | 問い | 推し |
| --- | --- | --- |
| U1 | amaterrace-gpu の license と GHCI の仕様の文書の license | **Zlib**（仕様の文書も） |
| U2 | KC705 の host link と PC | **A'**（Linux の PC + VFIO + QEMU の zedBSD）。PC は 10.0.30.5 の 13400F の desktop の 2 本目の slot（x8 以上が空いているか）。sukuna-risc は後から |
| U3 | host に Verilator（Debian の package、sudo）を入れる。cocotb は入れない | **入れる** |
| U4 | ZYBO の GPU の memory 64 MiB（p003 は DTB で 448 MiB、p006 からは `gpu.mem=64M` の pool） | **64 MiB** |
| U5 | 7010 の SU の構成を小さく（NPHYS 8 → 4、NWARPS 4 → 2、TU 無し、stencil 無し） | **可** |
| U6 | AMD の IP の bitstream の配布は EULA の確認まで保留 | **この規則で** |
| U7 | KC705 の HDMI: UG810 をユーザーが host に置く | **置いてもらう**。無ければ HDMI は別 Phase |
| U8 | Vulkan の v1 の部分集合 | **vkdemo → compositor の最小 → mview** |
| U9 | PCIe の照合は class code 0x038000 + BAR0 の magic。Amaterrace の subsystem は "AT"/"G1"、device ID は PCIe の IP の既定のまま | **この形**（vendor/device の固定の組にしない） |
| U10 | 名前: `src/drivers/gpu/ghci/`、`ghci_`、`isa/ats1/`、driver_name "ghci:AMAT:ATG1"/"ghci-dc:AMAT:ATDC"、`GPU_OP_GHCI_*` = 0x10100〜、`ghcictl`、`ghci-*` の試験、`pl-memory.c` | **この名前**（Q1 が番号と devfs を確かめる） |
| U11 | ZYBO の VGA を HDMI より先に | **VGA が先** |
| U12 | **register file**: 7010 は NREGS 32・NWARPS 4（BRAM36 12）、325T は NREGS 64・NWARPS 4（24/SU） | **この値**。複製 3 → 2 は p008 の実測で |
| U13 | GHCI の範囲（§2.1） | **この範囲** |
| U14 | ws.md の順（DC → BLT → CP）を CP + BLT → DC に変える（§8） | **変える** |
| U15 | ZYBO の SU で 16 bit packed の演算器を外す（README の「16-bit packed」からの逸脱。`PACKH/UNPACKH` は残す。KC705 は有り） | **外す**（7010 の DSP と LUT） |
| U16 | WS286 の例外（ZYBO の実機は生きた serial で判定、JTAG を使う Phase は担当が板を直に触る）を WS288 にも適用する | **適用する**（AGENTS.md の例外の行に WS288 を足すのは Q1） |
| U17 | 7010 では block を分けた bitstream で確かめ、全体の結合は Verilator と KC705（§3.6） | **この方針** |

HAL（`include/hal/hal.h`）の変更は **無い**。

## 10. 選ばなかった案と理由

| 案 | 理由 |
| --- | --- |
| 1. GPU MMU を v1 から | 7010 に入らない。aperture で「CPU の memory を壊さない」は守れる。GHCI の capability に場所を残した |
| 2. AXI Interconnect の IP | 7010 の LUT と simulation の model。自前の AMB |
| 3. predicate register と divergence の stack | compiler が exec を管理する（ユーザーの合意）。IR が if-converted |
| 4. register ごとの scoreboard | class の counter + `WAIT` の方が小さい |
| 5. 専用の補間の unit | `LDA` + FMA は小さく、varying の数が可変。性能が要る時に `IPL` |
| 6. SU が bilinear を命令で | 命令数 4 倍。ZYBO は TU 無し、KC705 は unit の bilinear |
| 7. tile-based の rendering | 7010 の BRAM。immediate mode から |
| 8. DC を GPU の中に | ユーザーの決定（別の IP）。KC705 では 1 つの driver の device に載せる（§6.5）|
| 9. cocotb | Verilator の C++ の harness と iverilog で足りる |
| 10. KC705 の host を soft CPU から | GPU の bringup が CPU に直列。PCIe + VFIO は既存の運用に載る |
| 11. 64 bit の fence・`WAIT_FENCE` の無し | 32 bit + `(int32)` + 折り返しの規則で足りる |
| 12. ring を複数に v1 から | p015 の後 |
| 13. bitstream を zedBSD の tree に | U6 と tree の分割 |
| 14. ACP で coherent に | L2 と SCU の前提が増える。HP + uncached |
| 15. warp の幅を板ごとに変える | README の「32-lane」。`32/NPHYS` cycle で流せば ISA は 1 つ |
| 16. vendor ごとの driver | README の GHCI の趣旨に反する。汎用の `ghci/` + ISA の backend |
| 17. 軽い fence（BRESP だけ） | 完了の意味が二重になり、driver の寿命の規則を誤る（C3）。v1 は完全な fence だけ |
| 18. 7010 で全 block を 1 つの bitstream に無理に入れる（NPHYS 4、varying 8…） | 97〜111% で timing と配線が閉じず、workflow の確立という目的に合わない。block ごとの bitstream の方が各 block の板の受け入れが早い |
| 19. `fpga.window` の boot parameter で窓を 128 KiB に | devcfg に無い機能を足す Phase が p005 の前に要る。DC だけの bitstream を offset 0 に置けば不要 |
| 20. PCIe を vendor/device の固定の組で照合 | AHCI/xHCI の class code の照合と違い、「汎用」に反する |

## 11. 敵対的な読み直し（版 2 の自己点検）

1. 完全な FENCE（§2.4）は BLIT と DRAW の非同期性を毎回打ち消す: submit ごとに 1 つの fence なので、1 submit の中の複数の draw は並ぶ。性能が要る時は軽い fence を後で足す（案 17）。
2. detach の 3（§6.2）で device が offline のまま 5 s を超える事: `fpgaload` に `EBUSY` を返し、次の `fpgaload` で retry する。永久に残らない（session は ENODEV で閉じる）。
3. `RPTR` で ring を再利用してよい（§2.7）は、CP が packet を **block に写し終えている**事が前提。`SET_REG` の value は STATE に写すので良いが、`DRAW` の payload（gx・gy・gz など）も CP の register に写す（block は ring を読まない）。RTL の規則にする。
4. KC705 の BAR1 の読み戻し（§2.7）は PCIe の往復（µs）で、submit ごとに 1 回なら許容。
5. 32 lane の coalescer は `32/NPHYS` cycle に分けて比較するので、1 つの `LDG` の address の計算に NPHYS/32 の割合で cycle が要る（performance の話で正しさではない）。
6. `KILL` の sticky が EXEC と別なので、`EXEC_SAVE` の値に killed が混ざらない。compiler は loop の条件に killed を含めない（IR の `KILL` の後の code は走ってよい）。
7. ZYBO の DC だけの bitstream が IRQ 61 を使い、GPU の bitstream も 61 を使う: 同時に居ないので衝突しない。KC705 は MSI で分ける。
8. §3.6 の表は内訳から計算したが係数は推定のまま。p002 と p008 の実測が最初の仕事（表に明記）。

## 12. review の指摘と対応（design-reviewer、2026-10-23、38 件）

| # | 指摘（要旨） | 対応（節） |
| --- | --- | --- |
| C1 | detach が unregister の EBUSY を当てにしていたが、unregister は先に device を offline にする。level shifter を閉じる前の AXI の静めが無い | §6.2 の detach の状態機械（session の数を先に見る、quiesce、retry、5 s の上限）。JTAG の書き直しは attach 中は禁止 |
| C2 | watchdog が AXI_IDLE を待たずに reset、KC705 の DC で AXI_IDLE が真にならない、DC が core の reset を受ける | §1.3: reset の領域を 3 つに分け、quiesce を定義、AXI_IDLE は GPU の client だけ、hardware は自分で reset しない、`AXI_STUCK` |
| C3 | FENCE が非同期の block の完了を含まない。IRQ の時点 | §2.4: 完全な fence（全 command の完了 + ROP の flush + 全 BRESP → 書く → その BRESP → IRQ）。軽い fence は無し（案 17） |
| H1 | cap の組が登録を通らず、libvulkan にも見つからない。jobs・capset が無い | §6.4: cap の組、jobs/commands/recovery の ops、capset（powervr の形、vendor の suffix） |
| H2 | 64 KiB の alias で二重 attach | §2.2: 17 bit を解き範囲の外は 0。登録は DEVICE_ID と EXTCAP も見て二重を拒む |
| H3 | `fpga.window` は実在せず、IRQ 62 も未対応 | §5.1・§6.2: ZYBO の DC だけの bitstream は窓の offset 0・IRQ 61。128 KiB の窓は KC705 の BAR0 だけ。R14・R15 を解消 |
| H4 | devcfg の IRQ 61 の handler と `fpgareg` の生の書き込みが driver と競合 | §6.2: attach 中は IRQ を block の driver に渡し、`FPGA_IRQ_WAIT`・`REG_WRITE` は EBUSY |
| H5 | CP の状態遷移が無い | §2.6 の表（状態、受ける物、reset で残る/消える register） |
| H6 | ring の wrap・満/空・RPTR の意味・barrier・先読み | §2.7 |
| H7 | 非同期の fault の帰属、session 単位の回復の穴 | §2.5: v1 は device 全体の fault → reset、session の error は parse の誤りだけ。JOB_TAG を header と FAULT_INFO に |
| H8 | 統一 shader の deadlock、PT の定義 | §3.8（新設）、§3.5 の PT、§3.6 に BRAM |
| H9 | ROP の rasterization order | §3.8: 番号と scoreboard。p007 の TB に重なる blend の順 |
| H10 | cache の無効化の約束が無い | §2.4 `INVALIDATE`、DRAW/DISPATCH の暗黙の無効化、ISA の `FENCE acquire`・`ATOM` の規則 |
| H11 | 見積もりの足し算が内訳と合わない | §3.6 を内訳から計算し直し（FMA 400 LUT）。結論を「7010 では block を分けた bitstream」に変え（R1・U17）、「極小は入る」を取り下げ |
| H12 | p011 ↔ p013 の循環 | §8: p013 は `ghci-blit` だけ、p014（KC705 の全 pipeline、N=1）を新設、p011 の板は p014 |
| H13 | 2 device の share の ops の取り違え、pool の所有者 | §5.2・§6.5: GPU は share、DC は scanout の import。pool は `pl-memory.c`（devcfg の寿命） |
| H14 | KC705 の device-local の VRAM は pages で表せない、BAR0 の外 | §6.5: KC705 は 1 device（same-device の share）。BAR0 を 128 KiB に |
| H15 | 標準に ATS1 の実行の model が混入、PCI の固定の組、「汎用」の過大 | §2.1・§2.3: 実行の model を vendor へ（slot、constant の領域、VEND の register）。PCI は class code + magic（U9）。README の文を直した |
| M1 | native の packet の検査 | §6.4: `CONFIG_GHCI_NATIVE` と root、opcode と index の白表 |
| M2 | DBG_DATA が GP0 を長く止める | §2.2: 非同期（ADDR → STATUS の poll → DATA）、slave は待たせない |
| M3 | EXTCAP の next の幅 | §2.2: 2 word の entry、相対の dword 数 16 bit |
| M4 | DC だけの header が CP の有無を表せない | §2.2 CAPS[0..3]、§5.1 の 10 word、attach の分岐 |
| M5 | 32 byte line と MIG の 512 bit、client の上限 | §4.1: `LINE_BYTES` 32/64、client ≤ 32 |
| M6 | IR の下げ方の漏れ、KILL の後の副作用 | §6.6 の全 op の表、§3.1 の killed の規則、`TEXB`・`TEXF` の level・`TEXSZ` の level・`QSWAP sub 3` |
| M7 | barycentric の意味、clip の頂点、provoking | §3.4 の入力（補正済み、元の 3 頂点）、§3.8 の clip、`PROVOKING` |
| M8 | Vulkan の最小の limit の未達 | §3.7 の表（属性 16、slot 16、mip 13、instancing を足す。indirect・cube は未達として記録） |
| M9 | DSP の主張、packed と「同じ binary」 | §3.4: DSP 2、`PACKH/UNPACKH` は必須、`PACKED` の無い構成は U15 |
| M10 | RF の書き込み port、LDA の port | §3.5: 書き込みの調停、primitive table の複製（§3.6 に） |
| M11 | MMCM の出力の数、25.175 | §1.3・§5.1: v1 は 2 mode、1 MMCM の 4 出力、BUFGMUX、25.2 MHz の注意 |
| M12 | fence の wrap・reset の初期化、IRQ での mutex | §2.5（書き戻しと折り返し）、§6.3（回収の thread） |
| M13 | PCIe の最初の列挙、SBR、MSI の vector | §4.3 A'（flash か realloc、SBR、vector の表） |
| M14 | LW と試験の計画の漏れ、ZYBO の例外 | §8 の LW（p006 24、p008 30、p011 48）、§6.8 の試験（detach/reattach、fault の注入、wrap の負荷、2 device）、U16 |
| L1 | reset の極性 | §1.3 |
| L2 | RF の表の重複、NPHYS 32 の BRAM | §3.5 |
| L3 | 参照の誤り | 直した（§11 の番号を付け直し） |
| L4 | `/dev/gpuN` の番号 | §6.1: 空いている最小 |
| L5 | `hal_*` でなく `kern_*`、末尾の保証 | §6.1・§6.3 |
| L6 | ws.md の mc64、順序の変更の記録、README の確定値の書き方、FRAME_COUNT の 64 bit | ws.md を直す（P2 の path）、U14、README-additions を「proposed」の書き方に、§5.1 の FRAME_COUNT |
