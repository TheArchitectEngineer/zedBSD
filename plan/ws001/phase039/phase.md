# ws001-p039: xargs の GNU の option

Status: cleared（2026-09-27）
Parent: [WS001](../ws.md)
Queue: なし（2026-09-27 ユーザー指示のサブエージェントの作業。main session が merge する）

## 目的

WS045 の未決の 1 件をユーザーが決め、WS001 に割り当てた（coordinator 経由、2026-09-27）:
xargs に POSIX の option の全部（台帳 #152: -E -I -L -n -p -s -t -x）、その後 GNU の -0/--null、-r/--no-run-if-empty、-d、-P を足し、
xargs を host の build の一覧に戻して configure の比較が zedBSD の xargs を使うようにする。

POSIX の option は p024（2026-09-27）で済んでいた（-0 と -r も）。この Phase は GNU の残りを足した。

## 範囲と結果

| 対象 | 変更 |
| --- | --- |
| `userland/base/xargs/main.c` | option の走査を `command_options` に（utility で option が終わる、並べ替えない）。`-d delim`（`--delimiter`。1 文字か `\n` `\t` `\r` `\f` `\v` `\a` `\b` `\\` `\xHH` `\ooo` `\0`、引用なし、終わりの文字列なし）、`-0` にも終わりの文字列なし（GNU と同じ）、`-P n`（`--max-procs`、0 は制限なし。同時に n 個まで走らせ、終わった順に状態を取る。255 と signal での停止は残りを待ってから 124・125）、`-a file`（`--arg-file`、utility の標準入力は xargs のもの）、`-o`（`--open-tty`）、旧い形の `-e[eof]`・`-i[replace]`・`-l[lines]`、long option の `--eof` `--replace` `--max-lines` `--max-args` `--interactive` `--no-run-if-empty` `--max-chars` `--verbose` `--exit` `--null` `--version` |
| `plan/tools/utils/build-host-utils.sh` | host の build の一覧に xargs を戻した（WS043/WS045 の configure の比較が zedBSD の xargs を使う） |
| `plan/tools/utils/configure-diff.sh` | BIN_DIR の awk・sed・grep 以外の道具（install、dd など）の path も host の `/usr/bin/` に揃えてから比べる |
| `plan/tools/utils/cases/xargs-gnu.sh` | 新設。GNU xargs と比べる（`-0`、`-0` と `-E`、`-d` の文字と escape、`--delimiter` と悪い区切り、`-r`、`-P` と並びを揃えた出力、`-P` の失敗の状態、`-a`、旧い形、long option、utility で option が終わる） |

## 受け入れ条件と結果（2026-09-27）

| 確認 | 結果 |
| --- | --- |
| host の差分試験（POSIX mode）`xargs`・`xargs-gnu` | 54/54・11/11 |
| host の差分試験（GNU mode、`--gnu`）`xargs`・`xargs-gnu` | 54/54・11/11 |
| host の全 case `util-diff.py --bin build/ws001/bin` | 1080/1080 |
| WS045 の GNU の case `util-diff.py --cases plan/tools/gnu-utils/cases --gnu` | 515/515 |
| 端末の case `tty-host-test.py`（xargs -p を含む） | 11/11 |
| 期待値の case `pinned-cases.py` | 11/11 |
| configure の比較 `plan/tools/utils/configure-diff.sh build/ws001/bin expat-2.8.5 coreutils-9.12`（`DISTFILES` は main の tree の `build/distfiles` を読むだけ） | expat・coreutils とも same（1 回目は configure が build/ws001/bin の install と dd を見つけた path だけが違った。configure-diff.sh は BIN_DIR の他の道具も `/usr/bin/` と書き換えるようにした） |
| amd64 guest `DUMP=1 sh plan/ws001/tests/guest-run.sh build/ws001/guest-p039.out xargs xargs-gnu` | 65/65 |
| style（`xargs/main.c`） | 違反 0、`cc -Wall -Wextra` の warning 0 |
| 実機 | 未実施 |
