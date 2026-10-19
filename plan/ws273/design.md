# WS273 の設計: Anbernic RG350（Ingenic JZ4770、MIPS32）の実機への移植

- 版 1: 2026-10-19 P2（ws273-p001、q1317 / q1317-i01）。
- 版 2: 2026-10-19 P1（q1320 / q1320-i01）。[レビュー 1](design-review-1.md) の B1・M1〜M10・Minor 1〜12・D3 の変更・D7 の追加に対応（末尾 §9 の表）。
- コードは書かない。design-reviewer の差分の確認待ち。

## 0. 出典と確認の水準

| 記号 | 出典 | 使い方 |
| --- | --- | --- |
| [L-dtsi] | Linux master `arch/mips/boot/dts/ingenic/jz4770.dtsi`（GitHub torvalds/linux、2026-10-19 `gh api`） | SoC の address・割り込みの番号 |
| [L-gcw0] | Linux master `arch/mips/boot/dts/ingenic/gcw0.dts` | GCW Zero（同じ JZ4770 の参照の基板） |
| [OD-dts] | OpenDingux/linux `jz-6.1` の `arch/mips/boot/dts/ingenic/` の `rg350.dts` → `rg350m.dts` → `rg280m-v1.0.dts` → `rg280m-v1.1.dts` → `rg280v.dts` → `bits/gcw0-base.dtsi` | RG350 の基板（panel・button・LED・memory・UART・MMC・timer の割当）。mainline に RG350 の DT は無い |
| [L-probe] | Linux master `arch/mips/kernel/cpu-probe.c`（`cpu_probe_ingenic`）、`arch/mips/include/asm/cpu.h`、`arch/mips/include/asm/mach-ingenic/cpu-feature-overrides.h`、`arch/mips/ingenic/Kconfig` | CPU の癖 |
| [L-sc] | Linux master `arch/mips/mm/sc-mips.c`（`mips_sc_probe`） | L2 |
| [L-ctx] | Linux master `arch/mips/mm/context.c`、`arch/mips/mm/c-r4k.c` | VTAG の I cache の扱い |
| [L-ost] | Linux master `drivers/clocksource/ingenic-ost.c` | OST の幅 |
| [L-panel] | Linux master `drivers/gpu/drm/panel/panel-simple.c`（`frida_frd350h54004`） | panel の timing・bus の flag |
| [UBI] | pcercuei/UBIBoot master（GPL-2.0）: `src/head.S`・`src/main.c`・`src/fat.c`・`src/uimage.c`・`src/board-gcw0.c`・`src/config-gcw0.h`・`src/config.h`・`config-gcw0.mk`・`ldscripts/target-jz4760.ld` | 起動の道・入口の CP0 の状態 |
| [BR] | buildroot `arch/Config.in.mips`（`BR2_mips_xburst` の help） | XBurst の FPU の不具合 |
| [PM] | Ingenic JZ4770 Programming Manual | **未入手**（2026-10-19 WebSearch で公開の所在が見つからない） |

- 版 2 の P1 は、レビュー 1 が `gh api` で取った複写（Q1 の scratchpad の `src/`）で、B1（`head.S` の Cause・Status）、M2（`timer@e0` が `tcu` の子、`ingenic-ost.c` の `jz4770-ost` → `is64bit = true`）、M4（`sc-mips.c` の JZ4770 の way の補正）、M5（`gcw0-base.dtsi` の X=PE27・Y=PE28、`rg280v.dts` の `&btn8` = PB10）、M6（`fat.c` の FAT32 の検査）、Minor 9（Select の BOOT_SEL1 の comment）を読み直して確かめた。それ以外のレビューの根拠は「確（レビュー 1）」と書く。
- 「確」= 上の出典で読んだ、「推測」= 出典の振る舞いからの推測、「未確認」= 読めていない。
- **GPL・MIT の出典の規則**（guardrail「MIT・GPL の出典のデータの引用」2026-10-15）: Linux・OpenDingux・UBIBoot の address・bit・timing・pin の値と表はデータとして使ってよい。使う block の直前に複数行の comment で「Value referenced: / Values referenced: <版> <path> (<表・define・関数の名前>)」、手順を参考にした block には「Procedure referenced: <版> <path> (<関数の名前>)」と書く。**file の先頭には参考・引用・出典を一切書かない。** code・comment・構造・symbol の名前（関数・型・macro・register の名前）は写さず、zedBSD の名前で書く（Zlib）。

## 1. 事実の表（受け入れの条件 1）

### 1.1 CPU
- core: XBurst1（MXU1.1）の 1 core。compatible `ingenic,xburst-fpu1.0-mxu1.1`（確 [L-dtsi]）。
- ISA: PRId の company は 0xd1（`PRID_COMP_INGENIC_D1`）で MIPS32r2 の扱い、Kconfig `MACH_JZ4770` は `SYS_HAS_CPU_MIPS32_R2`（確 [L-probe]）。EBase・`ehb`・`rdhwr` は使える見込み（p003 で PRId・Config を記録）。
- XBurst の癖（確 [L-probe]）:
  1. CP0 の Count/Compare が無い（`cpu_has_counter 0`）。timer は SoC の TCU/OST（§1.8）。
  2. I cache が virtually tagged（`MIPS_CACHE_VTAG`）。tag に ASID が入る（推測、[L-ctx] が ASID の一巡で `flush_icache_all` を呼ぶことから）。
  3. reset の後は Ingenic 独自の HPTLB の mode。CP0 5 sel 4（PageCtrl）に `0xa9000000` で VTLB に戻す。**UBIBoot が `board_init()` で既に書いている**（確（レビュー 1）[UBI]）。kernel でも TLB の初期化の前に書き直す（害は無い）。
  4. Config7 の bit 4（BTB の loop の検出）は Linux が切る（性能の測定のため、必須ではない）。
