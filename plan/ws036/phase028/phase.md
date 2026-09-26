<!-- awesome-plan project=zedbsd record=ws036p028 -->

# ws036-p028: Noct（と zedinst）を rpi4 に

Phase ID: `ws036-p028`
Parent: [WS036](../ws.md)
Status: **cleared**（2026-09-27、WS036 の subagent。zedinst は据え置き、下の判断）
Phase disposition: normal

## 範囲

p026 から分けた。aarch64 の sysroot ができたので、Noct（`/bin/noct`）を rpi4 で build できるようにする。zedinst は Noct の script。

## 変更

| 所在 | 変更 |
| --- | --- |
| `userland/base/noct/zedbsd-arm64.cmake`（新規） | Noct の aarch64 の cross build の toolchain file（i386 のものと同じ形。`aarch64-unknown-zedbsd`、`-march=armv8-a -mno-outline-atomics`、`HAL_ARCH_ARM64`・`KERN_USER_ABI_AARCH64`・`KERN_USER_ABI_LP64`） |
| `userland/base/noct/zedbsd.cmake` | aarch64 の分岐（link の emulation `aarch64elf`） |
| `userland/base/noct/Makefile` | `NOCT_ZEDBSD_ARCH` に arm64、sysroot の選択、arch ごとの toolchain file（amd64 は Noct の release の中のもの）を build の依存に。platform に rpi4 |
| `toolchain/llvm/sysroot.mk` | aarch64 の `libclang_rt.builtins.a` に `clear_cache.c`（`__clear_cache`）。Noct の JIT は書いたコードを実行する前に呼ぶ。EL0 の cache の保守は kernel が許している（WS044 p009 の `SCTLR_EL1.UCI`・`UCT`） |

JIT は有効（aarch64 の JIT の backend `jit-arm64.c` を使う。i386 は従来どおり無効）。

## 判断が要る点（可逆な既定を選んだ）

- **Noct の既定の選択**: 他の platform と同じく既定では選ばない（menu で選ぶ）。amd64 の CI の config は明示で選んでいる。
  rpi4 の CI の config（menu が生成する file）は program の一覧を持たないので変えていない。rpi4 の image に既定で入れるかはユーザーの判断。
- **zedinst は rpi4 に入れない**: zedinst は PC/AT・PC-98・amd64 の disk の配置に install する。rpi4 の SD の配置（FAT32 の boot＋UFS の root）を知らないので、
  入れても使えない。installer が SD の配置に対応するまで据え置く（Future Work の候補）。

## 検証

| 試験 | 結果 |
| --- | --- |
| rpi4 の Noct（`make build/<dir>/bin/noct`） | 成功。AArch64 の PIE、`/lib/ld.so`、JIT の symbol（`jit_build`・`__clear_cache`） |
| Noct を選んだ rpi4 の `disk-image`（既定の一覧＋`noct` の config） | 成功、image の検査 PASS |
| QEMU raspi4b の起動（`boot-test.sh`） | PASS（`build/boot-rpi4-p028/login.png`） |
| guest で Noct（`plan/ws036/tests/noct-rpi4.sh`） | JIT（`-j`）と interpreter だけ（`-j0`）が同じ結果（`fib=46368 sum=599994`、host の Noct と同じ）。File・Process・System の API（`file-ok process-ok shell=0`） |
| 既定の rpi4 の `disk-image`（Noct 無し） | 成功、warning 0 |
| 回帰: amd64・pcat の Noct の build | 成功（pcat は従来どおり `zedbsd-i386.cmake` と JIT 無効） |
| 実機 | 未実施（ユーザー） |

Noct の source（外部）の `interpreter.c` に clang の警告が 1 件ある（amd64 でも出る既存のもの。この repository の source ではない）。
