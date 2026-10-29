<!-- awesome-plan project=zedbsd record=ws313-gpu -->

# WS313: Mali-G310（Valhall）の GPU の driver と表示の設計の覚え

担当: P7 の配下（GPU）。2026-10-29。実機の試験は無し（ユーザー「今日の夜、まとめてテストします。」）。
ここに書く値は全て資料から読んだもので、**実機で確かめた物は一つも無い**。

## 1. 正体（調べた事と根拠）

| 項目 | 内容 | 根拠 |
| --- | --- | --- |
| GPU | Arm Mali-G310（Valhall、v10 の CSF。Bifrost の G31 ではない） | vendor の DTS の node 名 `valhall`（`build/ref/hk-common-drivers-s7d-6.12/arch/arm64/boot/dts/amlogic/meson-valhall.dtsi`）、vendor の kbase が CSF の版（`drivers/gpu/arm/midgard/csf/`）、upstream の binding の注記「nxp,imx95-mali # G310」 |
| GPU_ID の形 | arch major [31:28]、arch minor [27:24]、arch rev [23:20]、product major [19:16]、version major [15:12]、minor [11:4]、status [3:0] | Linux v7.3 `drivers/gpu/drm/panthor/panthor_gpu_regs.h`（GPU_ID の field） |
| G310 の印 | arch major 10、product major 4（kbase の名は TVAX） | Linux v7.3 `panthor_hw.c`（get_gpu_model_name の 10,4）、vendor `include/uapi/gpu/arm/midgard/gpu/mali_kbase_gpu_id.h`（GPU_ID2_PRODUCT_TVAX） |
| arch minor | 実機の GPU_ID で決まる。driver は自分の minor の file が無ければ 10.12・10.10・10.8 を順に試す（§4） | panthor が `arm/mali/arch<major>.<minor>/mali_csffw.bin` を開く（`panthor_fw.c` panthor_fw_load）、MODULE_FIRMWARE は 10.8・10.10・10.12 |
| register の窓 | 0xfd000000、8 MiB（基の dtsi の 0xfe400000 は s7d の dtsi が上書き） | `meson-s7d.dtsi` の `&gpu` の reg、実 DTB（`build/odroid-c5/odroid-c5.dtb` の `/valhall`）でも同じ |
| reset の窓 | 0xfe002000（GPU node の reg[1]、reset controller `amlogic,meson-sc2-reset`） | `meson-s7d.dtsi`、実 DTB |
| 割り込み | GIC SPI 144（GPU）、145（MMU）、146（JOB）、level high。kernel の番号は +32（176・177・178） | `meson-s7d.dtsi` の `&gpu` の interrupts、実 DTB |
| coherency | 無し（`system-coherency = <31>`） | 同上 |
| clock | `CLKID_MALI`・`CLKID_MALI_STACK` を FCLK_DIV5 から 400 MHz | 同上の assigned-clock-* |
| power domain | GPU の power domain は S7D の pwrdm の表に無い（常時 on と見る、未確認） | `include/dt-bindings/power/*s7d*`（PDID_S7D_* に MALI が無い） |
| vendor の reset の扱い | C5 の DTS の GPU node に `reset_cfg`・`capb_reset` の子が無いので、vendor の kbase は S7D で reset を pulse しない。power on の callback で PWR_KEY と PWR_OVERRIDE1（0xfff \| 0x20 << 16）を書き、その後に soft reset | vendor `drivers/gpu/arm/midgard/platform/devicetree/mali_clock.c`（mali_reset_info）・`mali_kbase_runtime_pm.c`（mali_hw_init）、`backend/gpu/mali_kbase_pm_driver.c`（kbase_pm_init_hw: callback_power_on の後に soft reset）、Kbuild の既定 platform は devicetree |
| compatible | vendor: `"arm,malit60x", "arm,malit6xx", "arm,mali-midgard"`。upstream: `"arm,mali-valhall-csf"` | `meson-valhall.dtsi`、upstream の binding |

## 2. register の表（driver が使う物）

GPU の control（窓の先頭から）。Values referenced: Linux v7.3 `drivers/gpu/drm/panthor/panthor_gpu_regs.h`。

