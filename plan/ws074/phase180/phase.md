<!-- awesome-plan project=zedbsd record=ws074-p180 -->

# ws074-p180: iframe の子を親ページの描画から除く

Status: cleared（q1003-i01、2026-10-13 22:54 JST。iframe 子の親ページ描画を除去）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074 を継承）
Related: [p100](../phase100/phase.md) の Acid3 pixel 完全一致に寄与
Prerequisites: p172 cleared、p179 cleared、[q1002](../phase178/evidence/20261013-q1002/report.json) の差画像・layout/paint。p178 whole は uncleared だが、p180 の原因調査の前提は q1002 の実測出力。
Queue: [q1003-i01](../queue.md) cleared。2026-10-13、このチャットでユーザーが下記最大90分の exact scope に「q1003 の範囲を承認する」と回答。

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

## q1003-i01 結果（2026-10-13 22:49〜22:54 JST）

**cleared**。`layout/box.c` の `box_build_element()` は iframe を通常要素として子 box と擬似要素まで生成していた。HTML iframe の子は親文書の表示内容でないため、iframe 自体の box と DOM を保持して子 box 生成だけを止めた。`display:contents` でも子 box を親へ移さない。`page/frame-load.c` は変更せず、既存の非同期 frame load 回帰を確認した。

- 新規 `host-iframe-fallback.c` は修正前に FAIL、修正後 PASS。0×0、80×40、`display:contents`、parser の生テキスト、DOM に追加した子 element を検査。DOM の子は残り、親 layout/paint に出ない。通常寸法の iframe box は 80×40 を保持。
- host build `sh plan/ws074/tests/host-build.sh plain` は warning/error 0。`run-frame-load-tests.py --program build/ws074-host/plain/host-frame-load` は **52/52 pass**。`git diff --check` pass。`clang-format` は環境に無く、変更 C と browser component の全文規約を手動確認。QEMU/別OSと main 最新 HEAD 全体の build は未実施。
- 固定 WPT の `run-acid-tests.py --out build/ws074-acid-q1003`: Acid3 score100/100、inventory100/100、例外0、全画面差 **42921/480000**（90.9404%→91.0581%一致、差43486→42921で565減）。Acid2差 **1865/120000** 不変。[report](evidence/20261013-q1003/report.json)、[Acid3並列画像](evidence/20261013-q1003/acid3-side.png)、[Acid2並列画像](evidence/20261013-q1003/acid2-side.png)、前後 layout/paint を保存。
- Acid3 test paint から iframe 内の `FAIL` 2 item が消失。参照用 object と score overlay に由来する別の `FAIL` は残る。本文 y=373.50 と reference y=350 の差は **不変**。当初の押し下げ原因という仮説は棄却し、次の別原因として form/inline line の高さを調べる。p100 の全画面差0、Acid2 の現行 exact は未達。

Source は独立 branch `codex/ws074-acid3` で WIP commit とし、main の作業 tree へ投影。GitHub Issue/Project publication と push は設定どおり保留。`master.md` は変更しない。
