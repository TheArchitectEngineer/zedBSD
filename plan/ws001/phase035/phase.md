# ws001-p035: who

Status: cleared（2026-09-27）
Parent: [WS001](../ws.md)
Queue: なし（2026-09-27 ユーザー指示のサブエージェントの作業。main session が merge する）

## 目的

台帳（ws.md §12）の #150 who を XCU（POSIX.1-2024）の要求に合わせ、規約の全文で書き直す。
以前の who は option と operand を無視し、user の行を UTC の固定の書式で書くだけだった。

## 範囲と結果

| 対象 | 変更 |
| --- | --- |
| `userland/base/who/main.c` | 書き直し。`-a`・`-b`・`-d`・`-H`・`-l`・`-m`・`-p`・`-q`・`-r`（記録する系だけ、`RUN_LVL` があるとき）・`-s`・`-t`・`-T`・`-u`、`who am i`（2 つの operand は `-m`）、file operand（`struct utmpx` の並び。端数の記録は捨てる）。時刻は `%b %e %H:%M`（TZ に従う）、端末の状態は group の書き込みで `+`/`-`、無ければ `?`、idle は 1 分未満 `.`、1 日未満 `hh:mm`、それ以上か boot 前は `old`。欄の並びと幅は他の系（GNU）と同じ。system の database では process の無い user を除く。読めない file は診断して状態 1（GNU は黙って 0。`pinned/posix.sh`） |
| `include/libc/utmpx.h` | POSIX の要る `NEW_TIME`（3）・`OLD_TIME`（4）・`INIT_PROCESS`（6）を足した。既存の `LOGIN_PROCESS` は 5 のまま（他の系は INIT 5・LOGIN 6。値を替えると既存の記録の意味が変わるので替えない） |

## 受け入れ条件と結果（2026-09-27）

| 確認 | 結果 |
| --- | --- |
| host の差分試験 `plan/tools/utils/cases/who.sh`（glibc の `struct utmpx` の file を python で作り、GNU who と zedBSD who の host build に同じ file を読ませる。全 option、`-m`、端末の状態と idle、空・零の file、usage。guest では流さない） | 5/5 |
| 期待値の case（host）`pinned-cases.py` | 11/11（guest だけの case 17 は流さない） |
| amd64 guest `DUMP=1 sh plan/ws001/tests/guest-run.sh build/ws001/guest-p035.out pinned`（`pinned/guest.sh` の who: console の root、`-q`、月、database の複写を file operand で、`-H`・`-u`・`-T`、端末の無い `-m`、無い file） | 28/28 |
| style（`who/main.c`） | 違反 0、`cc -Wall -Wextra` の warning 0。guest image の build の warning 0 |
| 実機 | 未実施 |

## 見つけたこと

- zedBSD では `BOOT_TIME`・`NEW_TIME`・`INIT_PROCESS`・`DEAD_PROCESS` の記録を書くものが login の `DEAD_PROCESS` のほかに無い。
  `who -b`・`-t`・`-p` は何も書かない（who は記録があれば書く）。
- devfs の `/dev/console` の atime は 1970 年のままで、idle は常に `old` になる。

## 残り

- `-r` の run level は記録する系が無い（zedBSD に run level は無い）。
