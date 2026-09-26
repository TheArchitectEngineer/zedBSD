# ws001-p027: expand・unexpand・fold・nl・comm

Status: cleared（2026-09-27）
Parent: [WS001](../ws.md)
Queue: なし（2026-09-27 ユーザー指示のサブエージェントの作業。main session が merge する）

## 目的

台帳（ws.md §12）の #41 expand、#136 unexpand、#48 fold、#87 nl、#21 comm を XCU（POSIX.1-2024）の要求に合わせる。

## 範囲

- `expand [-t tablist]`・`unexpand [-a|-t tablist]`: tablist（1 つの数は繰り返しの間隔、複数は昇順の列で comma か blank で区切る。
  最後の stop の後の tab は 1 つの space）。unexpand は行頭の blank、`-a`（と `-t`）は全ての blank の列で、stop の直前の 1 つの space は
  space のまま。backspace は column を 1 戻す。tablist の読み取りは `userland/base/expand/tabs.c` を共有する。
- `fold [-bs] [-w width]`: 端末の column（tab・backspace・carriage return）、`-b` は byte、`-s` は最後の blank の後で折る、
  幅に入らない 1 文字は単独の行に書く、最後の改行の無い行はそのまま。
- `nl`: 全 option（`-b`・`-h`・`-f` の `a`/`t`/`n`/`pBRE`、`-d`、`-i`、`-l`、`-n ln|rn|rz`、`-p`、`-s`、`-v`、`-w`）、論理 page の
  区切りの行（空行として書く。`-p` が無ければ番号を startnum に戻す。GNU と同じく各 section の区切りで戻す）、番号の無い行は同じ幅の space。
- `comm [-123] file1 file2`: `strcoll`（`LC_COLLATE`）で比べる、書く column の前にだけ tab、`-` は標準入力。

範囲外: 多 byte 文字の表示幅（LIBC-CTYPE-01、現在は POSIX locale の 1 byte 1 column）、comm の整列の検査（POSIX は未規定）。

## 受け入れ条件

1. host の差分試験: `plan/tools/utils/cases/{expand,fold,nl,comm}.sh`（expand.sh は unexpand を含む）が全件一致し、全 case（810 件）も一致。
2. 6 つの source が style 違反 0。
3. amd64 guest で同じ case（と WS043 の `misc.sh`）が一致。

## 記録

### 試験（2026-09-27）

| 確認 | 結果 |
| --- | --- |
| host の差分試験 | expand 17/17、fold 13/13、nl 15/15、comm 9/9、全 case 810/810 |
| amd64 guest `DUMP=1 sh plan/ws001/tests/guest-run.sh build/ws001/guest-p027.out expand fold nl comm misc` | 77/77 |
| style（`expand/main.c`・`expand/tabs.c`・`unexpand/main.c`・`fold/main.c`・`nl/main.c`・`comm/main.c`） | 違反 0 |
| 実機 | 未実施 |

GNU に合わせた選択: `expand -t ''` は既定の stop、`nl -i 0` を受ける、comm の失敗の状態は 1。

### 残り

- 多 byte 文字の column（fold・expand・unexpand）は LIBC-CTYPE-01 の後。