| 名（zedBSD の名） | offset | 意味 |
| --- | --- | --- |
| MALI_REG_GPU_ID | 0x000 | 上の field |
| MALI_REG_L2_FEATURES / CORE / TILER / MEM / MMU_FEATURES | 0x004 / 0x008 / 0x00c / 0x010 / 0x014 | MMU は VA bits [7:0]、PA bits [15:8] |
| MALI_REG_AS_PRESENT / CSF_ID | 0x018 / 0x01c | |
| GPU の割り込み RAWSTAT / CLEAR / MASK / STAT | 0x020 + 0x0/0x4/0x8/0xc | FAULT bit0、PROTM_FAULT bit1、RESET_COMPLETED bit8、POWER_CHANGED bit9、POWER_CHANGED_ALL bit10、CLEAN_CACHES_COMPLETED bit17、MCU_STATUS_CHANGED bit19 |
| MALI_REG_COMMAND | 0x030 | soft reset = 0x101、cache の flush = 4 \| (l2 << 8) \| (lsc << 12) \| (other << 16)（clean 1、invalidate 2） |
| MALI_REG_STATUS / FAULT_STATUS / FAULT_ADDRESS | 0x034 / 0x03c / 0x040 | |
| MALI_REG_POWER_KEY / POWER_OVERRIDE1 | 0x050 / 0x058 | key 0x2968a819、vendor の値 0xfff \| 0x20 << 16（vendor の kbase と同じく soft reset の前に書く） |
| SHADER / TILER / L2 PRESENT | 0x100 / 0x110 / 0x120（64 bit） | |
| SHADER / TILER / L2 READY | 0x140 / 0x150 / 0x160 | |
| SHADER / TILER / L2 PWRON | 0x180 / 0x190 / 0x1a0 | |
| SHADER / TILER / L2 PWROFF | 0x1c0 / 0x1d0 / 0x1e0 | |
| SHADER / TILER / L2 PWRTRANS | 0x200 / 0x210 / 0x220 | |
| MALI_REG_REVID / COHERENCY_FEATURES / COHERENCY_PROTOCOL | 0x280 / 0x300 / 0x304 | 非 coherent は 31 |
| MCU の control / status | 0x700 / 0x704 | control: disable 0、enable 1、auto 2。status: disabled 0、enabled 1、halt 2、fatal 3（`panthor_fw_regs.h`） |
| JOB の割り込み | 0x1000 + 0x0/0x4/0x8/0xc | GLB の interface bit31、CSG n は bit n |
| MMU の割り込み | 0x2000 + 0x0/0x4/0x8/0xc | AS n の fault は bit n（下の 16 bit） |
| AS n の register | 0x2400 + n × 0x40 | TRANSTAB +0x00、MEMATTR +0x08、LOCKADDR +0x10、COMMAND +0x18（NOP 0、UPDATE 1、LOCK 2、UNLOCK 3、FLUSH_PT 4、FLUSH_MEM 5）、FAULTSTATUS +0x1c、FAULTADDRESS +0x20、STATUS +0x28（ACTIVE bit0）、TRANSCFG +0x30（`panthor_mmu_regs.h`） |
| doorbell n | 0x80000 + n × 0x10000 | GLB の doorbell は 0、driver の stream は 1 |

AS の TRANSCFG: ADRMODE_AARCH64_4K = 6、INA_BITS(55 − va_bits) << 6、PTW の memattr NC = 1 << 24（driver の table は uncached の view で書くので NC）。
MEMATTR は 8 個の 8 bit の属性: slot 0 NC = 0x4c、slot 1 WB = 0x8f（`panthor_mmu.c` mair_to_memattr）。LOCKADDR は覆う 2 の冪の region（最小 32 KiB）の base と log2(size) − 1（`pack_region_range`）。
page table は AArch64 の stage 1、4 KiB の granule（Values referenced: Linux v7.3 `drivers/iommu/io-pgtable-arm.c`）: table/page = 3、AttrIndx [4:2]、AP_UNPRIV bit6、AP_RDONLY bit7、SH_IS 3 << 8、AF bit10、XN 3 << 53、出力の address [47:12]。

firmware の interface（共有の section の中の block。offset は Linux v7.3 `panthor_fw.h` の struct の並びから値として取った。`mali-interface.h` に全部ある）:

| block | control | input | output |
| --- | --- | --- | --- |
| global（section の先頭） | version 0x00、features 0x04、input_va 0x08、output_va 0x0c、group_num 0x10、group_stride 0x14 | req 0x00、ack_irq_mask 0x04、doorbell_req 0x08、progress_timer 0x10、poweroff_timer 0x14、core_en_mask 0x18（64 bit）、idle_timer 0x80 | ack 0x00、doorbell_ack 0x08、halt_status 0x10 |
| group n（0x1000 + n × group_stride） | features 0x00、input_va 0x04、output_va 0x08、suspend_size 0x0c、protm_suspend_size 0x10、stream_num 0x14、stream_stride 0x18 | req 0x00、ack_irq_mask 0x04、doorbell_req 0x08、allow_compute 0x20、allow_fragment 0x28、allow_other 0x30、endpoint_req 0x34（v4.0 から endpoint_req2 0x38 の 64 bit）、suspend_buf 0x40、protm_suspend_buf 0x48、config（AS 番号）0x50 | ack 0x00、doorbell_ack 0x08、status_state 0x18 |
| stream m（group の control + 0x40 + m × stream_stride） | features 0x00（下の byte + 1 が work register の数）、input_va 0x04、output_va 0x08 | req 0x00、config 0x04（priority [3:0]、doorbell [15:8]）、ack_irq_mask 0x0c、ringbuf_base 0x10、ringbuf_size 0x18、ringbuf_input 0x30、ringbuf_output 0x38 | ack 0x00、status_cmd_ptr 0x40、fault 0x80、fatal 0x84 |
| ring の block | insert 0x00、extract 0x08（input の page） | | extract 0x00、active 0x08（output の page） |

S7D の glue（Values referenced: hardkernel/kernel_common_drivers odroids7d-6.12.y 34daba9）:

