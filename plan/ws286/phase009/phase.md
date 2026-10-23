<!-- awesome-plan project=zedbsd record=ws286-p009 -->

# ws286-p009 規約の全文の見直し（conformance）

- 親: [ws286](../ws.md)、設計: [design.md](../design.md) 版 3 の §8 の p009 の行。規約: [plan/coding-style.md](../../coding-style.md) の全文。
- Queue: Q1 の割当（P2、2026-10-23、worktree `/home/awe/zedBSD-worktrees/p2-zybo-p009`、branch `agent/p2-zybo-p009`、基点 main 4536e2e6f、終わりに main de58b1dd9 を merge）。
- 範囲の決め（Q1、2026-10-23）: 書き方だけ、動作は変えない。P3 が WS289 p005 で変えている file（`src/drivers/ethernet/cadence-gem.c`・`zynq-ethernet.c`・`rtl8211e.c`・`src/kern/boot.c`・`zybo.c` の network の部分）は触らず、見つけた物は Q1 に一覧で渡す。WS289 の `zynq-sdhci.*`・`zynq-slcr.*` も WS289 の物なので範囲の外。
- Status: in-progress（見直しと直しは済み、Q1 の判定待ち）

## 対象（WS286 が足した・変えた C の source、基点 151b879de からの差分で数えた）

- HAL: `include/hal/arch/armv7.h`、`src/hal/armv7/*.c`・`*.h`（aeabi-divide・asm・bootinfo・cache・clock・cmain・debug・fdt・fpu・gic・int・irq・lib・page・percpu・selftest・smp・space・task と header）、`bsp-virt/*`、`bsp-zybo/*`（`.S` は C の規約の外）。
- kernel と driver: `src/kern/platform/armv7-virt.c`、`src/kern/platform/zybo.c`（WS286 の部分）、`src/drivers/serial/cdns-uart.c`・`pl011.c` と header、`src/drivers/platform/zybo/zynq-devcfg.c`・`zynq-devcfg-check.c`・`include/drivers/platform/zybo/zynq-devcfg.h`、`include/uapi/fpga.h`、`src/kern/devfs.c` の 1 行。
- 共有の file の WS286 の hunk だけ: `src/kern/elf.c`・`include/kern/elf.h`、`src/rtld/rtld.c`・`elf.h`、`userland/base/libc/posix.c`・`pthread.c`・`static-tls.c`、`src/libc/setjmp.c`・`include/libc/setjmp.h`、`include/libc/limits.h`、`include/uapi/stat.h`、`userland/base/ls/main.c`。
- userland: `userland/base/fpgaload/*`、`userland/base/fpgareg/main.c`。
- 試験: `plan/ws286/tests/abitest.c`・`aeabi-host-test.c`・`pte-host-test.c`・`devcfg-host-test.c`・`bitstream-host-test.c`。

## 方法

全文を基準に、機械で拾える規則は一時の script（scratchpad、commit しない）で全 file を走査し、拾った箇所と、拾えない規則（コメントの中身、段落の切り方、名前）は該当の関数を読んで判断した。走査した規則: for の初期化の宣言、禁止のコメントの文、`function_result`・`_local` の名前、複数行コメントの開き・閉じの行、呼び出しの結果の直の return、条件の中の関数呼び出し、入れ子の条件演算子、閉じ括弧の後の空行、static 関数の前方宣言、関数定義の引数の行の分け方、public・static 関数の前のコメント、コメント無しで始まる段落（if・return・文）、copyright の header。

## 直した物（56cfa8724、merge cb6718d53）

