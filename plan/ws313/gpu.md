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
| arch minor | 実機の GPU_ID で決まる。i.MX95 の G310 は arch10.12 の firmware を使うと見られる（未確認） | panthor が `arm/mali/arch<major>.<minor>/mali_csffw.bin` を開く（`panthor_fw.c` panthor_fw_load）、MODULE_FIRMWARE は 10.8・10.10・10.12 |
| register の窓 | 0xfd000000、8 MiB（基の dtsi の 0xfe400000 は s7d の dtsi が上書き） | `meson-s7d.dtsi` の `&gpu` の reg |
| reset の窓 | 0xfe002000（reset controller、`amlogic,meson-sc2-reset`、0x98 byte） | `meson-s7d.dtsi` の `reset: reset-controller@2000` |
| 割り込み | GIC SPI 144（GPU）、145（MMU）、146（JOB）、level high | `meson-s7d.dtsi` の `&gpu` の interrupts |
| coherency | 無し（`system-coherency = <31>`） | 同上 |
| clock | `CLKID_MALI`・`CLKID_MALI_STACK` を FCLK_DIV5 から 400 MHz | 同上の assigned-clock-* |
| power domain | GPU の power domain は S7D の pwrdm の表に無い（常時 on と見る、未確認） | `include/dt-bindings/power/*s7d*`（PDID_S7D_* に MALI が無い） |
| 保護された memory | `mali_protected`（size 0、使わない） | `s7d_s905x5m_odroidc5.dts` |
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
| MALI_REG_PWR_KEY / OVERRIDE0 / OVERRIDE1 | 0x050 / 0x054 / 0x058 | key 0x2968a819（Amlogic の旧い platform が OVERRIDE1 に 0xfff \| 0x20 << 16 を書く。S7D で要るかは未確認、今は書かない） |
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
| doorbell n | 0x80000 + n × 0x10000 | GLB の doorbell は 0 |

AS の TRANSCFG: ADRMODE_AARCH64_4K = 6、INA_BITS(55 − va_bits) << 6、PTW の memattr NC = 1 << 24、WB = 2 << 24、PTW の RA bit30。
MEMATTR は 8 個の 8 bit の属性（MAIR の形を Mali の形にした物）: NC = (1 << 6) \| (3 << 2)、WB = (2 << 6) \| (3 << 2) \| 3。
page table は AArch64 の stage 1、4 KiB の granule（io-pgtable の ARM_64_LPAE_S1。Values referenced: Linux v7.3 `drivers/iommu/io-pgtable-arm.c`）:
table/page = 3、AttrIndx [4:2]、AP_UNPRIV bit6、AP_RDONLY bit7、SH_IS 3 << 8、AF bit10、XN 3 << 53、出力の address [47:12]。

S7D の glue（Values referenced: hardkernel/kernel_common_drivers odroids7d-6.12.y 34daba9）:

| 物 | address | 値 | 出典 |
| --- | --- | --- | --- |
| clock controller | 0xfe000000 | | `meson-s7d.dtsi`（clkc の reg 0x0 + apb4 0xfe000000） |
| SYS_CLK_EN0_REG0 | +0x044 | bit6 = sys_mali（APB の gate） | `drivers/clk/meson/s7d.c`・`s7d.h` |
| MALI_CLK_CTRL | +0x15c | mux [11:9]（6 = fclk_div5 = 400 MHz）、div [6:0]、gate bit8、第 2 経路 [27:16]、最後の glitch-free mux bit31（0 = 第 1） | 同上（mali_0_mux・mali_0_div・mali_0・mali） |
| MALI_STACK_CLK_CTRL | +0x160 | 同じ形 | 同上（mali_stack_*） |
| reset の level | 0xfe002000 + 0x40 + 4 × (id / 32) | bit (id % 32)、0 で assert（level_low_reset） | Linux v7.3 `drivers/reset/amlogic/reset-meson.c`（level_offset 0x40、level_low_reset） |
| RESET_MALI / RESET_MALI_CAPB3 | id 34 / 33 | | vendor `include/dt-bindings/reset/amlogic,meson-s7d-reset.h` |

表示（VPU の OSD1、linear の MIF）。Values referenced: hardkernel/kernel_common_drivers 34daba9 `drivers/drm/vpu-hw/meson_vpu_osd_mif.h`・`meson_vpu_osd_mif.c`、Linux v7.3 `drivers/gpu/drm/meson/meson_registers.h`:

