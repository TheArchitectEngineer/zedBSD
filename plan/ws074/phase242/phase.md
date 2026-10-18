<!-- awesome-plan project=zedbsd record=ws074-p242 -->

# ws074-p242: ParentNode.replaceChildren と WPT cleanup のDOM操作

Status: uncleared（q04/q05 host partial cleared、prerequisite/target等whole未達）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006
Queue: ws074-q20261018-04-i01（basic partial）

## 調査の根拠と目的

q03の固定Interop2025 testharnessで、anchor-invalid-fallback・anchor-scope・@scope群が最初のcleanupの`container.replaceChildren()`未定義でharness_errorになり、残るsubtestがNOTRUNになる。テストを書き換えず、DOMの一般的なParentNode.replaceChildrenをElement/DocumentFragment/Documentに実装し、通常の内容交換とcleanupを正しく行えるようにする。

## 有限範囲の案

最大90分。bind/mixin.cを本体とし、element.c・document.c・text.cのmethod登録、bind/internal.hのprivate宣言、必要ならbind/insertion.cのreplace-all用validationを変更する。公開browser ABI/Wayland境界は不変。既存append/prependの仕様上の別問題を同時に全面改修しない。

- variadic引数をnode/stringへ順に変換し、文字列化の例外時に交換を実行しない。新しいTextと一時FragmentをGCのrootに保持する。
- 複数引数のnodeを一時Fragmentへ移す際の重複・移動順、既存子を引数にする場合、別Documentからのadoptを規定どおりに扱う。
- 親への循環と禁止node種別、DocumentへのText/複数element等は交換前に拒否する。Documentの既存doctype/elementはreplace-allのvalidationから除外し、通常のpre-insertionの既存規則は変えない。
- 削除・挿入の既存host通知とgeneration更新を通す。MutationObserverの集約通知は既存実装の可否を確認し、未対応ならwholeの残りとして明記する。

## 検証・依存

q03で得た固定binaryと実際のcleanup失敗を依存出力とする（p189 wholeは前提にしない）。独立DOM例で0/1/複数引数、node移動/重複、fragment、文字列化例外、循環拒否、Documentの既存root置換を検証する。Chromium比較、固定WPTの`dom/nodes/ParentNode-replaceChildren.html`を補助に、公式2025のcleanup失敗群を同条件で再測定し、harness_error→実際のFAILの進行とPASSを区別する。host build warning0、変更関数のC全文review、format/diff check。意味の区切りでAcidとtarget compile。Guardrail/C全文/browser-component全文を適用。

本Phaseの計画追加は実行開始ではない。q03完了後にexact scopeと承認の適用をQueueへ記録してから実装する。Master/共有Queue/GitHubのprojectionはpending。

## 実装前の確認（2026-10-18）

[DOM Standard](https://dom.spec.whatwg.org/#dom-parentnode-replacechildren)を確認。pre-insert validityは削除予定の既存childrenをexclusion listとして受ける。既存bind_validate_insertはexclusionなしのordinary insertionであり、その契約を維持してreplace-all専用のvalidationを追加する。固定WPTの補助`dom/nodes/ParentNode-replaceChildren.html`もDocumentの既存doctype/rootを無視する条件を明示している。

MutationObserverは既存environment.cにあるが、privateのrecord生成はadded/removed各1nodeのみ。replace-allの集約recordにはsequence対応が要る。既存dom_remove/dom_insert_beforeはlive Range/Iterator・iframe・select・style generationを更新するため、それらを飛ばす直接link操作は使わない。DOM変換・一時root・validation・native通知の基本を最初の有限部分、observerの完全なbatchingを必要なら次の有限部分に分け、wholeの未達を残す。

## q04 実行開始

2026-10-18 11:22 JST、ユーザーの自走指示を上記basic partial最大90分へ適用。q03はe876e0378に全証拠を保存済み。observer batchは未達なら後続へ残す。

## q04 結果

Event ws074-p242-q04-partial-clear-20261018: [証拠](evidence/20261018-q04/README.md)。基本交換39項目・DOM24/24、official cleanup13件のharness_errorは解消、12 FAIL/1 no_completion、whole PASS増分0。補助16 callback PASS。Acid3差0/Acid2差873、hostとamd64 5source compile warning0。basic partial cleared、whole uncleared。observer batch/source removal、補助factory/iterator依存、target runtimeが残る。共有投影pending。

## q05 observer partial

Event ws074-p242-q05-start-20261018: 5008906bdの基本交換後、observerのsource removalとreplace-all batchを最大90分で実装・検証する。private APIのsequence化、node snapshotの明示root、source sibling fieldsを範囲に含む。通常insert/append/prependの仕様上の別課題は変更しない。createHTMLDocument/collection iterableは別の依存として残す。

設計確認: DOM Standard replace-allは先にoutgoing childrenをsnapshot/removeし、その後insertがadoptを行う。単一の既存子の再利用は1 target recordでremovedにも含める。observer callbackはcreation index順であり、record生成順を優先する追加queueは不要。https://dom.spec.whatwg.org/#concept-node-replace-all と #mutation-observers を確認（2026-10-18）。

## q05 結果

Event ws074-p242-q05-partial-clear-20261018: [証拠](evidence/20261018-q05/README.md)。observer24項目Chromium一致・DOM25/25、補助16→18 PASS/31 callback、13 FAILはfactory/iterator不足。公式13は12 FAIL/1 no_completionでwhole増分0。host/target compile warning0、Acid3差0/Acid2差873。observer部分cleared、whole uncleared。共有投影pending。

q05 source/証拠のWIP commit: d328827f5。q06はこの固定browser/libraryを再利用し、hashをp190 q06 summaryへ保存した。

Event ws074-p253-attribute-model-plan-20261018: [p253](../phase253/phase.md)が既存childList registration/pending queue/GC outputを利用してordinary attributesを追加する候補。replaceChildrenの既存scope/history/whole未達は維持。attributes observe flags/record pathとDOM hookのownershipを別Phaseで検証し、その後childList controlsも非悪化確認する。新Queueまで実装しない。
