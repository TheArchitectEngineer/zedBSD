<!-- awesome-plan project=zedbsd record=ws074-p242 -->

# ws074-p242: ParentNode.replaceChildren と WPT cleanup のDOM操作

Status: planned
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006
Queue: 未選定（q03のcensus完了後の候補）

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
