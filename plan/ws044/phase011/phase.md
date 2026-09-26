<!-- awesome-plan project=zedbsd record=ws044p011 -->

# ws044-p011: 外部 package の aarch64 の cross build（基盤と libcxx）

Phase ID: `ws044-p011`
Parent: [WS044](../ws.md)
Status: **cleared**（2026-09-27、WS036/WS044 の subagent）
Phase disposition: normal

## 範囲

lldb（p003）は clang の package の一部で、C++ の runtime（libcxx の package）の上に build する。どちらも amd64・i386 の
cross build しか知らなかった。この Phase は aarch64 の cross build の基盤と libcxx まで。clang＋lldb の package の aarch64 の build は、
lldb の zedBSD の plugin に aarch64 の register context が要るので p003 で行う。

## 変更

| 所在 | 変更 |
| --- | --- |
| `userland/packages/external.mk` | `ZEDBSD_EXTERNAL_TRIPLE` に arm64（`aarch64-unknown-zedbsd`）。LLVM の backend の名 `ZEDBSD_EXTERNAL_LLVM_TARGET`（arm64 は `AArch64`、他は `X86`）。work tree を architecture ごとに: amd64 は従来どおり `build/packages`、他は `build/packages-<arch>`（同じ場所だと別の arch の build が amd64 の package を上書きし、作り直しになる） |
| `userland/packages/tools/gen-cross-toolchain.sh` | aarch64 の wrapper にも `-DKERN_USER_ABI_LP64`（public header が user ABI を選ぶ macro） |
| `userland/packages/devel/libcxx/Makefile`・`lang/clang/Makefile` | `LLVM_TARGETS_TO_BUILD`・`LLVM_TARGET_ARCH` を `ZEDBSD_EXTERNAL_LLVM_TARGET` から |

## 検証

| 試験 | 結果 |
| --- | --- |
| aarch64 の `libcxx`（`make ZEDBSD_CONFIG=config/ci/config-rpi4.mk libcxx`、先に `build/arm64/dynamic/libc.so`） | 成功。`libc++.so.1`・`libc++abi.so.1`・`libunwind.so.1` は AArch64 の共有 object。警告は amd64 の build と同じ種類（libunwind・libc++ の書式、外部の source） |
| amd64 の生成物 | wrapper の内容と work tree の場所は変わらない（`X86`、`build/packages`） |
| guest での C++ の実行 | 未実施（p003 の lldb で使う） |

## 注意（merge）

`gen-cross-toolchain.sh` と package の Makefile が変わるので、既存の tree では amd64 の libcxx が configure し直され、
それに依存する clang の package も build し直される（p029 の merge で既に起きる作り直しと同じ）。
