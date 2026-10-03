# ws001-p040: mesg

Status: cleared（2026-09-27。1 回目は devfs の chmod の未対応（[BUG-067](../../bugs/BUG-067.md)）で guest の case が通らず uncleared。BUG-067 の修正（main 7956ee55）の後に case を戻し、guest で PASS）
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
`git show salvage/ws001:plan/ws001/tests/pinned/guest.sh`（末尾の「#### mesg on the console」）。salvage の branch は後で消すので、case をここに写す（`pinned/guest.sh` の末尾に空行を挟んで足す）:

```sh
#### mesg on the console
# Guest only: the state of /dev/console is put back at the end.
m=$(mesg < /dev/console); mesg n < /dev/console; echo "st=$?"; mesg < /dev/console; echo "st=$?"; ls -l /dev/console | cut -c6,9; mesg y < /dev/console; echo "st=$?"; mesg < /dev/console; ls -l /dev/console | cut -c6; mesg < /dev/null; echo "st=$?"; case $m in "is y") mesg y < /dev/console;; *) mesg n < /dev/console;; esac
## status 0
## expect
st=1
is n
st=1
--
st=0
is y
w
st=2
```

## 残り・再開の条件

- devfs の端末の node が chmod（少なくとも group・other の write bit）を受けること。kernel の変更で、WS001 の範囲の外（[BUG-067](../../bugs/BUG-067.md) で追跡）。
- それが出来たら、上の guest の case を `pinned/guest.sh` に戻して `guest-run.sh ... pinned` で PASS を確かめ、この Phase を clear する。
- WS001 はユーザーの指示があるときだけ進める（2026-09-27）。

## 再確認（2026-09-27、BUG-067 の修正の後）

main（BUG-067 の修正 7956ee55: devfs が文字 device の node の chmod・chown を保つ）を merge し、上の case を `plan/ws001/tests/pinned/guest.sh` の末尾に戻した。

| 確認 | 結果 |
| --- | --- |
| amd64 guest（lean、serial）`DUMP=1 sh plan/ws001/tests/guest-run.sh build/ws001/guest-p040b.out pinned` | **31/31**（「mesg on the console」を含む。`mesg n` で group と other の write が外れ、`mesg y` で group の write が付き、端末でない標準入力では 2） |
| host の試験 | 9/9（1 回目のまま。mesg は変えていない） |
| 実機 | 未実施 |
