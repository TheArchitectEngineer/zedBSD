<!-- awesome-plan project=zedbsd record=ws074-p184 -->

# ws074-p184: loaded object の fallback DOM 描画

Status: cleared（q1007-i01、2026-10-13 23:19 JST）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074 を継承）
Related: [p100](../phase100/phase.md) の Acid3 pixel 完全一致に寄与
Prerequisites: p172/p179/p180/p181/p182/p183 cleared、[q1006](../phase183/evidence/20261013-q1006/report.json) の差画像・style/paint。
Queue: [q1007-i01](../../history/queue-q1007.md) cleared。ユーザーの最大90分 Queue 自動反復指示に従い、下記 exact scope を記録して選定。

## 観測と目的

固定 Acid3 は `object` A に 404 PNG、fallback の B に HTTP `text/html` 文書、B の fallback C に存在しない画像と `FAIL` text を持つ。q1006 test は `FAIL` を左上に描画し、reference は描かない。`frame-load.c` は B の HTML 子文書を `child_context` に作れるが、`layout/box.c` は `object` の画像が無い限り fallback 子を親の box に作る。B が実際に load 済みなら C と text は親で描画しないのが条件。

## q1007-i01 exact scope / clearance（最大90分）

最小 HTTP ページで loaded HTML object と失敗 object の fallback を切り分ける。成功した子文書を持つ `object` の fallback 子は DOM に残し、親 document の layout/paint から除く一般的な最小修正を `layout/box.c` 周辺に行う。読み込み失敗、画像 object、iframe と DOM の既存動作を維持。host fixture、`host-frame-load`、warning0 build、変更 C/browser component 全文規約、固定 Acid3/Acid2 再計測を行う。WPT/runner/font/参照画像は変えない。

Acid3 score100/100、inventory100/100、例外0、差 **12600/480000** 非増加、Acid2差 **1865/120000** 非増加が条件。実際の子文書が作られていない、他 module の大きな設計変更が必要、差増加または90分到達時は uncleared、証拠と再開条件を残す。p100全画面差0は別条件。

## q1007-i01 結果（2026-10-13 23:16〜23:19 JST）

**cleared**。最小実 HTTP host fixture で loaded HTML object の fallback が修正前に描画されることを再現。`dom_element.child_loaded_epoch` を実子文書 install 時に記録し、現在の load epoch の子文書を持つ object の fallback box を `layout/box.c` で作らない。HTTP 404 の object は `frame-load.c` で成功子文書を作らず、fallback を描く。DOM 子と `iframe`、画像 object の経路は保持。

- `python3 plan/ws074/tests/run-object-fallback-tests.py`: 修正前 FAIL、修正後 PASS。loaded object の fallback は親 paint から除外、失敗 object の fallback は描画、両方 DOM に残る。`host-frame-load` PASS。host build warning/error0、`git diff --check` PASS。`clang-format` は環境に無く、変更 C と browser component 全文規約を手動確認。QEMU/別OS、main 最新 HEAD 全体の再buildは未実施。
- 固定 WPT `run-acid-tests.py --out build/ws074-acid-q1007-final`: Acid3 score100/100・inventory100/100・例外0、差 **12417/480000**（97.413125%一致、q1006の12600から183減）、Acid2差 **1865/120000** 不変。[report](evidence/20261013-q1007/report.json)、[Acid3並列画像](evidence/20261013-q1007/acid3-side.png)、[Acid2並列画像](evidence/20261013-q1007/acid2-side.png)、前後 paint/layout を保存。左上の黒い `FAIL` は消え、透明な `FAIL` は paint list に残るが画素を変えない。
- 上部の赤い `YOU SHOULD NOT SEE...` は `:visited` と関係する。p100全画面差0とWS074受入は未達。次 Queue は独立の原因に限定する。

Source は独立 branch `codex/ws074-acid3` で WIP commit とし、main 作業 tree へ投影。GitHub Issue/Project publication と push は保留。`master.md` は変更しない。