- **Config2 は在る**（版 1 の「Config2 が無い」は cpu-probe.c の comment の誤読）。L2 は Config1.M → Config2 で読み、**way の数は Config2 が 5 と出るので 4 に直す**（確 [L-sc] `MACH_INGENIC_JZ4770` の分岐）。L2 の大きさは Config2 から求める（実値は未確認）。
- override（確 [L-probe]）: `cpu_has_4k_cache 1` は **R4K 型の cache の命令を持つという意味で、大きさではない**（版 1 の誤読を直した）。line 32 byte、`cpu_has_dc_aliases 0`、`cpu_has_divec 1`、`cpu_has_userlocal 0`、`cpu_has_watch 1`、`cpu_has_llsc 1`、64 bit なし、DSP なし。L1 の I・D は 16 KiB・4 way と見込む（推測、Config1 で確かめる）。
- TLB の entry 数: 未確認（Config1.MMU、HAL は既に Config1 から求める）。
- FPU: 有り。**FIR.F64（FR=1 の可否）は未確認**。XBurst の FPU には fused madd の不具合があり gcc は `-mno-fused-madd` が要る（確 [BR]）。LLVM では `-mno-madd4` に当たる（推測）。userland の WS へ引き継ぐ（§6）。
- endian: little（確 [L-probe]）。速度: PLL0 996 MHz、水晶 12 MHz（確 [UBI]・[OD-dts]）。

### 1.2 memory
- RAM: 512 MiB の見込み（UBIBoot の `rg350` の DDR2 の型から。量は未確認）。
- 物理: `0x00000000`〜`0x0fffffff` と `0x30000000`〜`0x3fffffff`（確 [OD-dts]、UBIBoot の `USES_HIGHMEM`）。
- 高い側は KSEG0/1 に届かない。この WS は低い 256 MiB だけ（D2 A）。高い側は Future Work（§6、Minor 12 の補い）。
- SoC の register は物理 `0x10000000`〜（KSEG1 `0xb0000000`〜）、boot ROM は `0x1fc00000`（確 [L-dtsi]）。

### 1.3 起動の道（boot ROM → UBIBoot → kernel）
- boot ROM が内部の microSD（MSC0）から UBIBoot を読み、**UBIBoot は `0x80000000` からの 8 KiB の cache の窓に code・data・stack を全部置いて走る**（確（レビュー 1）[UBI] ld script）。boot ROM の読み込みの形の詳細は未確認（[PM] 未入手）。
- U-Boot は使わない。RG350 は UBIBoot（確 [UBI] `config-gcw0.mk` の `rg350`）。
- kernel の読み込み（確 [UBI] `fat.c` `mmc_load_kernel`）:
  1. MSC0 の MBR の第 1 partition を読む。**FAT32 でないと読まない**（確 `fs_type` の検査）。初期化に失敗した時だけ 2 番目の MMC（slot は未確認）。
  2. 探す順: 通常は `UZIMAGE.BIN` → `VMLINUZ.BIN`。**alt key（PE28）を押した時は `UZIMAGE.BAK` → `VMLINUZ.BAK` → `UZIMAGE.BIN` → `VMLINUZ.BIN`**。header が不正だと**黙って次の file へ進む**（確（レビュー 1））。→ zedBSD の image が拒まれると通常の OpenDingux が起動し、zedBSD の失敗と見分けられない（D1 の条件、§2.3 の印）。
  3. `UZIMAGE` は legacy の uImage（magic `0x27051956`、os Linux、arch MIPS、type kernel、圧縮なし）。**header を含む最初の 1 sector だけを `LD_ADDR`（物理 `0x00600000`）に読んで本体の先頭を `load` へ memmove し、残りの cluster は `load` へ直に読む**（確（レビュー 1）`load_cluster_chain`・`process_uimage_header(…, MMC_SECTOR_SIZE)`）。版 1 の「5 MiB を超えると重なる」心配は当たらない。代わりに image の後ろに cluster の余りが書かれるが、kernel の bss は後で消すので害は無い。header の CRC は見ない（magic・os・arch・type・comp だけ、確 [UBI]）。
  4. `VMLINUZ` は読んだ所（KSEG1 の `0xa0600000`）へ飛ぶ。`UZIMAGE` は header の `ep` へ飛ぶ。
  5. 引数（o32）: `a0 = argc`、`a1 = argv`、`a2 = a3 = NULL`。DT は渡さない。
