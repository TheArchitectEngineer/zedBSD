<!-- awesome-plan project=zedbsd record=ws073-p037 -->

# ws073-p037: libm の `sqrt`・`sqrtf` を amd64・arm64 の平方根の命令に（BUG-109）

Status: cleared（2026-09-29）
Disposition: normal
Parent: [WS073](../ws.md)
Bug: [BUG-109](../../bugs/BUG-109.md)
Queue: main が WS035 のサブエージェント（worktree `ws035-keiland`、branch `wt/ws035`）に割り当て（2026-09-29、`src/libc/math/` の変更の許可つき）。
Queue の ID は main が記録する。Phase の番号は main の最新（p035）と `wt/ws073` の p036 の次の p037（WS073 のエージェントと重なったら main が直す）

## 範囲と受け入れ

- libm の `sqrt`・`sqrtf`（`sqrtl` は要る範囲）を amd64 の `sqrtsd`・`sqrtss`、arm64 の `fsqrt` にする。IEEE 754 の平方根は正しく丸めるので、
  WS076 の「群 A は正しく丸める」の方針と同じ結果。
- 確認: WS076 の libm の試験（host・guest）の回帰、`-fno-builtin` の下での呼び出しの形、ws035-p129 の Kei の印の計測の前後（mark.c の元の呼び出しの
  形で）、boot test。aarch64 は build だけ。

## 実装（`src/libc/math/sqrt.c`）

- **使う条件**: `__x86_64__ && __SSE2__`、または `__aarch64__ && __ARM_FP`。amd64・arm64 の userland の libc.so と sysroot の compiler-rt（amd64・arm64）が
  該当。amd64・arm64 の kernel（`-mgeneral-regs-only`）、i386（pcat・pc98 の `-mno-sse2 -msoft-float`）、sparc64・m68k は今までどおり整数の
  平方根（`sqrt_software`、中身は変えていない）。cross-compile で確かめた: amd64 は `sqrtsd`・`sqrtss`、arm64 は `fsqrt d`・`fsqrt s`、
  `-mgeneral-regs-only` と i386 は命令なし。
- **特殊な値と errno**: NaN は `x + x`、負は `__libm_invalid()`（`EDOM` と `FE_INVALID`）、0 と +Inf は自身（sqrt）を今までどおり C で先に扱う。
- **FE_INEXACT**: libc の浮動小数点の例外は software（`src/libc/fenv.c`）なので、命令の不正確の flag は使えない。整数の平方根と同じく不正確な根で
  `FE_INEXACT` を立てる: double は根の 2 乗を `libm_two_product` で誤差なしに求め、radicand と等しいときだけ正確（2^-900 未満は 2^1000 倍・根は
  2^500 倍して underflow を避ける）。float は根の 2 乗が double に正確に入る（48 bit）ので double で比べる。MXCSR・FPSR の flag を clear して読む
  形も試したが、host で 1 回 22 ns（2 乗の検査は 12 ns）で遅く、やめた。
- **`-fno-builtin` の下での呼び出しの形**: userland は `-ffreestanding -fno-builtin` なので、呼ぶ側は今までどおり libc.so の `sqrt`・`sqrtf` への
  call（PLT）。header での inline 化はしない（`__builtin_sqrt` は `-fno-math-errno` でないと library の call に戻るので、関数の中の命令は `__asm__`
  で書いた）。`sqrtl` は amd64 では long double = double なので `sqrt` を呼び、同じく速くなる。arm64 の binary128 の `sqrtl` は変えていない
  （double への落として求める既存の制限、WS076）。

## 検証

**host**:
- `plan/tools/libm/host-test.sh`（WS076、20000 件の参照、全 93 関数）: PASS。変更前の sqrt.c と後の出力は**全行同じ**（sqrt・sqrtf は 0 ulp、special cases 0 failed）。
- `plan/ws073/tests/p037/run-sqrt-compare.sh`（新）: 変更前の sqrt.c（5e4476b9）を参照に、特殊な値・全 binade・正確な 2 乗・2^-520 の刻みの小さな根の
  2 乗（正確と不正確）・200 万の乱数の bit 列（double と float）で、**値の bit・errno・FE_INEXACT・FE_INVALID が全て同じ**。PASS。時間は
  400 万回で 0.057 s（参照 0.527 s）。

**QEMU（amd64、Venus の guest、worktree の graphical の login の image）**:
- WS076 の libm の試験を guest で（`libm-test` を image の `build/amd64/dynamic/libc.so` に link し、2000 件の参照で SSH から実行。
  `plan/tools/libm/guest-test.sh` と同じ compile・link で、image だけ Venus の image）: PASS（sqrt 2202 件・sqrtf 2000 件 0 ulp、special cases 0 failed）。
- guest の 1 回の時間（`sqrtf` の 200 万回、`/root/gbench`）: **347.5 → 220.5 ns**。`feraiseexcept(FE_INEXACT)` だけで **205〜215 ns**（下の「分かったこと」）。
- ws035-p129 の Kei の印（mark.c を p129 の前の形 = 標本ごとに 3 回の `sqrtf` に戻した probe の image）: `glyphs-mark` **1415 → 961 ms**。今の mark.c
  （p129）と合わせると 135〜156 → **104 ms**、compose の計 1704〜1742 → 1663 ms。
- 回帰: `zdesktop-p101.sh` PASS。boot test（`plan/tools/boot-test.sh build/p037-final.img`）PASS。

**aarch64（build だけ）**: `make ZEDBSD_CONFIG=config/ci/config-rpi4.mk BUILD=build/arm64 sysroot-arm64 build/arm64/dynamic/libc.so` 成功、warning 0。
libc.so の `sqrt`・`sqrtf` と sysroot の `libzedbsd-compiler-rt.o` に `fsqrt`。試験は未実施（実機・QEMU とも）。

規約: `plan/tools/style-check.py src/libc/math/sqrt.c plan/ws073/tests/p037/sqrt-compare.c` 0。build（amd64 の graphical の login の image）warning 0
（package と noct の既存の warning を除く。sysroot の compiler-rt が変わるので worktree の `build/amd64` の userland と package が再 build された。共有の
toolchain の tree には触れていない: log に `Permission denied`・`llvm-source` の変更なし）。

## 分かったこと（範囲外、main へ）

**libm の関数の多くの時間は `feraiseexcept` の TLS の参照**: `feraiseexcept` → `__libc_fenv_location`（`_Thread_local`、global-dynamic）→
`__tls_get_addr`（`src/rtld/rtld.c`）が**呼ぶたびに `thread_self` の syscall（`KERN_THREAD_SELF_GET_TLS`）で thread pointer を取る**ので、1 回約 205 ns。
不正確な結果を返す libm の関数（sqrt・sin・exp など殆ど全て）が毎回払う。`errno` など他の shared library の TLS も同じ道なら、TLS を使う全ての
library の関数が遅い。amd64 の `%fs:0`、arm64 の `TPIDR_EL0` で thread pointer を読めば syscall は要らない（rtld と kernel の TLS の約束の確認が要る）。
rtld・libc の pthread は WS073 の他の bug か別の WS（main の判断）。

## Resume point

2026-09-29: cleared。TLS の件は main へ（新しい bug として起票するか main の判断）。
