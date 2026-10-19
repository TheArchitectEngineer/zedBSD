# WS273 の設計: Anbernic RG350（Ingenic JZ4770、MIPS32）の実機への移植

2026-10-19 P2（ws273-p001、q1317 / q1317-i01）。コードは書かない。design-reviewer の前の版 1。

## 0. 出典と確認の水準

| 記号 | 出典 | 使い方 |
| --- | --- | --- |
| [L-dtsi] | Linux mainline `arch/mips/boot/dts/ingenic/jz4770.dtsi`（GitHub torvalds/linux master、2026-10-19 に `gh api` で取得）https://github.com/torvalds/linux/blob/master/arch/mips/boot/dts/ingenic/jz4770.dtsi | SoC の address・割り込みの番号 |
| [L-gcw0] | Linux mainline `gcw0.dts`（同上の directory） | GCW Zero（同じ JZ4770 の参照の基板） |
| [OD-dts] | OpenDingux の kernel `arch/mips/boot/dts/ingenic/rg350.dts` → `rg350m.dts` → `rg280m-v1.0.dts` → `rg280m-v1.1.dts` → `rg280v.dts` → `bits/gcw0-base.dtsi`（branch `jz-6.1`）https://github.com/OpenDingux/linux/tree/jz-6.1/arch/mips/boot/dts/ingenic | RG350 の基板（panel・button・memory・UART・MMC）。RG350 の DT は mainline に無い |
| [L-probe] | Linux `arch/mips/kernel/cpu-probe.c` の `cpu_probe_ingenic`、`arch/mips/include/asm/cpu.h`、`arch/mips/include/asm/mach-ingenic/cpu-feature-overrides.h`、`arch/mips/ingenic/Kconfig`（master） | CPU の癖 |
| [L-panel] | Linux `drivers/gpu/drm/panel/panel-simple.c` の `frida_frd350h54004` | panel の timing |
| [UBI] | UBIBoot（Paul Cercueil、GPL-2.0）https://github.com/pcercuei/UBIBoot （`config-gcw0.mk`・`src/config-gcw0.h`・`src/config.h`・`src/main.c`・`src/fat.c`・`src/uimage.c`・`src/board-gcw0.c`・`ldscripts/target-jz4760.ld`） | 起動の道 |
| [PM] | Ingenic JZ4770 Programming Manual | **未入手**。WebSearch（2026-10-19）で公開の所在が見つからなかった。register の bit の意味は Linux の driver を手順の参照として読む（code は写さない、§10） |

GPL の source（Linux・OpenDingux・UBIBoot）は、register の address・順序・timing の値の事実を読むだけにし、code・table・comment を写さない。実装の Phase も同じ規則（WS245 と同じ）。
「確」= 上の出典で読んだ、「未確認」= 読めていない・推測。

## 1. 事実の表（受け入れの条件 1）

### 1.1 CPU
- core: XBurst（XBurst1、MXU1.1）の 1 core。DT の compatible `ingenic,xburst-fpu1.0-mxu1.1`（確 [L-dtsi]）。
- ISA: JZ4770 の PRId の company は `PRID_COMP_INGENIC_D1`（0xd1、"JZ4770, JZ4775, X1000"）で、D0（JZ4760 まで）と違い **MIPS32r2 を本当に持つ**扱い（確 [L-probe]、Kconfig `MACH_JZ4770` が `SYS_HAS_CPU_MIPS32_R2`）。R2 の EBase・`ehb`・`synci`・`rdhwr` は使える見込み（実機の値は p002 で PRId・Config を画面に出して確かめる）。UBIBoot も `-mips32r2` で build（確 [UBI]）。
- **XBurst の癖**（確 [L-probe]）:
  1. **CP0 の Count/Compare が無い**（`c->options &= ~MIPS_CPU_COUNTER`、override `cpu_has_counter 0`）。timer は SoC の TCU/OST を使う（Kconfig `MIPS_EXTERNAL_TIMER`）。
  2. **I cache が virtually tagged**（`MIPS_CACHE_VTAG`）。
  3. D1 の core は reset の後 Ingenic 独自の huge page の TLB の mode（HPTLB）で、標準の refill と合わず無限の tlb miss になる。**CP0 register 5 sel 4（PageCtrl）に `0xa9000000` を書いて VTLB の mode に戻す**必要がある。
  4. Config2 が無いので Config3 は直接読む。Config7 の bit 4（BTB の loop の検出）は Linux が切る（性能の測定のため。必須ではない）。
