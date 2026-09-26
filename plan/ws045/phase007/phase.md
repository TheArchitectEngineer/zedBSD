<!-- awesome-plan project=zedbsd record=ws045p007 -->

# ws045-p007: file utility の long option と小さな拡張

Phase ID: `ws045-p007`
Parent: [WS045](../ws.md)
Status: cleared
Queue: なし（サブエージェント）
依存: ws045-p001

## 目的

利用者の script がよく使う file utility の GNU の long option と小さな拡張を足す。build script での使用は少ない（調査）。

## 範囲

- `cp -v`・`-t DIR`・`-T`・`-a`・`--preserve`、`mv -v`・`-n`・`-t`、`rm -v`・`--force`・`--recursive`、`mkdir -v`・`--parents`・`--mode`、
  `ln -n`・`-r`・`-v`・`-t`、`basename -s`・`-a`・`-z`、`dirname` の複数 operand、`cut --complement`・`--output-delimiter`、`wc -L`、
  `uniq -i`・`-w`、`tr` の long option、`env -u`、`split -d`・`--additional-suffix`、`tee` の long option。

POSIX の不足が前提になるもの（`cp -R` の欠けなど）は WS001 の台帳に任せ、ここは GNU の拡張だけ。

## 受け入れ

- `util-diff.py --cases plan/ws045/tests/cases --gnu --only misc` の上の範囲の case が一致。WS043 の POSIX の case が全件一致。触った file の style-check が増えない。

## 実装（2026-09-27）

option は `command_options_next`（p002）で読み、GNU の long option と operand の後の option（`POSIXLY_CORRECT` では POSIX の順）を受ける。

| utility | 足したもの |
| --- | --- |
| `rm` | `-d`（空の directory）、`-v`（`removed 'x'`・`removed directory 'x'`）、`-I`・`--one-file-system`・`--preserve-root` ほか（受け付ける）、`--interactive[=WHEN]`、long option |
| `mv` | 書き直し（legacy の 16 → 0）: `-f`・`-i`（尋ねる）・`-n`（`RENAME_NOREPLACE`、従来どおり）・`-u`・`--update[=all\|none\|none-fail\|older]`・`-v`・`-T`・`-t DIR`、long option。file system をまたぐ移動（copy と削除、POSIX）は従来どおり無い（WS001 #83） |
| `mkdir` | 書き直し（18 → 0）: `-v`（`mkdir: created directory 'x'`）、`--parents`・`--mode`・`--verbose`。`-m` の mode を umask に関わらず最後の directory へ `chmod` で（POSIX の要求。従来は umask が効いていた）、`-p` の途中の directory は通常の mode |
| `ln` | `-n`・`-T`・`-t DIR`・`-r`（link の directory からの相対 path）・`-v`・`-i`、operand 1 つ（ここに同じ名前の link）、long option |
| `basename` | `-a`・`-s SUFFIX`（`-a` を含意）・`-z`、`--multiple`・`--suffix`・`--zero`。WS001 の試験（`plan/ws001/tests/basename-test.sh`）が単独で compile するので、共通の scanner を使わず file の中で読む |
| `cut` | `--complement`、`--output-delimiter`（field と byte の範囲の間）、`-z`、long option |
| `wc` | `-L`/`--max-line-length`（tab は 8 の倍数、印字可能な byte は 1 桁、GNU と同じ）、long option |
| `uniq` | `-i`、`-w N`、`-z`、long option |
| `tr` | `-t`（string1 を string2 の長さに切る）、long option |
| `env` | 書き直し: `-u NAME`・`-0`・`-C DIR`・`-S STRING`（`#!/usr/bin/env -S cmd args` の分割。引用符と backslash）・`-v`、long option。option は最初の代入か utility で止まる（permute しない） |
| `split` | 書き直し（12 → 0）: POSIX の `-a`（従来は無かった）、`-b` の単位（POSIX の `k`・`m` と GNU の `K`〜`E`・`KB`・`KiB`）、`-d`・`--numeric-suffixes[=FROM]`・`--additional-suffix`・`--verbose`、long option。suffix が尽きると「output file suffixes exhausted」 |
| `tee` | `--append`・`--ignore-interrupts`・`-p`/`--output-error`（受け付ける）。WS001 の試験（`run-tee-host-test.sh`）がそのまま通る |
| `cp` | option の読み取りを関数に分けて: `-t DIR`・`-v`（`'a' -> 'b'`、`-r` では各 file と directory）・`--preserve[=mode,ownership,timestamps,links,all]`・`--no-preserve`（受け付ける）・`--update[=none\|none-fail]`・`--recursive`・`--force`・`--no-clobber`・`--target-directory`、zedBSD の `--report-file` はそのまま |

範囲外・判断待ち: `dirname` の複数の operand（GNU）は、WS001 の試験（`plan/ws001/tests/dirname-test.sh`）が「多すぎる operand を拒む」ことを確かめているため入れなかった（ws.md の「判断が要る点」）。

## 結果（2026-09-27）

| 検証 | 結果 |
| --- | --- |
| `python3 plan/tools/utils/util-diff.py --bin build/ws045/bin --cases plan/ws045/tests/cases --gnu` | **517/517**（awk 58、grep 98、misc 163、sed 174、sort 24。misc に p007 の case を 44 足し、dirname の 1 件を外した） |
| `python3 plan/tools/utils/util-diff.py --bin build/ws045/bin`（WS043 の POSIX の case） | **492/492** |
| WS001 の試験: `basename-test.sh`・`dirname-test.sh`・`run-tee-host-test.sh`・`run-cmp-host-test.sh`・`run-base-command-host-test.sh` | 全て PASS |
| style-check: rm・mv・mkdir・ln・basename・cut・wc・uniq・tr・env・split | 0（mv 16→0、mkdir 18→0、basename 5→0、split 12→0） |
| style-check: legacy | tee 13→11、cp 127→116（足した関数は 0） |
| `sh plan/ws045/tests/target-check.sh`（13 file） | amd64・i386 とも warning 0 |
| image の build・guest | 未実施（p008） |
