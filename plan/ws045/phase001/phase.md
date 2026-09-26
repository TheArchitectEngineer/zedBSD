<!-- awesome-plan project=zedbsd record=ws045p001 -->

# ws045-p001: 調査と差分試験の土台

Phase ID: `ws045-p001`
Parent: [WS045](../ws.md)
Status: cleared
Queue: なし（2026-09-27 ユーザー指示のサブエージェント、worktree `worktree-agent-a881435ba8eed9779`）

## 目的

実際の script（configure・Makefile・build の shell script）が base の utility のどの GNU 拡張を使うかを調べ、Phase を使われる順に立てる。
GNU の拡張を GNU の道具と比べる差分試験の土台を作る。

## 受け入れ

- `build/distfiles` の全 tarball（57 package）の script を調べ、utility ごとの option と program の中の拡張を package 数で数える。
- GNU の case を POSIXLY_CORRECT 無しで比べられる。host の build が guest と同じ regex（zedBSD の TRE）を使う。
- WS043 の POSIX の差分試験が TRE の build でも全件通る。

## 調査の方法

[tests/survey.py](../tests/survey.py): 各 package の `configure`・`*.sh`・`Makefile*`・`*.mk`・`*.am`・`*.in`・`*.ac`・`*.m4`・`*.pl`・`*.awk`・`*.sed` を読み、
utility の呼び出しを文字列として見つけて option と、sed・grep・awk の program の中の拡張（`\+`・`\|`・`\t`・`gensub` など）を数える。
test suite（`tests/`・`t/`・`testsuite/` など）と、自分の option を試す coreutils は除いた。`git grep` などの subcommand も除いた。実行はしない（数は目安）。

```
python3 plan/ws045/tests/survey.py DIR --examples 2
```

DIR は tarball の script だけを展開した directory（`tar -xf X --wildcards --no-anchored '*.sh' configure '*Makefile*' ...`）。

## 結果（2026-09-27、56 package。coreutils を除く）

非 POSIX のものを、使う package の数の順に（主な例）。

| utility | 拡張 | package | 例 |
| --- | --- | --- | --- |
| grep | BRE の `\|` | 25 | `config.guess`（全 autoconf package）: `grep '^CPU\|^MIPS_ENDIAN\|^LIBCABI'` |
| sed | `-i` | 10 | fontconfig `new-version.sh`（`sed -i FILE -e ...`）、gcc・glib・qt・vim・tiff |
| sed | `-E`（POSIX 2024 にもある）・`-r` | 7・4 | curl・ffmpeg・ncurses（`\w` を -E で） |
| sed | `\t`（regex・置換・bracket の中） | 7 | binutils `opcodes/configure`: `s/^\t\(\$(AM_V_CCLD)\)/\t+ \1/`、emacs `[\t ]*` |
| sed | BRE の `\+`・`\?`・`\|` | 6・6・4 | emacs configure `[0123456789]\+`、bash aclocal `x\?emacs` |
| grep | `\w`・`\b`・`\<`・`\>` | 6 | cairo `'#.*\<include\>'` |
| grep | BRE の `\+` | 4 | cairo・ffmpeg・gcc・gdb |
| sed | `\<`・`\>`、`\s` | 3・2 | gdb configure `datadir\>` |
| grep | `-w` | 3 | ncurses configure `grep -w exec`、libpng・openssl |
| cmp | `--ignore-initial` | 3 | binutils・gcc・gdb の configure（`cmp --ignore-initial=2 t1 t2`） |
| touch | `--reference` | 2 | binutils `opcodes/configure`（56 行） |
| grep | `-o`・`-r`/`-rl`・`-A` | 2・2・2 | gdb `Makefile.in`、curl、gcc libstdc++ `po/Makefile` |
| sort | `-V` | 2 | emacs configure（`sort -V \| tail -n 1`）、gdb |
| date | `-d`・`--date`・`-r FILE` | 2・2・2 | vim configure（`date -u -d "@$SOURCE_DATE_EPOCH"`、fallback あり） |
| find | `-maxdepth`・`-iname` | 2・1 | gdb sim `Makefile.in`、libpng ci |
| awk | `gensub` | 1 | gdb の保守用 script |

