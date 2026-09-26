<!-- awesome-plan project=zedbsd record=ws036p026 -->

# ws036-p026: LLVM の zedbsd target に AArch64、aarch64 の sysroot、rpi4 の build を sysroot と driver に

Phase ID: `ws036-p026`
Parent: [WS036](../ws.md)
Status: **cleared**（2026-09-27、WS036 の subagent。Queue は main session が記録する）
Phase disposition: normal

## 範囲

p012 で分かった rpi4 の回避策を外す。

- LLVM の zedbsd target に AArch64 を足す: clang の target 情報（`__ZEDBSD__`・`__unix__`）と、driver の link の emulation（`aarch64elf`）。
- aarch64 の sysroot（`build/arm64/sysroot`）を x86 と同じ規則で作る。
- rpi4 の kernel・userland の `-DKERN_UAPI_NATIVE`・`-DKERN_KCRT_NATIVE` と、C library の header を tree から `-I` で読む指定を外す。
  application の link を clang の driver に（amd64 と同じ）。開発 file（`/usr/include`・`/usr/lib`）を rpi4 の root にも入れる。
- Noct・zedinst を rpi4 に入れる作業は大きさのために [p028](../phase028/phase.md) に分けた（2026-09-27）。

## 変更

| 所在 | 変更 |
| --- | --- |
| `toolchain/llvm/patches/0001-add-zedbsd-x86-target.patch` | `Targets.cpp` の aarch64 の OS 分岐に `ZedBSDTargetInfo<AArch64leTargetInfo>`。`ZedBSD.cpp` の linker が arch ごとに emulation を選ぶ（x86_64 `elf_x86_64`、aarch64 `aarch64elf`、他 `elf_i386`）。aarch64 では `-z max-page-size=4096`（zedBSD は 4 KiB page。ld.lld の aarch64 の既定は 64 KiB）。`TripleTest` と `clang/test/Preprocessor/zedbsd.c` に aarch64。file 名は x86 のまま（参照を増やさないため） |
| `toolchain/llvm/version.mk` | `ZEDBSD_LLVM_PATCH_LEVEL` を `zedbsd6` → `zedbsd7` |
| `toolchain/llvm/sysroot.mk` | macro を `ZEDBSD_BUILD_SYSROOT` に一般化（C library の assembly、compiler runtime の浮動小数の ABI flag と追加の source を引数に）。`sysroot-arm64`（`setjmp-aarch64.S`、binary128 の `softfloat128.c`・`compiler-runtime128.c`、`platform/arm64/user.ld`）。`sysroots` に arm64 |
| `Makefile` | arm64 に `ZEDBSD_TARGET_TRIPLE`（`aarch64-unknown-zedbsd`）と `ZEDBSD_TARGET_SYSROOT`。これで `CC` が sysroot 付きの driver になり、開発 file が root に入る。arm64 の `ASFLAGS` から `-m32` を外した |
| `platform/arm64/vmunix.mk` | kernel から `KERN_UAPI_NATIVE`・`KERN_KCRT_NATIVE`。static の試験 program は sysroot の `crt0.o`・`libc.o`（tree の libc を build dir で compile し直さない）。動的の object は `-isystem` の sysroot の header。application と `dyntest` の link を `$(CC)`（driver）に、`crt1.o` は sysroot から。未定義 symbol の検査の awk の引用の誤り（`'$$1 == \"U\"'` が awk の構文エラーになり、検査が常に通っていた）を直した |
| `userland/base/licenses/llvm-runtime/Makefile` | rpi4 にも LLVM の runtime の license（開発 file の `libclang_rt.builtins.a` を配る） |

合わせて `src/rtld/elf.h` の `PF_X`・`PF_W`・`PF_R`・`SHN_ABS` の綴りを `include/libc/elf.h` と同じにした（WS048 の報告どおり、現行の main では rpi4 の
`rtld.c` が `-Werror,-Wmacro-redefined` で止まっていた。amd64 は libc の header を `-isystem` で読むので警告されなかった）。

## 検証

| 試験 | 結果 |
| --- | --- |
| LLVM（worktree の `build/llvm`、zedbsd7）の build | 成功（`make llvm-toolchain llvm-native-tools`、約 11 分） |
| `clang --target=aarch64-unknown-zedbsd -E -dM` | `__ZEDBSD__ 1`、`__unix__ 1`、`__aarch64__ 1`、`__SIZEOF_LONG_DOUBLE__ 16`。x86_64 も `__ZEDBSD__` |
| LLVM の `TargetParserTests`（`TripleTest.ZedBSD`、aarch64 を足した）と `clang/test/Preprocessor/zedbsd.c` の 3 つの RUN（FileCheck を手で実行） | PASS |
| driver の link の行（`clang --target=aarch64-unknown-zedbsd -###`） | `-m aarch64elf`、`-z max-page-size=4096`、`-dynamic-linker /lib/ld.so`、`crt1.o`、`-lc`、`libclang_rt.builtins.a` |
| `make sysroot-arm64` | 成功。smoke の static link（AArch64、未定義 symbol 無し） |
| amd64 の sysroot の同一性 | 旧 LLVM（zedbsd6）で同じ source から作った sysroot と、`.comment`（clang の版の文字列）を除いて `libc.o`・`libzedbsd-compiler-rt.o`・`crt0.o`・`crt1.o`・header が同一 |
| rpi4 の `disk-image`（`config/ci/config-rpi4.mk`、新しい build dir） | warning 0、`arm64 vmunix check: PASS`、`Raspberry Pi 4 image check: PASS`。build の log に `KERN_UAPI_NATIVE`・`KERN_KCRT_NATIVE` は 0 件。application は AArch64 の PIE、LOAD の align 0x1000、interpreter `/lib/ld.so` |
| `POSIX-R1/R2/R2-REMAINING.ELF` と `dynamic-userland-check` | 成功（直した未定義 symbol の検査も通る。kernel.elf の未定義は weak だけ） |
| QEMU raspi4b の起動（`BOOT_MODE=raspi4b plan/tools/boot-test.sh`） | **PASS**（画面の login prompt、`build/boot-rpi4-p026/login.png`） |
| QEMU raspi4b のシリアル（`plan/ws044/tests/rpi4-serial.sh`） | login、`uname -a`（aarch64）、`id`、`df`、`/bin/dyntest` の全段（DL:01〜DL:06、終了状態 0）、`/usr/include` の 113 項目と `/usr/lib` の開発 file、tmpfs と SD の root への 1 MiB の書き込み |
| amd64 の回帰（外部 package を外した config、新しい LLVM と sysroot） | `disk-image` warning 0、`plan/tools/boot-test.sh` **PASS** |
| 実機（Raspberry Pi 4） | 未実施（ユーザー） |

## 引き継ぎと注意

- **toolchain の patch level が上がる**。merge した tree では `build/llvm`（zedbsd6）が受け入れられず、`make toolchain` が LLVM を source から
  build し直し、sysroot も作り直す。この worktree の `build/llvm`（zedbsd7）を写せば build は要らない。
- **GitHub Release `rev-0` の cache（`toolchain-cache`）は zedbsd6 のまま**。zedbsd7 の archive を upload して
  `ZEDBSD_LLVM_CACHE_SHA256` を更新するまで、`make toolchain-cache` は展開後の identity の不一致で止まる（CI も）。upload はユーザーの操作。
- 外部 package（`userland/packages/`）の aarch64 の cross build（`ZEDBSD_EXTERNAL_TRIPLE`）はまだ無い（WS044 の lldb の Phase で扱う）。
