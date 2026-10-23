<!-- awesome-plan project=zedbsd record=ws286-p001 -->

# ws286-p001 調べと設計（ARMv7-A の HAL・toolchain・ZYBO の BSP・driver）

- 親: [ws286](../ws.md)、Queue: Q1 の割当（P1、2026-10-23、worktree p1-zybo・branch agent/p1-zybo、基点 main 6206d397e）。
- Status: in-progress（版 3 を書いた。review の全部の指摘に対応。Q1 の判定（再 review の要否と clearance）待ち。U1〜U7 は 2026-10-23 に決定済み）。
- 成果物: [design.md](../design.md)（版 3）、review [review-20261023.md](review-20261023.md)、[proposed/hal-armv7.md](../proposed/hal-armv7.md)、[proposed/boot-test-armv7.md](../proposed/boot-test-armv7.md)。

## 目的と受け入れの条件

- ARMv7-A の HAL（入口・MMU・例外・GIC・timer・SMP・L2・VFP・atomic・TLS・ABI・signal）、toolchain/sysroot、起動の道と license、BSP と driver（UART・SLCR・SD・GEM+RTL8211E・USB・devcfg・reset）、試験、Phase と LW、risk、判断を design.md に書く。
- 受け入れ: Q1 の手配する review で致命の欠落が無く、U1・U3〜U8 の判断が付く（U2 は 2026-10-23 に解決: USB を外す）。
- 版 2 の範囲（2026-10-23 Q1 の割当、P1、worktree p1-zybo2・branch agent/p1-zybo2、基点 main 151b879de）: §12 の決定（OSS だけ、JTAG が先、JTAG で reset、SwitchBot 無し）と、到達目標を「JTAG で起動し PL の block を zedBSD から動かす」に絞る決定（USB を外す）を §4・§7〜§10・Phase の表に入れ、promise した 2 つの proposed を書く。

## 記録（2026-10-23 P1、版 1）

- 読んだ物: design §0 の表（tree の file と、Linux v6.12・U-Boot v2025.07・embeddedsw 2024.2・QEMU v10.0.11 の source。scratchpad に取り、tree には入れていない）。
- 実行した command: `git worktree add …/p1-zybo -b agent/p1-zybo main`、`grep`/`sed`、`qemu-system-arm -M help`・`-cpu help`、`curl` で source の取得。build・QEMU・実機は無し。
- 書けている節: §1（板と SoC の事実、clock、CPU1、QEMU の machine）、§2（toolchain: LLVM に ARM target が無い → Q1 の変更、sysroot の案）、§3（HAL の設計全部、hal.h は不変、arch.h/armv7.h の 3 点は事前承認の形）、§4（SD と U-Boot の起動 — **JTAG 先行の決定の前の版**）、§5（driver）、§6、§7（試験）、§8（Phase、**順の見直しが要る**）、§9（risk）、§10（判断）、§11。
- 未だの物: §12 の決定の反映（JTAG の起動を先に、OpenOCD の script の設計、SwitchBot 無し、OSS だけ）、`proposed/hal-armv7.md`・`proposed/boot-test-armv7.md`（p002 で書く予定だった）、review。
- HAL の API（hal.h）の変更: 不要。arch.h の節と arch/armv7.h の新規、handoff の名前 2 つ（design §3.13、U1）。
- toolchain の変更（Q1 の許可）: `LLVM_TARGETS_TO_BUILD` に `ARM`、patch level 11（p002 の前）。zedbsd の triple `armv7-unknown-zedbsd-eabihf` と sysroot（p008 の前）。
- 計画に無い依存: U-Boot SPL の build（GPL、tree の外、host の cross gcc）。OpenOCD の手順の確認（§12）。

## 記録（2026-10-23 P1、版 2）