- **入口の CP0 の状態**（確 [UBI] `head.S`）: Status = `0x00400004`（BEV=1、ERL=1）、**Cause = `0x00800000`（IV=1）**。PageCtrl は VTLB（上の 1.1-3）。
- **argv は D cache の中だけにある**（M1）: `kernel_params[]` と文字列は UBIBoot の 8 KiB の窓の `.data`・`.rodata` にあり、その cache line は dirty の印が無く、追い出されると中身が失われる（確（レビュー 1）`main.c` の comment）。版 1 の「読み込みが D cache を汚さない」は意味の取り違えだった。kernel の bss の消去（page の bitmap だけで 16 KiB 超）で確実に追い出される。→ **この WS では argv を使わない**（M1 の (a)）。`mips_board_boot` は a0・a1 を無視し、`boot.command-line` は空にする。使う必要が出た時は `locore.S` で bss の消去の前に KSEG0 から読んで KSEG1 の宛先へ写す（(b)、後の WS）。
- I cache: 入口の状態は BEV・ERL で cached の窓から来るので、kernel の入口で I cache を全部無効にする（`Index_Invalidate_I`）。D cache も入口で全部 writeback・invalidate する。

### 1.4 LCD
- LCDC `ingenic,jz4770-lcd`、`0x13050000`、INTC 31（確 [L-dtsi]）。
- panel `frida,frd350h54004`、320×240、24 bit の並列 RGB、6.0 MHz、h 320+44/+16/+20、v 240+2/+6/+2、hsync・vsync は負の極性（確 [OD-dts]・[L-panel]）。**bus の flag: DE は active high、pixel は clock の立ち下がりで sample**（確（レビュー 1）[L-panel]）。
- reset: DT は PE2（active low）を書くが、**Linux の panel-simple は `enable-gpios` しか読まず `reset-gpios` を使わない**（確（レビュー 1））。PE2 の順序と時間には参照が無い。p004 は「PE2 を high（解除）にしておく」だけから始め、映らなければ low→待ち→high を試す（試行の上限の中で）。
- backlight: PWM1（TCU の channel 1）、pin PE1。UBIBoot は PE1 を GPIO の low にする（確 [UBI]）→ 起動直後は消えている見込み。最初の段は PE1 を GPIO の high。**backlight だけの印は、panel に信号が無いと暗いまま光るだけの可能性がある**（推測）ので、印は LED と組む（§2.3）。
- pixel clock: OD の DT は LPCLK の親を PLL1（432 MHz）にする（確（レビュー 1））。UBIBoot が PLL1 を動かすかは未確認。案は 2 つ: (i) PLL1 が動いていれば 432/72 = 6 MHz、(ii) 動いていなければ PLL0（996 MHz）を 166 で割る（≈ 6.0 MHz）。どちらでも LCD の clock gate（CGU の CLKGR の LCD の bit）を開ける。bit と順序は Linux の `drivers/clk/ingenic/jz4770-cgu.c` と `drivers/gpu/drm/ingenic/ingenic-drm-drv.c` を「Procedure referenced:」で参照（p004 の最初の作業、版を記録）。
- **framebuffer と LCDC の descriptor の置き場所と書き方**（M4）: `mips_board_init` は `mips_page_init` より前に呼ばれるので、framebuffer（320×240×4 = 300 KiB、page に揃える）と descriptor は image の bss に置き、**KSEG1（uncached）の alias から書く**。L1・L2 の dirty な line が DMA と食い違わない。console の描画は KSEG1 経由（速度は 320×240 なら十分と見込む）。

### 1.5 SD（MSC）
- MSC0 `0x10021000`（INTC 37）、MSC1 `0x10022000`（36）、MSC2 `0x10023000`（35）（確 [L-dtsi]）。RG350: MSC0 = 内部の TF1、MSC1 = 外部の TF2（確 [OD-dts]）。driver は範囲外（§6）。

### 1.6 入力（DT のとおり、確 [OD-dts]）
| 名前（DT の label） | pin | 極性 |
| --- | --- | --- |
| D-pad 上・下・左・右 | PE21・PE25・PE23・PE24 | low |
| A・B | PE29・PE20 | low |
| **X** | **PE27** | low |
| **Y** | **PE28** | low |
| L1 | **PB10**（`rg280v.dts` の `&btn8` が PB20 を上書き） | low |
| L2 | PB20 | low |
| R1・R2 | PE26・PB11 | low |
| Start | PB21 | low |
| Select | PD18（**BOOT_SEL1 を兼ねる**、DT の comment） | high |
| Power・Vol+・Vol−・L3・R3 | PA30・PB12・PB13・PB14・PB15 | low |

- **UBIBoot の alt key は PE28**。UBIBoot の source は PE28 を「X button」と呼ぶが、DT の label では PE28 は **Y**（確 `board-gcw0.c` と `gcw0-base.dtsi`）。物理のボタンの刻印がどちらに当たるかは未確認。→ 手順は「Y（DT の PE28）を押す。OpenDingux が起動したら X も試す」（§3.2）。版 1 の §7-7（L2 = L1）の疑いは誤読で、L1 = PB10、L2 = PB20。
- analog stick: SADC `0x10070000`、INTC 18（確 [OD-dts]）。範囲外。
- GPIO `0x10010000`、port A〜F、割り込みは INTC 17〜12（port C が 15）（確 [L-dtsi]）。
- **電源の LED**: `gpb 30`（PB30）、active low、`panic-indicator`（確 [OD-dts] `gcw0-base.dtsi`）。早期の印に使う（§2.3）。

