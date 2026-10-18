<!-- awesome-plan project=zedbsd record=ws074-p253 -->

# ws074-p253: MutationObserverのordinary attribute records

Status: planned
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006
Queue: 未選定（q16後の候補）

## Objective / evidence

[p190](../phase190/phase.md) strict waitのTestRendered controlが[p209 q16](../phase209/evidence/20261018-q16/README.md) producer後も停止。Event dispatchとclass削除は成功し、attributes通知だけが無い。現registrationはchild_list/subtreeしか保持せず、observe optionsのattributesはvalidation-only。WPT test-wait.jsはattributes observerで解除後の2RAFを開始するので、native一般機能として直す。

## First finite design candidate

普通のnull-namespace属性のset/remove/reflection/classList/style等を共通DOM mutatorから通知し、binding層のobserve attributes/subtree/oldValue/filterに従うrecordを生成する。特定のclassやWPTをspecial-caseしない。private DOM document hookはopaqueでbinding header依存をDOMへ入れず、owner windowのdetach/managed traceと整合する。旧値/attributeName/targetがmutation後やrecord allocation時のGCで失われないrootを保持し、recordを完全構築して既存microtask pending queueへappendする。new/set-same/remove/no-op、subtree/非subtree、empty filter/oldValue、takeRecords/disconnect、複数observer/childList混在・child context/GCを検証する。

[DOM Standard mutation record](https://dom.spec.whatwg.org/#concept-mo-queue-mutation-record)と[attribute changes](https://dom.spec.whatwg.org/#concept-element-attributes-change)を2026-10-18参照。filterのsequence変換/namespace条件とVM ownershipを実コードで確認してexact scopeを選定する。multiple target registration/characterData/namespace mutation等の既存未実装は明示し、全MutationObserverを完成したとはしない。p242のreplaceChildren/childList verified outputを利用し、whole clearanceは前提にしない。

Guardrail/C全文/browser component全文適用。候補sourceはdom/dom.h・dom/node.c、bind/internal.h・window.c・environment.c中心、public browser ABI/Wayland/他WS/WPT/strict runner条件は不変。新Queue選定までsource生成しない。

## Acceptance / resume

p209 q16 frozen producerのscoped output上でordinary attribute mutationの独立Chromium/DOM・record payload/flags/GC/lifetimeを確認しstrict controls全15を通す。最新のcombined final sourceで全harness1491/static1561のq14 old whole491/839維持・全callback診断、host/target warning0/C全文format/Acid非増加を行う。p209のq16 unclearedを過去に遡ってclearせず、新attemptでproducerを再検証し、そのoutput後にp190 actual48へ進む。full WS/target/p100は別関門。

Event ws074-p253-planned-from-q16-20261018: q16のknown unmet attributesを独立native featureへ分離。p209コードの必要outputは限定preflightとして保持、required15/global未達を隠してPhase/itemをclearしない。q16終端と新Queueでの再開をQueue/historyへ記録し、p209/p190/WSへ構造eventを配送。共有projection pending。
