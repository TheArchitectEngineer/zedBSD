# ws001-p029: diff

Status: cleared（2026-09-27）
Parent: [WS001](../ws.md)
Queue: なし（2026-09-27 ユーザー指示のサブエージェントの作業。main session が merge する）

## 目的

台帳（ws.md §12）の #34 diff を XCU（POSIX.1-2024）の要求に合わせる。これまでの diff は同じ行番号どうしを比べるだけで、
差分の algorithm を持たなかった。

## 範囲

- 行の比較（`userland/base/diff/lines.c`、新設）: 各行に番号を振り（同じ行は同じ番号、`-b` では空白を畳む）、Myers の O(ND) の
  線形空間の方法（両端からの探索で最適な経路の中点を求めて二分する）で最長共通部分列を求める。相手の file に 1 度も現れない行は
  先に除く（どの共通部分列にも入らないので最小性は変わらない。無関係な file どうしでも速い）。最後の改行の無い行は、改行のある
  同じ文字列と別の行として扱う（`-b` では同じ。GNU と同じ）。
- 出力（`userland/base/diff/main.c`、書き直し）: 通常形式（`a`・`d`・`c`、`<`・`---`・`>`）、`-c`・`-C n`（header の日付は
  `%a %b %e %T %Y`、`!`・`+`・`-`、変わらない側は範囲だけ）、`-u`・`-U n`（header は `%Y-%m-%d %H:%M:%S.nnnnnnnnn %z`、範囲の規則）、
  `-e`（後ろから、単独の `.` の行は `..` と `s/.//` で直す）、`-f`（前から、文字が先）、`\ No newline at end of file`、
  `-` は標準入力、directory と file の組は directory の中の同じ名前の file と比べる、directory の中の組の前に `diff options a b` の行。
- directory の比較（`tree.c`、既存）は保つ。変えたのは 2 点だけ: 内容の違う file を `-q` では `Files a and b differ`、binary は
  `Binary files a and b differ` と書く（`--metadata` の installer の出力 `differ: contents` は変えない）、`-b` で行が同じになる組を
  状態 0 として受ける（以前は 2 にしていた）。
- 状態: 0 同じ、1 違う、2 問題。

範囲外: 多 byte 文字、GNU の長い option（WS045 の方針に従う）。

## 受け入れ条件

1. host の差分試験 `plan/tools/utils/cases/diff.sh` が全件一致（header の日付は case の側で削る）。
2. 乱数の file の組で、各形式を GNU の `patch`・`ed` に当てると第 2 の file になり、変わる行の数が GNU `diff --minimal` と同じ
   （`plan/ws001/tests/diff-random-host-test.py`）。
3. installer の呼び方（`diff -r -q --metadata`）の出力と状態が旧 diff と同じ（`plan/ws001/tests/diff-installer-compare.sh OLD NEW`）。
4. `main.c`・`lines.c`・`lines.h` が style 違反 0、`tree.c` は悪化させない。
5. amd64 guest で case が一致。

## 記録

### 試験（2026-09-27）

| 確認 | 結果 |
| --- | --- |
| host の差分試験 `util-diff.py --only diff` | 29/29 |
| 乱数の組 `diff-random-host-test.py --count 1000`（seed 1 と 7） | 1000/1000、1000/1000（最初は、改行の無い最後の行を同じ行とみなした誤りと、`-b` でそれを区別した誤りを見つけて直した） |
| installer の呼び方の比較（旧 diff は HEAD の main.c と tree.c を host で build） | 6 つの変種（同じ・内容・mode・時刻・hard link・片方だけ）で出力と状態が同じ |
| amd64 guest `DUMP=1 sh plan/ws001/tests/guest-run.sh build/ws001/guest-p029.out diff files` | 71/71（`diff -e` を guest の ed に当てる case を含む） |
| style | `main.c`・`lines.c`・`lines.h` 違反 0、`tree.c` 46 → 46 |
| 速さ（host、20000 行の無関係な 2 file） | 0.13 秒（GNU 0.08 秒）。行を除く前は 3.3 秒だった |
| 実機 | 未実施 |

### 知見

- `-C 0` の context 形式では、1 行の範囲と空の範囲が同じ数で書かれ、GNU patch は GNU diff 自身の出力でも誤読する
  （例: `printf 'q\na\nb\n'` と `printf 'n\nq\nb\n'`）。POSIX の書式どおりなので diff の側では直さず、乱数の試験では `-C 0` を当てない。

### 残り

- 最小の差分が複数あるとき、どれを選ぶかは GNU と違うことがある（どちらも最小）。