### 1.7 UART
- UART2、57600 8N1、pin PC28・PC30（確 [OD-dts]・[UBI]）。基板の pad の有無は未確認（WebSearch でも見つからない）。早期の console は LCD と LED（D4 A）。

### 1.8 割り込み・timer（M2 で直した）
- INTC `0x10001000`、CPU の IP2（確 [L-dtsi]）。64 本と読める（register の形は未確認、Linux の `irq-ingenic.c` を手順の参照に）。
- TCU `0x10002000` は自身が interrupt-controller で、親の INTC へは **25・26・27** の 3 本（確 [L-dtsi]）。
- **OST は TCU の子**（`timer@e0`、TCU の offset `0xe0`）で、DT の `interrupts = <15>` は **TCU の中の番号（TFR の bit 15）であり INTC の 15 ではない**（INTC の 15 は GPIO の port C）。OST が INTC の 25〜27 のどれを通るかは未確認。**OST は 64 bit**（確 [L-ost] `jz4770-ost` → `jz4760b` の info、`is64bit = true`）。Linux は OST を clocksource にだけ使い割り込みは使わない。
- OpenDingux の割当（確（レビュー 1）[OD-dts]）: system timer = TCU channel 0、clocksource = channel 2、OST は 12 MHz、channel 1 = backlight の PWM、channel 4 = 振動の motor。
- **案**: tick = TCU の channel 0 を 1000 Hz、monotonic = OST の 64 bit の counter（wrap の処理は要らない）。INTC の 25・26・27 を全部開け、handler は TFR を読んで channel 0 の flag だけを tick として扱い、**kernel に渡す前に TFR の flag を消す**（level の割り込み）。channel 1・2・4 には触れない。

## 2. WS271 の HAL との差（受け入れの条件 2）

WS271 の HAL は `src/hal/mips/`（共通）＋ `src/hal/mips/bsp-malta/`。board への要求は `src/hal/mips/bsp.h` の関数。

### 2.1 BSP の分け方
```
src/hal/mips/bsp-jz4770/   SoC: intc.c、tcu.c（TCU ch0 の tick、OST の counter）、cgu.c、gpio.c（LED・backlight の印を含む）、lcdc.c
src/hal/mips/bsp-jz4770/board-rg350.c   memory の表、panel の timing、pin（backlight PE1、LED PB30、reset PE2）
platform/mips32-rg350/     vmunix.mk・vmunix.ld・Makefile（uImage の段を含む）
```
- **console の共有**（Minor 5）: bsp-malta の `cons.c` の格子は compile 時の 80×30（`MALTA_CONSOLE_COLUMNS`・`ROWS`）で、pixel は `framebuffer.c` の `malta_framebuffer_cell` が PCI の KSEG1 へ書く。共有するには **格子の大きさ（列・行）と cell を描く callback を bsp が渡す形**にする（`cons.c` を `src/hal/mips/` の共通の層へ移し、格子の大きさを実行時の値にする。`font.c` も共通へ）。HAL の内部の file の移動で API は不変。

