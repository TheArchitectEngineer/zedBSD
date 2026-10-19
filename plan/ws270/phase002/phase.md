<!-- awesome-plan project=zedbsd record=ws270-p002 -->

# ws270-p002 riscv64 の desktop の build の不足（設計の判断に依らない物）

- 親: [WS270](../ws.md)、Status: in-progress（2026-10-19 P1、q1336 / q1336-i01）。設計（[design.md](../design.md) 版 1）の review の結果で Phase の名前が変わり得る。

## 範囲

設計の判断に依らない build の不足だけを直す。Noct（toolchain、`userland/base/noct/`・`build/NoctLang`）、libcurses・`bin/noct` の規則は触らない。toolchain は変えない。QEMU は起動しない。

## 変更

1. `platform/riscv64/userland.mk`: arm64 の形に合わせ `DYNAMIC_SYSROOT`・`DYNAMIC_LINK_CFLAGS`・`DYNAMIC_LINK_LDFLAGS`・`DYNAMIC_ELF_MACHINE` を置いて `platform/common/userland-dynamic.mk` を include（libutil の規則は common 側に移り二重定義を避ける）。基本の program の規則から `DYNAMIC_PORTABLE_PROGRAMS` を除く。
2. `userland/packages/external.mk`: triple `riscv64-unknown-zedbsd`、LLVM の target `RISCV`、libutil.so の link。`userland/packages/tools/gen-cross-toolchain.sh`: riscv64 の wrapper に `-DKERN_USER_ABI_LP64 -mno-relax`（無いと package の compile が `unknown target triple` と struct stat の ABI で失敗）。
3. openssl: `Makefile` の表に `zedbsd-riscv64`、patch `0001-add-zedbsd-target.patch` に target `zedbsd-riscv64`（asm なし、`SIXTY_FOUR_BIT_LONG RC4_CHAR`、hunk を 52→58 行）。
4. `userland/desktop/wayland/main.c`: `kwl_cycles` は riscv64 で `clock_gettime(CLOCK_MONOTONIC)` の ns（rdtsc の扱いを止めた）。`userland/desktop/libbrowser/bind/environment.c`: navigator.platform の machine に `riscv64`。

## 確認（2026-10-19、P1 の worktree の build/）

- 試しの config: [tests/config-visionfive2-desktop.mk](../tests/config-visionfive2-desktop.mk)（visionfive2 の config に rpi4 の program の組から firmware・bluetooth・wifi を除いた物。powervr-firmware は license の受諾が要るので外した）。
- `make ZEDBSD_CONFIG=plan/ws270/tests/config-visionfive2-desktop.mk -k -j16 rootfs`: rc=2。残る失敗は **3 件**、どれも「規則が無い」:
  - `bin/keiland-preview`: static の program。riscv64 に static の program の規則が無い（static TLS が無い設計の判断に依る、設計で扱う）。
  - `lib/libcurses.a`・`bin/noct`: toolchain の範囲（触らない）。
  - 他は全部 build（`build/visionfive2/bin` 214 個、`dynamic/*.so` 23 個、package の zlib・openssl・openssh・curl を含む）。compile は `-Werror` で通った。
- 既存の platform: `make ZEDBSD_CONFIG=config/ci/config-amd64.mk -j16 rootfs` rc=0、`config-rpi4.mk` rc=0。自前の code の warning 0（出たのは openssh・openssl の upstream の deprecated などの既存の warning だけ）。
- 未実施: QEMU・実機（画面なしの起動は設計の後の Phase）。

## 残り

- keiland-preview の static の規則（または riscv64 で dynamic にする判断）、libcurses・noct の riscv64 の規則（toolchain、main の許可が要る）。
