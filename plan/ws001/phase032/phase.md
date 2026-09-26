# ws001-p032: cat・cksum・dd

Status: cleared（2026-09-27）
Parent: [WS001](../ws.md)
Queue: なし（2026-09-27 ユーザー指示のサブエージェントの作業。main session が merge する）

## 目的

台帳（ws.md §12）の #13 cat、#19 cksum、#31 dd を XCU（POSIX.1-2024）の要求に合わせ、規約の全文で書き直す。
p031 の後、§12 の残りから、流れを扱う utility をまとめた。

## 範囲と結果

| utility | 変更 |
| --- | --- |
| cat | 書き直し。`-u`（出力を貯めないので何も変わらない）、`-` は標準入力、短い書き込みと EINTR、読めない file は報告して続ける、書き込みの失敗で止まる |
| cksum | 書き直し。POSIX の CRC を表で速く（以前は bit ごと）、長さを最下位の byte から、EINTR、標準入力は名前なし。installer の `-a sha256` の出力（digest、2 つの空白、名前、改行と backslash の escape）は保つ |
| dd | 書き直し。operand（`if`・`of`・`ibs`・`obs`・`bs`・`cbs`・`skip`・`seek`・`count`・`conv`）、式（`k`・`b`・`x`）、`conv=` の全部（`ascii`・`ebcdic`・`ibm` は POSIX の変換表、`block`・`unblock`、`lcase`・`ucase`、`swab`、`noerror`、`notrunc`、`sync`）と排他、`cbs=` のときの ascii→unblock・ebcdic→block、seek の後の切り詰め、pipe への seek は null block、統計（`%u+%u records in/out`、`truncated record(s)`）、SIGINT で統計を書いて SIGINT で終わる（起動時に無視されていれば無視のまま）。以前は `conv=` の変換と `cbs=` が無く、seek の後を切り詰めず、失敗の状態が 2 だった |

変換表（`userland/base/dd/tables.c`）は POSIX XCU dd の表で、host の GNU dd に全 256 byte を通して作り、`A`→0xC1・`a`→0x81・`0`→0xF0・空白→0x40 を確かめた。

## 受け入れ条件と結果（2026-09-27）

| 確認 | 結果 |
| --- | --- |
| host の差分試験 `plan/tools/utils/cases/stream.sh` | 32/32（最初は cat・cksum は一致、dd は 7 件が不一致） |
| 端末の case（dd の ^C） | `tty-host-test.py` 11/11 |
| 期待値の case（`cksum -a sha256` の書式） | `pinned-cases.py` 6/6 |
| amd64 guest `DUMP=1 sh plan/ws001/tests/guest-run.sh build/ws001/guest-p032.out stream pinned` | 38/38 |
| style（`cat/main.c`・`cksum/main.c`・`dd/main.c`・`dd/tables.c`） | 違反 0 |
| 実機 | 未実施 |

GNU の dd の 3 行目（転送量・時間・速さ）は GNU の拡張で、case では比べない。

## 残り

- dd: 読み取りの失敗（`noerror`）の再現試験は無い（壊れた媒体が要る）。
