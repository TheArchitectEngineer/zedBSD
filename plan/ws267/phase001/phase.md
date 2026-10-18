<!-- awesome-plan project=zedbsd record=ws267-p001 -->

# ws267-p001 sun4u を再び build し、QEMU の画面で login まで

Status: cleared（2026-10-19 Q1 判定: sun4u の vmunix・boot loader・image が warning 0 で build、QEMU sun4u の VGA の画面の console、T1-717 で画面に login:、T1-718 で root で login して uname -m が sparcv9（b9fac7058、merge 9dcfcb368）、amd64 と新しい 4 つの port の回帰も T1-717 で PASS）（旧: in-progress（2026-10-19 q1258-i01、P2。実装・build・解析の起動まで済み。test-wait（T1 への依頼は下の文面、Q1 経由）））

- 親: [WS267](../ws.md)。Queue: q1258-i01。
- 起点: 2026-10-18 ユーザー「sun4uはqemuで動けばOKです。」、決定（クリック）「画面の console を作る」、sparcv9 は「直してよい」。

## 壊れの洗い出し（2026-10-19）

壊れは 2026-10-04 以降の HAL・kernel の変更に sparcv9 が追従していなかった物。

1. kernel の compile: sparcv9 だけ `-Iinclude/libc` で libc の header を読み、`KERN_UAPI_NATIVE` 無しで #error。他の port と同じく compiler の freestanding header と tree の include/ だけにし、GCC（生の ELF）には `-D__ZEDBSD__=1` を足した。kernel に libc の object を link していた（`ZEDBSD_LIBC_SOURCES`）のを外し kcrt・heap に。
2. HAL の API: `struct hal_pmem`（旧 pmem の API）、`hal_cons_*`、`hal_set_allocator`・`hal_malloc`、`hal_task_init`・`hal_task_user_context`、`hal_irq_get_affinity` の旧形、`src/hal/pmem-constraints.c`（無い）。今の hal.h に合わせて HAL を書き直した。
3. kernel の image の大きさ: .vfs_bss・.net_bss が link script に無く PROGBITS の data に入り、4 MiB の窓に収まらない。bss に入れ、窓を 12 MiB（物理 4〜16 MiB、handoff は 16 MiB の手前の 8 KiB）に広げた（loader・link script・検査の script・HAL の予約を揃えた）。
4. `sun4u-cmd646.c`: 以前の機械的な書き換えで構文が壊れていた（submit の for）。規約の形で書き直した。
5. `fat.c`: GCC だけが出す maybe-uninitialized（1 行の初期化）。
6. userland: 古い手書きの libc の source の一覧・`KERN_UAPI_NATIVE`。sysroot と同じ source（`ZEDBSD_SYSROOT_LIBC_SOURCES` と compiler runtime、binary128）を `build/.../lib/libc-sparcv9.a` にして static に link する形に。GCC 用に: stdint の定数の macro（GCC は suffix の macro を持たない）、wchar_t（生の ELF GCC は 16 bit、zedBSD は 32 bit の int、predefined の macro を上書き）、`format.c` の `L"(null)"` を wchar_t の配列に、`posix.c` の static での weak の NULL 比較（GCC の -Waddress）を外した。動的の userland（ld.so・libc.so・試験の .so）と POSIX-R* の試験の規則は使われていないので削除。root は static の base の program（network の program は除く）を UFS の root（slice d）に。
7. 起動の解析（gdbstub・QMP の screendump、`-d int`）で見つけて直した物:
   - `hal_pmem_alloc` の上限の計算の桁あふれ（RAM の始まりが 0 の時）。
   - 画面の色: QEMU の std VGA は big-endian の機械では framebuffer を big-endian で読む → 拡張 register で little-endian に。
   - fast の MMU miss は fault status を更新しない → miss は load 扱い（store は protection の trap で判る）。
   - `__syscall6`（crt0・syscall-sparcv9.S）が呼び出し元の frame の外（%sp+2231）に書いていた → 自分の window を持つ形に。
   - trap の中の task の切り替えで、割り込まれた側の normal の global register が失われていた → kernel・user の trap の入口で normal の global を frame・user context に保存し、C は normal の global で走らせ、出口で戻す。fork の入口も戻す。
   - user の register window の spill が copy-on-write の page に書いて protection の trap（0x6c）になると kernel の入口で save → spill の再帰で TL5 → 0x6c も 0x68 と同じく user の扱いに。