| 物 | address | 値 | 出典 |
| --- | --- | --- | --- |
| clock controller | 0xfe000000（`amlogic,s7d-clkc` の reg[0]） | | `meson-s7d.dtsi`、実 DTB |
| SYS_CLK_EN0_REG0 | +0x044 | bit6 = sys_mali（APB の gate） | `drivers/clk/meson/s7d.c`・`s7d.h` |
| MALI_CLK_CTRL | +0x15c | 第 1 経路: mux [11:9]（6 = fclk_div5 = 400 MHz）、div [6:0]、gate bit8。第 2 経路: mux [27:25]、div [22:16]、gate bit24。最後の glitch-free mux bit31（0 = 第 1） | 同上（mali_0_mux・mali_0_div・mali_0・mali_1_*・mali） |
| MALI_STACK_CLK_CTRL | +0x160 | 同じ形（parent の並びの 6 も fclk_div5） | 同上（mali_stack_*） |
| reset の level | 0xfe002000 + 0x40 + 4 × (id / 32) | bit (id % 32)、0 で assert（level_low_reset） | Linux v7.3 `drivers/reset/amlogic/reset-meson.c`（level_offset 0x40、level_low_reset） |
| RESET_MALI / RESET_MALI_CAPB3 | id 34 / 33 | | vendor `include/dt-bindings/reset/amlogic,meson-s7d-reset.h` |

表示（VPU の OSD1、linear の MIF。S7D は `osd_mif_reg` の表と LINEAR_MIF を使う）。Values referenced: hardkernel/kernel_common_drivers 34daba9 `drivers/drm/vpu-hw/meson_vpu_osd_mif.h`・`meson_vpu_osd_mif.c`（s7d_osd_hw_init、osd_mem_mode、osd_linear_addr_config、line_stride_calc）、Linux v7.3 `drivers/gpu/drm/meson/meson_registers.h`:

| 名 | index（byte offset = index × 4、窓 0xff900000 = `amlogic, meson-s7d-vpu` の reg[0]） | 意味 |
| --- | --- | --- |
| VIU_OSD1_CTRL_STAT | 0x1a10 | bit0 block の enable、bit2 linear address（1）、bit21 OSD の enable |
| VIU_OSD1_BLK0_CFG_W0 | 0x1a1b | 色の形式など（読むだけ） |
| VIU_OSD1_BLK0_CFG_W1 / W2 | 0x1a1c / 0x1a1d | 元の窓の x / y（start [12:0]、end [28:16]） |
| VIU_OSD1_BLK1_CFG_W4 | 0x1a14 | linear の address >> 4 |
| VIU_OSD1_BLK2_CFG_W4 | 0x1a15 | 行の幅、16 byte 単位 [11:0]（vendor は 64 byte に揃える） |

## 3. 起動の手順（driver が書いた順。全て未試験）

`drv_mali_gpu_attach(fdt_phys)`（platform の thread、割り込みは on）:

1. 表示の node（§5）を先に登録する（gpu0。GPU が失敗しても残る）。
2. FDT で `arm,mali-valhall-csf`、次に `arm,mali-midgard` の有効な node を探し、reg[0]（8 MiB 以上、doorbell 1 まで入ること）と interrupts を `interrupt-names`（`GPU`・`MMU`・`JOB`、無ければ小文字、無ければ順）で読む。GIC の SPI だけ（+32）。
3. S7D の glue（`mali-s7d.c`）: clkc を compatible で、reset の窓を GPU node の reg[1]（無ければ `amlogic,meson-sc2-reset`）で map。power up: sys_mali の gate を開け、MALI と MALI_STACK の第 1 経路を fclk_div5 / 1 に（既にそうなら触らない。変える時は gate を閉じて mux・div、gate を開け、最後の mux を第 1 に）、RESET_MALI_CAPB3・RESET_MALI が assert なら deassert して 10〜20 µs 待つ（pulse はしない、vendor と同じ）。clock と gate の値を 1 行に出す。
4. register を map し、glue の registers_ready で PWR_KEY → PWR_OVERRIDE1。GPU_ID・features・present を読んで 1 行に出す（§9 の夜の行）。arch major が 10 でなければ止める。
5. 全ての割り込みを mask・clear して 3 本の line を level high で取り、GPU の fault と MMU の AS の fault を開ける（JOB は firmware の起動の後）。
6. soft reset（COMMAND 0x101）、RAWSTAT の RESET_COMPLETED を 100 ms まで poll。
7. COHERENCY_PROTOCOL = 31、L2 の PWRON（bit0 だけ）、L2_READY を 20 ms まで poll。shader と tiler は firmware が core_en_mask で入れる（panthor と同じ）。
8. 後の段の kthread を起こす。

後の段（`late_thread`）:

