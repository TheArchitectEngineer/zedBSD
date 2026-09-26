<!-- awesome-plan project=zedbsd record=ws045p004 -->

# ws045-p004: sed の script の GNU 拡張

Phase ID: `ws045-p004`
Parent: [WS045](../ws.md)
Status: cleared
Queue: なし（サブエージェント）
依存: ws045-p003

## 目的

GNU sed の script の拡張を足す。binutils の configure（`\t`）、emacs の configure（`[\t ]`）など、実際の configure が使うものを先に。

## 範囲

- regex の前処理: bracket の中を含めて `\t`・`\n`・`\f`・`\v`・`\a`・`\r`、`\dNNN`・`\oNNN`・`\xHH`、`\cX`。`\``・`\'`（buffer の始めと終わり）。
- 置換: `\U`・`\L`・`\u`・`\l`・`\E`、`\n` と escape（`\t` など）。
- address: `0,/re/`、`first~step`、`addr,+N`、`addr,~N`、`/re/M`。
- s の flag: `M`（`^`・`$` が各行）、`e`（結果を command として走らせる）。
- command: `Q [N]`、`q N` と `Q N` の終了 status、`T label`、`F`、`z`、`W file`、`R file`、`e [command]`、`v [version]`、`l N`、
  `a`・`i`・`c` の一行形の escape（GNU は `a\ttext` の `\t` を tab にする）。
- ERE の後方参照（`-E` の `\1`、TRE が扱えるか確かめる）。

## 受け入れ

- `util-diff.py --cases plan/ws045/tests/cases --gnu --only sed` が全件一致。WS043 の POSIX の case が全件一致。style-check 0、warning 0。

## 実装（2026-09-27）

- `compile.c`:
  - GNU の byte を作る escape（`\a`・`\f`・`\n`・`\r`・`\t`・`\v`・`\dNNN`・`\oNNN`・`\xHH`・`\cX`、`make_escape`）を regex（bracket の中も。`[\t ]`・`[^\n]`）、
    置換、`y` の文字列、`a`・`i`・`c` の text で byte に。regex の中では byte は regex の意味を保つ（GNU 4.9 と同じく `\x2e` は任意の 1 文字。backslash だけ escape）、
    置換では `&` と backslash が文字そのもの。`\``・`\'` は `^`・`$` に。
  - address: `0,/re/`（`SED_ADDRESS_ZERO`、範囲が最初の行の前から開いている。`0` が他と組むと「invalid usage of line address 0」）、`first~step`（`SED_ADDRESS_STEP`）、
    `addr,+N`（`SED_ADDRESS_PLUS`）、`addr,~N`（`SED_ADDRESS_MULTIPLE`）、`/re/M`（`REG_NEWLINE`）。
  - s の flag: `M`/`m`（`REG_NEWLINE`）、`e`（結果を command として走らせる）。
  - command: `Q [N]`、`T label`、`F`、`z`、`W file`、`R file`（file 名ごとに 1 つの reader を共有し、最初の読みで開く）、`e [command]`、`v [version]`。
    `--sandbox` は `e`・`R`・`W`・`s///e` も拒む。
- `execute.c`:
  - 置換の `\U`・`\L`・`\u`・`\l`・`\E`（`struct case_state`、`&` と `\1`〜`\9` にも効く）、`\0`（一致の全体）。
  - `+N`・`~N` の範囲（始まりで終わりの行を計算。`~N` は始まりより後の次の倍数、GNU と同じ）、`first~step`、`0,/re/`（`-s` では file ごとに開き直す）。
  - `Q`（pattern space を書かずに終わる、status 付き）、`T`（置換が無ければ分岐、あれば flag を戻す）、`F`（現在の行の file 名、標準入力は `-`）、`z`、`W`（最初の行）、
    `R`（行を読んで cycle の終わりに書く queue へ。queue が行を解放する）、`e command`（出力を今書く。`-i` なら file へ）、`e`（pattern space を走らせて出力で置き換える。
    最後の改行を落とす）、`s///e`。command の実行は `popen`（zedBSD の libc にある）。

## 結果（2026-09-27）

| 検証 | 結果 |
| --- | --- |
| `python3 plan/tools/utils/util-diff.py --bin build/ws045/bin --cases plan/ws045/tests/cases --gnu --only sed`（GNU sed 4.9） | **174/174**（p003 の 129 件に script の拡張の case を 45 足した） |
| `python3 plan/tools/utils/util-diff.py --bin build/ws045/bin`（WS043 の POSIX の case） | **492/492** |
| `python3 plan/tools/style-check.py userland/base/sed/*.c userland/base/sed/sed.h` | 0 |
| `sh plan/ws045/tests/target-check.sh userland/base/sed/*.c` | amd64・i386 とも warning 0 |
| image の build・guest | 未実施（p008） |

## 制限

- `M` の regex では TRE の `REG_NEWLINE` により `.` が改行に一致しない（GNU は一致する）。
- `\``・`\'` は `^`・`$` に置き換えるので、`M` の regex の中では buffer の端ではなく行の端になる。
- `v` は version を比べない。