## 画面の console（ユーザーの決定）

- loader（handoff 版 2）が OpenFirmware の木から PCI memory の基点と QEMU std VGA（1234:1111）の BAR0（framebuffer）・BAR2（register）を渡す。
- HAL（`bsp-sun4u/framebuffer.c`）が Bochs DISPI で 640x400x32（80x25 の 8x16）に設定し、`console.c` の grid を描く（riscv64 virt と同じ形、font は HAL の複写）。`hal_putc` は serial と画面。
- kernel の `sun4u-console.c`（新規）: /dev/console を teletype として登録し、16550 を 10 ms ごとに読んで tty に入れる（HAL の名前 `sun4u.serial.port`）。

## 結果（2026-10-19、P2 の解析の起動、判定ではない）

- build（`make ZEDBSD_CONFIG=plan/ws267/tests/config-sun4u.mk BUILD=build/ws267 build/ws267/hdd-image.img`）: rc=0、warning・error 0。`bootloader`（SPARC V9 boot check: PASS）、`check-disk-image`（SPARC V9 image check: PASS、UFS OK）、vmunix の検査 PASS。
- amd64 の vmunix（`config/ci/config-amd64.mk`、fat.c の変更の確かめ）: rc=0、warning 0、amd64 vmunix check: PASS。
- QEMU sun4u（解析、QMP の screendump）: 画面に `login:`、serial で root/root と `uname -a` を打ち、画面に `zedBSD kei 1.0.0-beta2+g67c70fd ... sparcv9`（`build/ws267-login-uname.png`）。login の prompt までおよそ 140 秒（TCG）。
- 観察: 起動の log の時刻（`at 20 ms`）が実時間より遅い。tick は割り込みが閉じている間の分を詰めずに 1 回にまとめる作り（HAL の timer）のため。動作には影響を見ていない（未調査）。
- 未実施: T1 の判定の試験（下の文面）。

## T1 への依頼の文面（Q1 経由）

> WS267 p001（sun4u の login）。image: worktree p2 の branch agent/p2-q1258（f56728c05 以降）で `make ZEDBSD_CONFIG=plan/ws267/tests/config-sun4u.mk BUILD=build/sun4u disk-image`（`build/sun4u/hdd-image.img`）。試験: `BOOT_MODE=sun4u plan/tools/boot-test.sh build/sun4u/hdd-image.img`（新しい mode、既定の時間切れ 360 秒）。合否: 画面に `login:`（既定の pattern）。続けて `BOOT_THEN` で `type:root`、`type:root`、`type:uname -m`、`expect:sparcv9` が画面に出ること。返し先: Q1。

## 規約

- 新しく書いた C（HAL の全 file、`sun4u-cmd646.c`・`sun4u-console.c`、loader の handoff の追加分）は plan/coding-style.md の形で書いた。全文の見直しは p002。
- HAL の API（hal.h）は変えていない。bootloader/ の header（handoff.h）を include するのは HAL（bsp.h）と loader だけ。
- 出典: 値は block ごとに `Values referenced:`（UltraSPARC-IIi の manual、PC16550D、ATA/ATAPI-6、QEMU v10.0.0 の vga-pci.c・bochs-vbe.h、Linux 6.8.12 の bochs.c の unblank の値）。

## 残件

- p002 規約の全文の見直し。
- Q1 への注: 共有の libc の source（`include/libc/stdint.h`・`src/libc/format.c`・`userland/base/libc/posix.c`）は sysroot の入力なので、merge の後の amd64・arm64 の build で sysroot が作り直しになる（toolchain の lock の扱いは Q1）。clang の build での振る舞いは変わらない（stdint は suffix の macro が無い時だけ、他の 2 つは同じ動き）。
- network の program（net・networkd・wifi）は sun4u の root に入れていない（network の device を駆動しないため）。init は networkd の不在を log に出して先へ進む。
