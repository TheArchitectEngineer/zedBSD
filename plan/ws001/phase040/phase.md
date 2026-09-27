# ws001-p040: mesg

Status: uncleared（2026-09-27。mesg の実装と host の試験は通り commit した。guest の console での確認が devfs の chmod の未対応で通らない）
Parent: [WS001](../ws.md)
Queue: なし（2026-09-27 の rate limit で止まったサブエージェントの作業を、片付けのサブエージェントが検証して commit した。`salvage/ws001` 4b59f03e、親 9b9f8d9c）

## 目的

台帳 #78（mesg、P2）: 端末が無いとき、`y`/`n` の解釈、他の permission bit を保つこと、診断と終了状態、guest の tty での試験。

## 範囲と結果

| 対象 | 変更 |
| --- | --- |
| `userland/base/mesg/main.c` | 書き直し。端末は標準入力・出力・エラーのうち最初に端末であるもの（無ければ `mesg: not a terminal` と 2）。`--` の後に高々 1 つの operand、`y` は group の write を足し、`n` は group と other の write を外す（他の bit は保つ）。operand 無しは `is y`/`is n`。終了状態は許可 0・不許可 1・誤り 2 |
| `plan/ws001/tests/mesg-host-test.py` | 新設。host の pty の slave を与えて mode を `os.stat` で読み返す（y・n・状態・`--`・標準エラーだけが端末・誤った operand・2 つの operand・端末無し） |

## 確認（2026-09-27、片付けのサブエージェント）

| 確認 | 結果 |
| --- | --- |
| host の build `sh plan/ws001/tests/build-host-ws001.sh build/ws001/bin mesg` | status 0。`cc -Wall -Wextra` の warning 0 |
| host の試験 `python3 plan/ws001/tests/mesg-host-test.py --bin build/ws001/bin` | 9/9 |
| style `python3 plan/tools/style-check.py userland/base/mesg/main.c` | 違反 0 |
| amd64 guest（lean、serial）`DUMP=1 sh plan/ws001/tests/guest-run.sh build/ws001/guest-p040.out pinned` | 既存の pinned 30/30。salvage にあった新しい case「mesg on the console」は **FAIL**: `mesg n < /dev/console` が `mesg: Operation not supported` と 2 |
| guest で直接（serial） | `chmod g-w /dev/console` も `Operation not supported`。devfs の node（`/dev/console`・`/dev/ttyv*`・`/dev/tty`）は `crw-rw-rw-` 固定で chmod を受けない（`src/kern/devfs.c` が `EOPNOTSUPP`）。mesg の不具合ではない |
| boot test | 未実施（userland の 1 utility の変更。guest は起動して login した） |
| 実機 | 未実施 |

guest の case は失敗するので commit していない（salvage の `plan/ws001/tests/pinned/guest.sh` の追加分）。再開するときは salvage の branch から戻す:
`git show salvage/ws001:plan/ws001/tests/pinned/guest.sh`（末尾の「#### mesg on the console」）。

## 残り・再開の条件

- devfs の端末の node が chmod（少なくとも group・other の write bit）を受けること。kernel の変更で、WS001 の範囲の外（bug として追跡する。ID の割り当ては main）。
- それが出来たら、上の guest の case を `pinned/guest.sh` に戻して `guest-run.sh ... pinned` で PASS を確かめ、この Phase を clear する。
- WS001 はユーザーの指示があるときだけ進める（2026-09-27）。
