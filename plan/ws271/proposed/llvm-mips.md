# 案: 共有の LLVM に Mips の target と zedBSD の mips の triple（WS271・WS272、Q1 が適用）

2026-10-19 P1（q1273）。**P1 は toolchain を変えていない**（AGENTS.md の toolchain の規則）。Q1 が `plan/tools/toolchain-lock.sh unlock` の間に適用する。
ユーザーの許可: 2026-10-19（クリック）toolchain「許可する」（LLVM に Mips の target、Q1 が行う）。

## 1. build の設定（必須、kernel の build に要るのはこれだけ）
`toolchain/llvm/llvm.mk` 361 行:
```
-		-DLLVM_TARGETS_TO_BUILD='PowerPC;AArch64;RISCV;X86' \
+		-DLLVM_TARGETS_TO_BUILD='PowerPC;AArch64;RISCV;X86;Mips' \
```
`toolchain/llvm/version.mk`: `ZEDBSD_LLVM_PATCH_LEVEL := zedbsd9` → `zedbsd10`（install の stamp を作り直すため）。
確認: `build/llvm/bin/clang --print-targets` に `mipsel`・`mips64el` が出る。
`clang --target=mipsel-unknown-elf -march=mips32r2 -mabi=32 -msoft-float -mno-abicalls -fno-pic -G 0 -c` と `ld.lld -m elf32ltsmip` で小さな object を link できる。

## 2. zedBSD の triple（userland が要る時、kernel の p002・p003 には要らない）
`toolchain/llvm/patches/0001-add-zedbsd-x86-target.patch` に足す（同じ形）:
- `clang/lib/Basic/Targets.cpp`: `case llvm::Triple::mips:`・`mipsel`・`mips64`・`mips64el` の OS の switch に
  `case llvm::Triple::ZedBSD: return std::make_unique<ZedBSDTargetInfo<MipsTargetInfo>>(Triple, Opts);`
- `clang/lib/Driver/ToolChains/ZedBSD.cpp` の linker の `-m`（313 行付近の if の列）:
  `mipsel` → `elf32ltsmip`、`mips64el` → `elf64ltsmip`（big endian の `mips`・`mips64` は `elf32btsmip`・`elf64btsmip`、作るのは後）。
- 同じ file で mips の時: `-mabi` の既定（mipsel = o32、mips64el = n64）、`-mno-abicalls` を既定にしない（user は PIC の abicalls、kernel は自分で付ける）。
- `llvm/unittests/TargetParser/TripleTest.cpp`: `mipsel-unknown-zedbsd`・`mips64el-unknown-zedbsd` の normalize と arch・OS の検査。
- `clang/test/Preprocessor/zedbsd.c`: `--target=mipsel-unknown-zedbsd` で `__ZEDBSD__` と `__mips__`・`_MIPSEL`。

## 3. softfloat の option（今回は作らない、場所だけ）
- compiler は `-msoft-float`・`-mhard-float -mfpxx` を flag で選ぶだけで、triple は同じ。sysroot（`toolchain/llvm/sysroot.mk`）を ABI ごとの directory に分ける変更が後で要る（Q1）。
- compiler-rt の builtins（soft の浮動小数点の helper `__adddf3` 等）が soft の userland に要る。LLVM の runtimes の build に mips の builtins を足すのは softfloat の WS で。
