<!-- awesome-plan project=zedbsd record=ws261-p002 -->

# ws261-p002 boot.h の sun4u の構造体を除く

Status: cleared（2026-10-18 Q1 判定: boot.h から sun4u の構造体・定数を除き、loader → HAL は bootloader/sparcv9/handoff.h、HAL → kernel は共通の record と名前ごとの値 2 つ。stage1.bin・stage2.bin と handoff.o・stage2.o は前と byte で一致、HAL の cmain.o・boot.o は -Werror で作れ、作れない object の error の集合は前と同じ、host 試験 16 checks 0 failures、amd64・rpi4・pc98・pcat の vmunix warning 0（P3 の報告）。QEMU sun4u の起動は vmunix が前から build できないので WS267 へ移した（T1 への依頼の文面はこの phase.md）。）（旧: in-progress（2026-10-18 q1255-i01、P3。実装・build・host 試験済み、cbae237f7・9b32b9ec6。QEMU の起動の確かめは sun4u の vmunix の既存の build の壊れで未実施））

- 親: [WS261](../ws.md)。Queue: q1255-i01。
- 起点: 2026-10-18 ユーザー「WS261ですが、sparcv9も対象にしてほしいです。」
- 規約: [Guardrail](../../guardrail.md)「HAL と kernel・boot loader と HAL の受け渡し」（2026-10-18 ユーザー）。手本は p001（git の 5d05f60ef^ の plan/ws261/phase001/phase.md）。

## 洗い出し（2026-10-18 P3、`git grep`、plan を除く）

| 名前 | 使う所 |
| --- | --- |
| `KERN_HANDOFF_VERSION_SUN4U` | loader の `bootloader/sparcv9/handoff.c`（書く）、HAL の `src/hal/sparcv9/cmain.c`（検査）、kernel の `src/kern/entry.c`（受け付け）・`src/kern/platform/sun4u.c`（照合）・`src/kern/vfs.c`（log と区画の照合） |
| `KERN_SUN4U_HANDOFF_MAGIC`・`_VERSION`・`_MAX_MEMORY_RANGES`・`_BOOTPATH_SIZE` | loader の `handoff.c`、HAL の `cmain.c`、kernel の `platform/sun4u.c` |
| `struct kern_sun4u_boot_handoff`・`struct kern_sun4u_memory_range` | loader の `handoff.c`・`handoff.h`・`stage2.c`、HAL の `cmain.c`・`bsp.h`・`bsp-sun4u/boot.c`・`page.c`、kernel の `platform/sun4u.c`（HAL が渡した構造体を cast して IDE の port と vendor・device を読む） |

- kernel が sun4u の handoff から読むのは、IDE の primary の command・control の port（と vendor・device の照合）だけ。memory・tick・PCI・serial・bootpath は HAL だけが読む。
- 範囲外: x68k の `KERN_X68K_*`・`KERN_HANDOFF_VERSION_X68K`（Q1 の指示）。

## 置き換えの形

- boot loader → HAL の定義は loader の `bootloader/sparcv9/handoff.h` に置いた（ユーザー 2026-10-18「bootloader/にヘッダがあってハンドオフ構造体を定義する分には、OKにします。」）。`SUN4U_HANDOFF_COMMON_VERSION`（4、loader が共通の record に書く版）、`SUN4U_HANDOFF_EXTENSION_MAGIC`（"S4UH"）、`SUN4U_HANDOFF_EXTENSION_VERSION`（1）、`SUN4U_MAX_MEMORY_RANGES`（16）、`SUN4U_BOOTPATH_SIZE`（256）、`struct sun4u_memory_range`、`struct sun4u_boot_handoff`。値と配置は boot.h の物と同じ。builder の宣言 `sparcv9_handoff_build` も同じ header のまま。
- この header を include するのは loader（`handoff.c`・`stage2.c`）と HAL（`src/hal/sparcv9/bsp.h`）だけ。
- HAL → kernel: HAL（`src/hal/sparcv9/bsp-sun4u/boot.c`）は loader の record を写し、kernel には共通の record だけを、共通の版（`KERN_HANDOFF_VERSION_MULTIBOOT`）と共通の size（`sizeof(struct kern_boot_handoff)`、24）で渡す（`sun4u_kernel_handoff()`、`cmain.c` の `kernel_entry`）。区画の形（scheme SUN・slice 1・LBA 4096）などの共通の field は loader の値のまま。
- 名前ごとの値（HAL が持つ、各 `uint64_t`）:

| 名前 | 中身 | 無い時 |
| --- | --- | --- |
| `sun4u.cmd646.primary.command` | CMD646 の IDE の primary の channel の command block の I/O port の offset | 無い事は無い（HAL は CMD646（vendor 0x1095・device 0x0646）の handoff しか受けず、受けなければ cmain で止まる） |
| `sun4u.cmd646.primary.control` | 同じ channel の control block の I/O port の offset | 同上 |

  - `boot.command-line` は今まで通り sun4u では NULL（loader が渡さない）。
- kernel:
  - `entry.c`: 受け付けから sun4u の版を外した。
  - `platform/sun4u.c`: 版は共通の物だけ、size は共通の record 以上。IDE の port は上の 2 つの名前で受ける（無い、または 16 bit を超える値なら 0 を返す）。vendor・device の照合は HAL の検査（`cmain.c` の `handoff_valid`）が保証するので kernel からは外した。
  - `vfs.c`: sun4u の log（「vfs: boot BIOS=.. Sun slice=.. devices=..」、文は同じ）と区画の照合（slice の index が同じ）は、共通の版で scheme が SUN の時。scheme SUN を出すのは sun4u の HAL だけ（amd64 は MBR・GPT、pc98 は LBA・MBR、pcat・rpi4・riscv64 は MBR、x68k は X68K の版）。
