# ws001-p037: dirname の複数の operand

Status: cleared（2026-09-27）
Parent: [WS001](../ws.md)
Queue: なし（2026-09-27 ユーザー指示のサブエージェントの作業。main session が merge する）

## 目的

WS045 の未決の 1 件をユーザーが決め、WS001 に割り当てた（coordinator 経由、2026-09-27）:
`dirname` は GNU のように複数の operand を取り、各結果を 1 行ずつ書く。`plan/ws001/tests/dirname-test.sh` をそれに合わせる。
POSIXLY_CORRECT で 1 つに限るかは WS001 に任された。

## 範囲と結果

| 対象 | 変更 |
| --- | --- |
| `userland/base/dirname/main.c` | 規約の全文で書き直し。複数の string を取り各結果を改行で終える。GNU の `-z`（`--zero`、NUL で終える）、`--`、`--version`。option の走査は WS045 の `command_options`（POSIXLY_CORRECT のときは最初の operand で option が終わる） |
| `plan/ws001/tests/dirname-test.sh` | 「too many operands」の失敗を、複数の operand の結果と `-z` の確かめに置き換えた。host の build に `common/command.c` を足した |
| `plan/tools/utils/cases/dirname.sh` | 新設。GNU dirname と比べる（1 つ、複数、`-z`、`--`、operand 無し） |

判断: POSIXLY_CORRECT でも複数の operand を受ける。POSIX の synopsis は 1 つだが、余分な operand は拡張として受けてよく、
GNU dirname も POSIXLY_CORRECT で複数を受ける。1 つの operand の結果は POSIX のまま変わらない。

## 受け入れ条件と結果（2026-09-27）

| 確認 | 結果 |
| --- | --- |
| `sh plan/ws001/tests/dirname-test.sh` | PASS |
| host の差分試験 `dirname`・`misc`（GNU と） | 5/5・23/23 |
| amd64 guest `DUMP=1 sh plan/ws001/tests/guest-run.sh build/ws001/guest-p037.out dirname misc` | 28/28 |
| style（`dirname/main.c`） | 違反 0、`cc -Wall -Wextra` の warning 0。guest image の build の新しい warning 0 |
| 実機 | 未実施 |
