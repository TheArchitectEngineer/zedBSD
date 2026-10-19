<!-- awesome-plan project=zedbsd record=ws270-p002 -->

# ws270-p002 riscv64 の desktop の build の不足（設計の判断に依らない物）

- 親: [WS270](../ws.md)、Status: in-progress（2026-10-19 P1、q1336 / q1336-i01）。設計（[design.md](../design.md) 版 1）の review の結果で Phase の名前が変わり得る。

## 範囲

設計の判断に依らない build の不足だけを直す。Noct（toolchain、`userland/base/noct/`・`build/NoctLang`）と `bin/noct` の規則は触らない（riscv64 の Noct は WS270 の外で Q1 が判断、design 版 2 §1.3）。libcurses の規則は platform の規則で toolchain ではない（q1338 で足した）。toolchain は変えない。QEMU は起動しない。

## 変更

1. `platform/riscv64/userland.mk`: arm64 の形に合わせ `DYNAMIC_SYSROOT`・`DYNAMIC_LINK_CFLAGS`・`DYNAMIC_LINK_LDFLAGS`・`DYNAMIC_ELF_MACHINE` を置いて `platform/common/userland-dynamic.mk` を include（libutil の規則は common 側に移り二重定義を避ける）。基本の program の規則から `DYNAMIC_PORTABLE_PROGRAMS` を除く。
2. `userland/packages/external.mk`: triple `riscv64-unknown-zedbsd`、LLVM の target `RISCV`、libutil.so の link。`userland/packages/tools/gen-cross-toolchain.sh`: riscv64 の wrapper に `-DKERN_USER_ABI_LP64 -mno-relax`（無いと package の compile が `unknown target triple` と struct stat の ABI で失敗）。
3. openssl: `Makefile` の表に `zedbsd-riscv64`、patch `0001-add-zedbsd-target.patch` に target `zedbsd-riscv64`（asm なし、`SIXTY_FOUR_BIT_LONG RC4_CHAR`、hunk を 52→58 行）。
4. `userland/desktop/wayland/main.c`: `kwl_cycles` は riscv64 で `clock_gettime(CLOCK_MONOTONIC)` の ns（rdtsc の扱いを止めた）。`userland/desktop/libbrowser/bind/environment.c`: navigator.platform の machine に `riscv64`。

## 確認（2026-10-19、P1 の worktree の build/）

- 試しの config: [tests/config-visionfive2-desktop.mk](../tests/config-visionfive2-desktop.mk)（visionfive2 の config に rpi4 の program の組から firmware・bluetooth・wifi を除いた物。powervr-firmware は license の受諾が要るので外した）。
- `make ZEDBSD_CONFIG=plan/ws270/tests/config-visionfive2-desktop.mk -k -j16 rootfs`: rc=2。残る失敗は **3 件**、どれも「規則が無い」:
  - `bin/keiland-preview`: static の program。riscv64 に static の program の規則が無い（static TLS が無い設計の判断に依る、設計で扱う）。
  - `lib/libcurses.a`・`bin/noct`: libcurses は platform の規則（版 1 の記録の「toolchain の範囲」は誤り。AGENTS.md の toolchain の定義に `userland/base/curses` は入らない）、noct は toolchain の範囲。→ q1338 で直した（下）。
  - 他は全部 build（`build/visionfive2/bin` 214 個、`dynamic/*.so` 23 個、package の zlib・openssl・openssh・curl を含む）。compile は `-Werror` で通った。
- 既存の platform: `make ZEDBSD_CONFIG=config/ci/config-amd64.mk -j16 rootfs` rc=0、`config-rpi4.mk` rc=0。自前の code の warning 0（出たのは openssh・openssl の upstream の deprecated などの既存の warning だけ）。
- 未実施: QEMU・実機（画面なしの起動は設計の後の Phase）。

## q1338（2026-10-19 P1、design-review-1 の M1・M2・Minor 11）

5. `platform/riscv64/userland.mk`: arm64 の形で static の object の規則（`$(BUILD)/user/%.o`、`-fno-pic`）、`lib/libcurses.a`（arm64 `vmunix.mk:344` の形）、static の program の規則（`RISCV64_USER_STATIC_COMMAND`、arm64 `ARM64_USER_STATIC_COMMAND` の形、`user.ld`・`--gc-sections`・`check-user-elf.py --machine riscv64`）と keiland-preview の float の object。
6. `platform/common/userland-dynamic.mk` の注釈に riscv64。
7. 試しの config から `noct` と、実行時に `/usr/bin/noct` を要る `emacs`（remacs、`REQUIRE = base/noct`）を外した。

確認（P1 の worktree の build/、QEMU なし）:
- `make ZEDBSD_CONFIG=plan/ws270/tests/config-visionfive2-desktop.mk -k -j16 rootfs`: **rc=0、残りの失敗 0**。`build/visionfive2/bin` 215 個、`dynamic/*.so` 23 個。`bin/keiland-preview` は `check-user-elf.py --machine riscv64` を通り、PT_TLS は大きさ 0（kernel は空の PT_TLS を無いと扱う、`src/kern/elf.c` の「Linkers may emit an empty PT_TLS」）。
- `config/ci/config-amd64.mk`・`config-rpi4.mk` の `rootfs`: どちらも rc=0、自前の code の warning 0（upstream の openssl・openssh の既存の warning と host の perl の locale だけ）。
- noct は build していない。QEMU・実機は未実施。

## 残り

- build の不足は無い（q1338）。riscv64 の Noct は WS270 の外（Q1）。gen-cross-toolchain.sh の wrapper の注釈（design 版 2 §1.1 の「残る注意」）を直すかは Q1 の判断。