9. 根の file system を 90 s まで待ち、`/lib/firmware/arm/mali/arch<major>.<minor>/mali_csffw.bin` を読む。無ければ arch10.12・10.10・10.8 を順に（同じ image、§4）。
10. header と entry を検べ（`mali-firmware-image.c`）、AS 0 の page table を作り、section ごとに 4 KiB 単位の memory（4 GiB より下、uncached の view）を取り、data を写し（残りは 0）、section の MCU VA に map（WR → 書ける、EX → 実行、cache mode が CACHED の時だけ WB、他は NC）。共有の region の section の終わりから driver の物を置く（mcu_next）。AS 0 に TRANSTAB・MEMATTR・TRANSCFG を書いて UPDATE。
11. JOB の RAWSTAT を clear、MCU_CONTROL = AUTO、JOB の RAWSTAT の GLB bit（bit31）を 1 s まで poll。来なければ MCU_STATUS・JOB raw・interface の version・GPU の fault・AS 0 の fault・core の ready を 3 行に出す。
12. 共有の section から interface を読む（`mali-interface.c`: version が 0 でない、input/output が section の中、group 3〜31、stream 8〜32、slot と stream が揃う）。「firmware interface vX.Y.Z、features、group 数、stream 数、work register 数、suspend の大きさ」を 1 行に出す。
13. global の input: core_en_mask = SHADER_PRESENT、poweroff 10 ms・idle 800 µs（GPU の cycle counter で 1024 cycle 単位、400 MHz）、progress 2560000、ack_irq_mask、req の IDLE_EN・COUNTER_EN（level）と ALLOC_EN・POWEROFF_TIMER・PROGRESS_TIMER（toggle）、GLB の doorbell。ack を 100 ms まで poll。JOB の割り込みを全部開ける。
14. PING を toggle して ack を 100 ms まで待つ。「firmware answers; MCU enabled」。
15. driver の stream（`mali-queue.c`）: AS 1 の空間を作り enable、ring 64 KiB（AS 1、書けて実行しない）、ring の block 8 KiB（MCU、input の page と output の page）、suspend の buffer（MCU、firmware が言う大きさ）。CS 0 に ring・block・config（priority 1、doorbell 1）・ack_irq_mask・req（IDLE_SYNC_WAIT・IDLE_EMPTY・START）、CSG 0 に allow の mask・endpoint（shader 数・tiler 数・priority 15）・config = AS 1・suspend・doorbell_req の stream 0 の bit の toggle、req の STATE = START と ENDPOINT_CONFIG の toggle、glb の doorbell_req の group 0 の bit を toggle して doorbell 0。CSG の ack を 100 ms、CS の ack の state = START を 100 ms まで待ち、doorbell 1 を一度鳴らす（ring は空）。「queue: group 0 stream 0 started on an empty ring」。
16. 以後 1 s ごとに割り込みが記録した fault・MCU の status の変化・最初の JOB の割り込みを出し、10 s ごとに PING（3 回続けて答えが無ければ止める）。

失敗はどこでも 1 行の log と負の errno（attach）か thread の終わり（後の段、MCU_CONTROL = DISABLE）で、boot は止めない。

## 4. firmware と license

- G310 は CSF の GPU なので MCU の firmware `mali_csffw.bin` が**必須**。ユーザーの判断（2026-10-29、ws.md）: 「取得・検証・同意 (推し)」→ P7 が `userland/firmware/` の package で入れる。
- linux-firmware の `arm/mali/arch10.8/`・`arch10.10/`・`arch10.12/` の `mali_csffw.bin` は**同じ file**（2026-10-29 に git.kernel.org の plain から GET して確かめた: 3 本とも 282,624 byte、SHA-256 `a27847ea11f8efb3136340c3ba8aab413ae25145eeb4a7f64ff5edd829a2405b`）。
- driver の parser で読んだ結果（host、`mali_fw_image_parse`）: header 0.3、version hash 01050000、section 7（protected 1 を skip）、共有の section は 0x04000000〜0x04010000（flags c000001b = SHARED・ZERO・CACHED_COHERENT → NC で map）、build info `git_sha: 95a25d71030715381f33105394285e1dcc860a65`。section は VA 0x0・0x400000・0x410000・0x800000（EX）・0x1000000・0x2000000（WR・ZERO）。driver の物は 0x04010000 から置く。
- G310 の arch minor は実機の GPU_ID で決まる。driver は自分の minor の file が無い時に 10.12・10.10・10.8 の順に試すので、package は 3 本のどれか（できれば全部）を置けばよい。

## 5. 表示（scanout）の方針（`display/display.c`・`display/osd.c`）

- guardrail「GPU の driver は firmware が出していた出力先以外に scanout を始めない」に従い、HDMI・VENC・VPU の clock と mode、OSD の窓・形式・blend には一切触れない。書く register は OSD1 の BLK1_CFG_W4（buffer の address）だけ。
- 表示の node（gpu0）: `kern_boot_framebuffer("odroidc5.framebuffer")` の画面、1 つの出力、1 つの plane、U-Boot の大きさ（60 Hz と報告）、1 つの lease。resource は連続・4 GiB より下・cacheable の RAM。
- attach で VPU の窓（`amlogic, meson-s7d-vpu`、空白の無い名も）を map し OSD1 の 6 語を読んで 1 行に出す。enable・linear・窓が (0,0) から・address と大きさと行の幅が boot の framebuffer と同じ時だけ「flip できる」。
- present:
  (a) flip できて、frame が画面と同じ幅・高さ・行の幅（OSD の行の幅）・byte の順で、始まりが 16 byte に揃い 4 GiB より下なら、frame を CPU の cache から clean し、BLK1_CFG_W4 を書き換え、1 frame（20 ms）待って返す。前の resource は返る（session が既に壊していればここで free）。
  (b) それ以外は CPU で boot の framebuffer へ写し（赤と青の入れ替え、alpha を 0xff に。plane は alpha で blend するため）、clean し、plane が resource を見ていれば framebuffer に戻す。