| 名 | index（byte offset = index × 4、窓 0xff900000） | 意味 |
| --- | --- | --- |
| VIU_OSD1_CTRL_STAT | 0x1a10 | bit0 block の enable、bit2 linear address（1）、bit21 OSD の enable |
| VIU_OSD1_BLK0_CFG_W0 | 0x1a1b | 色の形式など（読むだけ） |
| VIU_OSD1_BLK0_CFG_W1 / W2 | 0x1a1c / 0x1a1d | 元の窓の x / y（start [12:0]、end [28:16]） |
| VIU_OSD1_BLK1_CFG_W4 | 0x1a14 | linear の address >> 4 |
| VIU_OSD1_BLK2_CFG_W4 | 0x1a15 | 行の幅、16 byte 単位 [11:0]（32 bit の画素では (width × 32 + 127) >> 7） |

## 3. 起動の手順（driver が書いた順。全て未試験）

1. FDT で `arm,mali-valhall-csf` か `arm,mali-midgard` の有効な node を探し、reg[0]（register）・interrupts（GPU・MMU・JOB の順、名は interrupt-names）を読む。
2. S7D の glue: sys_mali の gate を開け、MALI と MALI_STACK の clock を fclk_div5 / 1 で gate を開け mux を第 1 経路に、RESET_MALI・RESET_MALI_CAPB3 を assert して 10 µs 待ち、deassert して 10 µs 待つ。
   U-Boot が既に開けていればそのまま（読み・書きとも同じ値）。
3. register を map し、GPU_ID・features・present を読んで 1 行に出す。arch major が 10 でなければ止める。
4. 全ての割り込みを mask し、soft reset（COMMAND 0x101）、RAWSTAT の RESET_COMPLETED を 100 ms まで poll。
5. COHERENCY_PROTOCOL = 31、L2 の PWRON（L2_PRESENT が 1 でない時も bit0 だけ）、L2_READY を 20 ms まで poll。shader と tiler は firmware が core_en_mask で入れる（panthor と同じ）。
6. MCU の address space（AS 0）: 4 GiB の VM の page table を作り、AS 0 に TRANSTAB・MEMATTR・TRANSCFG を書いて UPDATE。
7. 根の file system が来るのを待って `/lib/firmware/arm/mali/arch<major>.<minor>/mali_csffw.bin` を読み、header（magic 0xc3f13a6e、major 0）と entry を検べ、IFACE の section ごとに 4 KiB 単位の memory を取り、uncached で中身を写し（ZERO の flag なら残りを 0）、section の MCU VA に map する。0x04000000 の section が共有の interface。
8. JOB の割り込みを開け、MCU_CONTROL = AUTO、JOB の RAWSTAT の GLB bit（bit31）か、共有の region の version が 0 でなくなるのを 1 s まで poll。
9. 共有の region の先頭の global の control（version・features・input_va・output_va・group_num・group_stride）を読み、input/output を共有の region の中の CPU の位置に直し、group（0x1000 + n × stride）と stream（+0x40 + m × stream_stride）を検べる。
10. global の input: core_en_mask = SHADER_PRESENT、poweroff・progress・idle の timer、ack_irq_mask、req の IDLE_EN・COUNTER_EN、toggle（CFG_ALLOC_EN・CFG_POWEROFF_TIMER・CFG_PROGRESS_TIMER）、GLB の doorbell。ack が揃うのを 100 ms まで poll。
11. PING を一度 toggle して ack を確かめ、「firmware の interface vX.Y.Z、group N、stream M」を 1 行に出す。

## 4. firmware と license（ユーザーの判断が要る）

- G310 は CSF の GPU なので、MCU の firmware `mali_csffw.bin` が**必須**（無ければ GPU は何も実行しない）。
- 出所: linux-firmware の `arm/mali/arch10.<minor>/mali_csffw.bin`。license は linux-firmware の `LICENCE.mali_csffw`（再配布可と見られる、本文は未読）。
- 推奨: 他の firmware と同じく `userland/firmware/` に「取得して hash で確かめる」package を作り、root の `/lib/firmware/arm/mali/arch10.<minor>/` に置く。**license の確認と同梱にはユーザーの同意が要る**ので、この Phase では package を作っていない。
- arch minor は実機の GPU_ID でしか決まらない。夜の試験の最初の 1 行（`mali: Mali-G310 r?p? arch 10.N ...`）で確かめ、それに合う file を置く。driver は arch minor から path を作るので、file を置けば変更なしで読む。

## 5. 表示（scanout）の方針

