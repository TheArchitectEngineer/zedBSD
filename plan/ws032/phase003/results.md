# ws032-p003 結果: クロスビルド契約（2026-09-21）

q315-i03。実装し、host 試験で受け入れた。

## 1. 成果物

- `userland/packages/tools/gen-cross-toolchain.sh`: 構成済みターゲット 1 つぶんの
  クロスビルド入口を生成する。出力は `build/<platform>/packages/toolchain/`:
  - `bin/zedbsd-clang`、`bin/zedbsd-clang++`: リンク契約を吸収する wrapper。
  - `bin/zedbsd-{ar,ranlib,nm,objcopy,objdump,readelf,strip,ld}`。
  - `zedbsd.cmake`（toolchain file）と `cmake/Platform/zedBSD.cmake`（共有ライブラリ・
    soname・rpath の扱い）。
  - `zedbsd.cache`（autoconf cross cache）、`environment.sh`（`CC`/`CXX`/… の定義）。
- `userland/packages/external.mk`: `packages-cross-toolchain` 目標と
  `ZEDBSD_EXTERNAL_CROSS_{DIR,ENV,STAMP}`。sysroot の完成を prerequisite にする。
- `plan/ws032/tests/run-cross-host-test.sh`: host 試験。

## 2. wrapper が補うもの（なぜ wrapper が要るか）

外部の configure / CMakeLists は `cc foo.c -o foo` が動く前提で書かれている。一方
ツリーの動的リンク行は `-nostdlib` と明示 `crt1.o`、`-l:libc.so`、`build/<plat>/dynamic`
上の共有ライブラリを使う。**crt1.o は利用者の入力より前、`-l:libc.so` は後**に置く
必要があるため、固定の前置引数（clang config file 等）では表せない。そこで wrapper が
引数を見てリンク段かどうかを判定し、前後に分けて補う。

補う内容（実測した既存契約と同一）:

| 状況 | 付ける引数 |
| --- | --- |
| 常に | `--target`、`--sysroot`、`-fPIC` |
| リンク（実行ファイル） | `-nostdlib -pie -Wl,--no-relax -Wl,--hash-style=sysv -Wl,-z,now -Wl,-z,relro -Wl,-z,separate-code -Wl,-z,stack-size=0x100000 -Wl,--allow-shlib-undefined -Wl,--dynamic-linker=/lib/ld.so`、先頭に `crt1.o`、末尾に `-l:libc.so`、`-L`/`-rpath-link` |
| リンク（`-shared`） | `-nostdlib -Wl,--hash-style=both -z now/relro/separate-code`、末尾に `-l:libc.so` |
| `-c` / `-E` / `-S` / `-fsyntax-only` / `-M` | 何も足さない（素通し） |

`--allow-shlib-undefined` が要るのは、`libc.so` が `__rtld_exports` を ld.so から受け取る
ためで、既存のツリーと同じ理由・同じ指定である。

## 3. 判明した事実（記録）

- **zedBSD の clang driver には linker job が無い**。`toolchain/llvm/patches/0001-*.patch` の
  `ZedBSD` toolchain は include path と file path しか持たないため、リンクは
  **host の driver（gcc）経由で host の `ld`** が行う。そのため wrapper から
  `-fuse-ld=lld` や `--ld-path=` を渡すと `gcc: unrecognized command-line option` で失敗する。
  これは本 Phase が作った性質ではなく、**ツリーの既存の動的実行ファイル（`vkdemo` 等）も
  同じ経路でリンクされている**。host の binutils が事実上のビルド要件になっている。
  driver へ linker job を実装すれば解消するが、`ZEDBSD_LLVM_PATCH_LEVEL` の更新＝
  toolchain cache 無効化と LLVM 全再ビルドを招くため、本WSの範囲外（外部設計 §4.3）。
- 最初の実装で `-fPIC` を付けておらず、`-pie` と組み合わさって
  `DT_TEXTREL in a PIE` を出した（`check-dynamic-elf.py` が text relocation を拒否して検出）。
  全段で `-fPIC` を既定にして解消。**ld.so は copy relocation を実装していない**ため、
  実行ファイルは PIE でなければならない。

## 4. 検証

`sh plan/ws032/tests/run-cross-host-test.sh`: **15 checks, 0 failures**。

- `cc hello.c -o hello`（1 段）、`-c` してから link（2 段）、`-shared`、共有ライブラリへの
  リンク、静的アーカイブへのリンク、`-c` 単独が再配置可能オブジェクトを出すこと。
- 生成した全 ELF を `tools/build/check-dynamic-elf.py` の役割別契約に通した
  （`application` / `shared-library`）。共有ライブラリは `SONAME` と
  `DT_NEEDED libc.so` を正しく持つ。
- **CMake**: 生成した toolchain file で configure → build が通り、
  共有ライブラリと実行ファイルの両方が契約検査を通過。p005/p008 の前提が立った。

## 5. 制限

- autoconf cross cache は現状 8 項目のみ。実際の configure が要求する項目は p006/p007 で
  実測して足す（当て推量で埋めない）。
- C++ は未検証。ターゲット用 C++ ランタイムが無いため（p005）。
- 試験は amd64 のみ。i386 は sysroot を作れば同じ経路で通るはずだが未確認。

## 6. 次

p006（OpenSSL）。configure が要求する cross cache 項目と libc の不足を実測し、
不足は p004 として閉じる。
