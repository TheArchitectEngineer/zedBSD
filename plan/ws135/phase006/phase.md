<!-- awesome-plan project=zedbsd record=ws135-p006 -->
# ws135-p006: 全文の規約の確認と WS の完了の回帰

Status: in-progress（q656-i01、P2 generation7。規約の監査と修正は済み、WS の回帰は T2 の P2-04〜07 の結果待ち）
Disposition: normal
Parent: [WS135](../ws.md)
Queue: q656 / q656-i01

## 規約（`plan/coding-style.md` の全文）

- 監査: 読むだけの subagent（a6ddbf4cfb29a02ba）が WS135 の C の変更（d443272 からの差）を全文の規約で監査し、約 167 件（主に Succeeded の注釈の位置、非標準の
  `for` の 1 行、条件の中の関数呼び出し、3 項以上の条件の 1 行、連鎖した fallible な呼び出し、段落の空行と注釈、空疎な注釈、file-scope の宣言の役割の注釈）。
- 修正: 別の subagent（a5e15c23a3e905a23）が振る舞いを変えずに修正（log の書式の文字列は同一）。後から入った Files の open-with（`apps.c`）と settings-keys の
  prefix の部分も同じ基準で見直した。`settings-store.c`・`settings-app.c` の連鎖は単一の cleanup の label（files/places.c と同じ形）、`libkeiland/settings.c` の
  `settings_bind` は呼び出しごとの確認、probe は helper に分け success の return を最後に。
- 意図して残した物: `glass.c` の `loader_run` の `(void)argument`（同じ file の `prefetch_run` に合わせた、macro が無い file）、errno の対応の switch の各 case の注釈
  （switch の前の一つの注釈で表を説明）、WS135 が変えていない既存の行。
- 確認: zedBSD（wayland・settings・terminal・files・keiland-settings）と `make keiland-linux` warning 0、host-store 43/43・host-settings 38/38・Files host-default PASS・
  terminal-p009 PASS・Settings host-build と host-wallpaper PASS、`git diff --check`。

## 回帰（T2）

P2-04（settings-p003・settings-p007）、P2-05（settings-p004・p005・pages、volume-p005）、P2-06（terminal-p009-guest、FreeBSD の native build と host 試験）、
P2-07（files-open always・open）。T2 は 47b3418 の一つの image にまとめて流す（2026-10-04 T2）。この style の修正（下の commit）は振る舞いを変えないので、
結果が出たら Q1 の判定で WS の完了へ。