POSIX の範囲で足りるもの（多く使われるが拡張ではない）: `sed -n`/`-e`/`-f`、`grep -q`/`-v`/`-c`/`-e`/`-i`/`-E`/`-F`/`-l`、`sort -u`/`-k`/`-t`/`-n`/`-r`、`cut`、`tr`、`head -n`、`mkdir -p`、`ln -s`、`cp -p`/`-R`、`rm -rf`。

出荷していない utility: `mktemp`（30 package。autoconf の config.status が `mktemp -d` を試し、失敗なら自前の方法に落ちる）、`install`（`install-sh` に落ちる）、`base64`（2）。

加えて WS042 が要ると記録したもの: `grep -o`（`egrep -o`）、`grep -A`、`tail --bytes`、`/bin/echo -e`。

## 現状（2026-09-27、GNU の case。POSIXLY_CORRECT 無しで GNU と比較）

| case file | 一致 | 主な欠け |
| --- | --- | --- |
| [sed.sh](../tests/cases/sed.sh) | 38/93 | option（`-i`・`-s`・`-z`・long option）、`\U`/`\L`、GNU の address、`Q`・`T`・`F`・`z`・`W`・`R`・`e`・`v`、bracket の中の `\t` |
| [grep.sh](../tests/cases/grep.sh) | 9/64 | `-w`・`-o`・context・`-r`・`-h`/`-H`・`-L`・`-m`・long option。regex の `\|`・`\+`・`\<` は TRE で通る |
| [awk.sh](../tests/cases/awk.sh) | 19/43 | `gensub`、`**`、`func`、`\x`、時刻、RS の regex、`match` の配列、bit 演算、long option |
| [misc.sh](../tests/cases/misc.sh) | 20/78 | `sort -V`、`head -n -N`、`cmp -i`、`touch --reference`、`date -d`、`find -maxdepth` ほか、`xargs -0`、`readlink -f`、long option |

## 差分試験の土台（変更した file）

- `plan/tools/utils/build-host-utils.sh`: 全 utility に zedBSD の TRE（`src/libc/regex/*.c`）を link し、zedBSD の `<regex.h>` を include path の先に置く
  （host の glibc の regex は BRE の GNU 拡張を独自に持つので、host で通っても guest で通る保証にならなかった）。build する utility に
  `cmp find xargs date stat readlink realpath seq tac timeout truncate env tee` を足した。
- `plan/tools/utils/util-diff.py`: `--cases DIR`（case の directory）と `--gnu`（両側を POSIXLY_CORRECT 無しで走らせる）。
- `plan/ws045/tests/cases/`（新規）: `sed.sh`・`grep.sh`・`awk.sh`・`misc.sh`。

## 検証

| 検証 | 結果 |
| --- | --- |
| `sh plan/tools/utils/build-host-utils.sh build/ws045/bin`（TRE を link） | status 0 |
| `python3 plan/tools/utils/util-diff.py --bin build/ws045/bin`（WS043 の POSIX の case、TRE） | **492/492** |
| `python3 plan/tools/utils/util-diff.py --bin build/ws045/bin --cases plan/ws045/tests/cases --gnu` | 上の表（着手前の基準） |
| guest | 未実施（p008） |

## 見つけたこと

- `xargs -I`（POSIX）が status 127 で動かない、`awk` の `system()` が wait の status（768）を返す（gawk・POSIX は終了の status 3）。POSIX の範囲の不具合で、
  前者は WS001 の範囲に近い。後者は p006 で gawk と揃える（POSIX の文言とも合う）。
- `find` は知らない primary（`-maxdepth` など）を黙って何も出さない。