- plane が見ている resource が壊されたら、plane が他を見るまで free を延ばす。lease の終わり・close で plane を framebuffer に戻し、text console を再開する。
- resource は表示の node の run か、render の node（gpu1）が export した image の import（`import_image`、2026-10-29 q1675-i03）。import は RGBA8888・BGRA8888・linear・4 byte 単位・page 1 本の連続の物だけ（`display/scanout.c` の検査）、render の node の bo の参照を持つ。外れた物は ENOTSUP（1 度だけ 1 行）で、libvulkan は copy の route に戻る。constraints は COPY・SHARED・FOREIGN。import の frame は (a) の条件なら clean 無しで flip、だめなら render の uncached の view から CPU で copy。
- 経路が変わると「display lease N: shared|copied-in frames flipped|copied」を 1 行に出す（shared は import の frame、copied-in は表示の node の run）。
- 未確認の点: OSD1 の address が vsync で latch されるか（されなければ flip の瞬間に裂ける）。U-Boot が canvas mode なら (b) だけになる（安全側）。flip した frame の alpha が 0 だと透ける（Keiland の frame は不透明の見込み、未確認）。(b) は guardrail「画像の処理は GPU で」に反する予備の経路で、(a) が使えない時だけ。

## 6. 段の計画と大きさ

| 段 | 内容 | 大きさ（行） | 状態 |
| --- | --- | --- | --- |
| G1 | attach・S7D の glue・ID・reset・L2・割り込み | 約 1500（attach 557、s7d 347、gpu 588） | 書いた（実機未試験） |
| G2 | GPU の MMU（page table の builder と AS） | 約 930（pagetable 377、mmu 552） | 書いた（builder は host 試験） |
| G3 | firmware の読みと検べ、MCU の起動、global の interface | 約 1600（file 201、image 365、interface 411、csf 604） | 書いた（parser・interface の reader は host 試験、実 firmware の parse も確かめた） |
| G4 | CSG・CS の slot と ring、doorbell | 319（queue） | 空の ring での起動まで書いた。submit の口・同期の object は G6 |
| G5 | 表示の node（OSD1 の採用と flip、CPU の copy） | 約 1400（display 1291、osd 109） | 書いた（OSD の decode は host 試験） |
| G6 | render の node と Vulkan の executor | 約 9000 | 未着手（§11 に設計） |
| G7 | Valhall の shader compiler | 約 12000 | 未着手（§11） |
| G8 | CS の命令の builder と draw・dispatch の命令列 | 約 3000 | 未着手（§11） |

## 7. やっていない事（正直な一覧）

- **Vulkan の device は無い。** Keiland の compositor は libvulkan の graphics の pipeline（SPIR-V の shader）だけで描くので、G6〜G8 が無い限り desktop は出ない。
  CPU の copy・clear だけの Vulkan の device を作っても compositor の描画（shader）は動かず、また guardrail「画像の処理は GPU で」に反するので作っていない。
- render の node（gpu1）は登録しない（Vulkan の executor が無いので、libvulkan に壊れた device を見せないため）。
- shader・tiler の core の電源は firmware に任せる（host は L2 だけ）。
- DVFS・電源の切り・suspend・reset からの回復・MCU の halt は無い。fault は log に出すだけ（AS は止まったまま）。
- 表示の vsync の割り込み（VIU vsync SPI 197）は使っていない（20 ms の sleep で代える）。cursor plane・overlay も無い。
- 試験は host だけ。QEMU に S7D・Mali の machine が無いので kernel の起動は試していない。

## 8. host の試験

出力は担当の `BUILD` の下（例 `build/pb/mali-check/`）。

```sh
O=build/pb/mali-check; mkdir -p $O
cc -std=c11 -Wall -Wextra -Werror -Isrc -Iinclude \
   tests/mali-pagetable-host-test.c src/drivers/gpu/mali/mali-pagetable.c -o $O/pagetable && $O/pagetable
cc -std=c11 -Wall -Wextra -Werror -Isrc -Iinclude \
   tests/mali-firmware-host-test.c src/drivers/gpu/mali/mali-firmware-image.c -o $O/firmware && $O/firmware
cc -std=c11 -Wall -Wextra -Werror -Isrc -Iinclude \
   tests/mali-interface-host-test.c src/drivers/gpu/mali/mali-interface.c \
   src/drivers/gpu/mali/display/osd.c -o $O/interface && $O/interface
```

2026-10-29: 3 本とも `all checks passed`（P7 の配下 B が流した）。

## 9. 今の状態（2026-10-29）

kernel の build: `make ZEDBSD_CONFIG=config/ci/config-odroid-c5-keiland.mk BUILD=build/pb build/pb/vmunix` が exit 0・warning 0（配下 B が流した。配下 A の未 commit の変更を含む worktree で）。全て実機未試験。

