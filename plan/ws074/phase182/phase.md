<!-- awesome-plan project=zedbsd record=ws074-p182 -->

# ws074-p182: HTTP stylesheet 応答の MIME 判定

Status: planned（q1004 の残差から追加。実装 Queue 未選定）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074 を継承）
Related: [p100](../phase100/phase.md) の Acid3 pixel 完全一致に寄与
Prerequisites: p172/p179/p180/p181 cleared、[q1004](../phase181/evidence/20261013-q1004/report.json) の差画像・style/paint。
Queue: 未選定・未承認。下記 q1005 は提案であり、実装を始めない。

## 観測と設計の前提

固定 WPT の Acid3 `test.html` は `<link rel="stylesheet" href="empty.css">` を持つ。`empty.css.headers` は `Content-Type: text/html` を指定し、本文の CSS コメントも、適用されると `<h1>` が赤くなることを明記する。q1004 の test style/paint では `<h1>` が赤、reference は黒。[CSSOM の stylesheet fetch](https://drafts.csswg.org/cssom/#fetching-css-style-sheets) と [HTML Standard の link stylesheet 処理](https://html.spec.whatwg.org/multipage/links.html#link-type-stylesheet) は、非 quirks 文書では対応する styling MIME 以外の HTTP 応答を stylesheet として使わない。現行 `page/sheets.c` の非同期 `sheets_arrived()` は成功 HTTP status の body を MIME 判定せず `sheets_parse()` へ渡す。`net_response.content_type` は取得済み。

## 目的・完了条件

非 quirks HTML 文書の HTTP/HTTPS stylesheet 応答が CSS MIME (`text/css`、パラメータ付き・ASCII 大文字小文字違いを含む) なら従来どおり適用し、`text/html` 等の非 CSS MIME なら適用しない。Content-Type 欠落時と quirks 文書の既存経路、ローカル file/data 経路は今回の変更で壊さない。HTML DOM、link 要素、HTTP load 状態は保持する。

- 実 HTTP 応答を使う焦点を絞った host 試験で、CSS MIME 適用・非 CSS MIME 非適用と DOM 保持を確認。host build warning0、変更 C と browser component の全文規約確認。
- 固定 WPT・同一 runner/font の Acid3 score100/100、inventory100/100、例外0を維持し、差 **18596/480000** を増やさない。Acid2差 **1865/120000** を増やさない。h1色と paint の前後を確認。改善しない場合は uncleared。
- p100 の全画面差0は別条件。QEMU/別OS、main 現行 HEAD 全体の再buildは実施時に記録。

## q1005-i01 exact scope 案（最大90分）

`page/sheets.c` の `sheets_arrived()` が受けた `net_response.content_type` を最小 HTTP ページで切り分ける。原因が確認できた場合のみ、非 quirks HTML 文書で MIME essence が `text/css` 以外の応答を stylesheet として解析・適用しない最小修正を行う。`text/css; charset=...`、大小文字、`text/html`、Content-Type 欠落、既存 quirks 経路を host 回帰で確認する。WPT 本文・`.headers`・参照画像・runner 比較方式は変更しない。warning0 build と固定 Acid3/Acid2 再計測を保存する。

原因が異なる、MIME の処理に他 module の大きな設計変更が必要、差が増える、または90分に達した場合は attempt を uncleared として原因・証拠・再開条件を残す。quirks/cross-origin の全面的な MIME 方針やローカル file MIME の拡張は今回の Queue に含めない。実装にはユーザーの exact-scope 承認と Queue active 化が必要。

2026-10-13 q1004 後: Acid3 h1 が赤、reference が黒。`empty.css` の実ヘッダ text/html と `sheets_arrived()` の MIME 無判定を新しい原因候補として分離。p181 cleared と p100 全画面条件は変えない。
