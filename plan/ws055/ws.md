<!-- awesome-plan project=zedbsd record=ws055 -->

# WS055: zedBSD の clang が link に `--undefined-version` を既定で渡す

<!-- awesome-plan-current:start -->
Status: completed
Primary Milestone: MG001
Related Milestones: MG002
Objectives: O4
Parent: [Master](../master.md)
Queue: なし
Resume point: —（2026-09-27 完了。共有の toolchain の切り替えと cache の資産の公開が残る: 下の「移管」）
<!-- awesome-plan-current:end -->

## 目標

zedBSD の target（`x86_64-unknown-zedbsd`、`i386-unknown-zedbsd`、`aarch64-unknown-zedbsd`）の clang が、link の command に `--undefined-version` を既定で付け、version script に未定義の symbol があっても GNU ld と同じく通す。移植する package（zlib など）の configure が共有 library を作れると判断する。

## きっかけ

ws046-p008（q412-i01）: guest の ld.lld が version script の未定義の symbol を error にする（lld 16 からの既定）ので、zlib の configure が共有 library 非対応と判断した（F-009）。2026-09-24 ユーザー決定「すべて承認します。」（既定にする）。

## 結果（ws055-p001・p002、2026-09-27、サブエージェント）

- `toolchain/llvm/patches/0001-add-zedbsd-x86-target.patch` の `tools::zedbsd::Linker::ConstructJob()`（`clang/lib/Driver/ToolChains/ZedBSD.cpp`）で、`--allow-shlib-undefined` の後に `--undefined-version` を足した（新しい file の hunk は 146 → 152 行）。利用者の `-Wl,...` は後ろに並ぶので `-Wl,--no-undefined-version` で戻る。3 つの target に共通。
- `toolchain/llvm/version.mk`: `ZEDBSD_LLVM_PATCH_LEVEL := zedbsd8`、`ZEDBSD_LLVM_CACHE_SHA256 := PENDING`（zedbsd7 の値は WS036 が入れた rev-0 の資産のもので、新しい patch には合わない。`make toolchain-cache` は PENDING で明示的に断る）。
- LLVM は共有の directory に触れず、worktree の中の複写（`build/llvm8-tree`、`make -C toolchain/llvm ZEDBSD_LLVM_COMPILE_JOBS=32 ZEDBSD_LLVM_LINK_JOBS=4 install`）で作った。成果物は worktree の `build/llvm-zedbsd8`（main session が共有の `build/llvm-zedbsd8` へ複写して全 agent を切り替える）。

### 検証

| 確認 | zedbsd7（前） | zedbsd8（後） |
| --- | --- | --- |
| 未定義の symbol を持つ version script で `-shared` の link（`plan/tools/toolchain/link-undefined-version.sh`）、x86_64・i386・aarch64 | 3 target とも status 1（`symbol not defined`） | 3 target とも status 0 |
| 同じ link に `-Wl,--no-undefined-version` | status 1 | status 1（戻せる） |
| driver の link の command（`-###`） | — | `--allow-shlib-undefined --undefined-version` の後に利用者の `--version-script=...` |
| zlib 1.3.2 の configure（host の cross、GNU ld の分岐、`libc.so` のある sysroot、`plan/tools/toolchain/zlib-shared-configure.sh`） | `No shared library support`、`libz.so` を作れない | `Building shared library libz.so.1.3.2`、`make libz.so.1.3.2` status 0、`ZLIB_1.*` の版付きの export 54 |
| 既存の build（amd64、lean の image、sysroot を作り直して、`-Werror`） | — | warning 0 |
| boot test（amd64、`BOOT_MODE=uefi-nvme`） | — | PASS（`build/boot-test-ws055p001/login.png`） |

p002（規約と回帰）: 変更は LLVM の source への patch（C++、LLVM の書き方）で、`plan/coding-style.md`（C）の対象外。comment は周りの形に合わせた。回帰は上の build と boot test。

未実施: guest の clang（`userland/packages/lang/clang`）を作り直した image での zlib の configure（package の LLVM の再 build が重く、共有の toolchain の切り替えの後に package の image を作る時に確かめる）、i386・pc98・rpi4 の image の build と boot（i386 の pcat は `src/kern/sched.c:2012` の 64 bit の `__atomic_fetch_and` が `-Watomic-alignment` で既に止まる。この WS と無関係の既存の失敗）。実機は未実施。

## 移管

- **rev の cache の資産の公開はユーザーの作業**（GitHub の release に zedbsd8 の `zedbsd-llvm-23.1.0-x86_64-linux.tar.gz` を上げ、`ZEDBSD_LLVM_CACHE_SHA256` を埋める）。それまで `make toolchain-cache` は PENDING で断り、`make toolchain` で作る。
- 共有の `build/llvm-zedbsd8` の配置と各 worktree の切り替えは main session（2026-09-27 の合意）。
- guest の clang の package を作り直した image での zlib の確認（上の未実施）。

## Phase 一覧

Phase の記録は完了に伴い削除した（git の履歴に残る）。

| Phase | 内容 | Status |
| --- | --- | --- |
| ws055-p001 | driver が link に `--undefined-version` を渡す patch、LLVM の再 build、試験 | cleared（2026-09-27） |
| ws055-p002 | 規約の確認と回帰 | cleared（2026-09-27） |

## 道具

`plan/tools/toolchain/link-undefined-version.sh CLANG`（3 target の link の試験）、`plan/tools/toolchain/zlib-shared-configure.sh CLANG SYSROOT`（zlib の configure の共有 library の判定）。
