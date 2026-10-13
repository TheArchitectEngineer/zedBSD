<!-- awesome-plan project=zedbsd record=ws196-p003 -->
# ws196-p003: useradd・usermod・userdel（account-admin を呼ぶ root の前段）

Status: planned（2026-10-14 Q1、q972、P2）
Disposition: normal
Parent: [WS196](../ws.md)
Queue: q972
依存: [p002](../phase002/phase.md)（root の request の形 D1、merge 済み 7f2c850a7）

## 範囲

- `userland/base/useradd`・`usermod`・`userdel`（または 1 つの source の 3 つの名前）。root（実の uid 0）でなければ EPERM 相当で終わる。option を解析して p002 D1 の root の形の request を account-admin に渡し、答えの語を shadow-utils の終わりの状態と message に写す。
- option は [p001](../phase001/phase.md) §2 から `-d`・`-m`（usermod の home の移動）を除く（2026-10-14 ⑤）。useradd の `-m`・`-M` は home の作る・作らない。`-u`・`-g`・`-G` の範囲は p002 D1（④）。`usermod -U` の `!` だけの hash は `no-password`（F1）。
- install は `/sbin`（mode 0755、setuid にしない）、menu は Base、表示名は命令の名前。man（`docs` か package の man の置き場の慣習に合わせる）。
- `plan/coding-style.md` の全文を適用。

## 確かめ

- amd64 の build warning 0。host 試験: p002 の `plan/ws196/tests/account-admin-root-host-test.sh` の形で、前段の 3 つの命令の option の解析と終わりの状態（正常・使い方の誤り・last-administrator・id-taken・no-password・bad-value）。
- QEMU は p004 で T1（依頼の文を phase.md に。p002 の依頼と 1 つにまとめる）。