- override（確 [L-probe] の cpu-feature-overrides.h）: 4K の cache、line 32 byte（I・D）、`cpu_has_dc_aliases 0`、`cpu_has_userlocal 0`、`cpu_has_watch 1`、`cpu_has_llsc 1`、64 bit なし、DSP なし。
- L2: Kconfig `MIPS_CPU_SCACHE` を選ぶので L2 あり（大きさは未確認、Config から読む）。
- TLB の entry 数: 未確認（Config1.MMU から読む、§3 の HAL は既に Config1 から求める）。
- FPU: 有り（compatible の `fpu1.0`）。**FR=1（64 bit の FPR）の可否は未確認**。WS271 の U3 は FPXX（FR=1）。XBurst1 の FPU が FR=1 を持たないと、hardfloat の userland の ABI に影響する（D3）。kernel は FIR（FP の CP1 reg 0）の F64 bit と、Status.FR が書けるかを起動時に見て画面に出す。
- endian: little（確 [L-probe] Kconfig `MACH_INGENIC` は `SYS_SUPPORTS_LITTLE_ENDIAN` だけ）。WS271 の mipsel の build がそのまま合う。
- 速度: PLL0 で 996 MHz の設定（UBIBoot の `CFG_CPU_SPEED 996000000`、外部の水晶 12 MHz `CFG_EXTAL`、確 [UBI]・[OD-dts] `&ext 12000000`）。

### 1.2 memory
- RG350 の RAM は 512 MiB（UBIBoot の variant `rg350` は DDR2 `W972GG6KB_25I` = 2 Gbit と読める。量は未確認、hwvariant と合わせ実機で確かめる）。
- 物理の配置: `0x00000000`〜`0x0fffffff`（256 MiB）と `0x30000000`〜`0x3fffffff`（256 MiB）の 2 つ（確 [OD-dts] gcw0-base.dtsi の `memory`、UBIBoot の `USES_HIGHMEM`）。
- **高い側（`0x30000000`〜）は KSEG0/1（物理 0〜512 MiB）に届かない**。WS271 の HAL は direct map を KSEG0 だけで作るので、まず低い 256 MiB だけを使う（malta と同じ上限で、HAL の page.c は変えずに済む）。高い側は D2。
- SoC の register は物理 `0x10000000`〜（KSEG1 `0xb0000000`〜）、boot ROM は `0x1fc00000`（確 [L-dtsi]）。

### 1.3 起動の道（boot ROM → UBIBoot → kernel）
- boot ROM（内蔵、`0x1fc00000` に 8 KiB）が内部の microSD（MSC0）から最初の段を読む。読む位置・大きさ・stage1 の扱い（UBIBoot の `STAGE1_ONLY`、ld の 8 KiB の窓 `0x80000000`）の詳細は未確認（[PM] 未入手）。
- **U-Boot は使わない**。OpenDingux の RG350 は UBIBoot（確 [UBI]、`config-gcw0.mk` の `VARIANTS … rg350`）。
- UBIBoot の kernel の読み込み（確 [UBI]）:
  1. MSC0（`MMC_ID 0`、内部の TF1）の MBR の第 1 partition（FAT）を読む。初期化に失敗した時だけ 2 番目の MMC を試す（`MMC_ID2`、どちらの slot かは未確認）。
  2. 根の directory で `UZIMAGE.BIN`、無ければ `VMLINUZ.BIN` を探す。**X の button を押しながら起動**すると `UZIMAGE.BAK` / `VMLINUZ.BAK` を読む（`alt_key_pressed` = Port E pin 28）。
  3. 物理 `0x00600000`（`LD_ADDR`、KSEG1 を通して uncached）へ読む。`UZIMAGE` は U-Boot の legacy の uImage の header（magic `0x27051956`、os Linux、arch MIPS、type kernel、**圧縮なし**）を見て、本体を header の `load`（KSEG1 に直して）へ memmove し、header の `ep` へ飛ぶ。`VMLINUZ` は読んだ所へ飛ぶ。
  4. 飛ぶ時の引数（o32）: `a0 = argc`、`a1 = argv`（`"linux" "root=…" "rootfstype=vfat" "rootwait" "logo…" "--" "hwvariant=rg350" …`）、`a2 = NULL`、`a3 = NULL`。DT は渡さない（Linux は built-in の DTB、Kconfig `BUILTIN_DTB`）。
  5. cache: 読み込みを KSEG1 で行い、D cache を汚さない（`main.c` の comment）。I cache の状態は未確認なので kernel の入口で I cache を全部無効にする。