### 2.2 共通の層の変更（すべて p002、HAL の内部、API は不変）
| 項 | malta（WS271） | RG350 | p002 での変更 |
| --- | --- | --- | --- |
| **入口の CP0 の状態**（B1） | IV=0 で入る | UBIBoot は Cause.IV=1、Status = BEV|ERL で入る | `locore.S` の先頭で **Cause を 0**（`mtc0 $0, $13`）。Status は既存の初期化のまま（BEV・ERL を落とす所を確かめる）。**`vectors.S` の EBase+0x200 にも `mips_trap_entry` への分岐を置く**（IV が残った時の保険。`asm.h` の vector の配置に 0x200 を足す）。 |
| PageCtrl | 無い | VTLB に戻す（UBIBoot も書く） | PRId の company が 0xd1 の時、TLB の初期化の前に CP0 5 sel 4 へ `0xa9000000`。 |
| 入口の cache | — | cached の窓から来る | 入口で I cache を全部無効、D cache を全部 writeback・invalidate（`cache.c` の既存の全体の操作を早い段で呼ぶ）。 |
| argv（M1） | YAMON 風 | D cache の中だけ | `mips_board_boot` は bsp の関数のまま。RG350 の bsp は a0・a1 を読まない。共通の層は不変。 |
| **timer**（M2） | `clock.c` が CP0 Count/Compare、IP7 | Count が無い、tick は IP2 → INTC → TCU | timer を bsp の関数に移す（`mips_board_timer_init(hz)`・`mips_board_counter_read()`）。malta の bsp は今の Count/Compare を持つ。**tick の経路**: `int.c` の IP7 の経路は malta の bsp の物にし、RG350 は IP2 の bsp の handler から共通の tick の関数を呼ぶ。`cmain.c` の `wait_for_ticks` の IM の bit（今は IP7 の `MIPS_INTERRUPT_TIMER`）は bsp が返す mask にする。`mips_irq_begin_local` と `MIPS_ACK_LOCAL` が IP2 の経路でそのまま使えるかは p002 で code を読んで確かめ、使えなければ bsp の ack を挟む。`mips_board_count_frequency` は置き換える。 |
| **I cache の VTAG**（M3） | PIPT 扱い | VTAG（tag は VA と ASID、推測） | (1) `space.c` の `asid_assign()` で世代が変わる時に `tlb_flush_all()` に加えて **I cache 全体を `Index_Invalidate_I`**。(2) `hal_space_prot`（`hal_space_prot_query`）で EXEC が付く時、**kernel の alias で該当の page の D cache を書き戻し、I cache 全体を消す**（arm64 の WS044 p009 と同じ不足の型）。(3) VTAG の時は `hal_icache_invalidate_range` と `hal_sync_instruction_stream` を常に全体の消去にする（`src/kern/ptrace.c:328` は tracee の VA で呼ぶが、走っているのは tracer の space で Hit の操作は届かないため。malta の潜在の不具合の修正にもなる）。VTAG の判定は PRId（0xd1）か Config から。 |
| **L2**（M4） | 無い | Config2 で在る、way 4、DMA に coherent でない（推測） | `cache.c` に **L2 の Hit の操作（Hit_Writeback_Inv_SD・Hit_Invalidate_SD）**と Config2 の probe（JZ4770 の way の補正）を足す。DMA の同期の関数と `page.c` の `hal_pmem_map_uncached` で L1 の後に L2 も操作する。L2 の無い malta では何もしない。 |
| cache の色 | 無し | `dc_aliases 0` | Config1 から求める今の code のまま。 |
| 割り込み | IP2 の 8259 | IP2 の INTC | bsp の irq を差し替える。 |
| **表示の名前**（M8） | `cmain.c:147-149` が `display = "VGA"` に固定 | `DISPLAY LCDC` | bsp に表示の名前を返す関数を足し、`cmain.c` はそれを使う。 |
| **判定の文字列**（M8・Minor 6） | 1 行 | 40 字 | 判定の行を bsp の列の数で分けて出す共通の関数にする（malta は 80 列なので今と同じ 1 行）。 |
| 画面 | Bochs VGA 640×480 | LCDC 320×240、32 bit の pixel、KSEG1 から書く | bsp。 |
| EBase | 使う | R2 で使える見込み | 不変（p003 で記録）。 |
| FPU（M10） | FR=1 | FIR.F64 未確認 | **変えない**。今の `fpu.c:82-86` が FIR.F64=0 の時に FPU を使わない（D3）。 |

### 2.3 早期の観測（M7）
- LCD が出る前の段は、**電源の LED（PB30）と backlight（PE1）を GPIO で点滅させた回数**で段を示す。案:

| 段 | LED の点滅 | backlight |
| --- | --- | --- |
| `_start` に入った | 1 回 | 点ける |
| C（`mips_cmain`）に入った | 2 回 | 点けたまま |
| HAL の init の前 | 3 回 | 点けたまま |
| LCDC を起こした（単色で塗る） | 4 回 | 点けたまま |
| early exception | 長い点灯 1 回 + ExcCode の値の回数の短い点滅、繰り返し | 消灯 |

- 点滅の待ちは OST が動く前なので命令の数の空回し（996 MHz を前提に目で数えられる 0.3 s 程度）。`mips_early_exception()` は console に加えて、bsp の「印」の関数を呼ぶ hook を共通の層に足す（p002）。
- LCDC が動いたら、画面を単色で塗る段（赤 = HAL の init の前、緑 = console が描ける）を挟む。
- **成功の印を OpenDingux の起動と見分ける**（M6）: OpenDingux の起動は logo と menu。zedBSD は (1) LED の点滅の列、(2) 単色の画面、(3) 白黒の文字の console で、どれも OpenDingux には出ない。点滅が無く OpenDingux の logo が出たら「zedBSD の image が拒まれた」と判断する。
- gdbstub・UART は無い（D4 A）。

### 2.4 HAL の API の変更の要否
- **この WS の変更は API を変えない**。B1・M1〜M4・M8 の変更は `src/hal/mips/` の内部と platform だけ（2026-09-25 の規則で承認なしで可）。
- `boot.command-line` は空（argv を使わない、§1.3）。
- 承認が要るのは次の 2 点だけで、**案を `plan/ws273/proposed/` に置いた（未適用）**:
  1. [proposed/mips32-h-timer-comment.diff](proposed/mips32-h-timer-comment.diff): `include/hal/arch/mips32.h` の `HAL_TIMER_FREQUENCY` の comment（「the coprocessor 0 counter」）を RG350 でも正しい表現にする。意味は不変（Minor 10）。p002 で入れるかはユーザーの判断（D8）。入れなくても動作は変わらない。
  2. [proposed/mips32-h-fpregs-fr0.diff](proposed/mips32-h-fpregs-fr0.diff): FR=0 の `hal_fpregs` の意味（M10）。**この WS では使わない**。userland の WS で FR=0 に対応する時の叩き台。`fpregs.S` は `.set fp=64` で奇数の register にも `sdc1` を使い、`src/kern/ptrace.c:405-497` は `hal_fpregs` を `struct fpreg` に写すので、FR=0 の対応は API の意味の変更と kernel の変更を伴う。