| file | 内容 |
| --- | --- |
| `src/drivers/gpu/mali/mali-attach.{h,c}` | `drv_mali_gpu_attach`、FDT、bring up、後の段の thread、watch、firmware の minor の代わり |
| `src/drivers/gpu/mali/mali-s7d.c` | S7D の clock・reset・power override |
| `src/drivers/gpu/mali/mali-gpu.c` | register の accessor、ID、soft reset、L2、cache の flush、割り込み、`mali_gpu_wait_raw` |
| `src/drivers/gpu/mali/mali-mmu.c` | buffer、AS の init・map・enable・disable・flush（lock・flush・unlock）、MCU と group の空間の bump |
| `src/drivers/gpu/mali/mali-pagetable.{c,h}` | page table の builder（前任） |
| `src/drivers/gpu/mali/mali-firmware-file.c` | 根の file system を待って読む |
| `src/drivers/gpu/mali/mali-firmware-image.{c,h}` | firmware の parser（前任） |
| `src/drivers/gpu/mali/mali-interface.{c,h}` | interface の offset の表、reader、req の toggle・update・ack、timer の変換 |
| `src/drivers/gpu/mali/mali-csf.c` | section の load、MCU の起動、global の設定、PING、doorbell、ack の待ち、status の報告 |
| `src/drivers/gpu/mali/mali-queue.c` | CSG 0・CS 0 の空の ring での起動 |
| `src/drivers/gpu/mali/display/{osd.h,osd.c,display.c}` | 表示の node |
| `src/drivers/gpu/mali/mali-private.h`・`mali-regs.h` | 型と register の表 |
| `platform/arm64/mali.mk` | `ARM64_MALI_SOURCES` |
| `tests/mali-*-host-test.c` | §8 |

**夜の serial で見る行**（順に。`mali:` で始まる）:

1. `mali: display 1920x1080 pitch 7680 on the boot framebuffer at 7f800000, frames flipped|copied` と、その前の `mali: display: OSD1 control … address … stride …`（copied なら理由の行）。
2. `mali: s7d clocks <MALI_CLK_CTRL> <MALI_STACK_CLK_CTRL> gate <SYS_CLK_EN0>[, resets released]`。期待: 両方の clock の [11:9] = 6、bit8 = 1、bit31 = 0、gate の bit6 = 1。
3. `mali: Mali-G310 rXpY arch 10.N.R GPU_ID xxxxxxxx at fd000000 irq 176/177/178, … shader cores (…), tiler …, L2 …, spaces …, VA … PA … bits, CSF …, coherency …, clock 400000000 Hz`。**ここの arch 10.N が firmware の path を決める**。GPU_ID が 0 か ffffffff なら clock・reset・power の問題（2 の行の値を見る）。
4. `mali: soft reset did not complete` / `mali: L2 did not power on` が出なければ reset と L2 は通った。
5. `mali: firmware /lib/firmware/arm/mali/arch10.N/mali_csffw.bin, 282624 bytes`（無ければ `not read (2)` か `using arch10.12`）。
6. `mali: firmware header 0.3, version hash 01050000, 7 sections (1 protected skipped)`。
7. `mali: firmware interface vX.Y.Z, features …, N groups of M streams (R work registers), suspend … bytes`。失敗なら `mali: MCU did not start (status …, job raw …, interface version …)` と GPU・MMU の fault の 2 行。
8. `mali: firmware answers; MCU enabled`。
9. `mali: queue: group 0 stream 0 started on an empty ring (group state …, ring at 1000000, doorbell 1)`。失敗なら request・acknowledge・state の値。
10. その後 `mali: job interrupt 178 works (status …)`（line の番号の確かめ）。`mali: MMU fault …`・`mali: GPU fault …`・`mali: MCU … (was …)` が出たら値を記録。

**まだ無い物（次の担当がこの順に）**: §11 の G6 → G8 → G7（G8 の命令の builder が無いと G6 の queue が何も実行できず、G7 の shader が無いと draw ができない。compute の空の dispatch で G6・G8 を先に確かめる）。

## 10. 他の file への差分の案（P7 が適用する）

- `src/kern/platform/odroid-c5.c`: 変更は要らない（契約 `int drv_mali_gpu_attach(uint64_t fdt_phys);`、0 か負の errno のまま）。
- `userland/firmware/` の package（P7）: `arm/mali/arch10.8/`・`arch10.10/`・`arch10.12/` の `mali_csffw.bin` は同じ SHA-256（§4）。G310 の minor が分かるまでは 3 本全部を置くか、1 本を置けば driver の代わりの順（12・10・8）で読む。
- HAL（`include/hal/hal.h`）の変更は要らない: register の map は `kern_device_map`、uncached の RAM は `kern_pmem_map_uncached`、割り込みは `kern_irq_register`、FDT は `drv_fdt_*`、framebuffer は `kern_boot_framebuffer` の既存の口で足りた。

## 11. Vulkan の段（G6〜G8）の設計（code は無し）

参考: Mesa（MIT）の `src/panfrost/vulkan/`（panvk、CSF は `csf/`）、`src/panfrost/compiler/`（bifrost と valhall の backend、`valhall/ISA.xml`）、`src/panfrost/lib/genxml/`（`v10.xml` の descriptor の layout、`cs_builder.h` の CS の命令）。**この節の Mesa の file 名と中身は記憶からで、この session では Mesa を読んでいない**。G6 の最初に Mesa の版を決めて clone し、値と表は block ごとの出典で使い、code・comment・構造は写さない（guardrail）。