- **方針（案、D1）**: OpenDingux の入った card の第 1 partition に zedBSD の kernel を `UZIMAGE.BAK` として置き、X を押しながら電源を入れた時だけ zedBSD を起動する。既存の OpenDingux を壊さず、X を押さなければ元の system が起動する。UBIBoot を書き換えない。

### 1.4 LCD
- LCDC: `ingenic,jz4770-lcd`、`0x13050000`（大きさ `0x130`、DT に "tbc"）、INTC 31（確 [L-dtsi]）。
- panel（RG350）: `frida,frd350h54004`、320×240、24 bit の並列 RGB（pin の group `lcd-24bit`、`bpc = 8`）、60 Hz の mode は pixel clock 6.0 MHz、hsync 320+44/+16/+20、vsync 240+2/+6/+2、hsync・vsync は負の極性（確 [OD-dts] rg350.dts・gcw0-base.dtsi、[L-panel]）。reset は GPIO PE2（active low）。panel-simple なので SPI の初期化の列は要らない（RG280V などの `abt,y030xx067a` は SPI を使うが RG350 の DT は `spi0_gpio` を disabled）。
- backlight: `pwm-backlight`（PWM1、GPIO PE1）。UBIBoot は PE1 を GPIO の出力で low にする（`PIN_BKLIGHT`、`light()`、`BKLIGHT_ON` は既定で無効、確 [UBI]）。→ **起動直後は backlight が消えている**見込み。最初の段は PE1 を GPIO の high にするだけ（PWM は後）。
- LCD の pixel clock は CGU（`0x10000000`）の LPCLK の分周、LCDC の DMA の descriptor で framebuffer を読ませる。register の bit と順序は [PM] 未入手のため Linux の `ingenic-drm` と CGU の driver（`drivers/clk/ingenic/jz4770-cgu.c`）を手順の参照として読む（p003 の最初の作業、出典の版を記録）。
- UBIBoot が LCD を設定していない（logo は kernel の引数で、bootloader は描かない）見込みなので、HAL が LCDC と CGU を一から設定する。

### 1.5 SD（MSC）
- MSC0 `0x10021000`（INTC 37）、MSC1 `0x10022000`（36）、MSC2 `0x10023000`（35）（確 [L-dtsi]）。
- RG350: MSC0 = 内部の TF1（4 bit、`non-removable`、pin group `mmc0-4bit-a`）、MSC1 = 外部の TF2（4 bit、card detect PB2、電源は regulator `mmc1_vcc`）（確 [OD-dts] gcw0-base.dtsi）。
- driver は kernel 側の WS（範囲外、§6）。

### 1.6 入力
- GPIO の button（確 [OD-dts]、すべて active low、Select だけ PD18 active high）: D-pad 上 PE21・下 PE25・左 PE23・右 PE24、A PE29、B PE20、X PE28、Y PE27、L1 PB20、R1 PE26、L2 PB20（rg280v の上書き、L1 と同じ pin と読める。未確認）、R2 PB11、Start PB21、Select PD18、Power PA30、Vol+ PB12、Vol− PB13、L3 PB14、R3 PB15。
- analog stick: SADC（`ingenic,jz4770-adc`、`0x10070000`、INTC 18）の touch の channel（XP・XN・YP・YN）を 2 本の stick に使う（確 [OD-dts] rg350m.dts）。
- GPIO（pinctrl）`0x10010000`、port A〜F、各 port の割り込みは INTC 17〜12（確 [L-dtsi]）。

### 1.7 UART
- UART0〜3: `0x10030000`〜`0x10033000`（INTC 5・4・3・2）（確 [L-dtsi]）。console は UART2、57600 8N1（確 [OD-dts] `stdout-path = "serial2:57600n8"`、UBIBoot `LOG_UART 2`・`LOG_BAUDRATE 57600`、pin は Port C の pin 28・30（`__gpio_as_func_mask(GPIOC, 0x50000000, 0)`））。
- **RG350 の基板で UART2 の pad が取り出せるかは未確認**（公開の基板の写真・情報を確かめられていない）。取り出せない前提で、早期の console は LCD（§2.3）。
- 16550 に近い形で、Ingenic 固有の enable の bit がある（Linux `8250_ingenic` の存在から推測、未確認）。