### 2.5 HAL の外
- `Makefile`・`tools/menuconfig.py` に `mips32-rg350`（arch `mips32`、board `rg350`、build `build/mips32-rg350`）。
- uImage の header（64 byte、big endian、CRC32 が 2 つ）は自前の短い Python の script（`zlib.crc32`、D5 B）。UBIBoot は CRC を見ないが正しく付ける。
- link address: `0x80100000`（UBIBoot の窓 `0x80000000`〜8 KiB、`LD_ADDR 0x00600000` の 1 sector とは重ならない。§1.3-3 のとおり本体は `load` へ直に読まれる）。p003 で uImage の `load`・`ep` を link address と `_start` に合わせる。

## 3. 実装の Phase（受け入れの条件 3）

8 LW ≈ 2〜3 時間。QEMU で確かめられるのは build・host 試験と malta の回帰（T1）だけ。**QEMU は cache を模擬せず IV も立てないので、B1・M3・M4 の誤りは T1 では見えない**。実機の確認の方法は D7。

### 3.1 Phase の表
| Phase | 中身 | 依存 | LW | 確認 |
| --- | --- | --- | --- | --- |
| p002 | 共通の層の変更を全部（§2.2 の表の全行と §2.3 の印の hook）: Cause の消去と 0x200 の vector、PageCtrl、入口の cache、timer の bsp 化と tick の経路・IM の mask、VTAG の I cache の 3 項、L2 の操作、表示の名前、判定の行の分割、console の共通化。malta の振る舞いは不変 | WS271 | 12 | build（mips32-malta・mips64-malta、warning 0）、host 試験、T1 の malta の回帰（mips32・mips64 の画面） |
| p003 | `bsp-jz4770` の最小: 入口、LED・backlight の印、`platform/mips32-rg350`、uImage の script、PRId・Config0〜3・FIR を後で出すための記録 | p002、D1・D5・D7 | 6 | **実機 1**: 点滅 1〜3 回と backlight（目視）。build、malta の回帰は共通の層に触れた時だけ |
| p004 | CGU の LPCLK・clock gate・LCDC・panel、KSEG1 の framebuffer、console、単色の段 | p003 | 10 | **実機 2**: console の文字が読める。最後に PRId・Config1・Config2・FIR のまとめを出し直し、写真で返す（Minor 6）。**試行の上限は実機 3 回**、上限で映らなければ uncleared にし D4 B（UART の pad を探す）を再検討 |
| p005 | INTC・TCU ch0 の tick・OST の counter、`TIMER TICK` まで | p004 | 6 | **実機 3**: `MIPS32 HAL READY DISPLAY LCDC` と `TIMER TICK` |
| p006 | 例外・TLB・space・FPU の self-test（WS271 §6.1 と同じ）、**I cache の整合の試験**（user の page に code を書いて実行し、書き換えて prot・map を変え、もう一度実行して新しい結果が出るか）、**uncached・L2 の整合の試験**（cached で書いて clean し、KSEG1 から読んで一致するか）、kernel の link、VFS の段まで | p005 | 8 | **実機 4**: `PAGING PASS`・`ICACHE PASS`・`L2 PASS`・`VFS initialization failed` |
| p007 | 規約の全文の見直し | p002〜p006 | 4 | build、malta の回帰（T1） |

- **malta の回帰**（M8）: p003〜p006 で `src/hal/mips/*`（`bsp-*` を除く）に触れた時は、mips32-malta・mips64-malta の build と T1 の回帰を必須にする。原則として共通の層の変更は p002 に集め、後で要ると分かった時は小さく足してこの規則に従う。
- Minor 11 に従い版 1 の p005（10 LW）を p005（TIMER TICK まで）と p006（self-test と VFS まで）に分けた。
- WS の完了の条件の案: 1. 実機で kernel が起動し LCD に console（p004）、2. TIMER TICK と self-test と VFS の段まで（p005・p006）。SD の root・login・button はこの WS の範囲外（D6 A）。

### 3.2 実機の手順（p003 から、D1 A の条件込み）
1. card を PC に挿し、第 1 partition の file の一覧と FAT の種類（FAT32 であること）を記録する。UBIBoot の版（stock か OpenDingux の新しい版か）を分かる範囲で記録する（M6）。
2. 既存の `UZIMAGE.BAK`・`VMLINUZ.BAK` が有れば別の名前で退避し、記録する（`.BAK` は OpenDingux の更新が前の kernel を残す名前。OpenDingux の更新を流すと zedBSD の `.BAK` は上書きされる）。
3. zedBSD の image を `UZIMAGE.BAK` として置く。
4. **Y のボタン（DT の PE28、UBIBoot の alt key）を押しながら電源**。Select（PD18、BOOT_SEL1）は押さない。
5. LED の点滅が無く OpenDingux が起動したら、刻印が逆の可能性があるので X を押して試す。それでも OpenDingux なら image が拒まれた（header の誤り）と判断する。
6. 戻すには何も押さずに起動。退避した `.BAK` を戻す。