| 規則 | 箇所 |
| --- | --- |
| 閉じ括弧の後の空行と、その後の段落のコメント | `cache.c`、`cmain.c`（4）、`int.c`、`selftest.c`（2）、`smp.c`、`space.c`（3）、`bsp-zybo/board.c`、`fpgaload/main.c`（4）、`fpgareg/main.c`（9）、試験 5 file |
| 呼び出しの結果を直に return しない | `fdt.c` の `read_cells`・`read_number`（同じ値を返す。cells 0 の確かめを先に並べ替えただけ） |
| 条件の中で関数を呼ばない | `smp.c` の `hal_cpu_notify`（`hal_cpu_count()` を変数に）、`zynq-devcfg.c` の 2 つの poll の loop（`sched_ticks()` を `now` に）、`abitest.c`（`same_state`・`strcmp` を変数に） |
| 決定の中の unwind に空行を入れない | `gic.c` の `hal_irq_set_mode`（unlock と return の間の空行） |
| if・段落の前のコメント | `fdt.c`（blob の範囲の確かめ）、`irq.c`（slot の対の確かめ）、`zynq-devcfg.c`（PROG_B の 3 段、status の flag、窓の守り、IRQ の待ちの各段、buffer の所有、memset）、`fpgareg/main.c`（option の解析）、`fpgaload/main.c`（file の読み）、self-test の成功の行（`selftest.c` 3、`task.c` 1）、`abitest.c`（各 step） |

## 直さなかった物（判断）

- 段落の中の if（同じ段落のコメントが覆う既定値の代入と上書き、guard の続き、critical section の中の文）は、規約の「A purpose comment introduces the entire operation that follows it」と tree の慣習に合うので、そのままにした（走査の R13 で 61 → 判断で残したのは space・page・task・cons・lib などの段落の中の if）。
- `include/uapi/fpga.h` の `#define` の行末のコメントは複数行コメントでない（R4 の誤検出）。
- `src/libc/int64.c` は WS286 の前からの物（diff は無い。走査の誤検出）。
- `.S` の file は C の規約の外。

## 確認

| command | 結果 |
| --- | --- |
| `make ZEDBSD_CONFIG=config/ci/config-zybo.mk -j16 vmunix build/zybo/abi.img build/zybo/root.img` | rc 0、warning・error 0（merge の前と後の両方） |
| `make ZEDBSD_CONFIG=plan/ws286/tests/config-zybo-fpga.mk BUILD=build/zybo-fpga -j16 vmunix build/zybo-fpga/root.img`（先に `pl-build.sh`） | rc 0、warning 0（前と後）。vmunix.bin `4f864ac4…`、root.img `763a1449…` |
| `make ZEDBSD_CONFIG=config/ci/config-armv7-virt.mk -j16 vmunix …`（abi.img・root.img も merge の前に） | rc 0、warning 0（前と後） |
| `make ZEDBSD_CONFIG=config/ci/config-amd64.mk BUILD=build/p9-amd64 -j12 vmunix` | rc 0、warning 0（merge の前） |
| `make ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/p9-rpi4 -j12 vmunix` | rc 0、warning 0（merge の前） |
| host 試験 `devcfg-host-test`・`bitstream-host-test`（実の plio.bit）・`aeabi-host-test`（-m32）・`pte-host-test`（file の頭の command） | 4 つとも PASS |
| `git diff --check` | 出力なし |

- 実機（JTAG、2026-10-23 15:0x P2、U4、board lock を取って。merge の後の image）: `ARMV7 W^X PASS`・`ARMV7 CONTEXT PASS`・`ARMV7 HAL READY AEABI PASS ATOMIC PASS PAGING PASS TIMER TICK VFP PASS`、login、`uname -m` = `armv7`、`fpgaload plio.bit` → `DONE 1 INIT 1 ENABLED 1`、`fpgareg read 0` = `0x504c494f`、IRQ `irq count 1 status 0x00000002`、`wait 100` → timed out。動作が変わっていない事の確かめ（fdt の読み・self-test・driver・tool）。
- QEMU は流していない（書き方だけの変更、Q1 の確認の範囲は build と host 試験）。

## P3 の file で見つけた物（Q1 に渡す、直していない）

- 読むだけで走査した（main de58b1dd9 の時点。`zynq-ethernet.c`・`rtl8211e.c` はまだ main に無い）: `src/drivers/ethernet/cadence-gem.c` の 722 行（閉じ括弧の後に空行が無い）、1659・1676 行（空行の後の if にコメントが無い）。`src/kern/boot.c`・`zybo.c` の network の部分は P3 の変更が main に入っていないので見ていない。

## 残り

- Q1 の判定と merge。WS286 の全 Phase の後の WS の完了の判定は Q1。