- guardrail「GPU の driver は firmware が出していた出力先以外に scanout を始めない」に従い、HDMI・VENC・VPU の clock と mode には一切触れない。
- U-Boot が残した OSD1 を読み（CTRL_STAT の enable と linear、BLK1_CFG_W4 の address、W1/W2 の窓、BLK2_CFG_W4 の幅）、boot の framebuffer（`odroidc5.framebuffer`）と address・大きさが合う時だけ「flip できる」とする。
- 表示の node（gpu0）: 1 つの出力、1 つの plane、U-Boot の mode（60 Hz と報告）、1 つの lease。present は
  (a) 画面と同じ幅・高さ・行の幅で、16 byte に揃い 4 GiB より下の連続の resource なら、CPU の cache を clean して OSD1 の BLK1_CFG_W4 だけを書き換え（zero copy の flip）、1 frame（20 ms）待って前の buffer を返す。
  (b) それ以外は CPU で boot の framebuffer へ写し、OSD1 を framebuffer に戻す。
- lease の終わり・close で OSD1 を boot の framebuffer に戻し、text console を再開する。
- 未確認の点: OSD1 の register が vsync で latch されるか（されなければ flip の瞬間に裂ける）。U-Boot が canvas mode を使っていれば address が合わず、(b) の copy だけになる（安全側）。

## 6. 段の計画と大きさ（見積もり）

| 段 | 内容 | 大きさ | この session |
| --- | --- | --- | --- |
| G1 | attach・S7D の glue・ID・reset・L2・割り込み | 約 900 行 | 書いた |
| G2 | GPU の MMU（page table の builder と AS） | 約 600 行 | 書いた（builder は host 試験） |
| G3 | firmware の読みと検べ、MCU の起動、global の interface | 約 1000 行 | 書いた（parser は host 試験） |
| G4 | CSG・CS の slot と ring、doorbell、同期の object（submit の口） | 約 800 行 | 口と slot の起動だけ（stub） |
| G5 | 表示の node（OSD1 の採用と flip、CPU の copy） | 約 900 行 | 書いた |
| G6 | render の node と Vulkan の executor（wire の decode、memory・buffer・image・queue・fence） | 約 6000 行（powervr の vk/ が 8.5k 行） | 未着手 |
| G7 | Valhall の shader compiler（SPIR-V → Valhall の ISA、共通の front end `src/drivers/gpu/compiler/` を使う） | 約 10000 行以上 | 未着手 |
| G8 | CS の命令の builder（RUN_COMPUTE・RUN_FRAGMENT・tiler の heap・同期） | 約 2000 行 | 未着手 |

## 7. やっていない事（正直な一覧）

- **Vulkan の device は無い。** Keiland の compositor は libvulkan の graphics の pipeline（SPIR-V の shader）だけで描くので、G6〜G8 が無い限り desktop は出ない。
  CPU の copy・clear だけの Vulkan の device を作っても compositor の描画（shader）は動かず、また guardrail「画像の処理は GPU で」に反するので作っていない。
- render の node（gpu1）は登録しない（Vulkan の executor が無いので、libvulkan に壊れた device を見せないため）。
- shader・tiler の core の電源は firmware に任せる（host は L2 だけ）。
- DVFS・電源の切り・suspend・reset からの回復は無い。
- `PWR_OVERRIDE1` の Amlogic の値は書いていない（GPU の電源が入らない時に最初に試す）。

## 8. host の試験

```sh
cc -std=c11 -Wall -Wextra -Werror -Isrc -Iinclude \
   tests/mali-pagetable-host-test.c src/drivers/gpu/mali/mali-pagetable.c -o build/mali-check/pagetable && build/mali-check/pagetable
cc -std=c11 -Wall -Wextra -Werror -Isrc -Iinclude \
   tests/mali-firmware-host-test.c src/drivers/gpu/mali/mali-firmware-image.c -o build/mali-check/firmware && build/mali-check/firmware
```

## 9. 今の状態（2026-10-29、P7 の指示で途中でラップアップ）

書いて commit した物（全て実機未試験）:

| file | 内容 | 確かめ |
| --- | --- | --- |
| `src/drivers/gpu/mali/mali-regs.h` | §2 の register の表（出典の comment つき） | 下の compile に含まれる |
| `src/drivers/gpu/mali/mali-private.h` | device・buffer・address space・interface の型と、各 file の関数の宣言 | 同上 |
| `src/drivers/gpu/mali/mali-pagetable.c/.h` | AArch64 stage 1・4 KiB の page table の builder | host 試験 PASS、arm64 の compile warning 0 |
| `src/drivers/gpu/mali/mali-firmware-image.c/.h` | `mali_csffw.bin` の header・entry・section の parser | host 試験 PASS、arm64 の compile warning 0 |
| `src/drivers/gpu/mali/mali-gpu.c` | register の accessor、ID・features の読み、model 名、soft reset、L2 の power、cache の flush、3 本の割り込み（記録だけ）、事象の報告 | arm64 の compile warning 0（link は未） |
| `tests/mali-pagetable-host-test.c`・`tests/mali-firmware-host-test.c` | §8 の host 試験 | 両方 PASS |