- `include/kern/boot.h` から `KERN_HANDOFF_VERSION_SUN4U`・`KERN_SUN4U_*`・`struct kern_sun4u_memory_range`・`struct kern_sun4u_boot_handoff` を外した。

## 結果（2026-10-18）

- commit: cbae237f7（source）、9b32b9ec6（host 試験）。基点 86745dbc1（main ed2f1b265 を含む）。
- `git grep -nE 'kern_sun4u|KERN_SUN4U|VERSION_SUN4U' -- src include bootloader platform` は 0 件。`git grep -n 'bootloader/' -- src/kern src/drivers include` も 0 件。
- **既存の不具合（変更の前から）**: sun4u の vmunix は基点でも build できない。`src/hal/pmem-constraints.c` が無い（No rule）、HAL の `page.c` は `struct hal_pmem` の未定義、kernel の .c は `KERN_UAPI_NATIVE` が無く `#error`（ws035 p001 の baseline・q720 の記録と同じ）。sparcv9 はサポート外（Master のサポートの表）。
- build（worktree の中、`make -k ZEDBSD_CONFIG=plan/ws035/tests/config-sun4u.mk BUILD=build/ws261-sun4u{,-base} <target>`）:
  - boot loader: `boot/stage1.bin`・`boot/stage2.bin` と `boot/stage2/handoff.o`・`stage2.o` が変更の前（`build/ws261-sun4u-base`）と byte で一致（`cmp`）。名前の付け替えだけで、意図した差は無い。
  - HAL: `src/hal/sparcv9/cmain.o`・`bsp-sun4u/boot.o` は `-Werror` で作れる。`page.o` と kernel の object は既存の error で作れず、error の集合（行・桁を除いて 122 行）は変更の前と同じ。
  - kernel の sun4u の .c の型の確かめ: make の command に `-DKERN_UAPI_NATIVE -fsyntax-only` を足して sparc64 の gcc で手で compile。`platform/sun4u.c` は既存の `hal_cons_write` の暗黙の宣言の error だけ（基点と同じ）、`entry.c` は error・warning 0、`vfs.c` は既存の `swap.h` の error 6 種だけ（基点と同じ集合）。
  - amd64・rpi4・pc98・pcat（`config/ci/config-<x>.mk`、`build/ws261-<x>/vmunix`）: 全部 rc=0、warning 0、`vfs.c`・`entry.c` は各 1 回作り直された。「amd64 vmunix check: PASS」「arm64 vmunix check: PASS」「PC/AT vmunix contract: OK」、pc98 は `vmunix: 897948 bytes`。
- host 試験: `plan/ws261/tests/host-sun4u-handoff-test.c`（HAL の `bsp-sun4u/boot.c` を host の cc で、共通の record の版・size・field、HAL の写し、2 つの名前、前方一致・長い名前・NULL の拒否）: `cc -std=gnu11 -Wall -Wextra -Werror -DHAL_ARCH_SPARCV9 -DHAL_BOARD_SUN4U -DKERN_UAPI_NATIVE -Iinclude -Isrc -I. -Isrc/hal/sparcv9 plan/ws261/tests/host-sun4u-handoff-test.c src/hal/sparcv9/bsp-sun4u/boot.c` →「sun4u_handoff_host_test: 16 checks, 0 failures」。
- `git diff --check`: 差なし。
- 未実施: QEMU の sun4u の起動の確かめ。vmunix が既存の不具合で作れないので、今は image が作れず成立しない（plan/tools に sun4u の道具は無く、`platform/sparcv9/run.mk` の `make run` だけ）。boot loader は byte で一致するので、loader の振る舞いは変わっていない。

## T1 への依頼の文面（sun4u の vmunix の build が直った後に）

> WS261 p002（sun4u の boot handoff の置き換え）の起動の確かめ。image: main（cbae237f7 を含む）で `make ZEDBSD_CONFIG=plan/ws035/tests/config-sun4u.mk BUILD=build/<名前> disk-image`。QEMU: `qemu-system-sparc64 -machine sun4u -m 256 -drive file=<image>,format=raw,if=ide -boot c`（`platform/sparcv9/run.mk` と同じ）。合否: serial（`plan/tools/guest/serial.py`）で login prompt が出て login でき、`uname -a` が返る。止まったら gdbstub（`-S -gdb tcp::<port>`）で `kern_platform_init` に break して `hal_get_arch_handoff("sun4u.cmd646.primary.command")` の値（0 でない）と、`kernel_entry` の引数の `version`（3）・`boot_partition_scheme`（2）を見る。返し先: Q1。
> 前提: 今は sun4u の vmunix が既存の不具合（`src/hal/pmem-constraints.c` が無い など）で作れないので、この依頼は build が直るまで流せない。

## 残件

- sun4u の vmunix の build の既存の壊れ（範囲外、サポート外の platform）。直すかはユーザーの判断。
- 範囲外のまま: boot.h の x68k の「XXX: To be removed」の物（`KERN_X68K_*`・`KERN_HANDOFF_VERSION_X68K`）。