### 1.8 割り込み・timer
- INTC `0x10001000`（大きさ 0x40）、CPU の IP2 に繋がる（確 [L-dtsi] `interrupts = <2>`）。2 つの 32 bit の bank（番号 0〜63）と読める（register の形は未確認）。
- TCU `0x10002000`（INTC 27・26・25）、その中の OST（`ingenic,jz4770-ost`、TCU の offset `0xe0`、INTC 15）（確 [L-dtsi]）。OST の幅（32 か 64 bit）は未確認。
- timer の案: OST を free-running の counter（monotonic）に、TCU の channel の 1 つ（または OST の compare）を 1000 Hz の tick に。

## 2. WS271 の HAL との差（受け入れの条件 2）

WS271 の HAL は `src/hal/mips/`（共通）＋ `src/hal/mips/bsp-malta/`（board）で、board への要求は `src/hal/mips/bsp.h` の関数（`mips_board_boot`・`init`・`putc`・`describe`・`display_ready`・`count_frequency`・`memory_size`・`memory_total`・`ramdisk`・`kernel_handoff`・`irq_init`・`irq_register`・`interrupt`）。

### 2.1 BSP の分け方
```
src/hal/mips/bsp-jz4770/   SoC（JZ4770）: intc.c（INTC）、tcu.c（OST/TCU の timer）、cgu.c（clock、LPCLK）、
                           gpio.c、lcdc.c、uart.c（debug、pad が取れた時だけ）
src/hal/mips/bsp-jz4770/board-rg350.c   基板: memory の表、panel の timing と reset・backlight の pin、
                           UBIBoot の argv の解釈、button の表（kernel の入力の WS に渡す名前の値は後）
platform/mips32-rg350/     vmunix.mk・vmunix.ld・Makefile（uImage の header を付ける段を含む）
```
- bsp-malta の `cons.c`・`font.c` は board に依らない描画なので、bsp-jz4770 と共有する。置き場所を `src/hal/mips/` の共通の層へ移す（HAL の内部の file の移動、API は不変）。framebuffer の形（stride・bpp）は bsp が渡す。

### 2.2 共通の層の変更（HAL の内部、API は不変）
| 項 | malta（WS271） | RG350 | 変更 |
| --- | --- | --- | --- |
| timer | `clock.c` が CP0 Count/Compare、IP7 | Count が無い | **timer を bsp の関数に移す**（`mips_board_timer_init(hz)`・`mips_board_counter_read()`・tick の割り込みの ack）。malta の bsp は今の Count/Compare の code を持つ。`mips_board_count_frequency` は置き換える。 |
| PageCtrl | 無い | HPTLB を切る（§1.1） | `locore.S` か bsp の早い段で、PRId の company が 0xd1 の時に CP0 5 sel 4 へ `0xa9000000`。TLB の初期化の前。 |
| I cache | PIPT 扱い | virtually tagged | 命令の同期（exec・user の page を実行可能にする時）で、`synci` の代わりに I cache の全 index の無効化（`Index_Invalidate_I`）を使う（VTAG では hit の操作が VA の別の名で効かないため）。`cache.c` の分岐。 |
| cache の色 | 24Kf 1 KiB、無し | way = 16 KiB/4 way?（未確認）、Linux の override は `dc_aliases 0` | Config1 から求める今の code のままで判定。aliasing が出たら §3.5 の WS271 の規則がそのまま働く。 |
| 入口の引数 | YAMON 風（a3 = memory の大きさ） | UBIBoot（a2・a3 = NULL） | `mips_board_boot` の解釈を bsp ごとにする（今も bsp の関数なので共通の層は不変）。memory は bsp の固定の表（256 MiB、D2）。 |
| 割り込み | IP2 の 8259 | IP2 の INTC | bsp の `irq.c` を差し替えるだけ。 |
| 画面 | PCI の Bochs VGA、640×480 | LCDC、320×240、24 bit（32 bit の pixel で読ませる案） | bsp。console は 320×240 で 8×16 の font なら 40×15 文字。判定の文字列（§4）は 40 字に収まるよう行を分ける。 |
| EBase | 使う | R2 なので使える見込み | 不変（p002 で確かめる）。 |
| FPU | FR=1 | 未確認 | `fpu.c` が FIR を見て、FR=1 が無い時は FR=0 で動かし、FPXX の user だけを許す（D3）。 |

