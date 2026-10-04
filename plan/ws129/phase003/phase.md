<!-- awesome-plan project=zedbsd record=ws129-p003 -->
# ws129-p003: 版の一つの源

Status: in-progress（q711、P2、2026-10-05。実装・build・host 試験は済み、QEMU は T1 待ち。判定は Q1）
Disposition: normal
Parent: [WS129](../ws.md)
Focused goal: fg019（ベータ1）
Queue: q711（P2）
目安: 2h

## 範囲

Makefile の変数（例 `ZEDBSD_RELEASE`・`ZEDBSD_RELEASE_NAME`、nightly の既定は git の短い hash つき）から header と `/etc/os-release` を生成し、libc の `uname`（`userland/base/libc/posix.c:5324-5325`）、
起動の表示（`src/hal/i386/cmain.c` の版の文字、amd64 の起動の表示があればそれ）が使う。Settings の About（WS089 の `userland/desktop/settings/about.c`）の差分は main に依頼する。

## 受け入れ

QEMU の SSH で `uname -a` と `/etc/os-release`、`plan/tools/boot-test.sh` の PNG。build warning 0、全文規約。版の変数を変えると make が作り直す（依存の確認）。

## 所有 path

`Makefile` の版の部分、生成の規則、`userland/base/libc/posix.c` の uname の部分、`src/hal/i386/cmain.c` の版の文字、`plan/ws129/`。

## 依存

p001、ユーザーの版の名前。

## 未決の判断

版の名前。

## 実施（2026-10-05、q711、P2）

### 判断の入力

- U1（2026-10-04 ユーザー）: 利用者に見える名前は「Kei/zedBSD 1.0.0 Beta 1」→ `VERSION` は `1.0.0-beta1`、`PRETTY_NAME="Kei/zedBSD 1.0.0 Beta 1"`。
- U15: 公開の直後に `VERSION` を `1.0.0-beta2.dev` に上げる（p008）。その時の表示は「1.0.0 Beta 2 (development)」。
- tag の形（`zedbsd-<VERSION>`）と CI の release の job は p004。

### 設計

- **一つの源**: repository の root の `VERSION`（1 行 `1.0.0-beta1`）。Makefile が読み、`[0-9]+.[0-9]+.[0-9]+(-[a-z0-9]+(.[a-z0-9]+)*)?` でなければ make を止める。
- **release の文字**: `ZEDBSD_RELEASE_BUILD=y`（p004 の release の job が渡す）なら `VERSION` そのまま、既定（nightly・local）は `VERSION+g<git の短い hash>`（`.git` が無ければ `+unknown`）。
- **生成物**（`$(BUILD)/gen/`、FORCE の規則で毎回作り、中身が変わった時だけ書き換える。2 回目の make で時刻が変わらないことを確かめた）:
  - `os-release`: `NAME="zedBSD"`、`ID=zedbsd`、`VERSION="1.0.0 Beta 1"`、`VERSION_ID=1.0.0-beta1`、`PRETTY_NAME="Kei/zedBSD 1.0.0 Beta 1"`、`BUILD_ID=<hash>`、
    `ZEDBSD_RELEASE=1.0.0-beta1+g<hash>`、`ZEDBSD_VERSION="zedBSD <release> (<hash> <commit の日付>)"`。全ての root に `/etc/os-release`（0644）として入る（`ZEDBSD_PACKAGE_FILES`）。
    日付は commit の日付（build の日付にしないので image は再現できる）。
  - `zedbsd-version.h`: `#define ZEDBSD_VERSION "1.0.0-beta1"`。hash を含めないので、commit ごとに kernel を link し直さない。
- **uname**（libc、`userland/base/libc/posix.c`）: `/etc/os-release` の `ZEDBSD_RELEASE`・`ZEDBSD_VERSION` を読む（file が無い root では release `unknown`、version `zedBSD`）。
  libc は toolchain の sysroot（`toolchain/llvm/sysroot.mk`、`$(BUILD)` の生成物を見ない）でも build されるので、版を libc に compile せず実行時に読む形にした
  （toolchain を変えずに済み、版や hash が変わっても libc と sysroot を作り直さない）。読みは system call を直接使い（`call(KERN_SYS_open/read/close)`）、
  uname を cancellation point にせず、signal handler でも安全なまま、errno を変えない。
- **起動の表示**: amd64 の HAL（`src/hal/amd64/cmain.c`）に `zedBSD 1.0.0-beta1` の行を足す（既存の `zedBSD amd64 HAL` の行は残す）。i386・PC-98（`src/hal/i386/cmain.c`）の `0.0.1` を `ZEDBSD_VERSION` に。
  HAL の実装の修正で API（`hal.h`）は変えない。cmain.o だけに `-I$(BUILD)/gen` と生成の header への依存を付けた（top の Makefile）。
- `make version` で 2 つの生成物だけを作れる。
- Settings の About は uname の release を出すので、Kernel の行は自動で `zedBSD 1.0.0-beta1+g<hash>` になる。About に「Kei/zedBSD 1.0.0 Beta 1」（`PRETTY_NAME`）を出すのは
  WS089 の `userland/desktop/settings/about.c`（範囲外）で、main に依頼する（案: `se_about_read` で `/etc/os-release` の `PRETTY_NAME` を読み、About の hero card か Version の行に出す）。

### 変更

- `VERSION`（新規）、`Makefile`（版の節、cmain.o の規則）、`userland/base/libc/posix.c`（`uname_release`・`uname_field`）、`src/hal/amd64/cmain.c`・`src/hal/i386/cmain.c`、
  `plan/ws129/tests/host-version.sh`（新規）。

### 確認（host）

- `sh plan/ws129/tests/host-version.sh build/p2-q711/hv`: 15 passed, 0 failed（nightly・release・development の名前・git 無し・不正な VERSION の拒否・2 回目で書き換えない・libc の `uname_field` の key の読み）。
- build: zedBSD amd64 の `dynamic/libc.so`・`bin/uname`・`src/hal/amd64/cmain.o`（と worktree の sysroot の作り直し）warning 0
  （`ZEDBSD_CONFIG=plan/ws089/tests/config-amd64-settings.mk BUILD=build/p2-q711`）。i386（pcat）・PC-98 の `src/hal/i386/cmain.o` warning 0。kernel 全体と image は build していない（T1）。
- `make -n` で rootfs の stamp が `os-release` を作って `/etc/os-release` に入れることを確かめた。

### 未実施（T1 に依頼）

- QEMU: image（`plan/ws129/tests/config-amd64-ci-noclang.mk`）で `plan/tools/boot-test.sh` の PASS と PNG、SSH で `uname -a`（`zedBSD <host> 1.0.0-beta1+g<hash> zedBSD 1.0.0-beta1+g<hash> (<hash> <日付>) amd64`）と
  `cat /etc/os-release`、`dmesg | grep 'zedBSD 1.0.0-beta1'`（kernel の起動の表示）。
- About の `PRETTY_NAME` の表示（WS089、main に依頼）。