arm64 の単独 compile（kernel の object と同じ flag、board は ODROID-C5）:

```sh
CL="build/llvm/bin/clang --target=aarch64-unknown-zedbsd -nostdlibinc -Iinclude -Isrc -I. -Isrc/hal/arm64 \
 -Isrc/hal/arm64/bsp-s905x5m -DHAL_ARCH_ARM64 -DHAL_BOARD_ODROID_C5 -DCONFIG_DRIVER_MALI_GPU=1 \
 -DKERN_USER_ABI_AARCH64 -DKERN_USER_ABI_LP64 -march=armv8-a -mno-outline-atomics -mgeneral-regs-only \
 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables \
 -fno-common -ffunction-sections -fdata-sections -Os -Wall -Wextra -Werror"
for f in mali-pagetable mali-firmware-image mali-gpu; do $CL -c src/drivers/gpu/mali/$f.c -o build/mali-check/$f.o; done
```

**まだ無い物（次の担当がこの順に書く）**:

1. `src/drivers/gpu/mali/mali-attach.h` と `mali-attach.c`: P7 の要求どおり `int drv_mali_gpu_attach(uint64_t fdt_phys);`（0 か負の errno）。
   FDT（`arm,mali-valhall-csf` か `arm,mali-midgard`）から reg[0]・interrupts（SPI + 32）を読み、glue の power_up、map、`mali_gpu_read_identity`、1 行の log、
   arch major が 10 でなければ止める、`mali_gpu_irq_start`、soft reset、L2 on、後の段の thread を `kthread_create` で起こす（powervr の firmware-boot.c の形）。
2. `mali-s7d.c`: §2 の S7D の glue（sys_mali の gate、MALI・MALI_STACK の clock を fclk_div5/1、RESET_MALI 34・MALI_CAPB3 33 の level の pulse）。
3. `mali-mmu.c`: `mali_buffer_alloc`（`kern_pmem_alloc_limited` + `kern_pmem_map_uncached`）、page table の page の allocate/view、AS の TRANSTAB・MEMATTR（slot 0 NC、slot 1 WB）・TRANSCFG（AARCH64_4K | INA(55−va_bits) | WALK_UNCACHED）と UPDATE、AS_STATUS の ACTIVE の poll、`mali_mcu_alloc`（shared section の後ろから bump）。
4. `mali-firmware-file.c`: 根の file system を待って file を読む（powervr の firmware-file.c と同じ手順、自前の code で）。
5. `mali-csf.c`: §3 の 7〜11（section の memory と map、MCU_CONTROL=AUTO、global の interface の読みと初期化、PING）。interface の各 field の offset は Linux v7.3 `panthor_fw.h` の struct の並びから値として取る。
6. `mali-queue.c`: CSG 0・CS 0 の起動（ring・ring の input/output の page、CS_CONFIG、CSG の endpoint・allow mask・config = AS 番号、doorbell の toggle）。命令の builder は無し。
7. `display/osd.c`・`display/display.c`: §5 の表示の node（powervr の display/display.c の契約に倣う。U-Boot の OSD1 を読み、合えば BLK1_CFG_W4 の書き換えで flip、他は CPU の copy）。
8. `platform/arm64/mali.mk`（`ARM64_MALI_SOURCES`）。**1 と 2 ができるまで作らない**（今 wiring すると `drv_mali_gpu_attach` が無く link が失敗する）。

## 10. 他の file への差分の案（P7 が適用する）

- 今は無し。P7 は `platform/arm64/mali.mk` ができるまで `CONFIG_DRIVER_MALI_GPU=0` のままにする。
- mali.mk ができた後の案: `platform/arm64/vmunix.mk` の ODROID-C5 の source の一覧に
  `ifeq ($(CONFIG_DRIVER_MALI_GPU),y)` の中で `include platform/arm64/mali.mk` と `$(ARM64_MALI_SOURCES)` を足す（P7 の計画どおり）。
- Vulkan（libvulkan の変更）は G6〜G8 が無いので今は要らない。
