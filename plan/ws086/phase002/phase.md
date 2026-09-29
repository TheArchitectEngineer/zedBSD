<!-- awesome-plan project=zedbsd record=ws086-p002 -->

# ws086-p002: 実装と host・guest の試験（GNU ls の出力との比較）

Status: in-progress
Disposition: normal
Parent: [WS086](../ws.md)
Queue: main が subagent（worktree `wt/ws086`）へ依頼（2026-09-29、p001 の merge の後）
Approval: ユーザー（2026-09-29）「lsの結果が1つずつ改行されています。GNU lsと同じにしたいです。」

## 範囲と受け入れ

- [p001 の設計](../phase001/phase.md)の「含める」を `userland/base/ls/main.c` に実装する。
- host: GNU ls 9.7 と我々の ls を、端末と pipe、`LC_ALL=C` と `C.UTF-8` で byte 単位に比べ、一致する。
- 回帰: `plan/tools/utils/util-diff.py`（POSIX の case）と `ls-compare.sh`（旧 ls との pipe の比較、差は D7・D9 だけ）。
- guest（amd64）: 端末で `ls` が列に並ぶこと、pipe で 1 行に 1 つ。Terminal の画面。
- 判断 1（locale）はユーザーに確認中。ls は GNU と同じく locale に従う（main 2026-09-29）。

## 進み具合

- 2026-09-29: 着手。GNU の追加の観察: `TABSIZE` は幅を使う format のときだけ読み（空も不正として警告）、`POSIXLY_CORRECT` に
  関わらない。`-w`・`-T`・`COLUMNS`・`TABSIZE` の数は基数 0（`010` は 8）、`-w`・`COLUMNS` の桁あふれは 0（無制限）、`-T` の
  桁あふれは `ls: invalid tab size: 'X': <strerror(EOVERFLOW)>` で 2。`-C`・`-x` で幅 0 は `-m` と同じ流れで区切りが空白。
  `'` の後は `$'…'` を閉じた扱い（`'a'$'\001'\''b'`）。`-F` のとき `@` を含む名前も quote（`*=>@|` が quote の対象に加わる）。
  directory の見出しは `:` を含むと quote。

## Resume point

`userland/base/ls/main.c` の書き直し（作業中）。次は host の build と比較の script（`plan/ws086/tests/compare-gnu.sh`）。
