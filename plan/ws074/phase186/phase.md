<!-- awesome-plan project=zedbsd record=ws074-p186 -->

# ws074-p186: sharp CSS text-shadow と Acid3 見出し

Status: cleared（q1009-i01、2026-10-13 23:33 JST）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074 を継承）
Related: [p100](../phase100/phase.md) の Acid3 pixel 完全一致に寄与
Prerequisites: p172〜p185 cleared、[q1008](../phase185/evidence/20261013-q1008/report.json) の差画像・paint。
Queue: [q1009-i01](../../history/queue-q1009.md) cleared。ユーザーの最大90分 Queue 自動反復指示に従い、下記 exact scope を記録して選定。

## 観測と目的

Acid3 test の `h1:first-child` は `text-shadow: rgba(192,192,192,1.0) 3px 3px`。reference は見出しの灰色の影を別の div で +3px に描く。q1008 test の paint list は h1 本体のみで影がない。CSS の property registry・style・paint に `text-shadow` がない。[CSS Text Decoration 4](https://drafts.csswg.org/css-text-decor-4/#text-shadow-property) の text shadow から、blur0・spread0 の単一 sharp shadow を有限部分 scope とする。

## q1009-i01 exact scope / clearance（最大90分）

色と2 length の sharp `text-shadow` 値（色が前/後、none、currentColor）を一般的に parse/compute し、paint で通常の文字より下にオフセット copy を描く。継承、上書き、無効値 fallback を保ち、同じ layout を使う。blur、spread、複数 shadow、inset の全面実装は別 Phase に分ける。既存 box-shadow と text paint の境界を維持。host fixture で前後の paint 順・座標・色・none・継承を確認し、warning0 build、変更 C/browser component 全文規約、固定 Acid3/Acid2 を再計測。WPT/runner/font/参照画像は変えない。

Acid3 score100/100、inventory100/100、例外0、差 **4152/480000** 非増加、Acid2差 **1865/120000** 非増加が条件。設計変更が広がる、差増加、90分到達時は uncleared とし証拠と再開条件を残す。p100全画面差0は別条件。

## q1009-i01 結果（2026-10-13 23:29〜23:33 JST）

**cleared（sharp 単一 shadow の部分 scope）**。CSS property/value/style と paint に `text-shadow` を追加。色が前/後にある2 offset、none/currentColor、継承を扱い、影の glyph copy を本文の下に描く。blur・spread・リスト・inset はこの Queue では EINVAL/未対応のまま、後続の一般 CSS scope に残す。

- `run-sharp-text-shadow-tests.py` PASS: 色・x/y +3px・shadow→文字の paint 順・子 span 継承・none。`host-css-rule-model` 44/44。host build warning/error0、`git diff --check` PASS。`clang-format` は環境に無く、変更 C/browser component 全文規約を手動確認。QEMU/別OS、main 最新 HEAD 全体の再buildは未実施。
- 固定 WPT `run-acid-tests.py --out build/ws074-acid-q1009-final`: Acid3 score100/100・inventory100/100・例外0、差 **1701/480000**（99.645625%一致、q1008の4152から2451減）。Acid2差 **1865/120000** 不変。[report](evidence/20261013-q1009/report.json)、[Acid3並列画像](evidence/20261013-q1009/acid3-side.png)、[Acid2並列画像](evidence/20261013-q1009/acid2-side.png)、前後 paint を保存。見出し影は test の paint x60/y150、reference と一致。
- 残差は左上リンク文字801画素と紫の6番目の箱の右端900画素に分離できた。p100全画面差0とWS074受入は未達。次 Queue は負 padding の扱いに限定する。

Source は独立 branch `codex/ws074-acid3` で WIP commit とし、main 作業 tree へ投影。GitHub Issue/Project publication と push は保留。`master.md` は変更しない。
