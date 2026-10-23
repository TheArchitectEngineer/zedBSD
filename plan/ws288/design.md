<!-- awesome-plan project=zedbsd record=ws288-p001-design -->

# WS288 の設計: FPGA の GPU の IP「Amaterrace GPU」と GHCI — ZYBO（XC7Z010）で workflow、本命は KC705（XC7K325T）

2026-10-23 P2（ws288-p001、worktree p2-ws288-p001、基点 main c327d0c73）。親: [WS288](ws.md)。**版 3**（同日 P1、worktree p1-ws288-d3、基点 main 01f261168: ユーザーの 4 つの決定（§2.1 の冒頭）に合わせて §2.4（ring の command = Kei GPU command protocol の gpu-op、CP は RISC-V 風の `atg_cpu` の firmware が解く）と §3（GHCI の標準の shader ISA「GSI1」、RISC-V の 32 bit の形を低位に持つ 64 bit 固定、小さな hardware の scoreboard）を書き直し、§6.6・§6.7・§6.9・§8（p017〜p019 の新設、392 LW）・§3.9（systolic array と ISA の FIFO の register `rq`、同日のユーザーの要件）・§6.10・§9（R23〜R30、U18〜U30）・§10（21〜30）・§11・§13 を足した。版 2 の節の番号は保った。design-reviewer の指摘（Critical 2・High 10・Medium 22・Low 15）と対応は §13（末尾）。**版 2**（同日: design-reviewer の指摘 38 件（Critical 3・High 15・Medium 14・Low 6）の全部に対応。対応の表は §12。版 1 からの主な変更: detach と reset の順序（§1.3・§6.2）、FENCE の意味（§2.4）、drv_gpu の capability と jobs と capset（§6.4）、window と IRQ の devcfg の事実への修正（§5.1・§6.2）、CP の状態遷移と ring の約束（§2.6・§2.7）、fault は v1 は device 全体（§2.5）、統一 shader の deadlock と ROP の順序（§3.8）、cache の無効化（§2.4 `INVALIDATE`）、見積もりの計算し直しと **7010 では block を分けた bitstream で確かめる**方針（§3.6・R1）、GHCI の標準と vendor の境（§2.1）、KC705 は 1 device（§6.5）、Phase の循環の解消（§8）。）
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
- 版 3 で読んだ物（main 01f261168）: `include/uapi/gpu-op.h` の全部（2016 行: 冒頭の wire の規則 56〜122 行、1 submission の枠 93〜104 行、各 command の引数と「Sent by / Decoded by」、記録 93〜136 の形 1267〜1281 行、178〜180 の transport、zedBSD 固有の 0x10000〜の video）、`include/uapi/gpu.h`（`gpu_command_submit`・`gpu_command`・capability）、`include/uapi/gpu-job.h`（reserve/commit/cancel/capacity）、`src/drivers/gpu/powervr/vk/vk-stream.c`（frame の実行、route の表、reply の選択と trailer の release store、`EXECUTE_STREAMS` の入れ子）、`src/drivers/gpu/powervr/render-runtime.c`（`runtime_command` は frame をその場で実行、jobs の ops）、`src/drivers/gpu/compiler/ir.h`（op の一覧 263〜633 行、atomic・fence・texture の種類）、`src/drivers/gpu/i915/render/internal.h`（`i915_wire_reader` を powervr が再利用している）、amaterrace-gpu の p002 の途中（commit 06edac2 + 未 commit の `include/ghci_regs.h`・`ghci_packets.h`（placeholder）・`rtl/gpu/cp/atg_cp.v`（stub、ring 無し）・`atg_blt.v`・`atg_mc.v`・`atg_top.v`、合計 2162 行）、sukuna-risc の README（RV64GC、2-issue、2-SMT、約 22,000 LUT、KC705 で 200 MHz が目標。RTL は未 commit）、共有の LLVM の `clang --print-targets`（riscv32 が有る、実測）。

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
   Systolic:    SU <-(FIFO rq)-> SA <-- MC（重みの stream。KC705 だけ、optional）
   Display:     DC --> MC（KC705）/ 自分の AXI（ZYBO の HP1）

   +------------------------------------------------------------------+
   | atg_top（GPU の IP）                                                |
   |  atg_cp   GHCI の register、ring、µop の sequencer、state、fence、IRQ、DBG |
   |  atg_cpu  CP の制御 processor（RV32IM、firmware が Vulkan の command を解く） |
   |  atg_blt  2D の fill / copy / tile                                 |
   |  atg_vf   index と頂点の読み、primitive の組み立て                  |
   |  atg_su[N] shader unit（vertex も fragment も compute もここ）      |
   |  atg_rx   clip・viewport・edge function・quad の走査               |
   |  atg_tu   sampler と texture cache                                 |
   |  atg_rop  depth・stencil・blend・書き戻し（Buffer Writer）           |
   |  atg_sa   systolic array（SU と decoupled、FIFO で SU の rq に、重みは MC から。optional、§3.9） |
   |  atg_mc   内部の memory bus（AMB）の arbiter、aperture、AXI master  |
   +------------------------------------------------------------------+
   +------------------------------------------------------------------+
   | atg_dc（DC の IP、別の top。7010 では GPU と同時に載せない）         |
   +------------------------------------------------------------------+
```

- GPU（`atg_top`）と DC（`atg_dc`）は別の IP、別の top。7010 には同時に載せなくてよい（2026-10-23 ユーザー）。ZYBO では別々の bitstream（§8）。KC705 では 1 つの bitstream に両方を載せ、MC の口を共有し（§4.2）、zedBSD からは **1 つの GHCI の device**（display の ops 付き、§6.5）に見える。
- block の間の内部の memory の口は 1 つの自前の bus「AMB」（§4.1）。外の AXI は MC の `am_axi_master` 1 つだけが話す。block は AXI を知らない。
- primitive の流れで SU は 2 回通る（頂点の warp と fragment の warp）。同じ SU が両方を走らせ、RX は頂点の出力を post-transform の buffer（PT）から取って fragment の warp を作る（deadlock の回避は §3.8）。TU は fragment の warp（と compute）から使う。
- CP は 2 つの部分（版 3、§2.4.7）: `atg_cp`（hardware: HCR、ring の DMA、class N の µop の sequencer、fence、IRQ、fault、DBG）と `atg_cpu`（RV32IM の小さな制御 processor。firmware が ring の class V の packet = Vulkan の command を解いて µop を `atg_cp` に渡す）。firmware の無い構成（ZYBO の bitstream 1・3・4）では `atg_cp` だけで class N を実行する。
- 性能の counter と debug の register は小さく（§2.2）。DBG の ADDR/DATA で CPU が AMB を通して DDR を読み書きできるので、**最初の milestone は kernel の driver 無しに `fpgareg` だけで BLT の結果を確かめられる**（§8 p003）。
- **7010 では全 block が 1 つの bitstream に入らない見込み**（§3.6）。ZYBO は block を分けた bitstream（CP+BLT、DC、CP+RX+ROP、CP+SU の compute）で workflow と各 block を確かめ、全体の結合は Verilator と KC705 で確かめる（R1・U17）。

### 1.2 名前と parameter

- RTL の module の接頭辞: `atg_`（Amaterrace GPU の block: `atg_cp`・`atg_blt`・`atg_vf`・`atg_su`・`atg_rx`・`atg_tu`・`atg_rop`・`atg_mc`・`atg_dc`）、共通の部品は `am_`（`am_afifo`、`am_sync`、`am_axi_master`、`am_axi_regs`、`am_jtag_regs`）。
- GHCI の ID（§2.2）: magic `"GHCI"`、vendor `"AMAT"`（Amaterrace）、device `"ATG1"`（この GPU の 1 代目）、ISA `"GSI1"`（GHCI Shader ISA 1 = 標準の ISA、§3。Amaterrace の native も GSI1）。
- 主な parameter（top で与える。board ごとの値は §3.7 の表）: `NSU`（SU の数）、`NPHYS`（SU の物理の lane の数: 8/16/32。**warp は常に 32 lane**（README）で、`32/NPHYS` cycle で 1 命令を流す、§3.1）、`NWARPS`（常駐の warp の数）、`NREGS`（thread ごとの register の数、U12）、`LINE_BYTES`（AMB の line: 32 か 64）、`AMB_W`（AMB の beat の幅、64〜512）、`AXI3`（1 で AXI3 の master）、`TU_BILINEAR`、`ROP_STENCIL`、`PACKED`（16 bit packed の演算器）、`SUBGROUP`（`shfl`/`vote`/`ballot`）、`SA_COLS`（systolic array の列。0 = 無し、§3.9）、`SA_DTYPE`、`SA_FIFO_DEPTH`、`SB_ENTRIES`、`ADDR_W` = 32。

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
  - これで汎用の driver（zedBSD の `src/drivers/gpu/ghci/`）は ISA の backend を 1 つ（標準の ISA）持てば、どの GHCI の GPU でも shader まで動かせる。版 3 で決めた: 標準の ISA は §3 の **GSI1**（RISC-V を基にした 64 bit 固定）、Amaterrace の native = GSI1、driver の backend は `isa/gsi1/`（§6.6）。command の側は §2.4（ring の class V = gpu-op、CP の firmware が解く）。

- **決める（公開・版付き・vendor に依らない。amaterrace-gpu の `docs/ghci.md` が正本、GHCI 1.0）**: (a) Host Control Register（HCR）の配置と意味（§2.2）、(b) extended capability の list（DC・debug・vendor の register の見つけ方）、(c) command ring と packet の封筒、class V（Vulkan の command = Kei GPU command protocol の gpu-op）の意味と GHCI 固有の command（session・memory・program・native）、class N の標準の µop（§2.4）、(c') 標準の shader ISA「GSI1」と program の binary の形、native の ISA の送り方（§3）、(d) state register file のうち **Vulkan の固定機能に当たる標準の領域**（§2.3）、(e) fence・割り込み・fault・watchdog・aperture・quiesce・reset の約束（§2.5〜§2.7）、(f) memory の model（32 bit の GPU address、aperture、将来の MMU の capability）、(g) CP の状態遷移（§2.6）と ring の約束（§2.7）。
- **決めない（vendor、`VENDOR`・`DEVICE`・`ISA` の ID で識別）**: vendor の native の ISA（`NATIVE_ISA_ID`、任意。GSI1 の上位集合でも別物でもよい）、CP の firmware の中身（GHCI は class V の意味だけを決める）、**実装の都合**（NPHYS、NWARPS、NREGS、cache の大きさ、SB_ENTRIES、`PACKED`・subgroup の有無 = `VEND` の register と `CAPS` の bit。GSI1 の program は `nregs` などを header に持ち、firmware が実装に合うかを検査する、§3.4.4）、texture の tile の並び（capability で「linear だけ」「4×4 tile」を名乗る）、vendor の µop（class 1 の 0〜63、§2.4.2）、vendor の state（0x1E0〜0x1FF）、vendor の register（extended capability `VEND`。Amaterrace は NWARPS・NREGS・NPHYS・PACKED をここに置き、ISA の backend が読む）。
- 汎用の driver（zedBSD の `src/drivers/gpu/ghci/`、§6）は HCR・ring・fence・割り込み・memory・標準の state・標準の opcode・DC の capability を扱い、**ISA の ID で compiler の backend を選ぶ**（標準の "GSI1" は必ず持つ。`NATIVE_ISA_ID` の backend は任意）。shader の無い GPU（`ISA_ID` = 0）と firmware の無い構成（`CAPS[17]` = 0）は BLT と DC だけで attach する（display + 2D の device として使える。Vulkan は出さない）。**「汎用」の意味（版 3）**: host interface・BLT・DC・Vulkan の中継（`vk/`）・GSI1 の backend の全部が汎用で、Amaterrace に固有の driver の code は無い。vendor の違いは firmware と `VEND` の register に閉じる（README-additions の文もこの形）。
- 版: HCR の `GHCI_VERSION` の major が変わる時だけ互換を壊す。minor の追加は末尾の register と新しい capability と opcode だけ。zedBSD の driver は major 1 だけを受ける。R19: v1 は Amaterrace が実装する物だけを標準に書く。

### 2.2 Host Control Register（HCR、64 KiB の窓の中の offset、32 bit、little endian）

窓: ZYBO の GPU の bitstream は GP0 `0x43C00000`（devcfg の固定の窓、64 KiB、IRQ_F2P[0] = GIC 61）、ZYBO の DC だけの bitstream も **同じ窓の offset 0**（DC だけの GHCI、§5.1）と IRQ_F2P[0]。KC705 は PCIe の BAR0（**128 KiB**: +0x00000 HCR、+0x10000 DC の register）と JTAG。slave は **17 bit** を解き、範囲の外は 0 を返す（64 KiB の alias で二重に見えない）。

| offset | 名前 | R/W | 中身 |
| --- | --- | --- | --- |
| 0x000 | GHCI_MAGIC | R | `0x47484349`（"GHCI"） |
| 0x004 | GHCI_VERSION | R | [31:16] major（1）、[15:0] minor（0） |
| 0x008 | VENDOR_ID | R | FourCC。Amaterrace は `0x414D4154`（"AMAT"） |
| 0x00C | DEVICE_ID | R | FourCC。`0x41544731`（"ATG1"）。DC だけの instance は `"ATDC"` |
| 0x010 | ISA_ID | R | FourCC の標準の ISA。GHCI 1.0 は `0x47534931`（"GSI1"、§3）。0 = shader 無し |
| 0x014 | DEVICE_VERSION | R | vendor の実装の版（major.minor） |
| 0x018 | EXTCAP | R | extended capability の list の先頭の offset（byte、0 = 無し）。各 entry は 2 word: word0 `{[7:0] id, [15:8] cap の版, [31:16] 次の entry までの dword 数（この entry からの相対、0 = 終わり）}`、word1 = cap の register の offset（byte、窓の先頭から）。id: 1 `DISP`（DC、§5）、2 `DBG`（JTAG の bridge の有無）、3 `PERF`、4 `VEND`（vendor の register）、5 `MMU`（将来）、6 `CPFW`（CP の firmware の載せ替え、版 3、§2.4.10）、7 `SA`（systolic array の形、§3.9.3） |
| 0x01C | GCLK_HZ | R | core の clock。driver が timeout を秒に直す |
| 0x020 | CAPS | R | [0] CP と ring が有る、[1] BLT、[2] shader（`ISA_ID` ≠ 0）、[3] DC がこの窓に有る（`DISP` の entry と対）、[7:4] log2(warp の lane 数)（Amaterrace は 5）、[8] TU bilinear、[9] ROP stencil、[10] `WAIT_VSYNC` 可、[11] texture の tile（0 linear、1 4×4）、[15:12] 標準の state の版、[16] MMU、**[17] VKCP**（class V の packet を解く firmware が走っている。版 3）、**[18] FWLOAD**（`CPFW` で firmware を載せ替えられる）、**[19] NATIVE_ISA**（`NATIVE_ISA_ID` ≠ `ISA_ID`）、[23:20] log2(memory の line の byte)（5 = 32、6 = 64）、**[24] SUBGROUP**（§3.4 の `shfl`/`vote`/`ballot` が有る。版 3、U27）、**[25] SA**（systolic array、§3.9） |
| 0x024 | CAPS2 | R | [7:0] queue（ring）の数（v1 は 1）、[15:8] shared memory KiB、[23:16] constant の領域の word/16、[27:24] texture の slot の数/4（v1 は 4 = 16 slot）、[31:28] 頂点の属性の数/4（4 = 16） |
| 0x028 | SCRATCH | RW | 32 bit（疎通） |
| 0x02C | COUNTER | R | `gclk` の数（free running） |
| 0x030 | CTRL | RW | [0] RUN、[1] RESET（W1S、§1.3 の順で self-clear）、[2] HALT、[3] STEP（HALTED で 1 command）、[4] WDT_EN。状態遷移は §2.6 |
| 0x034 | STATUS | R | [0] CP busy、[1] BLT busy、[2] VF busy、[3] RX busy、[4] ROP busy、[5] TU busy、[6] SU busy（OR）、[7] SA busy（版 3）、[8] AXI_IDLE（GPU の client の outstanding が 0）、[9] FAULT、[10] HALTED、[11] RING_EMPTY、[12] MEM_READY（KC705: MIG の calibration 完了。ZYBO: 1）、[13] AXI_STUCK、[14] QUIESCED（新しい発行を止めている）、[19:16] 状態（§2.6 の番号） |
| 0x038 | IRQ_STATUS | R/W1C | [0] FENCE、[1] FAULT、[2] RING_EMPTY、[3] WATCHDOG、[4] DBG_DONE、[5] SOFT（IRQ_SET で立てる）、[6] HALTED（HALT の完了）、**[7] RESULT**（firmware が frame の result を書き終えた。版 3）、**[8] RETIRE**（`RPTR` が packet を越えた。版 3） |
| 0x03C | IRQ_ENABLE | RW | [8:0]。IRQ の線 = OR(IRQ_STATUS & IRQ_ENABLE)（level、plio と同じ約束） |
| 0x040 | IRQ_SET | W1S | [5] |
| 0x044 | WDT_CYCLES | RW | 1 command の上限の cycle 数（既定 2^28 ≈ 2.7 s @ 100 MHz） |
| 0x050 | RING_BASE | RW | ring の GPU address（32 byte 揃え）。RUNNING では書けない（無視、FAULT 種別 7） |
| 0x054 | RING_SIZE | RW | log2(byte)（10〜20）。同上 |
| 0x058 | RING_WPTR | RW | dword の index（ring の中、mod size）。書くと doorbell（§2.7） |
| 0x05C | RING_RPTR | R | CP が **解き終えて block に渡した**（parse 済みの）dword の index。非同期の block の完了は別（`WAIT_IDLE`・`FENCE`） |
| 0x060 | APERTURE_BASE | RW | GPU が触ってよい address の範囲の先頭（32 byte 揃え）。**全部の AMB の要求がここで検査される**。RUNNING では書けない |
| 0x064 | APERTURE_LIMIT | RW | 範囲の終わり（含まない） |
| 0x068 | FAULT_ADDR | R | 最後の fault の address |
| 0x06C | FAULT_INFO | R | [7:0] client の番号、[8] write、[15:12] 種別（1 aperture、2 未知の opcode、3 ring の誤り、4 watchdog、5 AXI の SLVERR/DECERR、6 RUN 中の KICK、7 RUNNING での禁止の書き込み、8 ISA の誤り（SU。版 3、§3.4.1。`FAULT_ADDR` = PC））、[23:16] 種別 1〜5 の時の block、[31:24] JOB_TAG（§2.4: fault した仕事の tag。CP が検出した時はその command の tag、非同期の時は block が持つ tag） |
| 0x070 | FENCE_LAST | R/W | 最後に書いた fence の値。**reset の後に driver が「最後に完了した値」を書き戻す**（§2.5） |
| 0x048 | NATIVE_ISA_ID | R | 版 3。vendor の native の ISA の FourCC（`GHCI_PROGRAM` で直接送れる物）。`ISA_ID` と同じなら native = 標準（Amaterrace: "GSI1"）。0 = native を直接は受けない |
| 0x0B0 | CP_HEAP_BASE | RW | 版 3。firmware の object の heap（aperture の中、32 byte 揃え）。RUNNING では書けない（FAULT 種別 7） |
| 0x0B4 | CP_HEAP_LIMIT | RW | 同上（終わり、含まない）。既定は driver が 8 MiB を与える |
| 0x0C0〜0x0DF | VEND（Amaterrace） | R | 8 word: NWARPS、NREGS、NPHYS、PACKED、SB_ENTRIES、PT の entry 数、reserved × 2 |
| 0x120〜0x13F | CPFW_CTRL / CPFW_STATUS / CPFW_IBASE / CPFW_IWORDS / CPFW_DBASE / CPFW_DWORDS / CPFW_VERSION / CPFW_MBOX0..3 | RW/R | 版 3、extended capability `CPFW`（id 6）。`CPFW_CTRL` [0] LOAD（W1S: IMEM の像と DMEM の像を DMA、`READY` で終わり）、[1] RUN、[2] HALT（今の packet の後で止める）。`CPFW_STATUS` [0] READY、[1] FAULT、[2] ALIVE（heartbeat）、[15:8] firmware の状態。LOAD は IDLE・HALTED で CPU が止まっている時だけ（§2.4.7） |
| 0x140〜0x14F | SA_CAPS / SA_SU | R / RW | 版 3、extended capability `SA`（id 7、§3.9.3） |
| 0x080〜0x08C | PERF0〜3 | R | busy の cycle の合計（標準の意味は「busy の cycle」4 本まで。Amaterrace: SU、RX、TU、MC）。`PERF_CTRL` 0x090 [0] で clear |
| 0x0A0 | DBG_ADDR | RW | AMB を通した memory の読み書きの address（4 byte 揃え、aperture の検査を受ける）。書くと read を始める（非同期） |
| 0x0A4 | DBG_DATA | RW | 読み: `DBG_STATUS.busy` が 0 の時の最後の read の値（slave は **待たせない**）。書き: `DBG_ADDR` へ write を始める |
| 0x0A8 | DBG_STATUS | R | [0] busy、[1] error（aperture の外）。完了で `IRQ_STATUS.DBG_DONE` |
| 0x0F00 | KICK | W | [0] BLIT、[1] DRAW、[2] DISPATCH（STATE の値で直接 kick。ring 無しの最初の試験と debug 用。IDLE と HALTED でだけ受ける。RUNNING では FAULT 種別 6） |
| 0x1000〜0x17FC | STATE[0..511] | RW | state register file の alias（§2.3）。IDLE・HALTED でだけ書ける（RUNNING は無視して種別 7）。ring の `SET_REG` と同じ場所 |
| 0x100〜 | EXTCAP の entry の list | R | `DISP`（KC705: +0x10000 を指す。ZYBO の DC だけの bitstream: +0x0200）、`DBG`、`PERF`（P2 の実装: capability の register `PERF_SRCn` は 0x110〜、counter は 0x080〜）、`VEND`（0x0C0）、`CPFW`（0x120）、`SA`（0x140）。版 3 の追加は P2 の 0x110 の後ろに置く（M5） |
| 他 | — | R | 0（書き込みは無視、応答は OKAY） |

### 2.3 state register file（STATE[0..511]。0x000〜0x1DF は GHCI の標準、0x1E0〜0x1FF は vendor）

| index | block | 中身（32 bit の word。float は IEEE の bit） |
| --- | --- | --- |
| 0x000〜0x01F | BLT | 0 OP（[1:0] 0 FILL、1 COPY、2 TILE（linear → vendor の tile）、3 UNTILE。[4] blend over。[9:8] bpp（0 = 32、1 = 16）。[12] COPY の向き）、1 DST_BASE、2 DST_STRIDE（byte）、3 SRC_BASE、4 SRC_STRIDE、5 DST_XY、6 SRC_XY、7 SIZE、8 COLOR、9 CLIP_MIN、10 CLIP_MAX |
| 0x020〜0x04F | VF | 0 TOPOLOGY（[2:0] 0 points、1 lines、2 line strip、3 triangles、4 triangle strip、5 triangle fan）、1 INDEX_BASE、2 INDEX_FORMAT（0 無し、1 u16、2 u32）、3 FIRST（first vertex / first index）、4 COUNT、5 PRIM_RESTART（[0]）、6 INSTANCE_COUNT、7 FIRST_INSTANCE、8 VERTEX_OFFSET（base vertex、signed）、9〜15 reserved、16+2k ATTR[k]_BASE、17+2k ATTR[k]_DESC（[15:0] stride、[19:16] format（0 R32F、1 RG32F、2 RGB32F、3 RGBA32F、4 RGBA8_UNORM、5 RGBA8_UINT、6 RG16F、7 RGBA16F、8 R32_UINT、9 RG32_UINT）、[20] per-instance、[31:24] instance の divisor）、k < 16。**属性 k の行き先の register と component の数は STATE `ATTR_DST[k]`**（0x1C0〜0x1CF、版 3、M12） |
| 0x050〜0x06F | RX | 0〜5 VIEWPORT（float: x_scale、y_scale、x_off、y_off、z_scale、z_off）、6 SCISSOR_MIN、7 SCISSOR_MAX、8 CULL（[1:0] none/front/back、[2] front = CCW）、9 DEPTH_CLAMP、10 RAST_MODE、11 VARYING_COUNT（0〜32 word）、12 DEBUG_COLOR（shader 無しの定数色、p007）、13 PROVOKING（[0] 0 = first、1 = last） |
| 0x070〜0x08F | ROP | 0 RT_BASE、1 RT_STRIDE、2 RT_FORMAT（0 BGRA8、1 RGBA8、2 RGB565）、3 RT_SIZE、4 DS_BASE、5 DS_STRIDE、6 DEPTH（[0] test、[1] write、[4:2] func、[5] D24S8、[6] D32F）、7 STENCIL_FRONT、8 STENCIL_OPS、9 STENCIL_BACK、10 BLEND（版 1 と同じ bit）、11 BLEND_CONST、12 WRITE_MASK |
| 0x090〜0x18F | TU（**16 slot × 16 word**） | 16s+0 BASE（level 0）、+1 SIZE（[15:0] w、[31:16] h）、+2 LEVELS（[3:0] mip の数 ≤ 13）、+3 SAMPLER（[1:0] wrap u、[3:2] wrap v、[4] mag linear、[5] min linear、[6] mip linear、[7] compare、[10:8] compare func、[12] format）、+4〜+15 MIP_OFFSET[1..12]（level 0 からの byte の offset。driver が計算） |
| 0x190〜0x1AF | program | 0 VS_BASE、1 VS_WORDS、2 FS_BASE、3 FS_WORDS、4 CS_BASE、5 CS_WORDS、6 CONST_BASE、7 CONST_WORDS（constant の領域 0。ISA が意味を決める）、8 LDS_BYTES、9 WG_SIZE（[7:0] x、[15:8] y、[23:16] z）、10 VS_OUT_WORDS（position の後の varying の word 数） |
| 0x1B0〜0x1BF | SA（版 3、§3.9.3） | 0 SA_CTRL、1 SA_W_BASE、2 SA_W_STRIDE、3 SA_K、4 SA_TILES、5 SA_W_TILE_STRIDE、6 SA_COLS_USED、7〜15 reserved |
| 0x1C0〜0x1CF | VF（版 3） | ATTR_DST[k]: [5:0] 先頭の register、[7:6] component の数 − 1（program の header の interface の表から firmware が書く） |
| 0x1D0〜0x1DF | reserved | 0 |
| 0x1E0〜0x1FF | vendor | Amaterrace v1 は 0x1FF を DBG_MARK に使う |

### 2.4 command（ring の中、32 bit の dword の列。版 3: Vulkan の command（gpu-op）の class と native の µop の class）

#### 2.4.1 方針（2026-10-23 ユーザー「コマンドは、現在のカーネルのコマンド（Venus由来）を使いましょう。」）

- ring が運ぶ command は **zedBSD の Kei GPU command protocol 版 1**（`include/uapi/gpu-op.h`。番号は Venus の wire format 1 と同じ、zedBSD だけの物は `GPU_OP_OWN_FIRST` = 0x10000 から）。libvulkan が `GPU_COMMAND_SUBMIT` で kernel に渡す stream（`[u32 番号][u32 flags][引数…]` の列、1 submission の枠は `SET_REPLY_STREAM(178)` → command… → `SEEK_REPLY_STREAM(179)` → `ENUMERATE_INSTANCE_VERSION(137)` の trailer、gpu-op.h の 93〜123 行）を、zedBSD の driver（§6.7）が**検証して GPU の memory の staging にほぼそのまま写し、ring には frame の packet を置き**（§2.4.6）、GPU の CP がそれを解く。
- CP の中で gpu-op を解くのは **firmware**（小さな RISC-V 風の制御 processor `atg_cpu`、§2.4.7。hardware で解く案は採らない。理由は §2.4.7 の表と §10 の 21）。firmware は Vulkan の object（pipeline・descriptor set・render pass・framebuffer・image view・sampler・command buffer の記録）を GPU の memory の中の heap に持ち、`QUEUE_SUBMIT` で記録を辿って、固定機能の block と SU を動かす **µop**（版 2 の `SET_REG`・`BLIT`・`DRAW`・`DISPATCH`・`FENCE`・`WAIT_IDLE`・`LOAD_CONST`・`LOAD_PROGRAM`・`INVALIDATE`…）に直して hardware の sequencer に流す。
- µop は GHCI の **class N（native）の packet** として ring にも直接書ける（driver 無しの bring-up、`ghci-blit` の試験、SU の compute の試験、CP に firmware が無い構成の GPU）。class N の標準の µop（class 0 の op 0〜63）は GHCI が決め、vendor の µop（class 1 の op 0〜63）は vendor が決める。**Amaterrace の v1 は class N の標準の µop だけで固定機能を動かせる**（版 2 の設計が成立する）ので、p003・p004・p007・p008 の板の受け入れは firmware 無しで行える。
- 1 本の ring に class N と class V（Vulkan）の packet が混ざってよい。CP は ring の順に処理し、class V の packet を firmware が解いている間は次の packet を取らない（µop の列は 1 本、順序は ring の順）。

#### 2.4.2 packet の封筒（envelope）

```
dword0（共通）: [31:30] class   [29:24] op（class N だけ）   [23:16] JOB_TAG   [15:0] n または [15:8] flags [7:0] n
  class 0 = N 標準の µop（op 0〜63）     dword0 = [31:30]=00 [29:24] op [23:16] JOB_TAG [15:8] flags [7:0] n（payload の dword 数 ≤ 255）
  class 1 = N vendor の µop（op 0〜63）  同上（Amaterrace: 0 DRAW_RAW、1 DBG_MARK）
  class 2 = V（Vulkan の command）       dword0 = [31:30]=10 [29:24] 0 [23:16] JOB_TAG [15:0] n（dword1 以降の dword 数 ≤ 65535）
      dword1 = gpu-op の番号（u32）、dword2 = [0] REPLY（libvulkan が送った flag のまま）[1] RESULT（dword3 が有効）[31:8] 0、
      dword3 = result の address（GPU の address、8 byte 揃え。RESULT = 0 なら 0）、dword4〜 = gpu-op.h の「Arguments」の wire のまま（u32/u64/構造体、little endian）
  class 3 = 詰め物                       dword0 = [31:30]=11 [15:0] n（CP は n dword を読み飛ばす。環の端の padding、§2.7）
