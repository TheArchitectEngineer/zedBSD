<!-- awesome-plan project=zedbsd record=ws045p008 -->

# ws045-p008: 実際の script と guest の回帰

Phase ID: `ws045-p008`
Parent: [WS045](../ws.md)
Status: cleared（2026-09-27。amd64 以外の image と実機は未実施）
Queue: なし（サブエージェント）
依存: ws045-p002〜p007

## 目的

足した拡張を実際の script と guest（zedBSD の libc・TRE・/bin/sh）で確かめる。

## 範囲

- `plan/tools/utils/configure-diff.sh` を拡張を使う package（binutils の opcodes 以外で軽いもの、emacs の該当部分を抜いた script、ncurses、bash など）で走らせ、GNU の道具と生成物を比べる。
- GNU の case を `--export` で guest に出し、amd64 の guest（`plan/tools/sh/guest-diff.sh`、POSIXLY_CORRECT 無しで）で走らせる。
- 4 platform の build と boot test（`plan/tools/boot-test.sh`）。

## 受け入れ

- configure-diff が同一、guest の GNU の case が host と同じ結果、boot test が PASS。

## 実施

### 道具（この Phase で足したもの）

| 道具 | 役割 |
| --- | --- |
| [tests/build-guest-utils.sh](../tests/build-guest-utils.sh) | 変えた utility を amd64 の guest 向けに cross build（既存の guest image の `dynamic/libc.so` に dynamic link。TRE と command.c は各 program に link）。sh は `guest-bin.sh/sh` に |
| [tests/guest-diff.sh](../tests/guest-diff.sh) | guest の中の runner（`PATH=$bin:/bin:/usr/bin`、`LC_ALL=C`、`TZ=UTC`。POSIXLY_CORRECT は呼び手のものを渡す） |
| [tests/guest-batches.sh](../tests/guest-batches.sh) | case を export して 40 件ずつ guest で走らせる（10 batch ごとに guest を起動し直す。BUG-029）。`POSIX=1` で WS043 の case、`SH=1` で guest の copy の `/bin/sh` と `/usr/bin/env` をこの tree のものに置き換える |
| [tests/config-amd64-base.mk](../tests/config-amd64-base.mk) | worktree で package の cache 無しに image を作るための amd64 の config（base の program だけ。package・firmware・desktop 無し） |
| [tests/target-check.sh](../tests/target-check.sh) | 変えた C file を amd64・i386・arm64 向けに `-Wall -Wextra -Werror` で compile だけする |
| `plan/tools/utils/configure-diff.sh` の変更 | `DISTFILES` の環境変数、`.tar.*` を `tar -xf` で。両側の directory の名前を `gnu`/`zed`（同じ長さ。config.status の行の折り返しの差を無くす） |

### guest で見つけて直したもの

- `sh` の builtin の `env`（`userland/base/sh/builtins.c`）が GNU の option（`-u`・`-S`・`-C`・`-0`、long option）を知らず status 127 で失敗していた。
  script が実際に走らせるのはこの builtin なので、builtin が知らない option は子で `/usr/bin/env` に同じ語を渡す（`env_external`）ようにした。
  `-`・`-i`・`--` は従来どおり builtin が扱う。host の `sh-diff --only builtins` は 41/41。
- `mv` の device をまたぐ移動（EXDEV）: ncurses の configure が `ncurses_cfg.h` を別の file system へ `mv` して失敗していた（configure-diff の差）。
  copy して元を消す fallback（`move_across`/`copy_contents`）を足した。
- `find` の `-and`/`-or` の実装（`normalize_words`）が primary の operand まで書き換えていた。`is_operator` で解析の時に判定する形に直した。

## 結果

QEMU の証拠と実機の証拠を分けて書く。実機は全て未実施。

### QEMU（amd64、NVMe、`build/ws053-full-hal-guest/hdd-image.img` の copy）

| 確認 | 結果 |
| --- | --- |
| `sh plan/ws045/tests/build-guest-utils.sh` の後 `sh plan/ws045/tests/guest-batches.sh`（GNU の case、image の /bin/sh のまま。p009 の前の source） | 511/515。落ちた 4 件は全て `env`（`-u`・`-S`・`-0`・`-C`）で、image の古い /bin/sh の builtin の env が走るため |
| 同じく `SH=1`（この tree の sh と env を guest の copy に入れる。p009 の後の source） | **515/515**（`SH=1 sh plan/ws045/tests/guest-batches.sh build/ws045/guest-gnu-sh.out`。env の 4 件も一致） |
| `POSIX=1 sh plan/ws045/tests/guest-batches.sh`（WS043 の POSIX の case、POSIXLY_CORRECT あり） | **492/492** |

### configure-diff（host、GNU の道具と zedBSD の道具で configure を走らせ config.status を比べる）

`DISTFILES=/home/awe/zedBSD-rpi4/build/distfiles sh plan/tools/utils/configure-diff.sh build/ws045/bin <package>...`

| package | 結果 |
| --- | --- |
| expat-2.8.5 | same |
| coreutils-9.12 | same |
| binutils-2.47 | same |
| ncurses-6.6 | same（mv の EXDEV の修正の後） |
| bash-5.3 | same |
| libpng-1.6.58 | same |
| pcre2-10.48 | same |
| wget-1.25.0 | 比較できず: GNU の道具の側でも configure が失敗する（host に GnuTLS の pkg-config が無い）。両側とも同じ所で止まる |

expat の `am__xargs_n` は、zedBSD の `xargs` が `-n` を持たない（WS001 #152）ため host の build の一覧から `xargs` を外して比べた（GNU の xargs が使われる）。

### image の build と boot test（QEMU）

| 確認 | 結果 |
| --- | --- |
| `make sysroots` | 32 秒 |
| `make -j48 ZEDBSD_CONFIG=plan/ws045/tests/config-amd64-base.mk BUILD=build/ws045/image disk-image` | 29.8 秒（sysroots の後）、warning 0（`build/ws045/image-build.log`） |
| `OUTPUT=build/ws045/boot-test plan/tools/boot-test.sh build/ws045/image/hdd-image.img` | **PASS**（画面: `build/ws045/boot-test/login.png`。init が syslogd・networkd・cron・getty_console を起動し `login:` が出る） |
| i386（pcat・pc98）と arm64（rpi4）の image の build と boot test | 未実施（compile は p009 の target-check で 3 architecture とも warning 0） |
| package を含む完全な config の image | 未実施（worktree に package の cache が無い） |

### 実機

未実施。

## 制限・残り

- lean image には openssh が無いので `guest.py`（SSH）の試験はできない。guest の試験は ws053 の image に utility を入れて行った。
- 出荷する image の `/bin/sh` の env の修正は、この branch の image でしか確かめていない（lean image の boot と、ws053 の image の copy への差し替え）。
