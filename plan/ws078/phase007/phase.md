<!-- awesome-plan project=zedbsd record=ws078-p007 -->

# ws078-p007（提案）: 注釈の `zdesktop` を一度に置き換える

Status: planning（2026-10-01 の提案。main が ws.md の Phase の表に入れるまで提案のまま）
Disposition: normal
Parent: [WS078](../ws.md)
Queue: なし
依存: **WS104 の p001〜p007 が main に入った後**（WS104 の patch は `zdesktop` を含む文脈の行を持つ）。`userland/desktop` と `platform/amd64/vmunix.mk` を変える
worktree が無い静かな時点（ws.md「改名は … 静かな時点で main か 1 つの agent が一度に」、main の判断「WS074・WS081 の作業が落ち着いた後、1 つの agent で一斉に」）。

## 範囲

[guide.md](../guide.md) §2.3 の B1・B4・B9: plan の外の注釈の `zdesktop`（2026-10-01 で 678 行 / 175 file。C の file では全て注釈）、「the zedBSD desktop」（10 行）、
`keiland-x11` の注釈の「zdesktop-x11」。

除く: `userland/desktop/ime/`（29 行 / 7 file、IME は人間が作業中）、`plan/` の全て（記録と試験、試験の log の印 `ZWL` 等と `/tmp/zdesktop.log`）、
make の変数（`DYNAMIC_ZDESKTOP_*`・`LIBZDESKTOP_*`、p008 提案）、`zed-*-v1` の file 名（p008 提案）、`userland/retro/`。

## 手順（2026-10-01 追記）

1. 始める前の確かめ（main）: `git worktree list` と master の割り当て、`git log --oneline -1 -- plan/ws104` で WS104 の p007 まで入ったこと。
2. 数え直し（guide.md §5.1 の 1〜3 行目）。値を記録する。
3. before の build（guide.md §5.3 の 1〜2 行目、`<W>` = `ws078-p007`）。
4. 置き換え。語の既定（文脈で読み、文が通ることを確かめる）:
   - 「zdesktop's」→「the compositor's」、「zdesktop」→「the compositor」（文頭は「The compositor」）。
   - 「the zedBSD desktop」→「the desktop」、「zedBSD zdesktop:」（shader の先頭）→「Keiland compositor:」。
   - 「zdesktop-x11」→「keiland-x11」。
   - 機械的な置き換えの後、`git diff` を file ごとに読んで文の誤り（「the the compositor」、冠詞の重なり、大文字）を直す。**1 行を 1 行で置き換え、行を足し引きしない**。
   - shader（`userland/desktop/wayland/shaders/*.frag`・`*.vert`・`regenerate.py`）は注釈だけ変え、`regenerate.py` は流さない。
5. after の build と object の比較（guide.md §5.3 の残り）。差が 0 であること。
6. style: 変えた C の file に `python3 plan/tools/style-check.py <file>…`（新しい違反 0。注釈の行の長さの規則に注意: 置き換えで行が長くなったら、その段落の中で
   折り返しを直す。その時は行の数が変わるので、object の比較で同じであることを確かめる）。`git diff --check`。
7. boot test（guide.md §5.4）。
8. 記録と、各 agent への通知（main）: 新しい語と、IME の 29 行が残ること。

## 完了の条件

- `git grep -I -c -i 'zdesktop' -- ':!plan' ':!.internal' ':!userland/desktop/ime'` の数が、make の変数（p008 の分）と header の file 名の参照（p008 の分）だけ。
- 前後の object が byte で同じ（`object-diff.txt` が 0 行）、build の warning 0、style の新しい違反 0、`boot-test: PASS`（PNG をユーザーに見せる）。