```

- 版 2 の header（`[31:24] opcode [23:16] flags [15:8] JOB_TAG [7:0] n`）は**この形に置き換える**（p004 で ring を作る時にこの封筒で作る。p002 の `ghci_packets.h` は placeholder のまま）。JOB_TAG の位置が class に依らず同じなので、`FAULT_INFO[31:24]` への写しは 1 本の配線。
- class V の `n` は driver が各 command の引数を**解いて**数える（gpu-op の wire には長さが無い: gpu-op.h 71〜73 行）。driver は中継する command の構造体の形を全部知っている（検証のために解く、§6.7）ので、長さは副産物。CP の firmware は `n` を使って packet の終わりを知り、解けない command は packet ごと捨てて result に `VK_ERROR_FEATURE_NOT_PRESENT` を書く（stream を止めない。gpu-op.h 117〜119 行の「executor が止める」は driver の段で起きる: driver が中継しない番号は driver が `ENOTSUP` で止める）。
- 1 packet は環の端を跨がない（§2.7）。class V の 1 packet は最大 256 KiB（ring は最大 1 MiB、`RING_SIZE` 10〜20）。**v1 の driver は class V の packet を `GHCI_FRAME`（8 dword。command の列は staging に置く、§2.4.6）だけにする**ので、この上限にも ring の満にも当たらない。command を直に ring に載せる形は封筒として許す（他の実装・試験用）。
- `RPTR`（§2.2）の意味は変わらない: class N は sequencer に渡した位置、class V は firmware が packet を解き終えて µop を sequencer に渡し終えた位置。driver は `RPTR` より前の ring を書き換えてよい（firmware は packet の中身を heap に写してから `RPTR` を進める。RTL と firmware の規則）。

#### 2.4.3 class N（µop）: hardware の sequencer が実行する物

版 2 の表をそのまま class N の標準の µop にする（意味は版 2 と同じ、§2.5 の FENCE の意味も同じ）。

| op | 名前 | payload | 意味 |
| --- | --- | --- | --- |
| 0 | NOP | n 個（読み飛ばす） | |
| 1 | SET_REG | (index, value) × n/2 | STATE[index] = value。index ≥ 512 は FAULT 種別 3 |
| 2 | BLIT | 0 | STATE の BLT の範囲で 1 回。flags[0] = 1 なら終わりまで sequencer が待つ |
| 3 | DRAW | 0 | VF を kick。開始の時に D$ と T$ を無効化 |
| 4 | DISPATCH | 3（gx、gy、gz） | compute。flags[1] = SA（この dispatch が systolic array を所有する、§3.9） |
| 5 | FENCE | 2（addr、value） | 完全な fence（§2.5）。flags[0] = 1 なら `IRQ_STATUS.FENCE` |
| 6 | WAIT_IDLE | 0 | flags = 待つ block の mask（0 は全部） |
| 7 | LOAD_CONST | 3（領域 k、addr、words） | constant の領域 k に DMA（実行中の warp が無い時。暗黙の `WAIT_IDLE(SU)`） |
| 8 | LOAD_PROGRAM | 1（stage） | 「stage の program が変わった」の印: I$ を無効化 |
| 9 | WAIT_FENCE | 2（addr、value） | memory の word が `(int32)(mem − value) ≥ 0` になるまで poll。watchdog の対象 |
| 10 | PERF_RESET | 0 | |
| 11 | WAIT_VSYNC | 0 | CAPS[10] の時だけ |
| 12 | INVALIDATE | 0 | flags の mask: [0] D$、[1] T$、[2] constant、[3] I$ |
| 13 | RESULT | 3（addr、status、aux） | **版 3 で追加**: `addr` に 2 word `{status, aux}` を書く（firmware が class V の result を書く道。driver も使ってよい）。FENCE と同じ順序の規則（先行する書き込みの後） |
| 14〜63 | reserved | | FAULT 種別 2 |
| vendor 0 | DRAW_RAW（Amaterrace） | 9（3 頂点 × x, y, z） | VF と SU 無しで RX に 1 つの三角形（p007） |
| vendor 1 | DBG_MARK（Amaterrace） | 1 | STATE[0x1FF] = payload |

- JOB_TAG・並列性（1 本の ring、1 queue）・順序の規則は版 2 のまま（§6.4）。
- sequencer への µop の入口は 2 つ: ring（class N）と firmware（`UOP_FIFO`、§2.4.7）。両方とも同じ封筒の形で、ring の順と firmware の順を CP が直列にする。

#### 2.4.4 class V: どの gpu-op を GPU が扱い、どれを driver が host で解くか

分類（zedBSD の driver の `vk/` が決める、§6.7）: **D** = driver だけ（ring に載せない）、**R** = そのまま中継（driver は検証と長さの計算だけ）、**R'** = driver が解いて書き換えた GHCI 固有の command（`GPU_OP_GHCI_*`、§2.4.5）にして中継、**X** = 拒む（`ENOTSUP`、libvulkan は `VK_ERROR_DEVICE_LOST`。bcm2711 と同じ）。原則: **host の memory・CPU の mapping・kernel の資源・SPIR-V と compile・driver が返事を作る物は D、GPU の object と GPU の仕事は R/R'**。

| gpu-op（番号） | 分類 | 根拠・中身 |
| --- | --- | --- |
| 0〜8、11、12、155（instance・physical device・device・queue） | **D**（12 は R も伴う） | 返事は driver が HCR の CAPS/CAPS2/VEND と §3.7 の limit から作る（bcm2711 の `fixed_query` と同じ形）。session の文脈（JOB_TAG）は node の `open` で作り（`GHCI_SESSION_OPEN`）、`close` で消す（`GHCI_SESSION_CLOSE`）。`CREATE_DEVICE` は D（driver の帳簿）、`DESTROY_DEVICE` は D + R（firmware はその device の識別子の下の object を返す。他の device の object は残る: 1 session に VkDevice が 2 つあってもよい）。`GET_DEVICE_QUEUE2` の timeline は 0 だけ（1 queue） |
| 9・10・13〜17（proc addr、layer、extension、GetDeviceQueue） | D | libvulkan が送らない（gpu-op.h） |
| 18 `QUEUE_SUBMIT` | **R** + FENCE | driver は packet の後に class N `FENCE(fence page, value = device の fence の次の値, IRQ)` を置き、session の「最後の fence の値」を更新する（jobs との結びは §6.4）。semaphore は 1 queue の in-order では順序に効かないので firmware は識別子の存在だけ確かめる。result = 受理の VkResult（firmware が packet を解いた時点で書く。§2.4.6） |
| 19・20（WaitIdle） | D | 送られない |
| 21 `ALLOCATE_MEMORY` | **R'** → `GHCI_MEMORY` | driver の allocator（§6.3）が GPU の address を取り（import の record なら blob の address）、`[memory の識別子][GPU address][bytes][flags]` を firmware に知らせる。firmware は memory の識別子 → address の表を持つだけ |
| 22 `FREE_MEMORY` | R | firmware は表から消す。driver は「その時点の session の最後の fence の値」が過ぎてから返す（§6.3） |
| 23〜27（map、flush、commitment） | D / X（27 は X） | |
| 28・29 `BIND_*_MEMORY` | R | firmware が buffer/image の object に address を結ぶ。driver も帳簿に bind を記録する（56 の返事と scanout の import のため） |
| 30・31 memory requirements、56 subresource layout、84 granularity | **D** | driver が buffer/image の create info（driver の帳簿、下の「driver が持つ物」）から計算。layout の式は `ghci_layout.h`（R23） |
| 32〜34 sparse | X | |
| 35〜39 fence | D | driver の fence の帳簿: VkFence → 「それを signal する submit の fence の値」。fence page と `FENCE_LAST` で答える（§2.5、§6.4） |
| 40・41 semaphore | R | firmware は識別子を覚えるだけ |
| 42〜46 event、47〜49 query pool | X | v1 は無し |
| 50〜55、57・58 buffer・buffer view・image・image view | R（driver は 50・54 の create info の形も帳簿に写す） | buffer view（52）は v1 の shader が使わない → R だが firmware は object を作るだけ |
| 59・60 `CREATE/DESTROY_SHADER_MODULE` | **D** | driver が SPIR-V の word を帳簿に持つだけ（compile は pipeline の作成の時。C1） |
| 65・66 `CREATE_GRAPHICS/COMPUTE_PIPELINES` | **R'**（`GHCI_PROGRAM` × stage の後に、command 自体を R で中継） | driver が `pStages[]` の module・stage・entry の名前を解き、stage ごとに SPIR-V を GSI1 に compile（`drv_gpu_shader_parse(words, count, stage, …)`、Guardrail: compile は kernel 空間）、binary（§3.4.4、interface の表を含む）を GPU の memory に置き、`GHCI_PROGRAM [pipeline の識別子][stage][ISA][address][bytes]` を pipeline ごと・stage ごとに送ってから create の command を中継する。firmware は (pipeline, stage) で program を引く。entry の名前: v1 は「その stage の entry が 1 つの module」だけ受け、複数なら `VK_ERROR_FEATURE_NOT_PRESENT`（共有の compiler に entry の選択を足すのは別の判断、§6.6）。compile の失敗は driver が返事に `VK_ERROR_INVALID_SHADER_NV` 相当（`VK_ERROR_INITIALIZATION_FAILED`）で書き、command を中継しない |
| 67 `DESTROY_PIPELINE` | R | firmware は object を消す。driver は pipeline の binary を「その時点の session の最後の fence」が過ぎてから返す（module の寿命とは無関係。H3） |
| 68・69 pipeline layout、70〜79 sampler・descriptor set layout・pool・set・update | R | firmware が descriptor set の内容を持ち、draw/dispatch の時に program の interface の表で (set, binding) → TU の slot・constant の領域・storage の base に写す |
| 61〜64 pipeline cache | D | 空の cache（`GET_PIPELINE_CACHE_DATA` は 0 byte） |
| 80〜83 framebuffer・render pass | R | |
| 85〜89 command pool・buffers | R | firmware が記録の置き場（heap）を持つ |
| 90〜92 begin/end/reset | R | firmware が記録の状態を変え、result を解いた時点で書く（§2.4.6）。driver は 90・91・92 の返事をその result から書く |
| 93〜95、98、100〜107、110、112〜116、119、121、126、132、133、135（記録される command のうち v1 が実装する物） | **R** | firmware が記録に積む（wire のまま写す）。`QUEUE_SUBMIT` で辿って µop にする。barrier（126）は `WAIT_IDLE` + `INVALIDATE`（§6.4 の規則）に。114 の拡大・縮小、121、115/116 の format の変換は BLT では出来ない → **firmware の内蔵の GSI1 の program**（firmware の像に入れた blit・clear の kernel。p018 の範囲、M16） |
| 96・97・99（line width、depth bias、depth bounds） | R（firmware が読んで捨てる） | i915 と同じ |
| 108・109・111（indirect） | X | v1 は `DRAW_INDIRECT` の µop が無い（§3.7） |
| 117・118（update/fill buffer） | **R** | firmware が BLT の µop（FILL、COPY）に。118 は BLT の FILL、117 は inline の byte を heap に写して COPY（M6 の規則で書き込みの応答を待ってから） |
| 120・122（clear depth/stencil、resolve） | 120 R（BLT の FILL で D24S8/D32F）、122 X | |
| 123〜125 event、127〜131 query、134 next subpass、136 execute commands | X | |
| 137 trailer、178 reply stream、179 seek | **D** | driver が返事と trailer を書く（§2.4.6）。ring に載せない |
| 180 `EXECUTE_STREAMS` | D | driver が blob の stream を自分で解いて（入れ子）、中の command を上の規則で扱う |
| 148・150・161（properties2、image format properties2、external buffer properties） | **D** | libvulkan が external memory の fd の道で送る（`external-properties.c`、`memory.c`）。driver が UUID と external の properties を答える |
| 0x10000〜0x1000d video | X | |
| `GPU_OP_GHCI_*`（§2.4.5） | — | driver が作る。libvulkan は送らない（userland からは `GHCI_NATIVE` だけを試験の build で受ける。他は `EINVAL`） |

- **driver が持つ物**（M1。「driver は Vulkan の object の意味を知らなくてよい」の正確な範囲）: 識別子の帳簿（種類と親の device）、buffer/image の create info（大きさ・format・tiling・usage・mip・layer: memory requirements・subresource layout・scanout の import のため）、memory の割当と bind、shader module の SPIR-V、pipeline → binary、fence の帳簿、command buffer の「begin 済み・end 済み」の状態（`QUEUE_SUBMIT` の検証）。持たない物: pipeline の状態の意味、descriptor の内容、render pass・framebuffer の意味、記録の中身。
- **GPU が「扱う」の意味**: firmware が object の意味（Vulkan の規格の、v1 の部分集合）を解いて µop にする。他の GHCI の GPU でも `vk/` は同じで、違うのは firmware。
- **どの session の仕事か**: JOB_TAG = session の番号（node の `open` ごとに 1〜255。0 は driver 自身の試験用）。firmware は tag ごとに object の表を分け、他の tag の識別子は見えない（識別子は libvulkan が session ごとに採るので衝突しない: gpu-op.h 59〜60 行）。v1 の fault の回復は device 全体のまま（§2.5）。

#### 2.4.5 GHCI 固有の gpu-op（`GPU_OP_GHCI_*`、`include/uapi/gpu-op.h` に足す。番号は **0x10100〜の案**、Q1 が確定（U10・U26））

| 番号（案） | 名前 | 引数（class V の wire） | 意味 |
| --- | --- | --- | --- |
| 0x10100 | `GPU_OP_GHCI_SESSION_OPEN` | [u32 tag][u32 flags][u64 heap の上限 byte] | firmware が tag の文脈を作る。result = 0 か `VK_ERROR_OUT_OF_DEVICE_MEMORY` |
| 0x10101 | `GPU_OP_GHCI_SESSION_CLOSE` | [u32 tag] | tag の object を全部返す。完了は後ろの FENCE で |
| 0x10102 | `GPU_OP_GHCI_MEMORY` | [u64 memory の識別子][u64 GPU address][u64 bytes][u32 flags（[0] host visible、[1] import）] | `ALLOCATE_MEMORY` の置き換え |
| 0x10103 | `GPU_OP_GHCI_PROGRAM` | [u64 pipeline の識別子][u32 stage][u32 ISA の FourCC][u64 binary の address][u32 bytes][u32 flags] | pipeline の stage の program（C1）。ISA は `ISA_ID`（標準 "GSI1"）か `NATIVE_ISA_ID`。それ以外は result = `VK_ERROR_INCOMPATIBLE_DRIVER`。他の実装の CP が JIT するのはここ（§3.4.5） |
| 0x10104 | `GPU_OP_GHCI_NATIVE` | [u32 count][count × class N の packet] | **試験用**: driver が白表で検査して class N として ring に写す。`CONFIG_GHCI_NATIVE` の build と root だけ。白表（試験の build）: NOP・SET_REG（index は全部）・BLIT・DRAW・DISPATCH（flags[1] の SA を含む）・FENCE・WAIT_IDLE・LOAD_CONST・LOAD_PROGRAM・INVALIDATE・RESULT。address の word は aperture の中かを見る（M20） |

- `GPU_OP_PROTOCOL_VERSION` は変えない（番号の追加だけ。i915 の video と同じ「自分の番号」の扱い。gpu-op.h の冒頭の文に GHCI の行を足すのは実装の Phase）。

#### 2.4.6 frame の運び方（staging）、返事（reply）と result、完了の通知

- **class V の frame は ring に直接は載せない**（H2・L6）: driver は 1 submission の中継する command の列（各 command に `[u32 番号][u32 flags][u32 result の index][引数…]` の封筒を付けた物）を **staging**（driver が GPU の memory に持つ環: 既定 4 MiB、aperture の中、session で共有）に写し、ring には class V の packet を 1 つだけ置く: dword1 = `GPU_OP_GHCI_FRAME`（0x10105、driver 内部）、dword4〜 = `[u64 staging の address][u32 bytes][u32 result page の address][u32 record の数]`。firmware はそこから読む。1 frame に割り込み 1 回、大きさの上限は staging（libvulkan の 1 MiB の記録の flush も入る）、ring の packet は小さいので ring が満ちる事は実質無い（満ちたら driver は `IRQ_STATUS[8] RETIRE`（版 3 で追加: `RPTR` が packet を越えるたび）を待つ）。
- **result の record**（2 word `{status, aux}` の列、frame ごとの result page）: firmware は **各 command を解いた時点で**（block の完了を待たず）その command の record を書く。`QUEUE_SUBMIT` の result は「記録を辿って µop を出せた」= 受理で、draw の完了ではない。frame の最後に firmware は「frame done」の record を書き、`IRQ_STATUS[7] RESULT` を立てる（CPU の制御 window から。H1）。firmware の DMA の書き込みが MC に受理された順で record が見えるよう、record の書き込みは firmware の DMA の応答を待ってから IRQ（M6）。
- **返事は driver が全部書く**（gpu-op.h 75〜79 行の形、trailer を最後に release store で。powervr の `vk-stream.c` の `version_reply` と同じ）。firmware は返事を書かない（U24）。返事の cursor の順序（M2）: driver は frame を解く時に、D の返事はその場で書き、R/R' の REPLY を求める command の返事の **大きさを計算して cursor の場所を空け**（返事の形は gpu-op.h の「Reply」で全部決まっている: `[number][VkResult]`、create の form の `[u64 1][識別子]`、`ALLOCATE_COMMAND_BUFFERS` の `[u64 count][…]` など）、result が来てから埋める。
- **ioctl の中で眠らない**（M3）: `commands.submit`（`GPU_COMMAND_SUBMIT`、`drv_gpu_completion` 付き）は staging に写し、ring に packet を置き、doorbell を鳴らして返る。device ごとの **reply thread**（kernel の thread）が `RESULT` の IRQ で起き、frame の record を読んで返事を埋め、trailer を release store で書き、**その後に** `drv_gpu_complete(completion, 0)`（libvulkan は通知を待ってから trailer を poll する: `context.c` の `vulkan_decoder_wait` → `vulkan_context_poll`）。REPLY を求める R/R' の無い frame（記録の送り）も同じ道（firmware の「frame done」で完了。順序を保つため）。fault の時は reply thread が pending の frame を error で完了する（`recovery.fault`）。
- 誤りの種類: (1) driver の検証で落ちる（`EINVAL`/`ENOTSUP`、ring に載せない。libvulkan は `DEVICE_LOST` にする）、(2) firmware が command を解けない/状態が使えない（record に VkResult、frame は続く、libvulkan はその command の VkResult を見る）、(3) 実行中の fault（§2.5、device 全体）。

#### 2.4.7 CP の構成: firmware か hardware か（推し: firmware。U19）

| 案 | 中身 | 7010 の資源（推定） | 工数 | 判断 |
| --- | --- | --- | --- | --- |
| **F. firmware**（推し） | `atg_cp`（HCR、ring の DMA、class N の sequencer、fence、IRQ、fault = P2 が作っている物）+ **`atg_cpu`**（RV32IM、in-order 3 段、IMEM 32 KiB、DMEM 16 KiB、AMB の port 1 つ、µop の FIFO、mailbox）+ firmware（C、`fw/cp/`）。firmware が gpu-op の wire を解き、object を heap（`CP_HEAP_*`、GPU の memory）に持ち、µop を出す | CPU LUT 2.5k、FF 1.5k、DSP 1（mul）+ 除算器 0.3k LUT、BRAM36 12（IMEM 8 + DMEM 4）。bitstream 1「2D」4.7k → **7.5k（43%）**、BRAM 5 → 17 | core 12 LW + firmware の runtime 20 LW + Vulkan の object model 44 LW（±50%）= 76 LW | 柔軟（Vulkan の規格の変化は firmware で吸収）。RTL は小さいまま。性能: draw 1 つの変換は object を heap（uncached の DDR、DMA 経由）から読むので **数 µs〜十数 µs** の見込み（M21。p017 の最初に測る。vkdemo・compositor の数百 draw/frame で数 ms なら v1 は受け、足りなければ object を DMEM に cache） |
| H. hardware で解く | RTL の状態機械が gpu-op の wire（pNext の chain、配列、入れ子の構造体、u64 の識別子の表）を解く | 部分集合（BIND・DRAW・SET_VIEWPORT・PUSH_CONSTANTS・BEGIN_RENDER_PASS）だけでも LUT 6〜10k + object の表の BRAM。pipeline の作成は不可能に近い | 30 LW 以上、変更のたびに合成 | **採らない**。結局 driver が object を解いて µop を作る事になり、「ring は gpu-op」の決定に反する |
| F'. firmware、CPU は sukuna-risc | ユーザーの CPU をそのまま | README: RV64GC、2-issue、SMT、**約 22,000 LUT**（7010 の 125%） | — | v1 では不可。**RV32 の最小の構成が sukuna-risc に出来たら `atg_cpu` を差し替えられる形**にする（同じ bus の口: 命令 fetch は IMEM の BRAM、data は DMEM + AMB の port + 制御 register の window。U20） |

- **`atg_cpu` の仕様（案）**: **RV32IM の全部を hardware**（`mul*` は DSP、`div*`/`rem*` は 32 cycle の逐次の除算器 ≈ 0.3k LUT。H5: `-march=rv32im` の clang は `div` を出すので software にはしない）、C 無し、FPU 無し、Machine mode だけ、割り込みは 1 本（ring に class V の packet が来た、fence の完了、HALT の要求）、`mcycle`、`csr` の代わりに memory-mapped の制御 window（`0xF000_0000`〜: 自分の packet の address と長さ、`UOP_FIFO`（µop の dword を書く。満なら stall）、**STATE の読み**（書きは `UOP_FIFO` の `SET_REG` だけ: 非同期の block が読んでいる STATE を直に書かない。H10）、`RPTR` の進め、DMA（AMB の line の読み書き、heap ↔ DMEM。書き込みの応答の数が読める）、`FENCE_LAST`、`COUNTER`、mailbox 4 word（`CPFW_MBOX`）、`RESULT` の IRQ、`DONE`（packet の処理の終わり）。3 段 pipeline（fetch、decode/execute、writeback）で 7010 -1 で 100 MHz、325T -2 で 200 MHz（R25）。**RTL は amaterrace-gpu の `rtl/gpu/cp/atg_cpu.v`**（U20）。
- **firmware**: C、`-ffreestanding -nostdlib -march=rv32im -mabi=ilp32`、共有の LLVM の clang（`build/llvm/bin/clang --print-targets` に riscv32 が有る、実測。**compiler-rt の builtins は無い**（`build/llvm/lib/clang/<版>/` に `lib/` が無い、実測）ので `fw/cp/` に `memcpy`・`memset`・`memcmp`・`__udivdi3`・`__umoddi3` 等を自前で持つ。toolchain は変えない）。大きさの目標: 64 KiB 以内（IMEM 32 KiB に入らなければ IMEM を 64 KiB = BRAM36 16 に。p017 の最初に測る、R26）。**firmware は bitstream の BRAM に初期値として入れる**（IMEM と DMEM の 2 つの像。`$readmemh`。driver は file を要らない）+ **`CPFW` の extended capability で host から載せ替えられる**（`CPFW_IBASE/IWORDS`（IMEM）と `CPFW_DBASE/DWORDS`（DMEM の .data と .rodata の像）の 2 対。H4）。**再起動の規則**: `.data` の原本は DMEM の像の中の「初期値の区画」に置き、`boot.S` が起動のたびに作業の区画へ写す（BRAM は reset で初期値に戻らないため）。p017 の受け入れに「書き換えた `.data` が RESET の後に戻る」を入れる。zedBSD の tree に blob を置くかは U21。
- **heap**: driver が attach で `CP_HEAP_BASE/LIMIT`（aperture の中、既定 8 MiB。**driver の allocator はこの領域・ring・staging・fence page・result page を session に貸さない**、L14）を与える。firmware の allocator（first-fit、session ごとの上限）。object の表は識別子の hash（64 bit → 12 bit、chain）。記録（command buffer）は heap の中の chunk の list。
- **watchdog と状態遷移**（M7）: class V の packet の処理も `WDT_CYCLES` の対象。counter は **CPU が `UOP_FIFO` の満で止まっている cycle と DMA の応答待ちの cycle を数えない**（長い submit で誤って発火しないため）。`HALT`・`STEP` の「1 command」は class V では **1 packet（= 1 frame）**。`CPFW_CTRL.HALT` は「今の packet の後で CPU を止める」（`CTRL.HALT` の quiesce の一部として CP が自動で行う）。`CPFW_CTRL.LOAD` は IDLE・HALTED で、かつ CPU が止まっている時だけ受ける。`CAPS[17] VKCP` は **静的**（bitstream に firmware が入っている）、走っているかは `CPFW_STATUS.READY`。reset で残る register に `CP_HEAP_*`・`CPFW_IBASE/IWORDS/DBASE/DWORDS`、消える物に `CPFW_STATUS`・CPU の状態（§2.6 の表に追記）。firmware が止まった時は FAULT 種別 4 → §2.5 の回復（`RESET` は `atg_cpu` も reset し、firmware は BRAM の像から再起動。heap は driver が作り直す）。
- **ZYBO の bitstream**: 1「2D」(p003・p004) に CPU は入れない。**1'「2D + FW」**（CP + CPU + MC + BLT、p017）で class V の道（`CMD_FILL_BUFFER`/`CMD_COPY_BUFFER` → BLT）を板で確かめる。3「raster」と 4「compute」には CPU を入れない（4 は 78% + 2.5k = 92% で閉じない見込み、U25）: SU と RX/ROP は class N で確かめ、class V の全部の道は Verilator と KC705（p011・p014）。

#### 2.4.8 firmware の中身（p017・p018 の設計の入口）

- 構成: `boot.S`（stack、bss、`.data` の写し、割り込みの入口）、`packet.c`（frame の受け取り、wire の reader: u32/u64/構造体/配列/pNext の chain。gpu-op.h の「wire form」56〜66 行の規則。**codec は driver と firmware で 1 つの表から生成する**（`ghci_vkwire.inc`: command ごとの引数の形。M21。i915 の `vulkan-codec.inc` は 2808 行、同じ物を 2 度は書かない）、`heap.c`、`object.c`（識別子の表、種類ごとの struct）、`memory.c`（識別子 → address）、`program.c`（binary の header と interface の表の検証、§3.4.4。他の実装の JIT の場所）、`pipeline.c`（create info → STATE の word の列）、`descriptor.c`、`pass.c`（render pass・framebuffer → ROP/RX の STATE、clear → BLT の FILL か内蔵の kernel）、`record.c`（記録の積み・辿り）、`submit.c`（記録 → µop: bind の状態機械、draw の前に変わった STATE だけ `SET_REG`、push constant → `LOAD_CONST`、barrier → `WAIT_IDLE`/`INVALIDATE`）、`blit.c`（内蔵の GSI1 の program: 拡大・縮小の blit、format の変換、clear attachments。binary は firmware の像に入れ、起動で heap に写す）、`uop.c`（`UOP_FIFO`）、`result.c`。
- **memory の順序の規則**（M6: `atg_mc` は client の間の順序を守らない）: firmware が DMA で heap に書いた物（inline の data、内蔵の program、push constant の写し）を他の client（CP の `LOAD_CONST`、BLT、SU、TU）が読む µop は、**その書き込みの応答が全部返ってから** `UOP_FIFO` に出す（window の「DMA の応答の数」で確かめる）。
- 1 draw の µop の列（典型）: `SET_REG` × 10〜40（変わった物だけ）、`LOAD_PROGRAM`（program が変わった時）、`LOAD_CONST`（push constant が変わった時: 暗黙の `WAIT_IDLE(SU)` で pipeline が空く。R24）、`DRAW`。
- 誤りは result に VkResult。firmware が自分の不変条件を破った時（assert）は `CPFW_STATUS.FAULT` と FAULT 種別 4 の道。
- **他の GHCI の実装**: firmware は vendor の物（GHCI は firmware の中身を決めない。決めるのは class V の意味 = Vulkan の規格の部分集合と、result・frame・session・program・memory の固有の command）。

#### 2.4.9 class V の 1 frame の流れ（vkCmdDraw を含む command buffer の submit）

1. app → libvulkan: `vkEndCommandBuffer` で記録（93〜135）と `END_COMMAND_BUFFER(91)` を 1 frame に、`GPU_COMMAND_SUBMIT`（libvulkan/context.c の transaction、`drv_gpu_completion` 付き）。
2. driver `commands.submit`（§6.7）: frame を解く。`SET_REPLY_STREAM` で返事の blob を選ぶ（D）。記録の各 command: 番号が R の白表に有り引数が解ける → staging に写す（result の index を付ける）。`END_COMMAND_BUFFER` も R（返事の場所を空ける）。class V の packet（`GHCI_FRAME`、staging の address と長さ、result page）を ring に置き、doorbell（`RING_WPTR`）。返る。
3. CP: ring から class V の packet → `atg_cpu` に割り込み → firmware が staging を読み、記録を heap に積み、record を書き、「frame done」→ `RESULT` の IRQ → `DONE` → `RPTR`。
4. driver の reply thread: record を読み、`END_COMMAND_BUFFER` の返事（VkResult）と trailer を書き、`drv_gpu_complete`。libvulkan の `vkEndCommandBuffer` が返る。
5. app → `vkQueueSubmit`: libvulkan は `GPU_JOB_RESERVE`（token = driver の marker）→ frame `QUEUE_SUBMIT(18)` + trailer を `GPU_COMMAND_SUBMIT` → `GPU_JOB_COMMIT`。driver: 2 と同じく staging + packet、その後ろに class N `FENCE(fence page, value = device の次の fence の値, IRQ)`、session の「最後の fence の値」を更新、doorbell。firmware が `QUEUE_SUBMIT` を解いて記録を辿り µop を流し、受理の record を書く → reply thread が返事と trailer → `drv_gpu_complete`。`commit`（§6.4）は marker を「最後の fence の値」に結ぶ。
6. sequencer: 先行の DRAW の完了を待って fence page に書き、`FENCE_LAST` を更新、`IRQ_STATUS.FENCE`。driver の IRQ の bottom half: fence page の値を読み、`(int32)(value − marker の値) ≥ 0` の marker を `drv_gpu_complete`（job の完了、libvulkan の `vkWaitForFences` が起きる）。

#### 2.4.10 P2 の p002（HCR・ring・fence・IRQ）との整合

- 変えない: HCR の offset と bit のうち P2 の `ghci_regs.h`/`.vh` にある物（§2.2。**P2 の実装は EXTCAP の list が 0x100、`PERF` の capability の register（`PERF_SRCn`）が 0x110〜0x11F、`PERF` の counter が 0x080〜0x08C**。版 3 の追加はその後ろに置く、M5）、CP の状態遷移（§2.6）、ring の約束（§2.7）、fence・IRQ・fault・watchdog・reset（§1.3・§2.5）、`KICK` と STATE の alias（class N の無い最初の道）。
- 足す（§2.2 の表に追記、P2 は p002 では触らない）: `NATIVE_ISA_ID`（0x048）、`CP_HEAP_BASE/LIMIT`（0x0B0/0x0B4）、`VEND`（0x0C0〜0x0DF、8 word: NWARPS、NREGS、NPHYS、PACKED、SB_ENTRIES、PT の大きさ、reserved × 2）、`CPFW` の extended capability（id 6、register は **0x120〜0x13F**: `CPFW_CTRL`（[0] LOAD W1S、[1] RUN、[2] HALT）、`CPFW_STATUS`（[0] READY、[1] FAULT、[2] ALIVE、[15:8] firmware の状態）、`CPFW_IBASE`・`CPFW_IWORDS`（IMEM の像）、`CPFW_DBASE`・`CPFW_DWORDS`（DMEM の像）、`CPFW_VERSION`、`CPFW_MBOX0..3`）、`SA` の capability（id 7、**0x140〜0x14F**、§3.9.3）、`CAPS[17] VKCP`（静的: firmware が入っている）、`CAPS[18] FWLOAD`、`CAPS[19]`（`NATIVE_ISA_ID` ≠ `ISA_ID`）、`CAPS[24] SUBGROUP`、`CAPS[25] SA`、`IRQ_STATUS[7] RESULT`・`[8] RETIRE`、`STATUS[7] SA_BUSY`、FAULT 種別 8。`CAPS2` は不変。`UOP_FIFO` は CPU の window だけ（HCR には出さない）。
- 変える: ring の packet の封筒（§2.4.2。p004 がこの形で作る。p002 の `ghci_packets.h` は placeholder のままでよい）、class N の `RESULT` µop（13）、class 3 の詰め物。
- firmware が無い構成（`CAPS[17]` = 0、ZYBO の bitstream 1・3・4）: driver は class V を送らず、`vk/` は attach しない。device の capability は `RESOURCE | TRANSFER | MAPPING | BLOB | SHARE | ALLOCATION_SHARE`（+ `DISPLAY | DISPLAY_EVENTS`）だけで、`CAPSET`・`COMMAND`・`NOTIFICATION`・`JOB` は名乗らない（libvulkan は device を作らない。L12）。`CAPS[2]` = 1 でも `CAPS[17]` = 0 なら Vulkan の device は出さない。

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

- reset で **残る** register: `APERTURE_*`、`RING_BASE/SIZE`、`IRQ_ENABLE`、`WDT_CYCLES`、`SCRATCH`、STATE、`CP_HEAP_*`、`CPFW_IBASE/IWORDS/DBASE/DWORDS`、`SA_SU`（版 3）。**消える**: `RING_WPTR/RPTR`（0）、`IRQ_STATUS`、`FAULT_*`、`FENCE_LAST`（driver が書き戻す）、`CTRL.RUN`、`CPFW_STATUS`、CPU の状態（firmware は BRAM の像から再起動）、SA の FIFO と accumulator、block の内部（cache、PT、warp）。driver は reset の後に ring を先頭から使い直す。

### 2.7 ring の約束

- ring は `RING_SIZE` の byte の、dword の環。`WPTR`・`RPTR` は dword の index（0 ≤ x < size/4）。空: `WPTR == RPTR`。満: `(WPTR + 1) mod n == RPTR`（**1 dword を空ける**）。driver の空きは `(RPTR − WPTR − 1) mod n`。
- packet は環の端を跨がない: 残りに入らない時は driver が class 3 の詰め物（§2.4.2、n = 残り − 1。n が 255 を超えてもよい）で端まで埋めて先頭へ。CP は header の `n` が `WPTR` を越える時（driver の誤り）を FAULT 種別 3 とする（待たない。driver は **packet 全部を書いてから** `WPTR` を進める）。
- doorbell の前の CPU 側の順序: ZYBO は uncached（Normal non-cacheable）の書き込みの後、device の register の書き込みの前に **`dsb st`**（`kern_` の mmio の write が barrier を含むかは p006 で確かめ、含まなければ driver が明示に置く）。KC705 は BAR1（AXI-MM の posted write）の後に **BAR1 の最後の word を読み戻してから** BAR0 の doorbell を書く（posted write の追い越しを防ぐ）。
- CP は doorbell のたびに `RPTR` の line を **読み直す**（先読みした古い line を捨てる）。CP の先読みは `WPTR` まで。
- ring が満ちた時: driver は `IRQ_STATUS[8] RETIRE`（`RPTR` が packet を越えた）を待って空きを得る（`RING_EMPTY` の IRQ は使わない）。v1 の driver の packet は小さい（frame は 8 dword、FENCE は 4 dword）ので実質起きない。
- `RPTR` は「parse して block に渡した」位置。driver は `RPTR` で ring の再利用を判断してよい（packet の中身は CP が block に写し終えているので、`RPTR` より前は書き換えてよい。block が参照する memory（頂点、texture）は fence で守る）。

## 3. SU（Shader Unit）と GHCI の標準の shader ISA「GSI1」（版 3。amaterrace-gpu の `docs/gsi1.md` の内容。版 2 の "ATS1"（64 bit の独自の形）を置き換える）

2026-10-23 ユーザー「ISAはRISC-Vベースでカスタムしましょう。RISC-V仕様に則る必要はないです。ベースにしつつ、他のGPUの平均を取って、実装しやすくよく使われるものを、我々の命令形式にフィットさせて取り込んでいきましょう。」「GHCIは標準のシェーダーISAを定義し、…」「ネイティブISAを直接送信も可能。」

- **名前**: GHCI Shader ISA 1、FourCC **"GSI1"**（`0x47534931`）。`ISA_ID` = "GSI1" は GHCI 1.0 の shader 付きの GPU の全部で同じ（標準の ISA）。Amaterrace の native の ISA は GSI1 そのもの（`NATIVE_ISA_ID` = "GSI1"、`CAPS[19]` = 0）。版 2 の "ATS1" の名前と `isa/ats1/` は **`isa/gsi1/`** に変える（U18）。
- **RISC-V を基にする、とは**: (a) 命令の低位の 32 bit は RISC-V の 32 bit の形（R・R4・I・S・B・U の field の位置と、7 bit の major opcode の値、funct3/funct7 の値）そのもの、(b) 整数・load/store・分岐・乗除・単精度の float の命令は RISC-V（RV32IMF + Zfa の `fround`、Zbb の min/max/andn）の mnemonic と意味を使う、(c) GPU の物（exec mask、texture、export、LDS、quad、shuffle、packed fp16、超越関数）は RISC-V の custom の major opcode（custom-0〜3）に置く。**RISC-V の規格には従わない所**: 64 bit 固定長（§3.3）、register が 64 本で x0 を 0 に固定しない、比較の結果は all-ones/0 の Boolean（§3.2）、FP の register file を分けない（1 つの register file）、CSR・ecall・特権・例外は無い、`fdiv` 無し。RISC-V の toolchain はそのまま使わない（assembler は自前の `gsi1asm.py`。mnemonic が同じなので読みやすい）。
- 他の GPU から取る物（共通で、実装しやすい物）: 表は §3.4.6。

### 3.1 実行の model

- **warp は 32 lane の scalar、16 bit packed 付き**（README）。lane ごとに 32 bit の register の列。physical lane は `NPHYS`（8/16/32）で、1 命令を `32/NPHYS` cycle で流す。ISA・compiler・driver・API から見える幅は常に 32（subgroup の幅 = 32）。同じ binary が NPHYS に依らず動く（p008 の受け入れ）。
- **SM/CU に当たる単位**（版 2 と同じ）: NPHYS の SIMD の datapath、`NWARPS` の常駐の warp の round-robin の scheduler、BRAM の register file（§3.5）、I$・D$、LDS、constant bank、PT、primitive table。
- **warp の状態**: PC（命令の index）、`EXEC`（32 bit の mask、compiler が `exec.*` で管理。hardware は再収束を扱わない）、`KILLED`（sticky、fragment の discard）、scoreboard の entry（§3.1.1）、`WAIT` の counter（class m・t・x の未完の数）、warp の種類（vertex/fragment/compute）と workgroup の番号。
- **predication**: lane が EXEC の外なら register の書き込みと memory・texture・export の副作用を行わない。`KILLED` の lane は副作用（`sw`・`atom`・`exp`・`tex`）を行わないが EXEC は変えない（版 2 と同じ）。**helper lane**（M10）: fragment の warp では RX が EXEC を **quad 単位**（4 lane の全部）で立て、三角形の外の lane（helper）も `qd` と `texb` の微分のために走らせる。副作用の mask = coverage（特殊 operand 119 `mask`、RX が書く）& ~KILLED で、helper の lane は `sw`・`atom`・`exp` を行わない（Vulkan の規則）。compute・vertex の warp では `mask` = EXEC の初期値。
- **分岐**: warp 単位。`bany`/`bnone`（Boolean の register を EXEC の lane で見る）、`bexnz`/`bexz`（EXEC が 0 でない/0）、`j`。compiler は IR（if-converted、`SELECT`）のまま、loop と「副作用の有る区間の skip」だけに分岐と EXEC を使う（§6.6）。
- **精度**: fp32 は IEEE の形、丸めは命令の rm（既定 RNE）、denormal は flush（入力も出力も）。fp16 の packed も同じ規則。超越は 2 ulp 程度、`rcp`・`rsq` は 1 ulp（table + Newton 1 段）。整数は 32 bit の 2 の補数。
- **命令の発行**: warp ごとに in-order、1 命令ずつ。短い命令（class a: ALU、class f: FP の pipeline）は順に完了する。長い命令（class m: memory、t: texture、x: 超越・除算）は順不同に戻る → scoreboard が依存を守る。

#### 3.1.1 scoreboard（2026-10-23 ユーザー「数は少なくていいので、scoreboardを実装できないでしょうか。性能に大きく影響しますので」）

- warp ごとに **`SB_ENTRIES` 個**（ZYBO 4、KC705 8。p008 で LUT を実測して決める）の entry `{valid, dst[6:0], class[1:0], width[1:0]}`。長い命令（class m・t・x）の発行で entry を取り（dst と、texture なら dst..dst+3 の幅）、戻りの書き込みで返す。entry が無ければ発行を止める（他の warp は発行できる）。
- 発行の検査（毎 issue）: 命令の source（rs1・rs2・rs3 と、`texl`/`texb`/`texf` の暗黙の rs1+1）と dst（rd と、texture の暗黙の rd..rd+3）の **範囲**を全 entry の dst の範囲と比べ、重なれば止める（RAW・WAW。WAR は source を発行の段で読む in-order の設計なので起きない: `sw`・`exp`・`atom` の data も発行の段で読む）。範囲の重なりは entry ごとに 2 つの比較（base ≤ x < base + width）× 5 operand × 8 entry ≈ 400 LUT（版 3 の見積もりを直した、M9）。
- **`wait` 命令は残す**（class ごとの counter: cm = 未完の `sw`/`atom`（BRESP 待ち）、ct = 未完の texture、cx = 未完の超越・除算）。値の待ちは scoreboard が行うので compiler は `wait` を **store の完了の順序（`fence`・`bar`・`end` の前）**にだけ使う。`end` は暗黙に `wait 0,0,0`。
- 短い命令（class a・f）の結果は **1 本の固定長の pipeline**（a も f と同じ段で書き戻す。長さは f の 5〜6 段）の順に書き戻る（bypass 無し）ので、a と f の間の WAW は起きない。同じ warp の次の発行は `32/NPHYS` cycle 後: NPHYS 8（4 cycle）では RF の読みの段が結果の書き戻しの 1〜2 cycle 前に来るので **interlock で 1〜2 cycle 待つ**（版 2 の「4 cycle が隠れる」は誤り、M9）。NPHYS 16 で 3〜4、NPHYS 32 で 4〜5 cycle。NWARPS の他の warp が埋める。
- EXEC が全部 0 の class m・t・x の命令は memory・texture の要求を出さず、scoreboard の entry も取らない（戻りが無いので待てない、M9）。lane ごとの mask は要求の数だけを変え、entry と `cm`・`ct`・`cx` の数は命令ごとに 1。`sw` の `cm` は MC の BRESP で減る（aperture の外は error の応答で減る）。
- compiler の scheduling（§6.6）: 長い命令の結果を使う命令を遠ざけ、独立の命令で埋める。NWARPS の他の warp が残りの latency を隠す。

### 3.2 register と operand の空間

| operand の番号（7 bit） | 意味 | 使える所 |
| --- | --- | --- |
| 0〜62 | `r0`〜`r62`（thread ごとの 32 bit の GPR。実際の数 `NREGS` は `VEND` の register。NREGS = 32 の構成で r32〜r62 を使う program は `GHCI_PROGRAM` の result で拒む: header の `nregs` > NREGS） | rd・rs1・rs2・rs3 |
| 63 | **`rq`**: systolic array の FIFO の口（dst に書く = push、source に読む = pop。GPR ではない。全部の実装で予約、§3.9.1） | rd・rs1・rs2・rs3（SA の dispatch の warp だけ。他は ISA の fault） |
| 64〜95 | `c0`〜`c31`（constant bank の先頭 32 word。以降は `lwc`） | rs1・rs2・rs3 |
| 96 `zero`、97 `one`（1.0f）、98 `onei`（1）、99 `half`（0.5f）、100 `two`（2.0f）、101 `mone`（−1.0f）、102 `lane`（0〜31）、103 `warp`、104 `exec`、105 `killed`（自分の lane の discard の bit を Boolean に）、106〜108 `wgx/wgy/wgz`、109〜111 `lidx/lidy/lidz`、112 `lidn`（local index）、113 `prim`、114 `front`（Boolean）、115〜117 `nwgx/nwgy/nwgz`、118 `oneh2`（packed 1.0h × 2）、119 `mask`（sample/coverage の mask、v1 は 0xFFFFFFFF）、120 `qstat`（SA の FIFO の数、§3.9.1）、121 `sastat`（SA の状態）、122〜127 reserved | 特殊（inline の定数と system の値。AMD の inline constant、NVIDIA の `SR_*` に当たる） | rs1・rs2・rs3 |

- **x0 の固定は無い**: `r0` も普通の register。RISC-V の pseudo（`mv rd, rs` = `addi rd, rs, 0`、`not` = `xori rd, rs, -1`、`neg`）はそのまま使える。`nop` は custom-3 の funct3 111 sub 0（何も書かない。`addi r0, r0, 0` は r0 の scoreboard に掛かるので nop にしない、L11）。
- **Boolean**: 32 bit の all-ones = true、0 = false（共有の IR と同じ）。**全部の比較命令（`slt`・`sltu`・`slti`・`sltiu`・`feq`・`flt`・`fle`・`fne`、packed の `fcmp2`）は Boolean を書く**（RISC-V の 0/1 からの逸脱。U23）。`exec.*`・`bany`・`ball`・`kill`・`vote`・`ballot` は Boolean を「0 でなければ true」と読む。`sel` は **bit ごと**（rd = (rs1 & rs2) | (~rs1 & rs3)、IR の SELECT と同じ。M15）。
- 符号化: rd・rs1・rs2 は word0 の RISC-V の位置に下位 5 bit、word1 に上位 2 bit（§3.3）。rs3 は R4 の位置（word0[31:27]）に下位 5 bit、word1 に上位 2 bit。特殊 operand（96〜127）は上位 2 bit が 11 なので **word1 が 0 の命令は r0〜r31 だけを使う純粋な RISC-V の形**になる（手で読める・書ける）。

### 3.3 命令の形（64 bit 固定。word0 = RISC-V の 32 bit の形、word1 = 拡張。U22）

```
word0（little endian の低位の 32 bit。RISC-V の形そのまま）:
  R   [6:0] opcode [11:7] rd [14:12] funct3 [19:15] rs1 [24:20] rs2 [31:25] funct7
  R4  [6:0] opcode [11:7] rd [14:12] rm/funct3 [19:15] rs1 [24:20] rs2 [26:25] fmt/funct2 [31:27] rs3
  I   [6:0] opcode [11:7] rd [14:12] funct3 [19:15] rs1 [31:20] imm[11:0]（符号拡張）
  S   [6:0] opcode [11:7] imm[4:0] [14:12] funct3 [19:15] rs1 [24:20] rs2 [31:25] imm[11:5]
  B   [6:0] opcode [7] imm[11] [11:8] imm[4:1] [14:12] funct3 [19:15] rs1 [24:20] rs2 [30:25] imm[10:5] [31] imm[12]（imm[12:1] を 12 bit の符号付きの**命令数**として読む: 目標 = PC + sext(imm[12:1])。±2048 命令。imm[0] は無い）
  U   [6:0] opcode [11:7] rd [31:12] imm[31:12]