### 2.3 早期の console
- UART の pad が無い前提で、LCD が最初の出力。LCDC と CGU が動くまで何も見えないので、その前の段の確かめ方:
  1. **backlight の点灯**（PE1 を high）を `_start` の直後の C で行い、「kernel に入った」を目で見る印にする（LCDC が動かなくても画面が明るくなる）。
  2. LCDC が動いたら、画面全体を単色で塗る段（例: 赤 = HAL の init の前、緑 = console が描ける）を挟む。
  3. panic の時も画面に出す（malta と同じ console）。
- gdbstub は実機に無い。EJTAG の pad は未確認。UART の pad が見つかれば、`uart.c` を debug の出力に足す（D4）。

### 2.4 HAL の API の変更の要否
- **要らない**。`include/hal/hal.h`・`include/hal/arch/mips32.h`・`include/kern/boot.h` は変えない。上の変更はすべて `src/hal/mips/` の内部（2026-09-25 のユーザーの規則で承認なしで可）と platform・Makefile。
- `hal_get_arch_handoff` の `boot.command-line` は UBIBoot の argv を空白で繋いで渡す（既存の名前）。
- そのため `plan/ws273/proposed/` は作らない。高い側の memory（D2 の案 B）を選んだ時も、HAL の内部（KSEG2 か TLB の wired の窓）で閉じる見込み。

### 2.5 HAL の外
- `Makefile`・`tools/menuconfig.py` に `mips32-rg350`（arch `mips32`、board `rg350`、build `build/mips32-rg350`）。
- uImage の header（64 byte、big endian の field、CRC32 が 2 つ）を付ける小さな host の道具が要る。`mkimage`（U-Boot の tools、GPL）を host の package から使うか、自前の短い script（Python）か（D5）。UBIBoot は header の CRC を確かめない（`check_uimage` は magic・os・arch・type・comp だけ、確 [UBI]）。
- link address: malta と同じ `0x80100000` で UBIBoot（`0x80000000` の 8 KiB の窓、`LD_ADDR 0x00600000`）と重ならない見込み。ただし uImage の memmove は `0x00600000` から `load` へ本体を動かすので、**kernel の大きさが 5 MiB を超えると読み込みの領域と重なる**（memmove は重なりを扱うが、UBIBoot の stack・bss の位置は未確認）。p002 で kernel の大きさと合わせて確かめる。

## 3. 実装の Phase（受け入れの条件 3）

8 LW ≈ 2〜3 時間。実機の確認はユーザーが画面を目で見る（boot-test.sh は QEMU 用で、RG350 の emulator は無い。QEMU で確かめられるのは build・host 試験と、malta の回帰（共通の層を変えた時、T1）だけ）。

| Phase | 中身 | 依存 | LW | 確認 |
| --- | --- | --- | --- | --- |
| p002 | 共通の層の整理: timer を bsp の関数へ（malta の Count/Compare を bsp-malta に移す）、cons・font を共通へ、PageCtrl・VTAG の I cache の分岐、`fpu.c` の FIR の判定。malta の振る舞いは不変 | WS271 | 8 | build（warning 0）、host 試験、T1 の malta の回帰（mips32・mips64 の画面） |
| p003 | `bsp-jz4770` の最小: 入口（UBIBoot の argv）、backlight の点灯、`platform/mips32-rg350`、uImage の作成、PRId・Config・FIR を記録する段。まず「画面が明るくなる」まで | p002、D1・D5 | 6 | **実機 1**: `UZIMAGE.BAK` を置き X を押して起動、backlight が点く（ユーザーの目視） |
| p004 | CGU の LPCLK・LCDC・panel の reset、320×240 の framebuffer と console、単色の段 | p003 | 10 | **実機 2**: console の文字が読める。PRId・Config1・FIR の値を画面に出しユーザーが写真で返す |
| p005 | INTC・TCU/OST の timer、例外・TLB・space・FPU の self-test（WS271 §6.1 と同じ）、kernel の link、VFS の段まで | p004 | 10 | **実機 3**: `MIPS32 HAL READY …`・`PAGING PASS`・`TIMER TICK`・`VFS initialization failed` が画面に出る |
| p006 | 規約の全文の見直し | p002〜p005 | 4 | build、malta の回帰（T1） |

