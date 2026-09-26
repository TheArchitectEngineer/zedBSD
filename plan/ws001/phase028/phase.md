# ws001-p028: split・csplit・pr

Status: cleared（2026-09-27）
Parent: [WS001](../ws.md)
Queue: なし（2026-09-27 ユーザー指示のサブエージェントの作業。main session が merge する）

## 目的

台帳（ws.md §12）の #113 split、#26 csplit、#95 pr を XCU（POSIX.1-2024）の要求に合わせる。

## 範囲

- `split [-l n] [-a n] [file [name]]`・`split -b n[k|m] ...`: 接尾辞（`aa`〜、`-a` の長さ）、尽きたら診断して状態 1（書いた file は残す）、
  空の入力は file を作らない、最後の改行の無い行、`-` は標準入力。
- `csplit [-ks] [-f prefix] [-n number] file arg...`: `/rexp/[offset]`・`%rexp%[offset]`・line number・`{num}`（line number は倍数で繰り返す）、
  検索は前の切れ目の次の行から（最初は 1 行目から）、大きさの出力（`-s` で黙る）、失敗のときは残りを 1 つの piece に書いてから
  piece を消す（`-k` で残す）、line number の減少は入力の前に拒否。
- `pr`: 全 option（`+page`・`-column`・`-a`・`-d`・`-e`・`-f`・`-F`・`-h`・`-i`・`-l`・`-m`・`-n`・`-o`・`-p`・`-r`・`-s`・`-t`・`-w`）、
  header は POSIX の書式 `"\n\n%s %s Page %d\n\n\n"`（日付は `%b %e %H:%M %Y`、file の修正時刻、標準入力と `-m` は現在時刻）、
  66 行の page、10 行以下は header と trailer 無し、段の幅は (width − (段数 − 1)) / 段数、長い行は切る、最後の page の段は均す、
  空の file は何も書かない。

範囲外: 多 byte 文字の幅、`pr -p`・`-f` の端末での停止の試験（未実施）。

## 受け入れ条件

1. host の差分試験: `plan/tools/utils/cases/{split,csplit,pr}.sh` が全件一致。pr の header の空白の数と段の間の tab/空白の違いは
   case の側で正規化する（`sed` で header の空白を 1 つに、`expand` で tab を空白に）。
2. 3 つの source が style 違反 0。
3. amd64 guest で同じ case が一致。

## 記録

### 試験（2026-09-27）

| 確認 | 結果 |
| --- | --- |
| host の差分試験 | split 13/13、csplit 21/21、pr 28/28 |
| amd64 guest `DUMP=1 sh plan/ws001/tests/guest-run.sh build/ws001/guest-p028.out split csplit pr` | 62/62 |
| style（`split/main.c`・`csplit/main.c`・`pr/main.c`） | 違反 0 |
| `pr -p`・`-f` の端末での停止 | 未実施 |
| 実機 | 未実施 |

### GNU との違い（意図したもの）

- pr の header: GNU は file 名を中央に、`Page n` を右端に置く。ここは POSIX の STDOUT の書式どおり 1 つの空白で区切る。
- pr の段の間: GNU は tab と空白で埋める。ここは空白（POSIX の「appropriate number of <space> characters」）。見た目の桁は同じ。
- `pr -d` で本文の行数が奇数のとき、GNU は page を 1 行短くする。ここは `-l` の長さを保つ（case は偶数の行数で比べる）。
- `pr -2 -d -t` の最後の行の後の空行: GNU は段組のときだけ省く。ここは 1 段と同じく書く（case から外した）。

### 残り

- 多 byte 文字の幅（LIBC-CTYPE-01）。