- 読んだ物: 版 1 の design・ws・phase、AGENTS.md・Guardrail の HAL の表・Master の Queue の節、`include/hal/arch.h`・`arch/riscv32.h`・`aarch64.h`、`src/hal/riscv32/bootinfo.c`（handoff の名前）・`bsp-virt/`（ramfb の console）、`src/kern/platform/riscv-virt.c`・`visionfive2.c`（ramdisk）・`README.md`、`plan/tools/boot-test.sh`（mode の配線、445 行）・`guest/serial.py`、`include/uapi/syscall.h`、`toolchain/llvm/llvm.mk`・`version.mk`、`plan/ws271/proposed/`（案の形）。外: U-Boot v2025.07（`mach-zynq/spl.c`・`cpu.c`・`u-boot-spl.lds`・`hardware.h`、`common/spl/spl.c`・`spl_ram.c`・`Kconfig`、`arch/arm/Kconfig`、`xilinx_zynq_virt_defconfig`、`board/xilinx/zynq/Makefile`・`board.c`）、QEMU v10.0.11（`hw/arm/virt.c`・`boot.c`）、host の OpenOCD 0.12.0 の `target/zynq_7000.cfg`・`interface/ftdi/digilent-hs1.cfg`・`board/digilent_zedboard.cfg`。外の source は scratchpad に取り、tree に入れていない（SHA-256 は design §0）。
- 実行した command: `git worktree add /home/awe/zedBSD-worktrees/p1-zybo2 -b agent/p1-zybo2 main`、`curl`（source の取得）、`grep`/`sed`/`python3 -I`（文書の編集）、`openocd --version`・`openFPGALoader --help/--list-fpga/--list-boards`・`which`・`dpkg -l`・`build/llvm/bin/clang --print-targets`・`ls /dev/serial/by-id`。build・QEMU・板への操作は無し。
- 分かった事: (1) SPL は JTAG の boot mode で `BOOT_DEVICE_RAM` を選び RAM の中身へ飛び得る → `board_boot_order` の hardware breakpoint で止める、(2) SPL の text base は 0x0・最大 192 KiB、BSS は DDR の 0x100000、`ps7_post_config` は jump の直前だけ → zedBSD が PL の level shifter を開ける、(3) U-Boot 本体の OCM の再配置は SPL では走らない、(4) upstream の OpenOCD の Zynq の cfg に この板の PL の IDCODE 0x03722093 が無い → 自分の cfg、(5) 共有の LLVM に ARM が無い（確認）、(6) QEMU の arm の virt は ramfb を許す（PNG の判定ができる）、zynq には表示が無い。
- HAL の API（hal.h）の変更: 不要。事前承認の形の 3 点（U1、proposed/hal-armv7.md）。
- toolchain の変更（Q1 の許可）: LLVM に `ARM` と armv7 の zedBSD の triple を 1 回の作り直しで（design §10 の依頼 1）。sysroot.mk の armv7（p007 の前）。
- 計画に無い依存: openXC7 の導入（ユーザー、p003 の前）。openXC7 の PS7 の対応は未確認（R14）。
- 未実施: 敵対的 review は design §9 の自分の読み直しだけ（別の担当の review は Q1 の手配）。板の probe は p002。

## 記録（2026-10-23 P2、版 3）

- 割当: Q1、P2（設計）、worktree `/home/awe/zedBSD-worktrees/p2-zybo-d3`・branch `agent/p2-zybo-d3`、基点 main cee5b9421。途中で Q1 の指示により main 02a1b7837（p002 の実機の結果、design §4.8・[phase002](../phase002/phase.md)）を merge して反映した（merge 5bf4243d5、衝突無し、§4.8 は P1 の節のまま残した）。
- 読んだ物: design §0 の「版 3 で読んだ物」（tree の rtld・libc・uapi・elf.c・boot.c・riscv32/riscv64 の platform・sysroot.mk・clang の patch・mips の fpu/task/selftest・hal.h、外は review の取った QEMU v10.0.11・Linux v6.12 の file を同じ SHA-256 で読み直し、Linux v6.12 の `fsr-2level.c`・`entry-armv.S`・`entry-header.S`・`vfphw.S` を新しく scratchpad `refs3/` に取った）、p002 の phase.md。
- 実行した command: `git worktree add`、`git merge main`、`grep`/`sed`/`python3 -I`（文書の編集）、`curl`（Linux の 4 file）、`sha256sum`。build・QEMU・板への操作は無し。plan/tools/ と phase002/ は変えていない。
- HAL の API（hal.h）の追加の変更: 無し（U1 の 3 点のまま、proposed/hal-armv7.md に状態を書いた）。
- toolchain（Q1 の許可）: 版 2 と同じ（LLVM の ARM と triple を 1 回で、cache asset の upload はユーザーの指示が要る）。版 3 で sysroot.mk の ARMV7 の builtin の manifest の中身を決めた（design §2.3）。
- 計画に無い依存として見つかった物: rtld の ARM 版と libc の arch の file（H4）→ p007 の範囲に入れ、login は新しい p010 に分けた（ws.md の表）。

## 版 3: review の指摘と対応（[review-20261023.md](review-20261023.md)）

