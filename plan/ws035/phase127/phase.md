<!-- awesome-plan project=zedbsd record=ws035p127 -->

# ws035-p127: zdesktop-p101.sh を今の image（kei の自動 login と password）に合わせる

Phase ID: `ws035-p127`
Parent: [WS035](../ws.md)
Status: cleared（2026-09-29、サブエージェント、worktree `wt/ws035`。QEMU の Venus）
Phase disposition: normal
Queue: なし（2026-09-29 main の指示「観察 4 の `zdesktop-p101.sh` は、古い前提（root の login）を今の image（kei の自動 login と password）に
合わせて直してかまいません」「観察 3 は、どの process かだけ特定できれば報告に」）

## 範囲と原因

[p126](../phase126/phase.md) の検証で、`plan/ws035/tests/zdesktop-p101.sh`（p101 の表示の引き継ぎの試験）が今の graphical の login の image で FAIL した。
試験は boot で greeter が出て root が Enter で login する前提だったが、今の image は boot で kei を自動 login し（sessiond の autologin）、kei の
password は「kei」（2026-09-29 のユーザーの決定、demo の accounts）。session の log も `/run/user/0` ではなく `/run/user/1000`。

## 実装（2026-09-29）

`plan/ws035/tests/zdesktop-p101.sh` の手順を並べ替えた（確かめる log の行は p101 と同じ）:

0. 自動 login の session（`SESSIOND HANDOFF go written=3`、`/run/user/1000/session.log` の `ZWL HANDOFF go=1`）を待つ。
1. App Home の Log Out を撮り続ける: `SESSION end user=kei`・`greeter ready: waits`・`session released=1`・`greeter go written=3`・`GREETER adopt`・
   greeter の `ZWL GREETER open`、文字 console の画が 0。
2. 引き継いだ greeter で `kei` と Enter の login を撮り続ける: `AUTH ok user=kei`・`GREETER stays`・`session ready=1`（2 回目）・`greeter released=1`・
   `go written=3`（2 回目）・session の `ZWL HANDOFF go=1`・greeter の `ZWL GREETER starting`、文字 console の画が 0。最後に desktop の画面の検査。

log の待ちは「COUNT 行以上」に（sessiond.log は boot からの累積）。黒の枚数は情報として出す（黒の時間の計測は `zdesktop-p126.sh`）。

## 検証（amd64、Venus の guest、main の a6e4970d に合わせた worktree の graphical の login の image `build/p127.img`、2026-09-29）

- `GUEST_RUNTIME=build/ws035-run plan/ws035/tests/zdesktop-p101.sh build/p127-p101`: **PASS**（log の 15 行が全て ok、Log Out 101 枚・login 95 枚で
  文字 console 0・黒 0、`again.png` の desktop の検査 PASS）。

## 観察: `kern: pid 135 killed by signal 11 (vector 14) at 0x...af0bc, address 0x18`

p126 の前の image（`build/p126-before.img`）の最初の guest の boot の dmesg で 1 度見た（boot の直後、`boot: starting init` の後）。その後の
p126 の guest の boot（4 回）と、この Phase の `build/p127.img` の boot 3 回（どれも image を複写した最初の boot）では出なかった（dmesg の
`killed by signal` 0）。process は特定できていない（死んだ process の名前は kernel の行に無く、再現しないため）。深追いはしない（main の指示）。
再現したら、同じ boot の `/var/log/messages` の `[135]` と、pc 0xaf0bc を持つ binary（`llvm-nm` で user の text を引く）で特定する。

## Resume point

2026-09-29: cleared。次は main が優先に指定した窓の四隅の resize（p128）。