## 4. 判定の文字列（320×240、8×16 の font で 40×15）
```
MIPS32 HAL READY DISPLAY LCDC
PAGING PASS TIMER TICK
ICACHE PASS L2 PASS
MIPS32 CPU 1 ONLINE
VFS initialization failed
```
- 行の分割は共通の関数が bsp の列の数で行う（malta の 80 列では今の 1 行のまま、§2.2）。
- `HAL ENTRY prid=…` などの 40 字を超える診断の行は折り返して流れるので、**最後に PRId・Config1・Config2・FIR の短いまとめの行を出し直す**（Minor 6）。

## 5. ユーザーの判断（D1…）

| ID | 問い | 選択肢 | 推奨 |
| --- | --- | --- | --- |
| D1 | 起動の道 | A: OpenDingux の card に `UZIMAGE.BAK` を置き alt key で起動／B: 専用の microSD／C: 自前の bootloader | **A**。条件: §3.2 の 1・2・5（FAT32 と UBIBoot の版の確認、既存の `.BAK` の退避、OpenDingux と見分けられる成功の印 §2.3） |
| D2 | memory の範囲 | A: 低い 256 MiB／B: 高い側も | **A**（B は Future Work、§6） |
| D3 | FPU（XBurst1 が FR=1 を持たない時） | A: この WS では保留、FIR.F64=0 なら FPU を使わない（今の `fpu.c` のまま）／B: FR=0 の対応をこの WS で（`hal_fpregs` の意味の変更の承認が要る）／C: RG350 だけ softfloat | **A**（**版 1 の推奨から変更**、M10）。FR=0 の対応は userland の WS で `hal_fpregs` の意味の変更の承認（proposed/mips32-h-fpregs-fr0.diff）と一緒に決める。D6 A なら FP を使う user は無い |
| D4 | UART の pad | A: 探さない（LCD と LED の点滅）／B: 基板を開けて探す | **A**。p004 の試行の上限（実機 3 回）で映らなければ B を再検討 |
| D5 | uImage の道具 | A: host の `mkimage`／B: 自前の Python | **B** |
| D6 | WS273 の範囲 | A: LCD の console と VFS の段まで／B: SD の root と login まで | **A** |
| **D7**（新、M9） | 実機の検証の方法 | A: ユーザーが実機の画面・LED を目で見て写真で返す結果を証拠にする。AGENTS.md の起動の確認（`boot-test.sh`）の例外として認める。**試験はユーザーが拾える試験の一覧に置き、今すぐは頼まない**。結果が届くまで該当の Phase は uncleared（または結果待ち）／B: 実機の確認をこの WS の受け入れから外す | **A**（RG350 の emulator は無く、実機の他に B1・M3・M4 を確かめる手段が無い） |
| D8（新、Minor 10） | `mips32.h` の `HAL_TIMER_FREQUENCY` の comment | A: proposed の案のとおり直す（HAL の API の file なので承認が要る）／B: 直さない | A でも B でも動作は同じ。**A**（comment の正確さ） |

## 6. 範囲外・後の WS
- MSC の driver と SD の root、GPIO の button と SADC の stick、音、USB、Vivante の GPU、PWM の backlight の明るさ、cpufreq。
- **高い側の memory（D2 B）**: `page.c` の `PAGE_HIGH_ALIAS 0x80000000` は malta の形で、`0x30000000` の bank を使うには **bsp が memory の map（複数の bank）を渡す形**が要る。その上で KSEG2 か TLB の wired の窓で届かせる（Minor 12）。
- mipsel の userland（Q1・toolchain）。**XBurst の FPU の fused madd の不具合**（gcc `-mno-fused-madd`、LLVM は `-mno-madd4` の見込み）と、FR=0 の `hal_fpregs`（D3）を引き継ぐ。
- argv の早い段の写し（§1.3 の (b)）。

## 7. 未確認の一覧
1. JZ4770 の Programming Manual の公開の所在。
2. boot ROM の読み込みの形、`MMC_ID2` の slot、ユーザーの card の UBIBoot の版。
3. RAM の量、card の partition の形。
4. PRId の実値、TLB の entry 数、L1 の形（16 KiB・4 way の見込み）、L2 の大きさ、FIR.F64。
5. INTC の register の形、OST が INTC の 25〜27 のどれを通るか、LCDC・CGU の bit、UBIBoot が PLL1 を動かすか。
6. UART2 の pad、EJTAG の pad。
7. X・Y の物理の刻印と DT の label の対応。
8. 推測のまま残るもの: I cache の tag に ASID が入ること、L2 が DMA に coherent でないこと、backlight だけでは panel に信号が無いと暗く光るだけの可能性、LLVM の `-mno-madd4` の対応。

## 8. 外部の code の規則（受け入れの条件 4）
- §0 の規則（guardrail 2026-10-15）に従う。この設計は値・pin・timing と手順の順序の事実だけを書いた。実装では block の直前の複数行の comment に「Value(s) referenced:」「Procedure referenced:」で出典（版・path・名前）を書き、file の先頭には書かない。code・comment・構造・symbol の名前は写さない。参照した file と版（commit か branch）を各 Phase の phase.md にも記録する。