| # | 指摘（要約） | 対応（design 版 3 の所） | 状態 |
| --- | --- | --- | --- |
| C1 | virt の RAM は `0x40000000` から。direct map と LMA が virt で成り立たない | platform ごとの `ARMV7_PHYS_BASE`（zybo 0、virt `0x40000000`）と `ARMV7_KERNEL_PHYS`（zybo `0x00100000`、virt `0x40010000`）、VMA = `0x80000000 + (KERNEL_PHYS - PHYS_BASE)`、`hal_pmem_to_kernel` を `PHYS_BASE` 起点に、一時の identity の section、pte の host 試験を 2 つの base で（§2.2・§3.2・§3.12・§7.1） | 対応済み |
| C2 | QEMU は ELF の kernel に `r2` も initrd も渡さない | virt は raw の `vmunix.bin` を QEMU の Linux の約束で入れる（`0x40010000`、`r2` = DTB、`/chosen` の initrd・bootargs、`boot.c` を読んで確かめた）。JTAG と同じ `r2` の約束。BSS の重なりの確かめ、p004 の gdb の `r2` の確かめ。boot-test の案を版 3 に（raw、`gic-version=2`）。QEMU zynq は ELF + `0x01F00000` のまま（理由: 置き場所と Non-secure、§4.6）（§1.3・§2.2・§4.1・§4.6・proposed/boot-test-armv7.md） | 対応済み |
| H1 | QEMU zynq も PL310・MIDR が実機と同じ。判別が逆 | 判別は `PSS_IDCODE` = 0 → QEMU（QEMU は reset の値無し、実機は p002 の実測 `0x13722093`）。PL310 の記述を訂正、`bsp_l2_present` を外し `bsp_is_emulated` に。timer の速さを QEMU と実機の両方で host の時計で測る（§1.4・§3.6・§3.11・§3.14・§7.3・§7.4） | 対応済み（QEMU の 0 は p005 の最初の T1 で実測） |
| H2 | 短い descriptor に software の状態の置き場所が無い | Linux の arm と同じ影の table（1 page = 影 2 + hardware 2、`cpu_v7_set_pte_ext` の +2048）。遷移の 3 つと `prot_query` の順、host 試験の遷移、TEX remap を採らない理由（§3.2・§3.3・§7.1・§7.6） | 対応済み |
| H3 | 1 CPU でも ACTLR.SMP と SCU が要る | zybo の boot で SCU を RMW で有効化（p002 の実測で SPL の後も無効）、ACTLR.SMP|FW、TTBR の S/NOS/WBWA、pte の clean 無し（Linux の SMP の kernel の形）。virt は触らない。`ATOMIC PASS` と ACTLR・SCU_CTRL・ID_MMFR3 の表示（§3.2・§3.5・§3.14・§7.6） | 対応済み（ACTLR の判定は実機だけ） |
| H4 | rtld・libc の arch の file・TLS variant I が範囲と見積もりに無い。`uname -m` が `unknown` | userland は riscv64 と同じ動的 PIE（static は kernel の `load_static_tls` が ARM に無いので不可、根拠を書いた）。rtld の ARM 版（REL、`R_ARM_*`、variant I の動的 TLS、entry）、`syscall-armv7.S`・`setjmp-armv7.S`・static-tls.c・posix.c（`armv7`）・pthread.c、userland.mk。p007 を 20 LW の runtime に、login を p010（8 LW）に分けた（§2.3・§8・§11） | 対応済み |
| H5 | kernel の build に `-DKERN_USER_ABI_ARMV7` が無い | vmunix.mk の CPPFLAGS に足し、stat.h に `HAL_ARCH_ARMV7` で macro が無ければ `#error` の 1 行。読み直しで「黙って食い違う」より先に既存の `_Static_assert` で落ちる見込みと分かった事も書いた（§2.2） | 対応済み |
| M1 | 32 bit の除算と `__aeabi_mem*`（引数の順）が無い | symbol の表（引数と戻り、memset の順）、kernel は自分の C と aeabi.S、user は compiler-rt の `arm/*` を manifest に、host 試験と target の `AEABI PASS`、「p009」の誤記を直した（§2.2・§2.3・§7.6） | 対応済み（最終の list は p004・p007 の `llvm-nm -u`） |
| M2 | virt の PSCI は HVC | DTB の `/psci` の method で選ぶ（§1.3・§3.7・§3.13） | 対応済み |
| M3 | `hal_cpu_idle` の不可分性 | mask のまま `dsb; wfi` → `cpsie i; isb` → `cpsid i`（§3.6） | 対応済み |
| M4 | devcfg の手順の抜け | DMA の前後で INT_STS を消す、1 回の転送で queue を超えない、8 byte 揃え・4 の倍数、PCFG_DONE は INT_STS bit 2、CTRL の helper と mask の host 試験、UNLOCK の定数、attach で INT_STS を消さない（§5.6） | 対応済み |
| M5 | QEMU の devcfg は PCFG_INIT/DONE を立てない | QEMU で確かめる範囲を DMA と `D_P_DONE` までに直し、`fpga.no-pl-wait=1` と `bsp_is_emulated` の二重の条件で待ちを飛ばす（`EMULATED`）（§5.6・§5.6.1・§7.3） | 対応済み |
| M6 | lazy VFP の sync・forget の欠落と試験の穴 | mips の呼び出しの所に合わせた sync・forget の表、Undefined は「EN=0 ならやり直し、EN=1 なら SIGILL」、`VFP PASS`（self-test）と `abitest` の VFP・signal・exec の項目（§3.10・§7.2・§7.6） | 対応済み |
| M7 | 排他の monitor を閉じる | 例外の戻りの全部で dummy の `strex`（Linux `entry-header.S` の形、A15 erratum 830321 の理由も）、user への戻りの spsr の確かめ（§3.3） | 対応済み |
| M8 | Cortex-A9 r3p0 の erratum 754322・775420 | 754322: 切り替えの 2 つの `dsb`。775420: WS286 は fault し得る maintenance をしないので踏まない、cacheflush の WS の規則として残す。SMP の WS の 764369（§3.3・§3.5・§3.11） | 対応済み |
| M9 | 最初の 1 MiB と async abort | zybo は PA 0〜0xFFFFF を RAM にも direct map にもしない（p002 の実測で穴と OCM の alias を確認）、`OCM_CFG` を変える案を比べて採らない理由、VBAR の後の `cpsie a` と保留の 1 回の扱い（§3.2・§3.3・§3.12） | 対応済み |
| M10 | SwitchBot の記録の食い違い | プラグ AC276E42B252 は 2026-10-23 時点で ZYBO に未接続（ユーザー D2）と ws.md・design §3.13・§7.4 に 1 つの事実として書き、ZYBO のためにどの道具も電源を切らないと明記（VF2 の道具の注は Q1 の範囲） | 対応済み（VF2 側の注は Q1） |
| M11 | Phase の依存・受け入れ・見積もり・LLVM の upload | U3 を p005〜p010・p008 の依存に、p006 の受け入れに `VFP PASS` を足し user mode の受け入れを p007 の `abitest` に移すと明記、p007/p010 の見積もり、LLVM の cache asset の upload はユーザーの指示が要ると §10 の依頼 1 に、boot-test の版 2 の案は適用しない（§7・§8・§10） | 対応済み |
| L1 | QEMU zynq の細部（GP0 は 0、UART の TX 無効、reset が no-op、`-dtb`） | §5.6（GP0）、§5.1（UART の初期化、SPL と同じ値）、§3.13（`ZYBO RESET FAILED` で止まる）、§1.3・§4.6（`-dtb` は効くが使わない理由） | 対応済み |
| L2 | 例外の符号の表 | Linux `fsr-2level.c` に合わせて表を直した（domain・walk の external・icache maintenance・CM・`bkpt` は Prefetch Abort）（§3.3） | 対応済み |
| L3 | 例外時の stack の揃え | Linux の `svc_entry` の SPFIX の形、20 word の frame と pad の印（§3.3） | 対応済み |
| L4 | OpenOCD の細部 | `adapter serial`（p002 の実装）、IDCODE の訂正、GP0 は AHB-AP で、p003 の順、kill の後の `jtag-reset.sh`（§4.1・§4.3・§7.5） | 対応済み（p002 の道具で実装済みの所は §4.8） |
| L5 | jtag-boot の入口と配置 | p002 の `jtag-boot.sh` が実装済み（raw/ELF の入口、範囲の確かめ）。raw の時の `_end` の確かめを p005 に、DDR の照合を残す（§4.1） | 対応済み |
| L6 | toolchain の文面 | 時間を 30〜60 分に揃えた、`-mtp=cp15` の置き場所の注、TTBR の IRGN の bit（§2.4・§3.2） | 対応済み |
| L7 | `FPGA_IRQ_WAIT` の意味 | 「前の呼び出しから保留の数」、unmask は呼び出しで、待ち手は 1 つ（2 つ目は `EBUSY`）（§5.6.1・§5.6.2・§7.7） | 対応済み |

- 未確認のまま残る事（推測と書いた所）: QEMU の `PSS_IDCODE` = 0 の実測、QEMU の A9 の ACTLR の書き込みの振る舞い、NSACR を QEMU の virt の firmware の真似が許す事、LLVM の `__aeabi_mem*` の libcall の名前と ARM の TLS の dialect、lld の ARM が TLS を relax しない事、devcfg の `CTRL` の危険な bit の位置（UG585）、JTAG の書き込みで INT_STS の PCFG_DONE が立つか、AHB-AP の sticky error の戻し方。どれも該当の Phase の最初の確かめに置いた。
