# ws001-p033: patch

Status: cleared（2026-09-27）
Parent: [WS001](../ws.md)
Queue: なし（2026-09-27 ユーザー指示のサブエージェントの作業。main session が merge する）

## 目的

台帳（ws.md §12）の #92 patch を XCU（POSIX.1-2024）の要求に合わせ、規約の全文で書き直す。
以前の patch は 1 つの file に unified の hunk を当てるだけで、option も他の形式も無かった。

## 範囲と結果

`userland/base/patch/` を 3 つの file に分けて書き直した。

| file | 役目 |
| --- | --- |
| `parse.c` | patch を行に分け、normal・context（`-c`）・unified（`-u`）・ed（`-e`）の差分と `Index:` の行を読む。差分の周りの文章（mail の header など）は飛ばす。複数の file の差分を順に持つ |
| `apply.c` | hunk を当てる。書かれた行に、前の hunk のずれを足した位置から前後へ探し、見つからなければ両端の context を 1 行、2 行まで外して（fuzz）探す。`-R` は逆向き、`-N` は当たり済みの hunk を無視、`-l` は空白の並びを同じと見る、`-D` は `#ifndef`・`#else`・`#ifdef`・`#endif` で両方を残す。context の行は file の行を残す。ed の差分は diff `-e` の出す command（`a`・`c`・`d`・`s/.//`）を行に対して実行する |
| `main.c` | option、file の決め方（operand、無ければ `***`/`---` の名前、`---`/`+++` の名前、`Index:` の名前の順に `-p` を当てて在るもの、`-p` が無ければ最後の成分）、新しい file の作成、一時 file と rename による置き換え（mode を保つ）、`-b`（file.orig を 1 回だけ）、`-o`、`-r`・file.rej、`-d`、状態 0/1/2 |

POSIX に合わせ、GNU と違えた点（期待値の case `pinned/posix.sh` に書いた）:

- 標準出力を使わない。`patching file` などの知らせは標準エラーに書く（GNU は標準出力）。
- `-N` は当たり済みの hunk を無視し、reject file に書かず、状態も 0 のまま（GNU は reject にして状態 1）。
- normal 形式の reject は copied context の形式で書く（`*** 2 ****`、`--- 2 ----`、`!`）。GNU は `*** 2`・`--- 2 -----` と `/dev/null` の名前。

拡張として残した点: `patch file patchfile`（2 つめの operand を patch file とする。歴史的な形で GNU・BSD も受ける）、
`/dev/null` からの差分は operand が無くても新しい file を作る（GNU の既定の mode と同じ。GNU の POSIX mode は作らない）。

## 受け入れ条件と結果（2026-09-27）

| 確認 | 結果 |
| --- | --- |
| host の差分試験 `plan/tools/utils/cases/patch.sh`（GNU patch 2.8 と比べる。normal・context・unified・ed、形式の強制、`-p`・`-p0`・`-d`・`-R`・`-b`・`-o`・`-r`・reject、offset、fuzz、`-l`、`-D`、複数の file と hunk、作成、`Index:`、改行の無い最後の行、前置きの文章、mode、`diff -ru` と `-p1`） | 32/32 |
| 期待値の case（`-N`、normal の reject、標準出力を使わない） | `pinned-cases.py` 9/9 |
| 乱数の組（`diff-random-host-test.py --count 2000 --patch build/ws001/bin/patch`：zedBSD diff の normal・`-c`・`-u`・`-C 1`・`-U 0`・`-U 1`・`-e` を zedBSD patch で当てる） | seed 1・seed 7 とも 2000/2000（直す前は 1643/2000） |
| diff の case（patch と組で使う） | 29/29 |
| amd64 guest `DUMP=1 sh plan/ws001/tests/guest-run.sh build/ws001/guest-p033.out patch diff pinned` | 70/70 |
| style（`main.c`・`parse.c`・`apply.c`・`apply.h`・`patch.h`） | 違反 0、`cc -Wall -Wextra` の warning 0 |
| 実機 | 未実施 |

乱数の組で見つけて直した誤り: context 形式の hunk は old と new の側で両端の context の数が違うことがある
（old の `-` の行と new の `+` の行の位置が違う）。両側で等しい行だけを context として数える（`shared_context`）。
new の側が省かれた hunk で、old の最後の行が消える行のときは、`\ No newline` を new の側に移さない。

## 残り

- 時刻（header の日付）は使わない（POSIX は要求しない）。
- SCCS からの取り出し（file の決め方の 4 番目）と、対話で file の名前を尋ねること（5 番目）はしない。見つからない file は報告して状態 1。
- unified の reject は unified のまま書く（POSIX は実装定義とする）。