既存の物で使う物:

- libvulkan（`userland/desktop/libvulkan/`）: そのまま。zedBSD の GPU の命令の protocol（`uapi/gpu-op.h`）を render の node に送る。powervr と同じく libvulkan は変えない見込み（G6 の最初に powervr の時の libvulkan の差分の有無を確かめる）。
- kernel の executor の wire: i915 の `drivers/gpu/i915/vk/vk-codec.h`（powervr の `vk/` も同じ物を使う）。
- shader の front end: `src/drivers/gpu/compiler/`（`spirv.c` で SPIR-V → 共通の IR、`optimize.c`）。powervr の `compiler/`（shader-spirv*.c で共通 IR から自分の IR へ、shader-backend.c で register の割当、usc-encode.c で命令の encode）が手本。

file の並び（案）:

| 段 | file | 口・中身 |
| --- | --- | --- |
| G6 | `src/drivers/gpu/mali/render.c` | render の node（gpu1）の `drv_gpu_ops`: open・close・get_info・resource（buffer の object、GPU VA の割当、AS の割当・LRU）・command（executor へ）・jobs（reserve・commit・cancel・capacity）・recovery（stop・fault・reset）。session ごとに AS 1〜7 の 1 つ（AS 0 は MCU）。group slot は session ごとに 1 つ（firmware の slot の数まで、足りなければ suspend・resume の scheduler） |
| G6 | `src/drivers/gpu/mali/sched.c` | group と queue の slot の割当、doorbell、CSG の IRQ（SYNC_UPDATE・IDLE・PROGRESS_TIMER・CS の FAULT・FATAL）の処理、timeout、fence の完了（`drv_gpu_complete`） |
| G6 | `src/drivers/gpu/mali/vk/vk-*.c` | powervr の `vk/` と同じ分け方: device（physical device の能力: Valhall の limit）、memory（memory type: HOST_COHERENT の NC の 1 つ）、buffer・image（linear と AFBC 無し、image の layout: Valhall の texture の plane の stride）、descriptor（Valhall の resource table: buffer・texture・sampler の descriptor を `v10.xml` の形で書く）、pipeline（G7 の binary と shader の program descriptor）、command buffer（G8 の命令列へ）、queue（ring に CALL で入れて insert を進め doorbell）、pass（tiler の heap・fragment の job）、transfer（copy・blit・clear を compute の shader で） |
| G7 | `src/drivers/gpu/mali/compiler/` | `shader-spirv*.c`（共通 IR → Valhall の IR）、`shader-ra.c`（register の割当: 64 本の 32 bit の GPR、uniform は FAU）、`shader-schedule.c`（message の命令の待ち: Valhall は clause が無く、命令ごとの wait の mask と slot）、`va-encode.c`（`ISA.xml` の値で 64 bit の命令を encode）、`shader.h`（vertex・fragment・compute の口、varying の layout、push constant、interpolation） |
| G8 | `src/drivers/gpu/mali/cs/cs-build.c`・`cs-draw.c` | CS の命令（MOVE・LOAD・STORE・WAIT・RUN_COMPUTE・RUN_IDVS・RUN_FRAGMENT・FINISH_TILING・SYNC_ADD・SYNC_WAIT・CALL・JUMP）の encode、draw の時の register の並び（`v10.xml` の draw の値）、tiler の heap と chunk の管理（TILER_OOM の処理）、render pass の fragment の job、同期の object |

順と確かめ（実機の各段で 1 本の script）:

1. G8 の最小（NOP・SYNC_ADD）を driver の stream（mali-queue.c）の ring に入れ、firmware が extract を進め sync の値が増えるのを見る（kernel の中の self-test）。
2. G6 の render の node と compute の dispatch（G7 の最小の compute shader: buffer に定数を書く）。`vkdemo` の compute で確かめる。
3. G7 の vertex・fragment、G8 の IDVS・tiler・fragment で三角形。
4. Keiland の compositor の shader（textured quad・blur・合成）が通るまで G7 を広げる。

見積り（LW、8 LW ≈ 2〜3 時間）:

| 段 | LW | 実時間の目安 |
| --- | --- | --- |
| G8 の最小と self-test | 10 | 3〜4 時間 |
| G6（render の node・scheduler・executor） | 40 | 10〜15 時間 |
| G7（compute の最小） | 20 | 5〜8 時間 |
| G7（vertex・fragment・compositor の shader） | 60 | 15〜22 時間 |
| G8（draw・tiler・fragment） | 25 | 6〜9 時間 |
| 計 | 約 155 | 40〜58 時間（実機の試験と bisect を除く） |

危険: Valhall の ISA の encode と scheduling の誤りは GPU の hang として見えるので、命令の encode の host 試験（Mesa の ISA の表との照合）と、firmware の CS の fault の値（`CS_FAULT`・`CS_FATAL`、exception type）を log に出す道具を G6 の最初に作る。