- WS の完了の条件の案: 1. 実機で kernel が起動し LCD に console（p004）、2. VFS の段まで（p005）。SD の root・login・button の入力は**この WS の範囲外**にし、次の WS（MSC の driver、GPIO の入力の driver、userland の mipsel）で行う案（D6）。
- 実機の試験の手順（p003 から）: ユーザーが card を PC に挿し、`UZIMAGE.BAK` を第 1 partition にコピーし、X を押しながら電源。戻すには X を押さずに起動するだけ。

## 4. 判定の文字列（320×240 で 40 字）
```
MIPS32 HAL READY DISPLAY LCDC
PAGING PASS TIMER TICK
MIPS32 CPU 1 ONLINE
VFS initialization failed
```
（WS271 の 1 行の文字列を 40 字に分けた。malta の文字列は変えない。）

## 5. ユーザーの判断（D1…）

| ID | 問い | 選択肢 | 推奨 |
| --- | --- | --- | --- |
| D1 | 起動の道 | A: OpenDingux の card に `UZIMAGE.BAK` を置き X を押して起動（UBIBoot を変えない）／B: 専用の microSD を作り UBIBoot と FAT の partition を書く／C: 自前の bootloader | **A**（壊さない、戻すのが楽。card の第 1 partition が FAT で UBIBoot が読むことは p003 の最初に実機で確かめる） |
| D2 | memory の範囲 | A: 低い 256 MiB だけ（WS271 と同じ）／B: 高い 256 MiB も（HAL の内部で TLB の窓か KSEG2） | **A**（この WS の到達点には十分。B は Future Work） |
| D3 | FPU の ABI（XBurst1 が FR=1 を持たない時） | A: kernel は FR=0 でも動き、userland の ABI は FPXX のまま（FPXX は FR=0 でも動く ABI）／B: RG350 だけ softfloat／C: FP32 の ABI | **A**（WS271 の U3 の FPXX は FR=0・FR=1 の両方で動くよう作られた ABI。p004 で FIR を見てから確定） |
| D4 | UART の pad | A: 探さない（LCD だけ）／B: ユーザーが基板を開けて pad を探す | **A**（開けるのは危険と手間。必要になったら B） |
| D5 | uImage の header の道具 | A: host の `mkimage`（u-boot-tools の package）／B: 自前の短い Python の script | **B**（依存を増やさず、header は 64 byte の固定の形。UBIBoot は CRC を見ないが正しく付ける） |
| D6 | WS273 の範囲 | A: LCD の console と VFS の段まで（SD・入力は次の WS）／B: SD の root と login まで | **A**（WS271 と同じ到達点。MSC・GPIO の入力・mipsel の userland は大きいので別の WS） |

## 6. 範囲外・後の WS
- MSC の driver と SD の root、GPIO の button と SADC の stick、音（I2S・codec）、USB（OTG・OHCI）、Vivante の GPU、PWM の backlight の明るさ、高い側の memory（D2 B）、cpufreq。
- mipsel の userland（zedBSD の Mips の triple、Q1・toolchain）。

## 7. 未確認の一覧（実機か資料で確かめる）
1. JZ4770 の Programming Manual の公開の所在（WebSearch 2026-10-19 で見つからず）。
2. boot ROM の読み込みの形、UBIBoot の stack・bss の位置、`MMC_ID2` の slot。
3. RG350 の RAM の量（512 MiB の見込み）と、出荷の card の partition の形。
4. PRId の実値、TLB の entry 数、L1・L2 の形、FIR.F64 と Status.FR。
5. OST の幅、INTC の register の形、LCDC・CGU の register の bit（Linux の driver を参照で読む）。
6. UART2 の pad の有無、EJTAG の pad。
7. L2 shoulder の pin（DT の読みで L1 と同じ PB20、誤りの可能性）。

## 8. 外部の code の規則（受け入れの条件 4）
- Linux・OpenDingux・UBIBoot は GPL-2.0。この設計は address・pin・timing の値と手順の順序の事実だけを書いた。実装でも code・table・comment を写さず、zedBSD の名前と構造で書く（Zlib の file）。参照した file と版（commit か branch）を phase.md に記録する。