## 9. レビュー 1 への対応
| 指摘 | 対応 | 箇所 |
| --- | --- | --- |
| B1 Cause.IV=1 で入る、0x200 の vector が無い | 入口の CP0 の状態を事実に足し、`locore.S` の先頭で Cause を 0、`vectors.S` の 0x200 に `mips_trap_entry` への分岐。p002 に入れ malta の回帰に含める | §1.3、§2.2、§3.1 p002 |
| M1 argv が D cache の中だけ | 事実を直し、この WS では argv を使わない（(a)）。(b) は後の WS | §1.3、§2.2、§2.4、§6 |
| M2 OST の割り込みの経路、tick の source | OST は TCU の子で 15 は TCU の中の番号、64 bit。tick = TCU ch0、OST = counter、INTC 25〜27 を開け TFR で振り分けて消す。`int.c` の IP7、`wait_for_ticks` の IM、`mips_irq_begin_local`・`MIPS_ACK_LOCAL` の扱いを p002 に | §1.8、§2.2 |
| M3 VTAG の I cache の不足 | ASID の世代、`hal_space_prot` の EXEC、`hal_icache_invalidate_range`・`hal_sync_instruction_stream` の全体の消去の 3 項を p002 に | §1.1、§2.2、§3.1 |
| M4 L2 と DMA・uncached | Config2 は在る・way 4 に直す。framebuffer と descriptor は bss に置き KSEG1 から書く。`cache.c`・`hal_pmem_map_uncached` に L2 の操作 | §1.1、§1.4、§2.2 |
| M5 ボタンの表 | DT のとおり X=PE27・Y=PE28・L1=PB10・L2=PB20。alt key は PE28、刻印は未確認、手順に「X も試す」 | §1.6、§3.2、§7 |
| M6 D1 A の前提と失敗の見分け | 探す順・黙って次へ進むこと・FAT32・`.BAK` の意味を事実に。退避・確認・見分けられる印を D1 の条件に | §1.3、§2.3、§3.2、§5 D1 |
| M7 LCD の前の観測、試行の上限 | 電源の LED（PB30）と backlight の点滅の表、early exception の code の点滅。p004 の上限 3 回と uncleared・D4 B の再検討 | §1.6、§2.3、§3.1 |
| M8 共通の層の変更が p002 に揃っていない、後の Phase の回帰 | 共通の層の変更を全部 p002 に。後で `src/hal/mips/*`（bsp-* 以外）に触れたら malta の build と T1 を必須 | §2.2、§3.1 |
| M9 cache の誤りを試験で見つけられない、検証の方法 | D7 を足した。p006 に I cache の整合と uncached・L2 の整合の実機の self-test | §3.1、§4、§5 D7 |
| M10 D3 は API の意味を変える | D3 の推奨を「この WS では保留（今の `fpu.c`）」に変更。FR=0 の `hal_fpregs` の案は proposed に置くだけ。madd の注記を userland の WS へ | §1.1、§2.2、§2.4、§5 D3、§6、proposed/mips32-h-fpregs-fr0.diff |
| Minor 1 「4K の cache」の誤読 | R4K 型の cache の命令と直し、大きさは Config1 で | §1.1 |
| Minor 2 memmove は 1 sector だけ | 読み込みの記述を直し、5 MiB の心配を消した。`VMLINUZ` の入口 `0xa0600000` | §1.3、§2.5 |
| Minor 3 入口の状態は分かる | Status・Cause・PageCtrl（UBIBoot が書く）を事実に。kernel も書き直す | §1.1、§1.3 |
| Minor 4 panel の reset・bus の flag・LPCLK の親 | reset-gpios は Linux が使わない（参照無し）、DE high・立ち下がり sample、PLL1 と PLL0/166 の案、clock gate | §1.4 |
| Minor 5 cons・font の共有の形 | 格子の大きさと cell の callback を bsp が渡す形 | §2.1 |
| Minor 6 40×15 で診断の行が流れる | 最後に PRId・Config・FIR のまとめを出し直す | §3.1 p004、§4 |
| Minor 7 GPL の規則が古い | guardrail 2026-10-15 の「Value(s) referenced:」「Procedure referenced:」、file の先頭に書かない、symbol の名前も写さない、に合わせた | §0、§8 |
| Minor 8 phase.md の path | `src/hal/mips32/` → `src/hal/mips/` に直した | phase001/phase.md |
| Minor 9 Select は BOOT_SEL1 | 手順に「Select を押さない」 | §1.6、§3.2 |
| Minor 10 mips32.h の comment | 案を proposed に置き、D8 で判断を求める | §2.4、§5 D8、proposed/mips32-h-timer-comment.diff |
| Minor 11 p005 の大きさ | p005（TIMER TICK まで）と p006（self-test・VFS）に分けた。規約は p007 | §3.1 |
| Minor 12 D2 B の見込み | bsp が memory の map を渡す形が要ると Future Work の記述に補った | §1.2、§6 |
| D3 の推奨の変更 | A（保留）に変更 | §5 |
| D7 の追加 | 実機の検証の方法を足した。あわせて D8 を足した | §5 |