## 12. compiler（G7、Valhall の shader compiler）

担当: P7 の配下 C。記録の本体は [phase010](phase010/phase.md)。2026-10-29、実機未試験。

- 場所: `src/drivers/gpu/mali/compiler/`（`shader.h` が口と shader の ABI の本体、`va-isa.h`・`va-encode.c`・`va-schedule.c`・
  `va-compile.c`）。共通の front end `src/drivers/gpu/compiler/` の IR から。
- 参照: Mesa 26.2.6（MIT）`src/panfrost/compiler/bifrost/valhall/ISA.xml`・`va_pack.c`・`va_insert_flow.c`・
  `bifrost_compile.c`・`pan_nir_lower_*.c`。値は block ごとの出典、code・構造は写していない。
- ABI の要点: FAU = push constants、vertex は viewport の 6 float、buffer の 64 bit の address。resource table 0 texture・
  1 sampler・2 attribute・3 vertex buffer。vertex は position program（offset 0）と varying program（`varying_offset`）、
  出力は table 61 entry 0 の LEA_BUF（r59）、varying location L は byte 16L。fragment は ATEST の後 BLEND（descriptor L）。
- host の試験: `plan/ws313/tests/mali-compiler-host-test.sh`（ISA.xml から生成した表 `tests/mali-compiler-isa-table.inc`、
  生成の tool `plan/ws313/tests/mali-compiler-isa-table.py`、encode の既知の語、assembler-cases の decode、1 lane の模型
  `tests/mali-compiler-model.c` と IR の参照）、`plan/ws313/tests/mali-compiler-desktop-test.sh`（desktop の全 60 module）。
  2026-10-29 両方 PASS（host のみ、GPU の証拠ではない）。

## 13. render・executor・CS（G6・G8、配下 D、2026-10-29、実機未試験）

記録の本体は [phase011](phase011/phase.md)。ここは形の要点だけ。

- **stream と scheduler**（`mali-queue.c`・`sched.c`）: driver の stream は group 0・AS 1・doorbell 1（self-test: NOP＋SYNC_ADD64）。render の stream は group 1・stream 0・doorbell 2・AS slot 2。ring（64 KiB）と sync の page は全ての session の空間の同じ VA（0x100000・0x200000）に map し、submit のたびに session の table を slot 2 に差し替える。ring の列: MOVE32 0 → FLUSH_CACHE2（clean+invalidate、signal 0）→ MOVE48 stream → MOVE32 bytes → WAIT(0) → CALL → WAIT(all) → FLUSH_CACHE2 → WAIT(0) → MOVE48 sync → MOVE48 1 → SYNC_ADD64（system、error propagate）→ ERROR_BARRIER（Values referenced: Linux v7.3 panthor_sched.c queue_run_job）。ring の register は stream の最後の 4 本。
- **CS の register の割当**（executor の stream）: r0〜r60 は RUN_* の staging（v10.xml の COMPUTE/IDVS/FRAGMENT SR）、r64〜r79 は copy・fill の data、r80〜r89 は loop の scratch、r92〜r95 は ring。stream の work register が 96 未満なら executor は submit を拒否する。scoreboard: load/store は 0、endpoint は 1（SET_SB_ENTRY）。各 RUN_COMPUTE・pass の終わりで WAIT(all)。
- **descriptor**（`vk/vk-desc.c`、host 試験あり）: SRT の pointer は address | table 数（4 の倍数）、FAU の pointer は address | (64 bit の entry 数 << 56)、table 0 texture・1 sampler・2 attribute・3 vertex buffer（C の §12 の ABI）。FAU は push constants → buffer の address（fau_word）→ range（size_fau_word）→ texture の size → vertex の viewport の 6 float（viewport_fau_word）。
- **render pass**: 1 subpass、color だけ。tile 16×16、RT の internal は R8G8B8A8（4 byte/px）、writeback は linear。tiler context と heap の descriptor は pass ごとに stage に新しく書き、heap の 1 chunk（8 MiB）を使い直す。draw は RUN_IDVS（malloc enable、varying の選択 (0,0,1,0)、fragment (2,2,2,0)）、pass の終わりに FINISH_TILING → RUN_FRAGMENT（FBD の pointer、bbox）。
- **LOAD の pre-frame の draw**（2026-10-29 q1675-i03、`vk/vk-frame.c`・`vk/vk-preload.c`）: colour target 0 が LOAD の pass は FBD の pre-frame 0 に frame shader の DCD（FragCoord で前の内容を texelFetch する fragment program、node で 1 回 compile）。mode は INTERSECT（CLEAR の target があれば ALWAYS）。失敗は 1 行で従来の cleared tile に戻り、pre-frame を含む submission の FAULT・timeout の後は node の preload を止める。
- **clean tile write**: CLEAR かつ STORE の RT に立てた（primitive の無い tile も書き戻す。前は古い内容が残った）。
- **未確認の点**（夜に見る）: 動いている group の AS の差し替え、FLUSH_CACHE2 の flush id 0、RUN_COMPUTE の task increment 1、HEAP_SET 無しの tiler、blend の memory format、sample の位置の表、tiler の hierarchy mask、RT の swizzle（BGRA）、DCD flags の early/late。
