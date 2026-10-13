<!-- awesome-plan project=zedbsd record=ws074-p180 -->

# ws074-p180: iframe の子を親ページの描画から除く

Status: planned（q1002 の残差から追加。実装 Queue 未選定）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074 を継承）
Related: [p100](../phase100/phase.md) の Acid3 pixel 完全一致に寄与
Prerequisites: p172 cleared、p179 cleared、[q1002](../phase178/evidence/20261013-q1002/report.json) の差画像・layout/paint。p178 whole は uncleared だが、p180 の原因調査の前提は q1002 の実測出力。
Queue: 未選定・未承認。下記 q1003 は提案であり、実装を始めない。

## 目的と完了条件

Acid3 `test.html` の `<iframe src="empty.png">FAIL</iframe>` と `<iframe src="empty.txt">FAIL</iframe>` の子テキストが親ページに描かれる一般的な欠陥を、最小ページで確認して修正する。[HTML Standard の iframe 定義](https://html.spec.whatwg.org/multipage/iframe-embed-object.html#the-iframe-element)では iframe の子孫は表示内容を表さず、通常の fallback content として描画しない。子の DOM を削除・改変せず、iframe のロード/イベントも変えない。

- 0×0 と通常寸法の iframe 内のテキスト・子要素が親の layout/paint に出ない。DOM の子は保持し、iframe 自体の寸法・既存 frame 処理を維持する。
- 固定 WPT、同一 runner/font、Acid3 800×600・settle 15000 ms で得点100/100、inventory100/100、例外0を維持し、全体差 **43486/480000** を増やさない。Acid2 差 **1865/120000** も増やさない。`FAIL` の paint item の消失と本文位置を実測し、改善しない場合は uncleared とする。
- 変更箇所の host build warning0、焦点を絞った iframe host 試験、C全文と browser component 全文規約を確認する。別 OS/QEMU は実施時に記録する。

## q1003-i01 の exact scope 案（最大90分）

`libbrowser/layout/box.c` の `box_build_element()` が iframe を通常の inline 要素として扱い、子 text box を生成している経路を調べる。`page/frame-load.c` と frame 関連試験を読み、子の DOM・load/event を保持したまま layout/paint に子を出さない一般的な修正に限る。HTML/CSS の fixture と host 試験で修正前後を比べ、同じ固定 Acid3/Acid2 の画像・score・例外・差画素を保存する。WPT 本文・参照画像・runner 比較方式は変更しない。

原因が異なる、複数 module/iframe 実装の設計変更が必要、差が増える、または90分に達した場合は attempt を uncleared として原因・証拠・再開条件を残す。p100 全画面差0や p178 whole の clearance を本 Phase の条件にしない。ユーザーの exact-scope 承認と Queue active 化が実装の前提。

## 出所・設計変更

2026-10-13 q1002 後: [test paint/layout](../phase178/evidence/20261013-q1002/acid3-test.paint) に `FAIL` の text item が2個あり、2つの iframe の子として test layout に残る。test の本文は y=373.50、reference は y=350。iframe 子の描画が本文の段を押し下げる原因候補。p178 の得点位置を直した後の残差43486のうち、別の局所領域として分離した。WS074 の Phase table と再開点へ投影し、p178/q1002の結果・p100の全画面条件は変えない。