word1（拡張。0 なら「RISC-V の形のまま、r0〜r31、修飾無し」）:
  [1:0] rd[6:5]  [3:2] rs1[6:5]  [5:4] rs2[6:5]  [7:6] rs3[6:5]
  [8] n1（rs1 を負に）[9] a1（rs1 の絶対値）[10] n2 [11] a2 [12] n3 [13] a3   ※ float の命令だけ。整数の命令では 0
  [14] sat（結果を [0,1] に飽和。float だけ）
  [16:15] hsel（packed: 00 両方、01 low だけ、10 high だけ、11 swap して両方）
  [21:17] sub（副種別: atomic の種類、texture の slot、quad の種類、shuffle の種類、超越の種類、sactl/exec の種類）
  [31:22] imm_hi[9:0]（I/S/B の imm の上に足して 22 bit の符号付き offset）
  U 形（`lui`/`movi`）だけ別: [1:0] rd[6:5]  [13:2] imm[11:0]（`movi` の下位 12 bit。`lui` は 0）  [31:14] 0
```

- **なぜ 64 bit 固定か**（U22。§10 の 23・24）: (1) decode が 1 種類（I$ から毎 issue 1 word、PC は命令の index）、(2) FMA の 3 source + 修飾 + 64 本の register + constant/特殊 operand が入る、(3) word1 = 0 で RISC-V の 32 bit の形がそのまま読める、(4) I$ 2 KiB = 256 命令で版 2 と同じ（shader は小さい）。AMD の 32/64 可変・NVIDIA の 128 固定・PowerVR の 64 固定の中で、FPGA で最も単純な物。
- 命令の index は PC（8 byte 単位）。program の base は `VS_BASE` 等（§2.3、8 byte 揃え）。分岐の offset は命令数。
- 即値（M11）: I/S の offset = **sext(imm12) + (sext(imm_hi[9:0]) << 12)**（22 bit の符号付き、±2 MiB。word1 = 0 なら RISC-V の `lw r1, -4(r2)` と同じ値）。B の offset は上の命令数。`lui rd, imm20` は RISC-V と同じ（rd = imm20 << 12）。`movi rd, imm32` は `lui` と同じ符号化で word1[13:2] に imm[11:0] を足した物（rd = (imm20 << 12) | imm[11:0]。word1[13:2] = 0 なら `lui`）。`movi` は assembler の名前で、decoder は `lui` の 1 種類。

### 3.4 命令の一覧と opcode の対応（v1）

#### 3.4.1 major opcode の対応（word0[6:0]。RISC-V の値）

| opcode | RISC-V の名前 | GSI1 での用途 |
| --- | --- | --- |
| 0110011 | OP | 整数 R: `add sub sll slt sltu xor srl sra or and`、M: `mul mulh mulhsu mulhu div divu rem remu`（funct7 0000001。`div*`/`rem*` は共有の除算器、class x）、Zbb: `min max minu maxu`（funct7 0000101）、`andn orn xnor`（funct7 0100000、funct3 111/110/100） |
| 0010011 | OP-IMM | `addi slti sltiu xori ori andi slli srli srai` |
| 0110111 | LUI | `lui`、`movi`（word1 に imm[11:0]） |
| 0010111 | AUIPC | reserved（PC 相対の data は無い） |
| 0000011 | LOAD | `lw rd, imm(rs1)`（funct3 010、global、D$）、`lwc rd, imm(rs1)`（funct3 110、constant bank を word の index で。rs1 は index、imm は word）。`lb lh lbu lhu` は reserved（v2） |
| 0100011 | STORE | `sw rs2, imm(rs1)`（funct3 010）。`sb sh` は reserved |
| 1100011 | BRANCH | funct3 000 `bany rs1, off`（EXEC の lane のどれかで rs1 ≠ 0）、001 `bnone rs1, off`、010 `ball rs1, off`、011 `j off`、100 `bexnz off`（EXEC ≠ 0）、101 `bexz off`。RISC-V の `beq` 等（lane ごとの条件）は無い（warp 単位の分岐だけ） |
| 1101111 / 1100111 | JAL / JALR | reserved（v1 は subroutine 無し。v2 で `jal`） |
| 1010011 | OP-FP（fmt = 00、single） | `fadd.s fsub.s fmul.s`（funct7 0000000/0000100/0001000）、`fsgnj.s`（= `fmv`、修飾で neg/abs が出来るので `fsgnjn/fsgnjx` は pseudo）、`fmin.s fmax.s`（0010100）、`fsqrt.s`（0101100、class x）、`fcvt.w.s fcvt.wu.s`（1100000、rm: rtz が IR の F2I）、`fcvt.s.w fcvt.s.wu`（1101000）、`feq.s flt.s fle.s`（1010000、funct3 010/001/000）+ **`fne.s`（funct3 011、unordered も true）**、`fround.s`（Zfa: 0100000、rs2 = 00100、rm: rne/rtz/rdn/rup = round even/trunc/floor/ceil）。**`fdiv.s` は無い**（`rcp` + Newton を compiler が組む。IR に FDIV は無い）。`fclass fmv.x.w fmv.w.x` は無い（register file は 1 つ） |
| 1000011 / 1000111 / 1001011 / 1001111 | MADD / MSUB / NMSUB / NMADD | `fmadd.s rd, rs1, rs2, rs3`（rd = rs1×rs2 + rs3）、`fmsub.s`、`fnmsub.s`、`fnmadd.s`（RISC-V と同じ意味。修飾で負・絶対値も可） |
| 0001011 | custom-0「memory・同期」 | funct3 000 `lds rd, imm(rs1)`（shared の word）、001 `sts rs2, imm(rs1)`、010 `atom rd, (rs1), rs2`（global、sub = 種類: IR の 11 種 `and or xor xchg add sub smax smin umax umin cmpxchg`、`cmpxchg` の比較値は rs3。MC の atomic unit、D$ の line を無効化、class m）、011 reserved（shared の atomic、v2）、100 `fence`（imm[2:0]: [0] global = 未完の `sw`/`atom` の BRESP を待つ、[1] shared、[2] acquire = D$ を無効化）、101 `bar`（workgroup の全 warp の到達）、110 `wait cm, ct, cx`（imm[11:0] = 3 × 4 bit の上限）、111 `end` |
| 0101011 | custom-1「texture・入出力・lane の間」 | funct3 000 `texl rd, rs1, rs2`（rs1 = u、rs1+1 = v、rs2 = lod。slot = sub[3:0]（16 slot）。rd..rd+3。texel の offset は v1 では無し: IR の offset ≠ 0 は拒む、M14）、001 `texb rd, rs1, rs2`（rs2 = bias。lod は quad の微分から TU が計算）、010 `texf rd, rs1, rs2`（整数の texel、rs1 = x、rs1+1 = y、level = rs2。slot = sub[3:0]）、011 `texsz rd, rs1`（level rs1 の w・h を packed 16 × 2。slot = sub[3:0]）、100 `exp rs2, imm`（target = imm12: 0x000+k POS の k、0x010+4v+c varying v の c、0x100+c COLOR0 の c、0x104 DEPTH。非同期、`end` で揃う）、101 `lda rd, imm`（primitive table の頂点 imm[1:0] の word imm[7:2]）、110 `qd rd, rs1`（sub: 0 `qdx`（lane^1 との差、fine）、1 `qdy`（lane^2）、2〜5 `qswap`（lane^1 / lane^2 / lane^3 / quad の lane 0 を配る））、111 `shfl rd, rs1, rs2`（sub: 0 idx、1 xor、2 up、3 down、4 bfly。rs2 = lane/mask/delta。**`CAPS[24]` の時だけ**） |
| 1011011 | custom-2「16 bit packed」 | funct3 000 `fadd2`、001 `fsub2`、010 `fmul2`、011 `ffma2`（R4 の形）、100 `fmin2`/`fmax2`（funct7 で）、101 `iadd2`/`isub2`/`imul2`、110 `fcmp2`（funct7: lt/ge/eq/ne、half ごとに 0xFFFF/0）、111 `packh rd, rs1, rs2`（2 つの fp32 → half × 2）/ `unpackh rd, rs1`（sub: low/high → fp32）/ `swaph`（funct7 で）。`packh`/`unpackh` は **`PACKED` に依らず必須**、他は `PACKED` = 0 の構成で FAULT（U15） |
| 1111011 | custom-3「GPU の制御・超越・選択・投票」 | funct3 000 `sel rd, rs1, rs2, rs3`（R4 の形: rd = (rs1 & rs2) | (~rs1 & rs3)）、001 `exec.set rs1` / `exec.and rs1` / `exec.or rs1` / `exec.save rd`（sub）、010 `kill rs1`（sticky）、011 超越（sub: 0 `rcp`、1 `rsq`、2 `exp2`、3 `log2`、4 `sin`、5 `cos`、6 `fract`（= x − floor(x)、短い: class f）。0〜5 は class x）、100 `vote rd, rs1`（sub: 0 any、1 all → 全 lane に同じ Boolean。`CAPS[24]`）、101 `ballot rd, rs1`（EXEC の中で rs1 ≠ 0 の lane の mask を全 lane に。`CAPS[24]`）、110 `sactl rs1`（systolic array の制御の word、§3.9.1。`CAPS[25]`）、111 `nop`（sub 0。他の sub は reserved） |
| 0000111 / 0100111 / 0001111 / 1110011 / 1010111 / 1110111 | LOAD-FP / STORE-FP / MISC-MEM / SYSTEM / OP-V / OP-VE | **vendor の拡張の空間**（GSI1 は使わない。vendor の ISA（`NATIVE_ISA_ID`）が GSI1 の上位集合ならここに置く） |

- word0[1:0] は常に 11（RISC-V の 32 bit の形の印。00/01/10 は FAULT: 圧縮命令は無い）。
- 命令ごとの形（L11）: OP・OP-FP・custom の 2 source の命令は R、`fmadd` 系・`sel`・`ffma2`・`atom`（rs3 = cmpxchg の比較値）は R4、OP-IMM・`lw`・`lwc`・`lds`・`lda`・`texsz` は I、`sw`・`sts`・`exp` は S、BRANCH は B、`lui`/`movi` は U、`texl`/`texb`/`texf`/`qd`/`shfl`/`vote`/`ballot`/`sactl`/`exec.*`/`kill`/`fence`/`bar`/`wait`/`end`/`nop` は R（使わない field は 0。`wait` の 3 つの上限は imm12 の 3 × 4 bit なので I）。
- 未定義の opcode・funct・sub、`CAPS` に無い機能（packed、subgroup）、`nregs` を超える register は **ISA の fault**: warp は止まり、SU が `FAULT` 種別 8（**版 3 で追加**: ISA の誤り。`FAULT_INFO[23:16]` = SU の番号、`FAULT_ADDR` = PC）を立てる → §2.5 の回復。

#### 3.4.2 命令の群と latency の class

| 群 | 命令 | class | 備考 |
| --- | --- | --- | --- |
| 整数 | `add sub and or xor sll srl sra slt sltu` と I 形、`min max minu maxu andn orn xnor`、`lui movi` | a（1 issue） | `slt*` は Boolean |
| 乗算 | `mul mulh mulhu mulhsu` | f（DSP、3 cycle） | |
| 除算 | `div divu rem remu` | x（共有の除算器、lane ごと 34 cycle） | scoreboard |
| fp32 | `fadd.s fsub.s fmul.s fmadd.s fmsub.s fnmsub.s fnmadd.s fmin.s fmax.s fsgnj.s fround.s fcvt.* feq flt fle fne`、`fract` | f（DSP の pipeline、4〜6） | 修飾 n/a/sat |
| 超越 | `rcp rsq exp2 log2 sin cos fsqrt.s` | x（共有の 1 本、32 + 8 cycle） | scoreboard |
| packed | `fadd2 fsub2 fmul2 ffma2 fmin2 fmax2 fcmp2 iadd2 isub2 imul2 swaph`、`packh unpackh` | f | `PACKED`（U15） |
| memory | `lw sw lwc lds sts atom fence bar wait end` | m（`lw` は D$ hit で 4、miss で AMB の latency。`lwc`・`lds` は 2） | `sw`/`atom` は cm、`lw`/`atom` の戻りは scoreboard |
| texture | `texl texb texf texsz` | t | scoreboard（rd..rd+3） |
| 入出力・lane | `exp lda qd shfl vote ballot` | a（`shfl` は 2 pass = 2 × 32/NPHYS cycle）| `shfl`/`vote`/`ballot` は `CAPS[24]`（KC705 のみ。+0.5k LUT/SU の staging RAM、§3.6） |
| 制御 | `bany bnone ball j bexnz bexz exec.set exec.and exec.or exec.save kill sel` | a | 分岐は 1 cycle の bubble |
| systolic | `rq` の push/pop（どの命令でも operand 63）、`sactl`、`qstat`/`sastat` | a（FIFO が空/満なら発行で止まる） | `CAPS[25]`（KC705 だけ、§3.9） |

#### 3.4.3 入力と出力（版 2 と同じ。GHCI が決めるのは「属性 k は入力 slot k」「fragment の入力は ISA が決める」だけ）

- vertex の warp: VF が属性 k の component（1〜4 word）を STATE `ATTR_DST[k]`（§2.3、0x1C0〜。firmware が program の header の interface の表から書く）の先頭の register から連続に書いてから始める（k < 16。16 属性 × 4 component は NREGS 32 に入らないので、compiler は使う属性と component だけを表に出す、M12）。出力は `exp`（POS、POINT_SIZE = 0x004、varying、SECOND_COLOR = 0x108+c）。
- fragment の warp: RX が `r0 = b0`、`r1 = b1`、`r2 = b2`（perspective 補正済み、元の 3 頂点に対する barycentric）、`r3 = z`、`r4 = x`、`r5 = y` を書いてから始める。varying は `lda` × 3 + `fmul` + `fmadd` × 2 で compiler が補間（flat は `lda` の頂点 0、noperspective は §3.8 の式）。
- compute の warp: 入力は特殊 operand（`wg*`、`lid*`、`lidn`、`nwg*`）。

#### 3.4.4 program の binary の形（`GHCI_PROGRAM` が指す物。`gsi1_isa.h`）

```
word 0  magic "GSI1"（0x47534931）       word 1  版（1）
word 2  stage（0 vertex、1 fragment、2 compute）  word 3  flags（[0] packed を使う、[1] subgroup（shfl/vote/ballot）を使う、[2] LDS を使う、[3] atomic を使う、[4] systolic array（rq）を使う）
word 4  code_words（64 bit 命令の数 × 2）  word 5  code_offset（header の先頭からの word 数、= 16）
word 6  nregs（使う r の最大 + 1 ≤ NREGS） word 7  lds_bytes
word 8  wg_size（x | y << 8 | z << 16）    word 9  const_words（c0.. の使う数）
word 10 iface_offset（interface の表の word の位置、= 16）  word 11 iface_entries（表の entry の数）
word 12 entry（code の中の命令の index）   word 13 isa_min_version（1）
word 14 push_bytes（push constant の byte 数）  word 15 reserved（0）
word 16〜 interface の表（entry ごとに 4 word: kind（1 頂点の属性、2 varying の入力、3 出力、4 uniform block、5 sampled image、6 storage buffer、7 push constant、8 shared の大きさ）、set、binding か location、slot/register/offset/size（kind ごと: 属性 = 先頭の register | component << 8、uniform block = constant の領域の word の offset | size、sampled image = TU の slot、storage = constant bank の base の word（address と大きさを firmware がここに置く）、push = constant の word の offset））
code_offset〜 命令（8 byte 揃え）
```

- firmware（`program.c`）は magic・版・`nregs ≤ NREGS`・flags と `CAPS` の整合・`code_words`・interface の表（slot の数 ≤ CAPS2、constant の word ≤ 領域）を検査し、`GHCI_PROGRAM` の result に VkResult を書く。driver も compile の時に同じ検査をする（二重）。**interface の表が descriptor と program を結ぶ唯一の約束**（C1）: firmware は draw/dispatch の時に bound の descriptor set の (set, binding) をこの表で TU の slot・constant の領域の offset・storage の base に写し、`SET_REG`/`LOAD_CONST` にする。
- **native の ISA の直接の送信**: `GHCI_PROGRAM` の ISA の FourCC が `NATIVE_ISA_ID` なら binary の形は vendor の物（GHCI は magic と版の 2 word だけ決める: word 0 = vendor の FourCC、word 1 = 版）。driver は `NATIVE_ISA_ID` の backend を持つ時だけ送る。Amaterrace では native = GSI1 なので区別が無い。

#### 3.4.5 他の実装の CP-JIT（GHCI の規則）

- GHCI 1.0 の shader 付きの GPU は GSI1 の program を**必ず**受ける。native の ISA が GSI1 でない実装は、`GHCI_PROGRAM` で firmware が GSI1 → native に変換（JIT）して heap に置く（driver からは見えない。result で失敗を返せる: `VK_ERROR_INITIALIZATION_FAILED`）。変換の cache は firmware の物（識別子ごと）。
- JIT のために GSI1 が約束する事: (1) program は自己完結（header に register 数・LDS・入出力）、(2) 分岐の目標は命令の index で静的（間接分岐無し）、(3) 特殊 operand と `exp` の対象は番号で固定、(4) memory の model は「aperture の中の 32 bit の address、coherent でない cache、`fence` で順序」。
- Amaterrace は JIT をしない（hardware がそのまま実行）。

#### 3.4.6 他の GPU から取った物と出典の扱い

| 物 | どこに有るか（公開の文書の一般知識） | GSI1 での形 |
| --- | --- | --- |
| compiler が管理する exec mask、hardware の divergence stack 無し | AMD GCN/RDNA の EXEC、Arm Mali | `exec.*`、`bany`/`bnone` |
| 長い命令の宛先の scoreboard + 明示の wait の counter | NVIDIA（Volta 以降の dependency barrier）、AMD の `s_waitcnt` | 小さな hardware の scoreboard + `wait`（§3.1.1） |
| FMA の source の修飾（neg/abs）と飽和 | NVIDIA・AMD VOP3・Intel | word1 の n/a/sat |
| inline の定数と system の値を operand の番号で | AMD の inline constant、NVIDIA の SR | 特殊 operand 96〜127 |
| constant bank の operand | NVIDIA の `c[bank][offset]`、AMD の SGPR | `c0`〜`c31` + `lwc` |
| 16 bit packed（2 × fp16） | AMD VOP3P、NVIDIA HFMA2、Arm、PowerVR | custom-2 |
| quad の微分と swap | 全部 | `qd` |
| shuffle・vote・ballot | NVIDIA SHFL/VOTE、AMD DPP/permute、Intel | custom-1 `shfl`、custom-3 `vote`/`ballot`（optional） |
| texture の LOD/bias/fetch/size | 全部 | custom-1 |
| 超越関数の専用 unit | NVIDIA MUFU、AMD `v_exp_f32` 等 | custom-3 の 011 |
| LDS・barrier・atomic | 全部 | custom-0 |

- **出典の扱い**（Guardrail「MIT・GPL の出典のデータの引用」）: GSI1 は公開の ISA の文書の**一般的な知識**（命令の種類と意味）から設計し、他の GPU の opcode の値・field の位置・名前は写さない。RISC-V の opcode の値と field の位置は RISC-V の仕様（CC-BY 4.0 の文書。値は事実）から取り、`gsi1_isa.h` の該当 block に「Values referenced: The RISC-V Instruction Set Manual, Volume I: Unprivileged ISA, version 20240411, chapter "RV32I Base Integer Instruction Set" (opcode map), "M", "F", "Zfa", "Zbb"」と**版を含めて**書く（Guardrail、L10）。gpu-op の番号を firmware と `docs/ghci.md` が使う所は「Values referenced: zedBSD include/uapi/gpu-op.h（が引く venus-protocol の XML、commit ca19b635…）」。code・comment・構造は写さない。zedBSD の driver と amaterrace-gpu の両方 Zlib。**商標**（推測の懸念、L10）: RISC-V は RISC-V International の商標なので、GSI1 は「RISC-V 互換」を名乗らず、文書では「RISC-V の 32 bit の形を基にした独自の ISA」とだけ書く。

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

版 3 の追加（推定、表には未反映。p008・p017 の実測で直す）: 64 bit の decode と scoreboard（§3.1.1）+0.4k LUT/SU、`shfl`/`vote`/`ballot` の staging RAM +0.5k LUT/SU（KC705 だけ、U27）、`atg_cpu` 2.5k LUT + DSP 1 + BRAM36 12（bitstream 1' と KC705。§2.4.7）、`atg_sa` 32 × 8 の int16 で LUT 9〜11k + DSP 256 + BRAM36 4（KC705 だけ。N=8 では 32 × 4、§3.9.2）。

内訳（LUT）: fp32 の FMA は物理 lane あたり DSP 3 + **LUT 400**（正規化・丸め。版 1 の 250 は楽観。幅は 300〜700）、整数 ALU LUT 150、packed の fp16 LUT +150 + DSP 2。超越 1.5k + DSP 4、除算 0.3k、scheduler・decode・interlock・sequencing 1.8k、D$ と coalescer 1.5k、LDS/const/EXP/LDA/PT の制御 0.6k。固定の block: CP 1.8k、BLT 1.0k、VF 1.5k（instancing 入り 1.7k）、RX 3.3k（DSP 15）、ROP 1.5k（DSP 8。stencil 無しで 1.2k）、TU 2.2k（DSP 12。最近傍だけで 1.4k、DSP 4）、MC 1.5k、regs 0.4k。

| 構成 | 中身 | LUT | BRAM36 | DSP | 7010 の % | 判断 |
| --- | --- | --- | --- | --- | --- | --- |
| 固定の block 全部（KC705 の形） | CP+BLT+VF+RX+ROP+TU+MC+regs | **13.4k** | 12 | 35 | 76% | SU 無しでも 7010 の 3/4 |
| SU、NPHYS 8、A、packed 無し | 8×550 + 1.5k + 0.3k + 1.8k + 1.5k + 0.6k | **10.1k** | 12 RF + 1 I$ + 1 D$ + 1 LDS + 0.5 const + 2 PT + 4 prim(16 varying、2 複製) ≈ 22 | 24 + 4 + 2 = 30 | 57% | — |
| SU、NPHYS 4、NWARPS 2、A' | 4×550 + 1.5k + 0.3k + 1.5k + 1.2k + 0.5k | 7.2k | 6 + 1 + 1 + 1 + 0.5 + 2 + 2 ≈ 14 | 12 + 6 = 18 | 41% | — |
| ZYBO 全部（NPHYS 4、TU 最近傍、stencil 無し） | 13.4k − 0.8k − 0.3k + 7.2k | **19.5k** | 26 | 45 | **111%** | **入らない** |
| **ZYBO bitstream 1「2D」** | CP+BLT+MC+regs | 4.7k | 5 | 0 | 27% | p003・p004 |
| **ZYBO bitstream 2「DC」** | DC（regs、dma、tgen、VGA/TMDS、I2C 無し） | 2.0k | 3 | 0 | 11% | p005 |
| **ZYBO bitstream 3「raster」** | CP+MC+regs+RX+ROP（stencil 無し）+ VF（DRAW_RAW だけなら VF 無し） | 8.2k（VF 入り 9.9k） | 7 | 23 | 47〜56% | p007 |
| **ZYBO bitstream 4「compute」** | CP+MC+regs+SU（NPHYS 8、A） | 13.8k | 27 | 30 | **78%** | p008。閉じなければ NPHYS 4（10.9k、62%） |
| ZYBO「graphics + SU」（NPHYS 4、TU 無し、VF 入り） | 9.9k + 7.2k | 17.1k | 21 | 41 | 97% | **当てにしない**（p009 は Verilator と KC705） |
| KC705 unit（NPHYS 16、C、packed） | 16×700 + 1.5k + 0.3k + 1.8k + 1.5k + 0.6k | 16.9k | 24 RF + 2 + 1 + 4 LDS + 0.5 + 2 + 16 prim（4 複製）≈ 50 | 80 + 6 = 86 | — | — |
| KC705 N=4 | 4 × 16.9k + 13.4k + MIG 10k + PCIe/XDMA 8k + DC 2k + JTAG 0.3k | **101k（50%）** | 200 + 12 + 4 + 16 + 3 = **235（53%）** | 344 + 35 = **379（45%）** | 325T | 最初の目標 |
| **KC705 N=6**（版 3 の板の目標、M17） | 6 × 17.8k（SU + decode/scoreboard 0.4k + shuffle 0.5k）+ 13.4k + MIG 10k + XDMA 8k + DC 2k + JTAG 0.3k + RX 2 本 3.3k + `atg_cpu` 2.5k + `atg_sa` 10k | **156k（77%）** | 300 + 12 + 4 + 16 + 3 + CPU 12 + SA 4 = **351（79%）** | 6 × 86 + 35 + CPU 1 + SA 256 = **808（96%）** | 325T | CPU と SA（32 × 8）を載せる最大。DSP が限界: SA を 32 × 4 にすれば 680（81%） |
| KC705 N=8 | 8 × 17.8k + 上 + CPU + SA | **192k（94%）** | 435 + 16 = **451（> 445）** | 723 + 257 = **980（> 840）** | 325T | **入らない**（M17）。SA 無し・RF の複製 2 でも BRAM が限界。N=8 は SA 無しの構成として p015 で実測 |

- 結論（R1・U17）: **7010 では GPU の全 block を 1 つの bitstream に載せない**。block ごとの bitstream で workflow と各 block を板で確かめ、全体の結合は Verilator と KC705。KC705 の本命は **N=4 から始めて N=6（CPU + SA 付き）を目標**にし、N=8 は SA 無しの構成として p015 で実測する（README の「複数の SU」は満たす。版 3 で N=8 + CPU + SA は入らないと分かった、M17）。
- p002 の最初の仕事は CP+MC+BLT（bitstream 1）を合成して LUT を実測し、この表の係数を直す事。SU の係数は p008 の最初の合成で直す。

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

### 3.9 systolic array（SA、`atg_sa`）と ISA の FIFO の register `rq`（版 3、2026-10-23 ユーザーの要件）

ユーザー:「シェーダISAのレジスタのうち、1つをreservedにできる？このレジスタはFIFOにつながってて、systolic arrayのwavefrontのリードやライトに使えるよ。シェーダユニットはオプションでsystolic array (SA)を持つことができ、SAのPEの横方向にシェーダISAからFIFOでRWアクセスできるよ。SAはSUの中にあるわけではなく、SAとSUはdecoupledなユニットだよ。SAにはMCから重みをストリーミングできるよ。」

#### 3.9.1 ISA の側: register 63 = `rq`（queue register）

- **operand 番号 63 は GPR でなく FIFO の口 `rq`**（GSI1 の全部の実装で予約。GPR は r0〜r62。U12 の NREGS 32 の構成でも 63 は `rq` で、GPR は r0〜r31。header の `nregs` は GPR の数で 63 を含まない）。`rq` を dst に書くと **push**（SU → SA の in-FIFO に 1 wavefront）、source に読むと **pop**（SA → SU の out-FIFO から 1 wavefront）。
- **wavefront = 1 entry = 32 lane × 32 bit = 128 byte**（lane i の word が SA の PE の行 i の横の口に対応する）。pop は lane i に entry の word i を渡す。push は lane i の値を word i に置く。word の中の型は `SA_CTRL` の dtype で決まる: int16 は word の下位 16 bit（符号付き、上位は無視）、int8 は下位 8 bit。
- **使える命令**（H9）: `rq` は **class a・f の命令**（整数・fp32・packed・`sel`・`mv` など、発行の時に operand を読み、pipeline の順に書き戻す物）の dst・source にだけ使える。class m・t・x（`lw`・`texl`・超越・除算）の dst に `rq` は **ISA の fault**（戻りの順が発行の順と違い、push の順序が狂うため）。暗黙の連続の register（`texl` の rs1+1、rd..rd+3）が 63 に掛かる命令も fault。
- **EXEC との関係**（M19）: push/pop は wavefront 単位で、**EXEC に依らず必ず 1 entry 進める**（EXEC が全部 0 でも押す/取る。warp の間で push の数が揃うため）。EXEC の外の lane の push の word は 0（端の tile を 0 で埋める用途に使える）、pop の値は捨てる。
- **blocking（back-pressure）**: `rq` を読む命令は out-FIFO が空なら **発行の段で止まる**（その warp だけ。scheduler は他の warp を発行する）。`rq` に書く命令は「in-FIFO の空き − 発行済みで未着の push の数（credit）」が 0 なら止まる。scoreboard の entry は使わない（pop は発行時に `32/NPHYS` cycle で済む短い命令。class a）。1 命令の中で `rq` を source に 2 回書けるが pop は 1 回（同じ値。assembler は警告）。`rq` を source と dst の両方にする命令は pop → 演算 → push。
- **非 blocking の確かめ**: 特殊 operand 120 `qstat`（[15:0] out-FIFO の entry の数、[31:16] in-FIFO の空きの数、warp ごとに一様。読んだ後に他の warp が使い得るので、値は所有している warp でだけ信用できる）。
- **所有**（H7）: FIFO の対は SA ごとに 1 組で、**1 つの warp** が持つ: `sactl 4`（acquire: SA が空くまでその warp が発行の段で止まる）→ push/pop/`sactl 1〜3` → `sactl 5`（release: in/out-FIFO に残った entry は捨てる）。所有していない warp が `rq`・`sactl 1〜3`・`qstat` を使うと ISA の fault。dispatch の側の条件: `DISPATCH` の flags[1] = SA の dispatch の warp だけが acquire できる（他の dispatch の warp は fault）。CP は SA の dispatch を SA が結ばれた SU（`SA_SU`）にだけ配り、`SA_BUSY` が落ちるまで次の SA の dispatch を待つ。workgroup の中の複数の warp が順に acquire して使う形は許す（1 度に 1 warp）。program の header の flags[4]「SA を使う」で firmware が事前に `CAPS[25]` を確かめ、無ければ `GHCI_PROGRAM` の result で拒む。
- **deadlock**: 所有している warp が空の out-FIFO を待ち、SA がその warp の push を待つ組は compiler（と shader の書き手）の責任（規則: 1 tile は `SA_K` 回 push してから `SA_COLS_USED` 回 pop）。`bar` を持ったまま acquire しない（assembler の規則）。`HALT`・`RESET` の時は CP が SA に abort を出し、pop の待ちは 0 の entry で解け、push の待ちは捨てて解ける（quiesce が止まらない）。watchdog が最後の網。
- **shader からの制御**: custom-3 の funct3 110 `sactl rs1`（warp で一様な値を SA の制御 word に書く: [2:0] 0 なし、1 tile の開始（重みの stream を始める）、2 drain（accumulator を右へ押し出す。push が `SA_K` に満たない時は何もせず `sastat[4]` に 1 を立てる）、3 abort、4 acquire、5 release、[8] accumulator を 0 に）、特殊 operand 121 `sastat`（[0] busy、[1] 重みの stream 中、[2] drain 中、[3] この warp が所有、[4] 直前の `sactl` が拒まれた、[15:8] 残りの tile）。`exec.save`・`vote`・`ballot`・`sactl`・`qstat`・`sastat` の結果は EXEC に依らず全 lane に書く（M13）。制御の word は STATE の `SA_*` でも書ける（§2.3。CP の firmware か class N の `SET_REG`）。shader からの `sactl` は dispatch の中で tile を回す時に CP への往復を避けるため。

#### 3.9.2 SA の unit（`atg_sa`、SU と decoupled、MC の client）

- **配置**: `atg_sa` は `atg_su` の中ではなく、`atg_top` の兄弟の block。SU との間は in-FIFO（SU → SA、`SA_FIFO_DEPTH` entry × 128 byte）と out-FIFO（SA → SU）の 2 本だけ。MC には自分の client（重みの読み。優先の group 3。§4.1 の client の内訳に `atg_cpu` と `atg_sa` を足す、L13）。CP からは STATE の `SA_*` と `DISPATCH` の flag と `STATUS[7] SA_BUSY`。**無い構成**では `CAPS[25]` = 0、`rq` は fault。
- **PE の配列**: `SA_ROWS` = 32（wavefront の lane と 1:1。固定）× `SA_COLS`（4/8/16）。**出力固定（output-stationary）の systolic**: 入力の wavefront（activation、A の列）が左の口から右へ 1 step に 1 PE 進み、重み（B の行）が上から下へ MC の stream から 1 step に 1 PE 進み、PE(i, j) が `acc += a × b` を積む。K step（`SA_K`）の後に drain: accumulator を右へ押し出し、右の口から out-FIFO に `SA_COLS_USED` 個の wavefront が出る（**出る順は列の逆順**: 最初の wavefront が列 COLS−1、最後が列 0。lane i = C[i][j]。M18）。skew（行 i を i step 遅らせる、列 j を j step 遅らせる）は SA の中で行い、ISA からは「K 個 push して COLS 個 pop する」だけに見える。1 tile = 32 × K の A と K × COLS の B → 32 × COLS の C。**1 tile の step 数 ≈ K + 31 + COLS**（skew の充填と排出。K = 8 では利用率 ≈ 17%、K = 128 では ≈ 76%。小さい K は向かない）。
- **重みの stream**: `SA_W_BASE`（GPU の address、aperture の中）、`SA_W_STRIDE`（行の byte）、`SA_K`、`SA_TILES`（続けて回す tile の数。tile ごとに `SA_W_BASE += SA_W_TILE_STRIDE`）。SA は MC から `SA_COLS × SA_K` 個の重みを line 単位で先読みして上の口に流す。先読みの FIFO は **4 KiB**（BRAM36 1。DDR3 の latency ≈ 100〜200 ns × 3.2 GB/s ≈ 0.5 KiB を十分に覆う、M18）。aperture の外は FAULT 種別 1（client = SA）。
- **PE の型**（`SA_DTYPE`、capability で名乗る）: v1 は **int16 × int16 → int32 の accumulator**（DSP48E1 1 つ/PE、飽和なし、wrap）と **int8**（同じ DSP に 2 つの 8 bit の積を詰める案は v2。v1 は int8 も 1 DSP/PE、accumulator 32 bit）。**fp16 × fp16 → fp32** は PE ごとに正規化の LUT が +150〜200 要り（32 × 8 で +40〜50k LUT）、v1 では持たない（U29）。bf16 も同じ。shader は packed の命令（`packh` 等）と `fcvt` で入出力を変換する。
- **大きさ**（推定、325T）: 32 × 8 の int16 = **DSP 256**、accumulator 256 × 32 bit = 8k FF、PE の制御と skew と drain の mux ≈ **LUT 9〜11k**、重みの先読み BRAM36 1、in/out-FIFO（深さ 16 × 128 byte × 2）BRAM36 2。**KC705 の板の目標は N=6 の SU + CPU + SA（32 × 8）**（§3.6 の表。N=8 では DSP も BRAM も足りない、M17・R29）。**7010 には載せない**（32 × 2 でも DSP 64 = 80 の 80% で SU が残らない。`CAPS[25]` = 0。SA の RTL の TB と Verilator で確かめ、板は KC705）。
- **性能**（325T、gclk 200 MHz、32 × 8 の int16）: PE の峰は 256 MAC/cycle = 51 GMAC/s。SU の push は 1 wavefront / 発行（`32/NPHYS` = 2 cycle @NPHYS 16）= 12.8 GB/s で SA の 1 step（1 wavefront/step）に足り、律速は push の前の `lw`（A を memory から読む: `lw` + push で 2 発行 = 4 cycle/wavefront → 利用率 ≤ 50%）と tile の skew（上）。**実効は 64〜128 MAC/cycle の見込み**（p020 で `PERF` で測る）。重みの帯域: 8 × 2 byte × 200 MHz = 3.2 GB/s（DDR3 の 12.8 GB/s の 1/4）。複数の SU から同時に push する形（SA の in-FIFO を SU ごとに持つ）と A も MC から stream する mode は v2（R30）。
- **状態と fault**: `STATUS[7] SA_BUSY`、`PERF` の 4 本目を SA に替える構成は `VEND` で名乗る。HALT・quiesce・RESET は他の block と同じ（SA は MC の client として「新しい AR を止め、応答を待つ」+ FIFO の abort、§3.9.1）。reset で FIFO と accumulator は消える。dispatch が終わった時（`SA_BUSY` が落ちる = 所有の warp が release したか `end` した）に FIFO に残った entry は捨てる。

#### 3.9.3 GHCI での見え方（標準の optional な capability）

- `CAPS[25] SA`、extended capability `SA`（id 7、register は 0x140〜0x14F、M5）: `SA_CAPS`（[7:0] ROWS = 32、[15:8] COLS、[19:16] dtype の mask（[0] int8、[1] int16、[2] fp16、[3] bf16）、[27:20] FIFO の深さ）、`SA_SU`（結ばれる SU の番号。RW、IDLE/HALTED でだけ）。
- STATE の `SA` の範囲（0x1B0〜0x1BF、reserved から取る）: 0 `SA_CTRL`（[2:0] 0 停止、1 run（dispatch の開始で重みの stream を始める）、[4:3] dtype、[5] acc を 0 に、[6] shader の `sactl` を許す）、1 `SA_W_BASE`、2 `SA_W_STRIDE`、3 `SA_K`、4 `SA_TILES`、5 `SA_W_TILE_STRIDE`、6 `SA_COLS_USED`（≤ COLS。少ない列で回す）、7〜15 reserved。
- `DISPATCH` の flags[1] = SA（この dispatch の warp が SA を acquire できる。CP は `SA_BUSY` が落ちるまで次の SA の dispatch を待つ）。
- GSI1 の側: `rq`（63）、`qstat`（120）、`sastat`（121）、`sactl`、header の flags[4]。SA の無い GPU でも `rq` の番号は予約のまま（GPR にしない）。

#### 3.9.4 zedBSD の側（§6.10）

- Vulkan には systolic array の API が無い。`VK_KHR_cooperative_matrix` は subgroup ごとに A・B・C を shader が任意の address から load し、`MulAdd` が C を入力に取るので、**この SA の形（重みは STATE の静的な stream、accumulator を入れる口が無い、SA は 1 つ）では実装できない**（H8）。
- **見せ方の推し（U30）**: v1 は **GHCI 固有の compute の経路**: (b) `GPU_OP_GHCI_NATIVE`（試験の build）で `SET_REG` の `SA_*` + `LOAD_PROGRAM` + `DISPATCH(SA)` を送り、kernel は `gsi1asm.py` で書く（p020 の受け入れ）。製品の経路（p021、**WS288 の外の変更を含む**）は (c) 独自の拡張 `VK_KEI_systolic_dispatch`（compute pipeline の作成に `SA_*` の設定を chain し、SPIR-V の側は GSI1 の `rq`/`sactl` を inline の asm 相当で出す = 共有の compiler に `OpExtInst` の vendor の set を足す）を Q1 に提案する。`VK_KHR_cooperative_matrix` は「`sactl` で `W_BASE` を register から渡す」「C を push で入れる」「warp ごとの独立の SA」が揃う v2 の Future Work。
- compute の use（ML の推論の GEMM・conv の im2col の後）: driver は SA の有無を `CAPS[25]` で見て、拡張と capset に 32 × K × COLS の形と dtype を名乗る。

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

- `src/drivers/gpu/ghci/`（prefix `ghci_`、Zlib、WS288 の範囲。**GHCI の汎用の driver**。版 3: Amaterrace に固有の code は無い。`isa/gsi1/` は GHCI の標準の ISA の backend）:
  - `ghci.c`（attach の共通: MAGIC/VERSION/CAPS の確かめ、EXTCAP の走査、`drv_gpu_register`、session の数の帳簿）、`zybo-attach.c`（PL の窓、§6.2）、`pci-attach.c`（KC705）、`hcr.h`（§2.2）、`packets.h`（§2.4。amaterrace-gpu の `include/ghci_packets.h` の写し、§7.2）。
  - `cp.c`（ring、doorbell、fence、IRQ の handler、状態遷移の driver 側、watchdog、fault の報告と reset）、`memory.c`（GPU の address の allocator: first-fit の区間の木、session の帳簿、遅延の解放の list と **回収の thread**）、`blit.c`、`native.c`（試験用、§6.4）、`state.c`（標準の state の word の組み立て）、`jobs.c`（§6.4 の jobs の ops）、`capset.c`。
  - `display/`（`display.c`・`display-irq.c`・`scanout.c`・`adv7511.c`）。
  - `isa/gsi1/`（§6.6）。backend は `struct ghci_isa_backend {fourcc, compile, release, limits}` の表で `ISA_ID`（と `NATIVE_ISA_ID`）から選ぶ。
  - `fw/`（版 3）: `CPFW` の載せ替え（`ghcictl fw-load`、root）と firmware の版の読み。blob の置き場は U21。
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
- 寿命の規則: 参照を含む command の fence が signal するまで free しない。`resource_destroy` は「その時点の session の最後の fence の値（§6.4）≤ 完了した値」を確かめ、違えばその値と共に遅延の list に積む（L7）。**回収は IRQ の handler ではなく回収の thread**（handler は完了した fence の値を更新して thread を起こす。thread が allocator の mutex を取って返し、`drv_gpu_capacity_changed` を呼ぶ）。

### 6.4 drv_gpu との結び（capability・jobs・capset・command の道）

- **capability**（`drv_gpu_register` の検査（`gpu.c` 1024〜1045 行）と libvulkan の要求（`context.c` 124 行、`instance.c` 732 行）を満たす組）: GPU の device は `RESOURCE | CAPSET | BLOB | TRANSFER | COMMAND | MAPPING | NOTIFICATION | FENCE | JOB | JOB_CAPACITY | SHARE | ALLOCATION_SHARE`（KC705 は + `DISPLAY | DISPLAY_EVENTS`）。DC だけの device は `RESOURCE | TRANSFER | MAPPING | BLOB | SHARE | ALLOCATION_SHARE | DISPLAY | DISPLAY_EVENTS`。
- **ops**: `commands.submit/drain`（submit = frame を staging に写し ring に packet を置いて doorbell、`QUEUE_SUBMIT` の frame には後ろに FENCE を置く。完了は reply thread が trailer の後に `drv_gpu_complete`（§2.4.6）。drain = session の pending の frame と fence を全部待つ）、`jobs.reserve/commit/cancel/capacity`（**powervr の `render-runtime.c` の marker の形**、C2: reserve = session の marker（固定の数、例 64）を 1 つ取って completion を持ち token を返す（sequence は core の物で driver は見ない）。commit = marker を「session の最後の fence の値」（直前の `QUEUE_SUBMIT` の frame が置いた FENCE の値。driver の 32 bit の device 全体の fence、§2.5）に結ぶ。その値が既に過ぎていれば commit の中で `drv_gpu_complete`。cancel（通常）= marker を返す。cancel（fault）= completion を保ち、回復で ERROR で完了。capacity = 空きの marker の数）。IRQ の bottom half は fence page の値を読み、過ぎた marker を順に `drv_gpu_complete`、`recovery.stop_begin/stop_poll/fault/reset`（§2.5）、`share.*`、`display.*`（KC705 と DC）、`scanout.*`（DC）。
- **capset**: `get_capset` は powervr の `runtime_capset`（`render-runtime.c` 175 行）と同じ形: id 4、176 byte、wire 1、XML の版は libvulkan の `VULKAN_PROTOCOL_XML_VERSION`、timelines ≠ 0、160 byte に vendor の magic、164 byte に flag `OPAQUE | STRICT_QUEUE | QUIESCE`（libvulkan が native の device を作る条件）。**libvulkan は変えない**。
- **native の packet（試験用）**: `GPU_OP_GHCI_NATIVE`（§2.4.5、**0x10104 の案。番号は Q1 に確認**、U10）。payload は class N の packet の列で、driver はそのまま ring に写す。**build の option `CONFIG_GHCI_NATIVE`（試験の image だけ）と root だけ**に限り、driver は opcode の白表（NOP/SET_REG/BLIT/FENCE/WAIT_IDLE/INVALIDATE）と `SET_REG` の index の白表（BLT の範囲だけ）で検査し、address の word は aperture の中かを見る。それ以外は `EINVAL`。`userland/tests/ghci-blit/` が使う。compositor は使わない。
- 並行性と lock: ring への書き込みは 1 つの mutex（`cp_lock`）。IRQ の handler は spin で `completed_fence` と pending の list を守り、`drv_gpu_complete` は spin の外。
- 失敗: §2.5（v1 は device 全体の fault → reset。parse の誤りだけ session の error）。
- **BLT と ROP の順序（driver の規則）**: render pass の clear（BLT の FILL）の後、DRAW の前に `WAIT_IDLE(BLT)`。copy（BLT）が draw の結果を読む前に `WAIT_IDLE(ROP)`。CPU・BLT が書いた texture・buffer を shader が読む前に `INVALIDATE(D$|T$)`（DRAW/DISPATCH の暗黙の無効化で足りる場合が多い）。pipeline barrier は `WAIT_IDLE` + `FENCE`。

### 6.5 display device

- ZYBO: `display/display.c` が display だけの device を登録（`drv_gpu_unregister` できる寿命。bcm2711 の static の形は取らない）。`query`（1 output、mode は 640×480@60 と 1280×720@60）、`mode`（timing と `PCLK_SEL`）、`claim/release`（lease は 1 つ）、`present`（`FB_NEXT`）、`wait`・`events`（64 bit の software の sequence、VSYNC で進める）。`scanout.import_image` で GPU の device が export した allocation（pool の中の linear の image）を scan に使う。
- KC705: 1 つの device に display の ops（same-device の share の import）。
- cursor・overlay は無し。

### 6.6 compiler の backend（`isa/gsi1/`、IR → GSI1。版 3）

- 入力は共有の `struct drv_gpu_shader_ir`（`src/drivers/gpu/compiler/ir.h`、op の一覧は 263〜633 行）。SPIR-V の front end は書かない（Guardrail: compile は kernel 空間の driver）。出力は §3.4.4 の binary（header + 64 bit の命令）。backend の表 `struct ghci_isa_backend {fourcc, compile, release, limits}` に "GSI1" を登録し、`ISA_ID` で選ぶ（GHCI 1.0 の shader 付きの GPU は全部 "GSI1" なので、実質この 1 つ）。
- **IR の op の対応表（全部）**:

| IR | GSI1 |
| --- | --- |
| CONST / ICONST / BOOL | `movi`（小さい整数は `addi rd, zero, imm`） |
| LOAD_INPUT（vertex） | 属性の slot k = r_k（VF が書く）。`mv` |
| LOAD_INPUT（fragment） | `lda` × 3 + `fmul.s` + `fmadd.s` × 2（flat は `lda` の頂点 0、noperspective は §3.8） |
| STORE_OUTPUT | `exp` |
| LOAD_PUSH / LOAD_UBO | `c` の operand（32 word まで）か `lwc` |
| FADD FSUB FMUL | `fadd.s fsub.s fmul.s`（FNEG/FABS は operand の修飾 n/a に畳む） |
| RSQ SIN COS RCP SQRT EXP2 LOG2 | custom-3 の `rsq sin cos rcp exp2 log2`、`fsqrt.s` |
| FLOOR FTRUNC FROUND_EVEN | `fround.s`（rm = rdn / rtz / rne） |
| FRACT | `fract` |
| FMIN FMAX | `fmin.s fmax.s` |
| FLT FGE FEQ FNEU | `flt.s`、`fle.s`（operand を入れ替え）、`feq.s`、`fne.s` |
| AND OR NOT SELECT | `and or`、`xori rd, rs, -1`、`sel` |
| KILL | `kill` |
| IADD ISUB IMUL INEG IAND IOR IXOR INOT SHL SHR ASR | `add sub mul`、`sub rd, zero, rs`、`and or xor`、`xori -1`、`sll srl sra` |
| UDIV UMOD IDIV IREM | `divu remu div rem`（class x） |
| I2F U2F F2I F2U | `fcvt.s.w fcvt.s.wu fcvt.w.s fcvt.wu.s`（rm = rtz） |
| ILT ULT | `slt`、`sltu`（Boolean） |
| IGE UGE | `slt`/`sltu` + `xori rd, rd, -1`（2 命令） |
| IEQ INE | `xor t, a, b` + `sltiu rd, t, 1` / `sltu rd, zero, t`（2 命令） |
| MOVE / LOOP_BEGIN / LOOP_END | `mv` / `exec.save rS`（loop の前）+ label / 条件 → `exec.and cond` + `bexnz label` + `exec.set rS`（loop の後。`exec.save` は EXEC に依らず全 lane に書く、M13） |
| SKIP_BEGIN / SKIP_END | `exec.save` + `bexz` + … + `exec.set` |
| SAMPLE / SAMPLE_BIAS / SAMPLE_LOD | `texb`（bias 0 / bias）/ `texl` |
| DDX DDX_FINE DDY DDY_FINE | `qd`（sub 0/1）、coarse は `qd` + `qd sub 5` |
| PACK_HALF / UNPACK_HALF | `packh` / `unpackh` |
| TEXTURE（LD / RESINFO / SAMPLE* の 2D・array 無し） | `texf` / `texsz` / `texb`・`texl`。cube・3D・array・gather・compare は **拒む**（`ENOTSUP`） |
| LOAD_STORAGE / STORE_STORAGE / ATOMIC / STORAGE_SIZE | `lw` / `sw` / `atom` / firmware が interface の表の storage の entry の constant の word に置く大きさを `lwc`（C1）。述語付き（ir.h の component 1、`ATOMIC_PREDICATED`）は `exec.save` + `exec.and pred` + 命令 + `exec.set`（M14） |
| ATOMIC（shared、`LOCATION_SHARED`） | **拒む**（shared の atomic は GSI1 v1 で reserved、M14） |
| TEXTURE の texel の offset ≠ 0、SAMPLE_DERIVS、gather、compare | **拒む**（`ENOTSUP`、M14） |
| STORE_OUTPUT（POINT_SIZE / SECOND_COLOR / LAYER） | `exp 0x004` / `exp 0x108+c` / 拒む |
| NOP | 出さない（`nop` は pad だけ） |
| LOAD_SYSTEM | 特殊 operand（`wg*`、`lid*`、`lidn`、`prim`） |
| LOAD_SHARED / STORE_SHARED / BARRIER / FENCE | `lds` / `sts` / `bar` / `fence` |
| LOAD_VERTEX_INPUT / EMIT_VERTEX / END_PRIMITIVE（geometry） | **拒む**（geometry stage 無し） |
| SA の拡張の op（版 3 の案、ir.h にはまだ無い: `VK_KEI_systolic_dispatch` の `OpExtInst`、§3.9.4・§6.10） | `sactl`、`rq` の push/pop、`qstat`/`sastat`。SA の無い GPU（`CAPS[25]` = 0）では拒む |

- register の割り当て: linear scan、r0〜r(NREGS−1)（63 は `rq`、§3.9）。溢れは拒む（`ENOTSUP`、R8）。header の `nregs` に最大 + 1。
- **compile の時点と entry**（C1）: pipeline の作成で stage ごとに `drv_gpu_shader_parse(words, count, stage, …)`。v1 は「その stage の entry point が 1 つの module」だけ受ける（entry の名前で選ぶ API は共有の `spirv.c` に無い。足すのは共有の compiler の変更で Q1 の判断）。binary の interface の表（§3.4.4）は IR の `drv_gpu_shader_ir_uniform`（set、binding、kind、offset、size）と入出力の location から作る。
- scheduling: 長い命令（class m・t・x）の結果を使う命令を遠ざけ、独立の命令で埋める（scoreboard が止める間を減らす）。`wait` は `sw`/`atom` の完了が要る時（`fence`・`bar` の前）だけ。
- host の試験: `tests/ghci-gsi1-compiler-host-test.c`（符号化の往復 + `tests/gsi1-model.c`（amaterrace-gpu の `sim/models/gsi1_model.c` の写し、§7.2）で実行して CPU の参照と比べる）。
- 出典の規則（§3.4.6）: `gsi1_isa.h` の opcode の値の block に RISC-V の仕様の「Values referenced:」。他の GPU の物は写さない。

### 6.7 Vulkan の executor（`vk/`）= 検証する中継（版 3）

- libvulkan は変えない。`vk/` は gpu-op の stream を**解いて検証し**、§2.4.4 の表のとおりに D（driver が答える）/ R（そのまま ring へ）/ R'（`GPU_OP_GHCI_*` に書き換えて ring へ）/ X（拒む）に分ける。Vulkan の object の意味（pipeline の状態、descriptor の内容、render pass）は firmware の物で、driver は持たない。
- **既存の executor の構造との対応**: powervr の `vk/`（`vk-stream.c` の frame と route の表、`i915_wire_reader` の再利用）を型にする。route は `device`（D: instance・physical device・device・queue。capset と limit の表）、`memory`（R': allocator、`GHCI_MEMORY`、遅延解放）、`program`（D: module の SPIR-V の帳簿。pipeline の作成で stage ごとに compile → `GHCI_PROGRAM`、C1）、`object`（R: 50〜58・65〜89 の長さの計算と識別子の帳簿（session の close で `SESSION_CLOSE` に先立って driver 側の帳簿を消す））、`command`（R: 90〜136 の白表と長さ）、`queue`（R + FENCE + job）、`transport`（D: 178・179・137・180）。i915 の「object → recording → range → transport」の順の dispatch と同じ形で、handler の中身が「長さを数えて写す」に変わる。
- **wire の長さ**: handler は各 command の引数を gpu-op.h の「Arguments」の形で読み（構造体は field の順、pNext の chain、配列は count と u64 の count）、読んだ byte 数が packet の `n`。知らない sType の chain は拒む（`EINVAL`）。
- **返事**: D の返事はその場で書く。R/R' で REPLY を求める command は返事の大きさ分の cursor を空けて result の index を付け、frame を staging に写して ring に `GHCI_FRAME` を置いて返る。device の **reply thread** が `RESULT` の IRQ で record を読んで返事を埋め、trailer を release store で書き、`drv_gpu_complete`（§2.4.6。ioctl の中では眠らない、M3）。
- **session**: `open` で tag（1〜255、空きの最小）を取り `SESSION_OPEN` を ring に送る。`close` で `drain`（pending の fence を待つ）→ `SESSION_CLOSE` + FENCE → driver の帳簿と allocation を返す。
- **大きさ**: 6〜8k 行（codec は生成、帳簿と D の返事と reply thread と layout を含む。版 2 の 8〜12k より減る: Vulkan の object の意味が firmware に移ったため。M1）。p019 の LW 26。
- 最初の目標は vkdemo の部分集合（U8）: instance/device/queue、memory、buffer・image（2D、RGBA8/BGRA8、D24S8）、shader module、descriptor set（UBO・sampled image・storage）、pipeline、render pass（1 subpass）、command buffer（bind・draw・draw indexed・dispatch・copy・barrier・clear）、fence/semaphore、swapchain は WSI。

### 6.9 firmware（`fw/cp/`、amaterrace-gpu。版 3）

- 置き場所は amaterrace-gpu の `fw/cp/`（C、Zlib、`docs/style.md` の規約 + zedBSD の `plan/coding-style.md` を当てる: WS288 の p016 で全文を見直す）。build は共有の LLVM の clang（riscv32 の target、実測で有る）で `-ffreestanding -nostdlib -march=rv32im -mabi=ilp32`、link script は `fw/cp/link.ld`（IMEM 0x0000_0000、DMEM 0x2000_0000、制御の window 0xF000_0000）。toolchain は変えない（使うだけ）。
- 成果物 `atg1-cp.bin`（+ `.hex` を bitstream の BRAM の初期値に）。zedBSD の tree に blob を置くか（`userland/firmware/amaterrace/`、`CPFW` の載せ替え用）は U21。v1 の driver は blob 無しで動く（bitstream に入っている firmware を使う）。
- 試験: Verilator の harness（p017 から `atg_cpu` を含む）、firmware の単体の試験（host の C で packet の reader・heap・object の表を試す: `fw/cp/tests/`）。

### 6.10 systolic array の zedBSD の側（版 3、§3.9.4）

- driver: `CAPS[25]` と `SA` の capability を読み、capset と拡張 `VK_KEI_systolic_dispatch`（案、U30）に 32 × K × COLS の int16 → int32 / int8 → int32 の形を名乗る。`VK_KHR_cooperative_matrix` は v1 の SA の形では実装できない（H8、§3.9.4）。firmware: `DISPATCH` の flags[1] と STATE の `SA_*`（compute pipeline の create info に chain した構造体から）。
- compiler: SPIR-V の vendor の `OpExtInst` の set（`sactl`・push・pop・`qstat`）を IR の op に（共有の `compiler/ir.h`・`spirv.c` の変更 → **WS288 の外、Q1 の判断**。p021 に仮置き）。GSI1 の backend は §6.6 の表の行のとおりに下げる。
- 試験の道（p020、libvulkan の拡張の前）: `gsi1asm.py` で書いた GEMM の kernel を `GPU_OP_GHCI_NATIVE`（`SET_REG` の `SA_*` + `LOAD_PROGRAM` + `DISPATCH(SA)`）で流し、`DBG` か `resource_read` で C を読んで CPU の参照と比べる（`userland/tests/ghci-sa/`）。

### 6.8 試験の道（host → simulation → 板）

| 段 | 道具 | 何を確かめるか | 誰が |
| --- | --- | --- | --- |
| RTL の lint | `verilator --lint-only -Wall`（**U3**）。無ければ `iverilog -Wall` | 0 warning | 担当 |
| block の unit の TB | iverilog（`sim/tb/<block>_tb.v`、`PASS <name>`、`sim/run.sh NAME`） | AXI の slave/master、`am_afifo`、AMB の arbiter と quiesce、CP（**状態遷移、ring の wrap と満、FENCE の完全性（非同期の BLIT の後の fence）、FAULT と reset の順、EXTCAP**）、BLT、RX、ROP（**重なる 2 三角形の blend の順**）、SU（命令ごと、NPHYS 8/16/32 で同じ）、TU | 担当 |
| 全体の simulation | **Verilator**（C++ の harness: DDR の model、HCR、`ghci_packets.h`、ISA の model） | 各 milestone の板の場面（fill → DBG、fence → IRQ、三角形の PNG、compute）、**fault の注入 → reset → 次の仕事が動く**、**ring の wrap の負荷（満ちる・空ける）** | 担当 |
| Vivado の合成 | `vivado-build.sh`（IP 有りは `vivado-build-ip.tcl`）、ZYBO は Vivado（新設）か openXC7 | 実測の使用量、timing、DRC | 担当 |
| 板 ZYBO | `fpgaload` + `fpgareg`（`plan/tools/zybo/ghci-*.sh`）。SSH は WS289 の LAN が有る image なら `deploy.sh`。**生きた serial の判定と担当が JTAG を直に触る例外は WS286 の物（AGENTS.md）で、WS288 への拡張は U16** | p003〜p005・p007・p008 の板の受け入れ、p006 の `ghci-blit`・`ghci-display`、**detach/reattach（session を開いたままの `fpgaload` が EBUSY、閉じた後に通る）** | 担当、画面の目視はユーザー |
| 板 KC705 | `jtag-load.sh` + `jtag-reg.sh` | p012、p014 | 担当 |
| firmware（版 3） | `fw/cp/tests/`（host の C で packet の reader・heap・object の表を単体で）、Verilator の harness に `atg_cpu` + firmware を含めて class V の packet を流す（p017・p018）、`atg_cpu` の命令の TB（riscv-tests 風の自前の部分集合） | fill/copy の中継、object の作成 → µop の列の期待、watchdog → RESET → 再起動、`CPFW` の載せ替え | 担当 |
| QEMU | GPU は無い。kernel 側の build と boot の回帰（armv7-virt の PNG、driver の無い platform を壊さない） | 各 Phase | T1 |
| VFIO の実機 | A' の QEMU の guest（SSH/QMP の PNG） | p013 以降 | T1 |

QEMU と実機の証拠は分けて書く。やっていない確認は「未実施」と書く。

## 7. repository の分割、license、bitstream の配布

### 7.1 何をどこに置くか

| 置き場所 | 中身 |
| --- | --- |
| **amaterrace-gpu** | `README.md`（ユーザーの物。書き足しの案は [proposed/README-additions.md](proposed/README-additions.md)）、`docs/ghci.md`（**GHCI 1.0 の正本**: §2）、`docs/gsi1.md`（§3、GSI1 の正本）、`docs/style.md`、`rtl/common/`、`rtl/gpu/{cp,blt,vf,rx,su,tu,rop,mc}/`（`cp/` に `atg_cpu.v`）、`rtl/dc/`、`fw/cp/`（CP の firmware、§6.9）、`include/`（`ghci_regs.vh`・`gsi1_isa.vh`、C の `ghci_regs.h`・`ghci_packets.h`・`ghci_layout.h`・`gsi1_isa.h`）、`boards/zybo/`、`boards/kc705/`、`boards/kc705-sukuna/`（将来）、`sim/`、`tools/`、`LICENSE`。Vivado の project・IP の生成物・bitstream は commit しない |
| **zedBSD** | `src/drivers/gpu/ghci/`、`src/drivers/platform/zybo/pl-memory.c` と `zynq-devcfg.c` の変更、`include/uapi/gpu-op.h` の番号、`userland/tests/ghci-*`、`userland/base/ghcictl/`、`tests/ghci-*-host-test.c`・`tests/gsi1-model.c`、`plan/tools/zybo/ghci-*.sh`・`plan/tools/kc705/jtag-reg.sh`・`pcie-reload.sh`、`plan/ws288/`。既存の `plio` と `kc705/bringup` は zedBSD に残す |
| **sukuna-risc** | CPU の RTL（WS288 の外） |

### 7.2 共有する header の規則

- `ghci_regs.h`・`ghci_packets.h`・`ghci_layout.h`（image の layout の式: driver の memory requirements と firmware が同じ物を使う、R23）・`gsi1_isa.h` は amaterrace-gpu が正本。zedBSD は写しを置き、先頭に「copied from amaterrace-gpu <commit>」と SPDX。`plan/tools/ghci-sync.sh` で `diff`（CI では走らせない）。ISA の model も同じ。GHCI の版の定数を header に持ち、driver が attach で比べる。

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

## 8. Phase と LW（8 LW ≈ 2〜3 時間。p001 はこの設計。版 3 で p017〜p019 を新設し、p004・p008・p010・p011 を書き直した）

| Phase | 内容 | 受け入れ（板で見る物） | 依存 | LW |
| --- | --- | --- | --- | --- |
| p002 | amaterrace-gpu の骨組み、`docs/ghci.md` 1.0 の草稿（host interface の部分。command の節は版 3 の §2.4 を写す）、`am_axi_regs`・`am_axi_master`（quiesce 付き）・`am_afifo`・`atg_mc`、TB の枠、Verilator の harness の骨、**bitstream 1（CP+MC+BLT）の合成で LUT を実測し §3.6 を直す**（2026-10-23 P2 が実装中: `rtl/common/*`、`atg_cp.v`（stub）、`atg_blt.v`、`atg_mc.v`、`atg_top.v`、`include/ghci_regs.h` を読んだ） | host: TB PASS、lint 0 | p001、U1、U3 | 14 |
| p003 | CP v0（HCR、状態遷移、IRQ、STATE、`KICK`、非同期の `DBG`、EXTCAP）+ BLT + ZYBO の top（PS7、GP0、HP0）→ 板: `fpgareg` で fill/copy → `DBG` で DDR を照合。DDR の領域は DTB の `memory@0` を 448 MiB に | **最初の端から端**: `fpgareg` だけで fill/copy。`IRQ_SET` → `fpgareg wait` | p002 | 12 |
| p004 | ring + **版 3 の封筒（§2.4.2）** + class N の sequencer（§2.4.3 の 14 µop、`RESULT` を含む）+ fence + 割り込み + fault + reset: `RUN`、ring の約束（§2.7）、完全な FENCE、watchdog、FAULT、`RESET` の順、class V の packet は **firmware が無い構成では FAULT 種別 2**。板: `DBG` で ring を書き doorbell、`fpgareg wait` で FENCE、aperture の外で FAULT → RESET → 次の fill が通る | ring の fill → FENCE → IRQ → 値、**fault → reset → 回復**、wrap の TB、class 3 の詰め物 | p003 | 14 |
| p005 | DC（bitstream 2）: VGA（先）→ TMDS、`TEST_PATTERN` → DDR の framebuffer。窓の offset 0、IRQ_F2P[0] | color bar の目視、framebuffer の目視、`fpgareg wait` で VSYNC | p002 | 14 |
| p006 | zedBSD: devcfg の block の登録と IRQ の受け渡し、`pl-memory.c`、`ghci.c`・`cp.c`・`memory.c`・`blit.c`・`native.c`（`GPU_OP_GHCI_NATIVE`）・`state.c`・`jobs.c`・`capset.c`、display device と scanout、`ghci-blit`・`ghci-display`、host の試験（封筒・allocator・fence の wrap・状態遷移の driver 側） | `ghci-blit` PASS（板）、`ghci-display` の画面（目視）、**detach/reattach の試験**、QEMU armv7-virt の回帰（T1） | p004、p005 | 24 |
| p007 | RX + ROP（bitstream 3、`DRAW_RAW`、`DEBUG_COLOR`、ROP の順序の scoreboard）→ DC | 三角形の画面、depth、重なる blend の順、Verilator の PNG | p004、p005 | 18 |
| p008 | SU v0 と **GSI1 の compute の部分集合**（bitstream 4「compute」、CPU 無し）: `atg_su`（NPHYS の sequencing、RF、書き込み port の調停、I$、D$、LDS、const、超越、**scoreboard**（§3.1.1）、64 bit の decode（§3.3））、整数・fp32・memory・LDS・制御・`sel`・`exec.*` の命令、ISA の model（C）、`gsi1asm.py`/`gsi1dis.py`、TB、`DISPATCH`。**U12 の確定、SB_ENTRIES の実測**。板: vector add と reduction を `DBG` で | compute kernel が板で正しい、model と一致（NPHYS 8/16/32）、scoreboard の TB（RAW/WAW/entry 満） | p004、U12、U22 | 34 |
| p009 | VF + SU の vertex/fragment + PT + primitive table + `lda` + `exp` + `qd` + helper lane と coverage の mask（M10）+ `ATTR_DST`（M12）+ TU（最近傍 → bilinear）+ texture の命令 + deadlock の回避 + `PACKED`（KC705 の構成）+ `shfl`/`vote`/`ballot`（`CAPS[24]`） | textured な三角形（**Verilator の PNG**。板は KC705 の p014） | p007、p008、U27 | 28 |
| p010 | compiler の backend（zedBSD `isa/gsi1/`、IR → GSI1、§6.6）、**`gsi1_isa.h` の binary の header と interface の表の確定**（C1。p018 はこれに依存）、host の試験（符号化の往復 + model で実行して CPU の参照と比べる、interface の表の検査） | host PASS | p008、U22、U23 | 24 |
| p011 | **端から端の統合**: libvulkan → `vk/`（p019）→ ring → firmware（p018）→ block で vkdemo の 1 frame。**場**（H6）: host で build した `vk/` と libvulkan を、偽の drv_gpu（`tests/` の host の harness: ioctl の代わりに関数、HCR と memory を Verilator に DPI で結ぶ）と Verilator（`atg_cpu` + firmware を含む）で繋ぐ。性能の最初の計測（draw あたりの firmware の cycle、1 frame の往復） | vkdemo の 1 frame の readback が参照と一致（**Verilator**。板は p014） | p009、p010、p018、p019 | 28（±30%） |
| p012 | KC705: MIG、MMCM、`LINE_BYTES` 64、JTAG の bridge、CP + BLT + DC（HDMI は U7）、`vivado-build-ip.tcl` | `jtag-reg.sh` で fill → `DBG`、color bar | p004、p005、U7 | 20 |
| p013 | KC705 の host link A': PCIe + XDMA、BAR、MSI、`pci-attach.c`、`pcie-reload.sh` | guest の zedBSD から `ghci-blit` PASS | p006、p012、U2 | 24（**後回し**: Future Work fw-kc705-pcie） |
| p014 | KC705 の全 pipeline の bitstream（N=1: VF+SU+RX+TU+ROP、`PACKED`、**CPU と firmware**）、vkdemo を板で（JTAG の bridge で ring と firmware を載せ、画面は DC） | **vkdemo が KC705 で参照と一致** | p011、p012 | 18 |
| p015 | 複数の SU（N=4 → 6〜8）、work の分配、RX 2 本、200 MHz（R3）、PERF、BRAM の実測で N を決める | N=4 で vkdemo、N≥6 で合成が閉じる | p014 | 18 |
| p016 | 規約の全文の見直し（C: driver・firmware）、RTL の lint、`docs/ghci.md` 1.0・`docs/gsi1.md` の確定 | 動作は不変 | p015 | 10 |
| **p017**（新設） | **CP の CPU と firmware の土台**: `atg_cpu`（RV32IM、§2.4.7）、IMEM/DMEM、制御の window、`UOP_FIFO`、`CPFW` の extended capability（load/run/halt/version/mbox）、class V の packet の受け渡しと `RPTR`、watchdog の対象化、`fw/cp/` の build（共有の clang、`-march=rv32im`）、firmware の runtime（boot、heap、packet の reader、result、µop の出力）、**class V の最初の道**: `GHCI_SESSION_OPEN/CLOSE`・`GHCI_MEMORY`・`CREATE/DESTROY_BUFFER`・`BIND_BUFFER_MEMORY`・`CMD_FILL_BUFFER`/`CMD_COPY_BUFFER` の記録と `QUEUE_SUBMIT` → BLT の µop。bitstream 1'「2D + FW」 | TB: CPU の命令の試験（riscv-tests の自前の部分集合）、firmware の fill/copy が Verilator と **板（ZYBO 1'）**で `DBG` の照合に合う、`CPFW` の載せ替え、firmware の無限 loop → watchdog → RESET → 回復 | p004、U19、U20 | 32 |
| **p018**（新設） | **firmware の Vulkan の object model と draw/dispatch**（§2.4.8）: object の表、image/image view/sampler、`GHCI_PROGRAM`（pipeline の stage の binary と interface の表の検証）、pipeline layout・descriptor set layout/pool/set/update、render pass・framebuffer、graphics/compute pipeline → STATE の word の列、記録の全部の R の command、`QUEUE_SUBMIT` の辿り（bind の状態機械、interface の表で descriptor → `SET_REG`/`LOAD_CONST`、`LOAD_PROGRAM`、barrier、clear → BLT、内蔵の blit/clear の kernel、`DRAW`/`DISPATCH`）、record の VkResult と `RESULT` の IRQ、`DESTROY_DEVICE`/session の close の回収 | Verilator: firmware の試験の harness（host の C から staging と frame の packet を作って流し、µop の列と STATE と record の期待と比べる）、vkdemo の部分集合の command の列が期待の µop になる | p017、p009（STATE の意味）、**p010（binary の interface の表）**、U24 | 44（±50%） |
| **p020**（新設、版 3 の SA） | **systolic array**: `atg_sa`（32 × COLS の int16/int8 の PE、skew、drain、重みの先読み、MC の client、in/out-FIFO）、SU の `rq`（operand 63 の push/pop と発行の blocking、`qstat`/`sastat`/`sactl`）、`DISPATCH(SA)` と STATE の `SA_*`、`CAPS[25]`/`SA` の capability、ISA の model に SA を足す、TB（tile の GEMM が参照と一致、FIFO の満/空の blocking、abort、fault）、KC705 の合成で DSP/LUT を実測して COLS を決める | Verilator: 32 × K × COLS の GEMM（int16）が CPU の参照と一致、**板（KC705、p014 の bitstream に足す。host は無いので `plan/tools/kc705/jtag-reg.sh` で ring・STATE・memory を載せて `DBG` で読む道具 `ghci-sa-jtag.sh` を足す、H6・M20）**: GEMM が一致、`PERF` で MAC/cycle | p008、p014（板）、U28〜U30 | 28 |
| **p021**（新設、版 3 の SA。**WS288 の外の変更を含むので Q1 の判断**） | zedBSD で SA を Vulkan から使う: 独自の拡張 `VK_KEI_systolic_dispatch`（案）を libvulkan が名乗る、共有の compiler に vendor の `OpExtInst` の set、GSI1 の backend の下げ、driver の capset、`userland/tests/ghci-sa/` を Vulkan の経路に（H8） | GEMM の compute の結果が参照と一致（Verilator、KC705） | p020、p011、U30 | 20（libvulkan と compiler の分は別の WS なら除く） |
| **p019**（新設） | **zedBSD の `vk/` = 検証する中継**（§6.7）: codec の生成（`ghci_vkwire.inc`、firmware と共有、M21）と wire の reader（i915 の `i915_wire_reader` を powervr と同じ形で再利用）、D/R/R'/X の表、driver の帳簿（M1）、D の返事（capset・limit・memory requirements・**`ghci_layout.h`**（driver 側の正本の写し、M22）・fence）、R' の書き換え（`GHCI_MEMORY`、pipeline の作成で stage ごとの compile → `GHCI_PROGRAM`）、staging と `GHCI_FRAME`、result page と **reply thread**、jobs の marker（§6.4）、`EXECUTE_STREAMS` の入れ子、session の open/close、host の試験（gpu-op の frame を作って staging と ring の packet の列と返事の blob を比べる）、**ZYBO 1' で libvulkan の `vkCmdFillBuffer`/`vkCmdCopyBuffer` を実 driver で通す試験**（H6。T1 に依頼） | host PASS（staging の列と返事の blob の形が libvulkan の読む形）、ZYBO 1' の fill/copy（板）、**Verilator の端から端は p011** | p006、p010、p017（`GHCI_*` の形）、U24、U26 | 26 |

合計 **454 LW**（p013 の 24 を除くと 430。p018 の ±22 で 432〜476（L8）。SA の p020・p021 の 48 を除くと 406）。版 2 は 304。増分の主は firmware（p017 + p018 = 76）で、p011 が 48 → 24 に減った（Vulkan の object の意味が driver から firmware に移った分）。最初の端から端（p002〜p004）は 40 LW、画面まで（p005・p006）で 78 LW。

依存の図: p002 → {p003 → p004, p005} → p006; p004 → p007 → p009; p004 → p008 → {p009, p010}; p004 → p017 → p018 ← p009; {p006, p010, p017} → p019; p010 → p018（binary の interface の表、M22）; {p009, p010, p018, p019} → p011; {p004, p005} → p012 → p013（後回し）; {p011, p012} → p014 → p015 → p016; {p008, p014} → p020 → p021 ← p011。循環は無い。**Verilator の端から端（p011）の前に板で見える物**: ZYBO の 1・1'・2・3・4 の bitstream（p003・p017・p005・p007・p008）。

**ws.md の「進め方（案）」（DC → BLT → CP の順）からの変更（U14）**: 版 2 のまま（CP + BLT が先。`DBG_ADDR/DATA` で DDR を読めるので driver も画面も無しに板で結果を照合できる）。

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
| R23 | （版 3）image の layout の式が driver（memory requirements の返事）と firmware（bind・draw）で食い違う | 式は `ghci_layout.h` の 1 つの C の関数（amaterrace-gpu が正本、zedBSD は写し、§7.2）。host の試験で両方の写しを同じ入力で比べる（p019） |
| R24 | （版 3）`LOAD_CONST` が暗黙の `WAIT_IDLE(SU)` を挟むので push constant が変わる draw のたびに pipeline が空く | v1 は受ける（vkdemo・compositor の draw 数では ≤ 1 ms/frame の見込み）。p015 で constant の領域を warp ごとの base（STATE の word を SU が warp の開始で読む）に変える案を実測で決める |
| R25 | （版 3）`atg_cpu` が 325T -2 で 200 MHz に閉じない | 3 段の短い pipeline、BRAM の IMEM/DMEM を register で挟む。閉じなければ CPU だけ `gclk/2` の領域（`am_afifo` で µop を渡す） |
| R26 | （版 3）firmware が IMEM 32 KiB に入らない | 目標 64 KiB 以内で設計し、IMEM を 64 KiB（BRAM36 16）に出来る parameter にする。7010 の bitstream 1' でも BRAM は 60 の内 25 |
| R27 | （版 3）REPLY を求める frame ごとに firmware の往復（`QUEUE_SUBMIT`・create は毎回）で latency が積む | result は firmware が解いた時点で書き、GPU の完了は待たない（H1）。1 往復 = firmware が frame を解く時間（µs〜十数 µs）+ IRQ + reply thread。p011 で計測。libvulkan の transport の timeout は 10 s（`context.c`） |
| R28 | （版 3）firmware の Vulkan の部分集合が compositor の要る物に足りない（p018 の規模 ±50%） | U8 の順（vkdemo → compositor の最小 → mview）で部分集合を固定。足りない物は result の `VK_ERROR_FEATURE_NOT_PRESENT` で見える |
| R29 | （版 3）SA の DSP が N=8 の SU と合わせて 325T の 840 を超える（32 × 8 で 723 + 256） | N=8 は SA を 32 × 4（128）、または SU の packed を外す。p020 の合成で決める |
| R30 | （版 3）SA の入力の帯域が 1 つの SU の push に律速される（128 MAC/cycle 程度） | v1 は受ける。v2 で SU ごとの in-FIFO（SA の左の口を SU の数だけ）か、SA が A も MC から stream する mode |
| R31 | （版 3）`rq` を待つ warp と SA の相互の待ち（deadlock） | compiler の規則（K 回 push してから COLS 回 pop、`bar` の中で触らない）と `qstat`、最後は watchdog |

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
| U10 | 名前: `src/drivers/gpu/ghci/`、`ghci_`、`isa/gsi1/`（版 3）、driver_name "ghci:AMAT:ATG1"/"ghci-dc:AMAT:ATDC"、`GPU_OP_GHCI_*` = 0x10100〜、`ghcictl`、`ghci-*` の試験、`pl-memory.c` | **この名前**（Q1 が番号と devfs を確かめる） |
| U11 | ZYBO の VGA を HDMI より先に | **VGA が先** |
| U12 | **register file**: 7010 は NREGS 32・NWARPS 4（BRAM36 12）、325T は NREGS 64・NWARPS 4（24/SU） | **この値**。複製 3 → 2 は p008 の実測で |
| U13 | GHCI の範囲（§2.1） | **この範囲** |
| U14 | ws.md の順（DC → BLT → CP）を CP + BLT → DC に変える（§8） | **変える** |
| U15 | ZYBO の SU で 16 bit packed の演算器を外す（README の「16-bit packed」からの逸脱。`PACKH/UNPACKH` は残す。KC705 は有り） | **外す**（7010 の DSP と LUT） |
| U16 | WS286 の例外（ZYBO の実機は生きた serial で判定、JTAG を使う Phase は担当が板を直に触る）を WS288 にも適用する | **適用する**（AGENTS.md の例外の行に WS288 を足すのは Q1） |
| U17 | 7010 では block を分けた bitstream で確かめ、全体の結合は Verilator と KC705（§3.6） | **この方針** |
| **U18**（版 3） | 標準の shader ISA の名前と FourCC: "GSI1"（GHCI Shader ISA 1）。Amaterrace の native = GSI1。`isa/gsi1/`、`docs/gsi1.md`、`gsi1_isa.h`、`gsi1asm.py` | **"GSI1"**（GHCI の標準なので vendor の名前（ATS1）を付けない） |
| **U19**（版 3） | CP の形: F. firmware（`atg_cpu` RV32IM + `fw/cp/`）か H. hardware で gpu-op を解くか（§2.4.7） | **F**（H は部分集合でも 6〜10k LUT、pipeline の作成は不可能に近く、結局 driver が µop を作る事になる） |
| **U20**（版 3） | `atg_cpu` の RTL の置き場所と出自: (a) amaterrace-gpu の `rtl/gpu/cp/atg_cpu.v` に最小の RV32IM を新しく書く、(b) sukuna-risc に RV32 の最小の構成を作って使う（README の構成は RV64GC・22k LUT で 7010 に入らない） | **(a)** を p017 で。bus の口（IMEM・DMEM・制御の window・AMB の port）を文書にし、後で (b) に差し替えられる形にする |
| **U21**（版 3） | firmware の配り方: (a) bitstream の BRAM の初期値だけ（driver は file を要らない）、(b) (a) + zedBSD の `userland/firmware/amaterrace/atg1-cp.bin`（Zlib、amaterrace-gpu の commit を記録）を `CPFW` で載せ替え | **(a) を v1**、(b) は開発の反復（`ghcictl fw-load`、root）のために `CPFW` の capability だけ作る。blob を zedBSD の tree に入れるかは p017 の後に Q1 に聞く |
| **U22**（版 3） | 命令の長さ: (a) 64 bit 固定（word0 = RISC-V の 32 bit の形、word1 = 拡張）、(b) 32/64 の可変（AMD 型）、(c) 32 bit 固定（純粋な RISC-V の形、r0〜r31、修飾無し） | **(a)**（§3.3。decode が 1 種類、FMA の 3 source + 修飾 + 64 本の register が入る、word1 = 0 で RISC-V の形がそのまま読める） |
| **U23**（版 3） | 比較命令（`slt*`・`feq/flt/fle`）の結果を RISC-V の 0/1 でなく all-ones/0 の Boolean にする（共有の IR と同じ。RISC-V からの逸脱） | **Boolean**（compare の unit が 1 つで済み、`sel`・`exec.*` が直に読める） |
| **U24**（版 3） | 返事（reply）の書き手: (a) driver が全部書き、firmware の結果は result の record（frame の最後の FENCE を待つ）、(b) firmware が返事の blob に直接書く | **(a)**（返事の cursor は frame の中で順に進むので、2 つの書き手では順序が守れない。§2.4.6） |
| **U25**（版 3） | ZYBO の bitstream 3「raster」・4「compute」に `atg_cpu` を入れない（class N で板の受け入れ。class V の全部の道は Verilator と KC705）。bitstream 1'「2D + FW」を p017 で足す | **入れない**（4 は 78% + 2.5k ≈ 92% で閉じない見込み） |
| **U26**（版 3） | `GPU_OP_GHCI_*` の番号（0x10100〜0x10104 の案、§2.4.5）と `include/uapi/gpu-op.h` への追加の文 | **この番号**（Q1 が U10 と一緒に確かめる） |
| **U27**（版 3） | `shfl`/`vote`/`ballot`（subgroup の命令）を GSI1 の **optional**（`CAPS[24]`）にし、Amaterrace は KC705 だけ実装（+0.5k LUT/SU）、ZYBO は無し | **optional** |
| **U28**（版 3、SA） | FIFO の register の番号: **63**（operand の空間の最上位。GPR は r0〜r62）。NREGS 32 の構成でも 63 | **63** |
| **U29**（版 3、SA） | SA の v1 の PE の型と大きさ: int16 × int16 → int32（と int8）、32 行 × 8 列（N=4）/ 4 列（N=8）、fp16/bf16 は v2 | **この形**（fp16 の PE は +40〜50k LUT） |
| **U30**（版 3、SA） | Vulkan からの見せ方: (a) `VK_KHR_cooperative_matrix`（v1 の SA の形では実装できない、H8: 重みは静的な stream、accumulator の入口が無い、SA が 1 つ）、(b) 当面は `GPU_OP_GHCI_NATIVE` と手書きの kernel だけ（p020）、(c) 独自の拡張 `VK_KEI_systolic_dispatch`（libvulkan と共有の compiler の変更 = WS288 の外。p021） | **(b) を p020、(c) を p021 で**。(a) は v2 の Future Work（条件は §3.9.4）。p021 を WS288 に入れるか別の WS にするかは Q1 |

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
| 21.（版 3）CP の hardware が gpu-op の wire を解く | pNext の chain・配列・入れ子の構造体・u64 の識別子の表を RTL で解くのは部分集合でも 6〜10k LUT で、pipeline の作成は不可能に近い。結局 driver が object を解いて µop を作る = 版 2 の形に戻り、「ring は gpu-op」の決定に反する（§2.4.7） |
| 22.（版 3）firmware が返事の blob に直接書く | 返事の cursor は frame の中で順に進み、D の返事（driver）と R の返事（firmware）が混ざる frame で順序が守れない。result の record と frame の最後の FENCE で十分（§2.4.6） |
| 23.（版 3）32 bit 固定の純粋な RISC-V の形 | FMA の 3 source + 修飾、64 本の register、constant/特殊 operand、texture の slot、32 bit の即値が入らない。`lui`+`addi` の対と `mv` が増え、shader の性能に効く |
| 24.（版 3）32/64 bit の可変長（AMD 型） | fetch が 2 word の窓と line を跨ぐ場合を扱い、PC が 4 byte 単位になる。I$ の密度の利点は shader が小さいので要らない |
| 25.（版 3）sukuna-risc をそのまま CP の CPU に | README の構成（RV64GC、2-issue、SMT、約 22k LUT）は 7010 の 125%。制御 processor には過大。RV32 の最小の構成が出来たら差し替える（U20） |
| 26.（版 3）driver が Vulkan の object を解いて µop の列を ring に書く（版 2 の形を保つ） | ユーザーの決定（ring は gpu-op）に反し、driver が vendor ごとになる。class N の道は試験と bring-up に残した |
| 27.（版 3）scalar unit（AMD の SALU）と scalar の register | uniform な値は `vote`/`ballot` と constant bank で足りる。SU の LUT を増やさない |
| 28.（版 3）hardware の divergence stack と predicate register | 版 2 の案 3 のまま（compiler が EXEC を管理、IR が if-converted） |
| 29.（版 3）`fdiv.s` を hardware に | IR に FDIV は無く、`rcp` + Newton で足りる。除算器は整数だけ |
| 30.（版 3）SA を SU の中の命令（`mma` 型の tensor の命令）に | ユーザーの要件（SA は SU と decoupled、FIFO で横の口に）。decoupled なら SA の大きさ・型を SU と独立に決められ、SU 無しの DMA の mode も将来足せる |
| 31.（版 3）SA の FIFO を特殊 operand（96〜127）に | dst の field は 0〜63 だけなので push が書けない。register 63 を予約すれば push も pop も普通の命令の operand で済む |
| 32.（版 3）weight-stationary（TPU 型）の SA | 重みを MC から stream する要件に合うのは出力固定（A は横、B は縦に流れ、C が PE に留まって drain で横から出る）。weight-stationary だと C が縦から出て「横の口の RW」に合わない |

## 11. 敵対的な読み直し（版 2 の自己点検）

1. 完全な FENCE（§2.4）は BLIT と DRAW の非同期性を毎回打ち消す: submit ごとに 1 つの fence なので、1 submit の中の複数の draw は並ぶ。性能が要る時は軽い fence を後で足す（案 17）。
2. detach の 3（§6.2）で device が offline のまま 5 s を超える事: `fpgaload` に `EBUSY` を返し、次の `fpgaload` で retry する。永久に残らない（session は ENODEV で閉じる）。
3. `RPTR` で ring を再利用してよい（§2.7）は、CP が packet を **block に写し終えている**事が前提。`SET_REG` の value は STATE に写すので良いが、`DRAW` の payload（gx・gy・gz など）も CP の register に写す（block は ring を読まない）。RTL の規則にする。
4. KC705 の BAR1 の読み戻し（§2.7）は PCIe の往復（µs）で、submit ごとに 1 回なら許容。
5. 32 lane の coalescer は `32/NPHYS` cycle に分けて比較するので、1 つの `LDG` の address の計算に NPHYS/32 の割合で cycle が要る（performance の話で正しさではない）。
6. `KILL` の sticky が EXEC と別なので、`EXEC_SAVE` の値に killed が混ざらない。compiler は loop の条件に killed を含めない（IR の `KILL` の後の code は走ってよい）。
7. ZYBO の DC だけの bitstream が IRQ 61 を使い、GPU の bitstream も 61 を使う: 同時に居ないので衝突しない。KC705 は MSI で分ける。
8. §3.6 の表は内訳から計算したが係数は推定のまま。p002 と p008 の実測が最初の仕事（表に明記）。

版 3 の自己点検:

9. gpu-op の wire に長さが無いので、driver は中継する command の引数を**全部**解けなければ ring に載せられない（§2.4.2）。知らない sType の chain は拒む。これは bcm2711・i915 と同じ制約で、libvulkan が送る形は gpu-op.h が全部書いている。
10. class V の packet を firmware が解いている間、ring は止まる（µop は 1 本）。長い object の作成（pipeline）が draw の列に挟まると GPU が空く。v1 は受ける（object の作成は frame の前）。複数の ring（R6）の時に class V を別の ring に分ける。
11. firmware の無限 loop は watchdog（class V の packet も `WDT_CYCLES` の対象）で FAULT → RESET → firmware の再起動。heap は driver が作り直すが **他の session の object も全部消える**（v1 の回復は device 全体、§2.5 と同じ）。
12. R の command の返事は firmware が command を解いた時点の record から書く（§2.4.6）ので、`vkEndCommandBuffer`・`vkQueueSubmit` の VkResult は Vulkan の規格どおりの場所で返る。GPU の実行の誤りは fence の error（job の ERROR）で出る。
13. 64 bit 固定の命令で word0 が RISC-V の形でも、**RISC-V の binary は動かない**（x0、Boolean、分岐の意味が違う）。「RISC-V を基にする」の意味は §3 の冒頭に限る。
14. `shfl` の staging RAM は NPHYS の複製（§3.6 の +0.5k/SU は NPHYS 16 の値）。NPHYS 32 では 32 複製 = 32 Kbit の distributed RAM（1k LUT）。KC705 は NPHYS 16 なので問題ない。
15. scoreboard の entry が texture の rd..rd+3 を 1 entry で覆う（width）ので、4 register の内の 1 つを読む命令も止まる。正しい。
16. `ISA fault`（種別 8）は warp を止めるが他の warp は続く → device 全体の FAULT で止める（§2.5）。「他の session は続ける」は parse の誤りだけ、の規則のまま。
17. `rq` の pop が発行の段で止まる間、同じ warp の後ろの命令は出ない（in-order）が他の warp は出る。SA の dispatch の warp が全部 pop で止まり、SA が drain に入らない（K 回の push が揃わない）と止まる → `sactl 2`（drain）は push が K に満たない時は拒む（`sastat` で分かる）。compiler は K を `SA_K` と一致させる。
18. `rq` を dst にする命令が EXEC の外の lane で 0 を push する規則は、lane の mask で行列の一部を 0 にする用途（端の tile）に使える。

## 12. review の指摘と対応（版 2、design-reviewer、2026-10-23、38 件。節の番号は版 2 の物）

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

## 13. review の指摘と対応（版 3、design-reviewer、2026-10-23、49 件: Critical 2・High 10・Medium 22・Low 15）

reviewer が照合した code: `gpu-op.h`、`gpu.h`、`gpu-job.h`、`drivers/gpu/gpu.h`、`gpu.c`、powervr の `vk-stream.c`・`render-runtime.c`・`vk-draw.c`、`compiler/ir.h`・`spirv.h`、libvulkan の `context.c`・`queue.c`・`sync.c`・`commands.c`・`external-properties.c`・`memory.c`、Guardrail、共有の LLVM の resource dir、amaterrace-gpu の `ghci_regs.h/.vh`・`ghci_packets.h`・`atg_mc.v`・`atg_cp.v`。P1 が対応の前に確かめた物: `spirv.h:48` の `drv_gpu_shader_parse` の stage、`gpu.h:36・53〜56` の ops の形、powervr の `runtime_reserve/commit`（marker、`powervr_session_pend`）、`gpu.c:1140・1155・3310・6032`（`command` と `commands->submit` の使い分け）、`context.c:884〜891・962`（通知の後に trailer を poll）、`build/llvm/lib/clang/<版>/` に `lib/` が無い事、P2 の `ghci_regs.vh` の `GHCI_EXTCAP_LIST = 0x100`・`PERF_SRC0 = 0x110`。

| # | 指摘（要旨） | 対応（節） |
| --- | --- | --- |
| C1 | `CREATE_SHADER_MODULE` の時には stage も entry も interface も無く compile できない。binary に resource の表が無い | compile は **pipeline の作成の時**に stage ごと（59・60 は D、65・66 は R' で `GHCI_PROGRAM [pipeline][stage]` を先に送る）。binary の header に **interface の表**（属性・varying・uniform block・sampled image・storage・push・shared）を足し、firmware が descriptor を slot に写す唯一の約束にした。v1 は stage ごとに entry 1 つの module だけ。p010 が表を確定し p018 が依存（§2.4.4・§2.4.5・§3.4.4・§6.6・§8） |
| C2 | `reserve` に sequence は無く、commit の前に fence が完了し得る。「commit = doorbell」と submit の中の doorbell が矛盾 | powervr の marker の形に: driver の 32 bit の device 全体の fence、`QUEUE_SUBMIT` の frame が FENCE を置いて session の「最後の fence の値」を更新、commit は marker をその値に結ぶ（過ぎていれば commit の中で完了）、通常の cancel・fault の cancel を書いた（§6.4・§2.4.9） |
| H1 | 返事を完全な FENCE で待つので submit・create が GPU の完了を待つ | result は **firmware が command を解いた時点**で record に書き、frame の最後に `RESULT` の IRQ。FENCE は job の完了だけ。R27 を直した（§2.4.6） |
| H2 | 1 MiB の記録の frame が ring に入らず、満の時に待つ手段が無い | class V の frame は **staging**（GPU の memory の環、4 MiB）に写し、ring には `GHCI_FRAME`（8 dword）だけ。1 frame に割り込み 1 回。ring の満は `IRQ_STATUS[8] RETIRE` で待つ（§2.4.6・§2.7） |
| H3 | module の destroy で binary を解放すると pipeline が解放後の memory を実行する | binary は pipeline の物（C1）。`DESTROY_PIPELINE` の後に session の最後の fence が過ぎてから返す（§2.4.4） |
| H4 | firmware の再起動で DMEM が初期値に戻らず、CPFW は IMEM だけ | `CPFW_IBASE/IWORDS` と `CPFW_DBASE/DWORDS` の 2 対、`.data` の原本を像の中に持ち `boot.S` が毎回写す、p017 の受け入れに試験（§2.4.7・§2.2） |
| H5 | `-march=rv32im` の clang は `div` を出す。compiler-rt が無い | RV32IM の全部を hardware（逐次の除算器 0.3k LUT）、`mem*`・`__udivdi3` 等は `fw/cp/` に自前。toolchain は変えない（§2.4.7） |
| H6 | libvulkan + driver + firmware を端から端で流す場が無い（KC705 に host が無い、ZYBO は 1' だけ） | p011 に host の harness（偽の drv_gpu + Verilator の DPI）を明記、p019 に ZYBO 1' で libvulkan の fill/copy、p020 に KC705 の JTAG の道具 `ghci-sa-jtag.sh`（§8） |
| H7 | SA の FIFO を dispatch の全 warp が共有して混ざる | **1 warp が `sactl 4/5` で acquire/release**（他の warp は fault、acquire は空くまで止まる）。CP は SA の dispatch を直列に（§3.9.1） |
| H8 | `VK_KHR_cooperative_matrix` はこの SA の形では実装できない | U30 を (b) NATIVE → (c) 独自の拡張 `VK_KEI_systolic_dispatch` に変え、cooperative matrix は v2 の Future Work（条件を書いた）。p021・§6.10・§3.9.4 を直した |
| H9 | `rq` を class m/t/x の dst にすると順序が狂う。暗黙の register の範囲が 63 に掛かる | `rq` は class a・f だけ、m/t/x の dst と暗黙の範囲が 63 に掛かる命令は ISA の fault。push の満は credit で（§3.9.1） |
| H10 | firmware が STATE を window から直に書くと非同期の block と競合 | window の STATE は読みだけ。書きは `UOP_FIFO` の `SET_REG` だけ（§2.4.7） |
| M1 | 「driver は object の意味を知らなくてよい」が D の表と矛盾 | driver が持つ物（帳簿・create info・bind・SPIR-V・fence・記録の状態）を列挙し、p019 を 6〜8k 行・26 LW に（§2.4.4・§6.7） |
| M2 | 返事の cursor の順序（D と R が混ざる frame） | R の返事の大きさは gpu-op.h の「Reply」で決まるので cursor を空けて後で埋める（§2.4.6） |
| M3 | ioctl の中で眠る案は NOTIFICATION の形に合わない | 眠らない。device の reply thread が IRQ で返事と trailer を書いてから `drv_gpu_complete`（libvulkan は通知の後に trailer を poll する）（§2.4.6・§6.7） |
| M4 | session の tag の取り方が 2 通り | tag は node の open。`DESTROY_DEVICE` は D + R で device の下の object だけ返す（§2.4.4） |
| M5 | `CPFW` と `SA` の offset の重なり、`VEND` の幅、P2 の EXTCAP/PERF の offset とのずれ | `VEND` 0x0C0〜0x0DF（8 word）、`CPFW` 0x120〜0x13F、`SA` 0x140〜0x14F。P2 の `PERF_SRC` 0x110・list 0x100 を事実として書いた（§2.2・§2.4.10） |
| M6 | MC は client の間の順序を守らない | firmware の DMA の書き込みの応答が揃ってから、それを参照する µop と `RESULT` の IRQ を出す規則（§2.4.6・§2.4.8） |
| M7 | watchdog・HALT/STEP・CPFW_CTRL.HALT・LOAD・CAPS[17]・reset で残る register | 全部書いた: counter は `UOP_FIFO` の満と DMA の待ちを数えない、STEP = 1 packet、HALT の関係、LOAD の条件、`CAPS[17]` は静的、§2.6 の表に追記（§2.4.7・§2.6） |
| M8 | fence の値の領域が一貫しない | device 全体の 32 bit の 1 本に統一（§6.4、C2） |
| M9 | scoreboard の穴（暗黙の register、範囲の WAW、EXEC = 0、WAR、a/f の latency、NPHYS 8 の主張） | 範囲の比較（400 LUT）、暗黙の source/dst を含める、EXEC = 0 は要求も entry も無し、source は発行の段で読む、a と f は 1 本の固定長の pipeline、NPHYS 8 は interlock 1〜2 cycle（§3.1.1） |
| M10 | helper lane と coverage の規則が無い | EXEC は quad 単位、副作用の mask = coverage（`mask` operand）& ~KILLED（§3.1、p009） |
| M11 | 分岐の単位と即値の結合が曖昧 | B は imm[12:1] を命令数（±2048）、I/S は sext(imm12) + (sext(imm_hi) << 12)（§3.3） |
| M12 | 「属性 k = r_k」は vec4 を表せない | 属性の行き先は STATE `ATTR_DST[k]`（0x1C0〜）と interface の表（§2.3・§3.4.3） |
| M13 | `exec.save`・`vote`・`ballot` の predication、loop の下げ方 | EXEC に依らず全 lane に書く。`exec.save` は loop の前（§3.9.1 の注・§6.6） |
| M14 | §6.6 の表の漏れ | 述語付きの store/atomic、shared の atomic（拒む）、texel の offset・derivs・gather（拒む）、POINT_SIZE・SECOND_COLOR・LAYER、NOP、`texf`/`texsz` の level を register に（§6.6・§3.4.1） |
| M15 | `sel` の定義が 2 通り | bit ごと（rd = (rs1 & rs2) \| (~rs1 & rs3)）に統一（§3.2・§3.4.1） |
| M16 | 拡大・縮小の blit・clear attachments は BLT では出来ない | firmware の内蔵の GSI1 の program（`blit.c`、p018）（§2.4.4・§2.4.8） |
| M17 | KC705 の N=8 は CPU + SA を足すと BRAM も DSP も超える | 板の目標を **N=6**（156k LUT、351 BRAM、808 DSP）に。N=8 は SA 無しの構成として p015 で実測（§3.6） |
| M18 | SA の帯域・利用率・先読み・型の詰め方・drain の順 | push 12.8 GB/s、1 tile ≈ K + 31 + COLS step、実効 64〜128 MAC/cycle、先読み 4 KiB、int16/int8 は word の下位、drain は列の逆順（§3.9.2） |
| M19 | SA の EXEC = 0 と終わり・`sactl 2` の拒み・HALT | push/pop は EXEC に依らず必ず 1 entry、dispatch の終わりで FIFO を捨てる、`sactl 2` の拒みは `sastat[4]`、HALT/RESET は abort で待ちを解く（§3.9.1・§3.9.2） |
| M20 | `GHCI_NATIVE` の白表が SA の試験を通さない、KC705 に host が無い | 試験の build の白表を広げた（SET_REG 全部、DISPATCH、LOAD_PROGRAM…）、KC705 は JTAG の道具（§2.4.5・p020） |
| M21 | firmware の性能の見積もりが楽観、codec を 2 度書く | 「数 µs〜十数 µs」に直し p017 で測る。codec は `ghci_vkwire.inc` を 1 つの表から生成して driver と firmware で共有（§2.4.7・§2.4.8・p019） |
| M22 | Phase の依存の漏れ | `ghci_layout.h` は p019、p010 → p018、p008 ← U22・U23、p009 ← U27、p019 ← U24・U26。toolchain は変えない（H5）（§8） |
| L1 | 148・150・161 が表に無い | D（external の properties）（§2.4.4） |
| L2 | `GHCI_*` の受け入れの文の矛盾 | libvulkan は送らない、userland の試験の build は `GHCI_NATIVE` だけ（§2.4.4） |
| L3 | vendor の opcode の範囲が 3 通り | class 1 の op 0〜63 に統一（§2.1・§2.4.1） |
| L4 | 詰め物が NOP（n ≤ 255）では埋められない | class 3 の詰め物（§2.7） |
| L5 | `EINVAL` でも結局 DEVICE_LOST | 文を直した（§2.4.6） |
| L6 | 256 KiB を超える packet | staging（H2）で上限に当たらない（§2.4.2） |
| L7 | `FREE_MEMORY` の遅延解放が待つ fence | その時点の session の最後の fence の値（§2.4.4・§6.3） |
| L8 | LW の範囲の計算 | 454 LW、±22 で 432〜476（§8） |
| L9 | 下書きの文、節の順、冒頭の件数、ws.md の U の範囲 | `movi`・ILT の文を直した。§6.9 → §6.8 と §12 の前の §13 は版 2 の番号を保つため: §13 は末尾に移した。冒頭と ws.md を直した |
| L10 | 出典の版、gpu-op の出典の引き継ぎ、商標 | RISC-V の仕様の版（20240411）、gpu-op.h の引用の書き方、「RISC-V 互換」を名乗らない注（§3.4.6） |
| L11 | `nop` が r0 に書く、`wait` の置き場、`texl` の slot、形の列 | `nop` は custom-3 111、`wait` は imm、slot は sub[3:0]、命令ごとの形の行（§3.2・§3.3・§3.4.1） |
| L12 | firmware の無い構成の capability | `CAPSET`・`COMMAND`・`NOTIFICATION`・`JOB` を名乗らない（§2.4.10） |
| L13 | MC の client に `atg_cpu`・SA が無い | §3.9.2 に注（§4.1 の内訳は p002 の実測の時に直す） |
| L14 | allocator が `CP_HEAP` を除外する規則、shader が heap を壊せる | 除外の規則（§2.4.7）。R7 の「session の隔離が無い」に含む |
| L15 | `GET_DEVICE_QUEUE2` の文 | 書き直した（§2.4.4） |

未対応で残す物: 無し（全部に対応）。確かめていない事: reviewer の M21（firmware の性能）は推測のまま p017 で測る。L10 の商標は推測の懸念で、文言の変更だけ行った。
