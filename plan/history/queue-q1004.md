<!-- awesome-plan project=zedbsd record=q1004 -->

# q1004 / WS074 HTML `type` 属性 selector

Status: finished（2026-10-13 22:58〜23:02 JST、最大90分）
Approval: このチャットで [p181 の exact scope](../ws074/phase181/phase.md) を示した質問へユーザーが「q1004 の範囲を承認する」と回答。承認はこの q1004-i01 に限る。

q1004-i01 / [ws074-p181](../ws074/phase181/phase.md): **cleared**。Acid3 の `<input type=HIDDEN>` が UA `input[type=hidden]` と一致せず form に24pxの行を生成した。`css/parser.c`・`css/internal.h`・`css/cascade.c` で HTML `type` 値の既定 ASCII case-insensitive 比較と、明示的 `s`/`i` flag を区別。SVG と他属性の既存比較を維持。最小 host fixture は修正前 FAIL、修正後 PASS（`type` の6比較演算子、`s`/`i`、他属性、SVG）。`host-css-rule-model` 44/44、前 Phase の `host-iframe-fallback` pass、host build warning/error0。固定 WPT の Acid3 score100/100・inventory100/100・例外0、RGB差 **18596/480000**（q1003の42921から24325減、96.1258%一致）、Acid2差 **1865/120000**（不変）。form line24→0、本文 y373.50→349.50。[report](../ws074/phase181/evidence/20261013-q1004/report.json)、Acid3/Acid2 並列画像、修正前後 layout/paint。`git diff --check` pass。`clang-format` は環境に無く、C全文・component全文を手動確認。QEMU/別OSと main 最新 HEAD 全体の再buildは未実施。

本文の reference y350 との残り0.5px差や上部の見えてはいけない文字、h1とslashの色、他の pixel差が残る。p100の全画面pixel差0、Acid2現行exact、WS074 acceptanceは未達。独立 branch の成果は main 作業 tree に投影してコミットはメインセッションとの調整待ち。GitHub Issue/Project publication と push は保留。`master.md` は未変更。次 Queue は未承認。
