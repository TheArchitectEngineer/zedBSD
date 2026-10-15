<!-- awesome-plan project=zedbsd record=ws248-p002 -->

# ws248-p002: AX211 の driver の規約の全文の見直し

Status: planned（2026-10-15、q1201 の後半で P2）
Parent: [WS248](../ws.md)
Approval: 2026-10-15 ユーザー（ws.md の原文）

## 範囲

不要な関数の前方宣言の削除（intel-ax211.c ほか）と、plan/coding-style.md の全文への適合。書き方だけで動作は変えない。

## 確かめ

style-check.py、build warning 0、AX211 の host 試験、preprocess の後の比較で差が宣言の削除・並べ替えなど意図した物だけ。

## 結果

（未記入）
