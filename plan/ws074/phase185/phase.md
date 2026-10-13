<!-- awesome-plan project=zedbsd record=ws074-p185 -->

# ws074-p185: Acid3 の inline bucket と後続本文の 0.5px 差

Status: cleared（q1008-i01、2026-10-13 23:25 JST）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074 を継承）
Related: [p100](../phase100/phase.md) の Acid3 pixel 完全一致に寄与
Prerequisites: p172/p179/p180/p181/p182/p183/p184 cleared、[q1007](../phase184/evidence/20261013-q1007/report.json) の layout/paint 差。
Queue: [q1008-i01](../../history/queue-q1008.md) cleared。ユーザーの最大90分 Queue 自動反復指示に従い、下記 exact scope を記録して選定。

## 観測と目的

固定 Acid3 は得点と本文の top/baseline が reference より0.5px上にあり、本文の全行と枠下辺に差が伝播する。q1007 の test は `#result` top229.50、instructions top349.50、reference の得点 top230、instructions top350。WPT の test CSS comment は bucket の line と `#result` の margin を数式で示す。`layout/inline.c` の line extent/vertical-align と `layout/block.c` の次 block placement を最小ページで追い、一般的な 0.5px 欠陥を見分ける。

## q1008-i01 exact scope / clearance（最大90分）

bucket の six inline-block、font-size0 line-height0、vertical-align:2em、padding/border、`#result` 負 margin を最小 fixture で再現し、どの layout 単位で0.5pxが加わるか記録する。一般的な inline line extent または block placement の誤りと確認できた場合だけ最小修正し、host geometry 回帰、warning0 build、変更 C/browser component 全文規約、固定 Acid3/Acid2 再計測を行う。WPT/runner/font/参照画像は変えない。曖昧な基準に合わせる定数補正はしない。

Acid3 score100/100、inventory100/100、例外0、差 **12417/480000** 非増加、Acid2差 **1865/120000** 非増加が条件。原因が確定しない、範囲外の設計変更が必要、差増加または90分到達時は uncleared、証拠と再開条件を記録。p100全画面差0は別条件。

## q1008-i01 結果（2026-10-13 23:22〜23:25 JST）

**cleared**。`font:0/0` による line strut を最小ページで再現し、修正前は実際の font rasterizer が最小1px の metrics を返し、zero font の strut が上0.5px/下-0.5pxとなるため inline-block で伸びた行の高さが161.5pxだった。`layout/inline.c` の font-size0 metrics をゼロにして、CSS のゼロサイズ strut を維持。定数で結果画像だけを合わせていない。

- `run-zero-font-inline-tests.py` は修正前 FAIL（行高161.5、次 block top161.5）、修正後 PASS（162/162）。`host-position` 25/25。host build warning/error0、`git diff --check` PASS。`clang-format` は環境に無く、変更 C と browser component 全文規約を手動確認。QEMU/別OS、main 最新 HEAD 全体の再buildは未実施。
- 固定 WPT `run-acid-tests.py --out build/ws074-acid-q1008-final`: Acid3 score100/100・inventory100/100・例外0、差 **4152/480000**（99.135%一致、q1007の12417から8265減）。Acid2差 **1865/120000** 不変。[report](evidence/20261013-q1008/report.json)、[Acid3並列画像](evidence/20261013-q1008/acid3-side.png)、[Acid2並列画像](evidence/20261013-q1008/acid2-side.png)、前後 layout と paint を保存。bucket line162、結果 top230、本文 top350 は reference と一致。
- 残差は h1 text-shadow、左上の未訪問リンク文字、若干の他の paint 差。p100全画面差0とWS074受入は未達。

Source は独立 branch `codex/ws074-acid3` で WIP commit とし、main 作業 tree へ投影。GitHub Issue/Project publication と push は保留。`master.md` は変更しない。
