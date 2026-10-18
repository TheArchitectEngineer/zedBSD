<!-- awesome-plan project=zedbsd record=ws074-p253 -->

# ws074-p253: MutationObserverのordinary attribute records

Status: uncleared（q17 scoped partial cleared、whole API/standalone等は未達）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006
Queue: ws074-q20261018-17-i01（ordinary attributes partial）

## Objective / evidence

[p190](../phase190/phase.md) strict waitのTestRendered controlが[p209 q16](../phase209/evidence/20261018-q16/README.md) producer後も停止。Event dispatchとclass削除は成功し、attributes通知だけが無い。現registrationはchild_list/subtreeしか保持せず、observe optionsのattributesはvalidation-only。WPT test-wait.jsはattributes observerで解除後の2RAFを開始するので、native一般機能として直す。

## First finite design candidate

普通のnull-namespace属性のset/remove/reflection/classList/style等を共通DOM mutatorから通知し、binding層のobserve attributes/subtree/oldValue/filterに従うrecordを生成する。特定のclassやWPTをspecial-caseしない。private DOM document hookはopaqueでbinding header依存をDOMへ入れず、owner windowのdetach/managed traceと整合する。旧値/attributeName/targetがmutation後やrecord allocation時のGCで失われないrootを保持し、recordを完全構築して既存microtask pending queueへappendする。new/set-same/remove/no-op、subtree/非subtree、empty filter/oldValue、takeRecords/disconnect、複数observer/childList混在・child context/GCを検証する。

[DOM Standard mutation record](https://dom.spec.whatwg.org/#concept-mo-queue-mutation-record)と[attribute changes](https://dom.spec.whatwg.org/#concept-element-attributes-change)を2026-10-18参照。filterのsequence変換/namespace条件とVM ownershipを実コードで確認してexact scopeを選定する。multiple target registration/characterData/namespace mutation等の既存未実装は明示し、全MutationObserverを完成したとはしない。p242のreplaceChildren/childList verified outputを利用し、whole clearanceは前提にしない。

Guardrail/C全文/browser component全文適用。候補sourceはdom/dom.h・dom/node.c、bind/internal.h・window.c・environment.c中心、public browser ABI/Wayland/他WS/WPT/strict runner条件は不変。新Queue選定までsource生成しない。

## Acceptance / resume

p209 q16 frozen producerのscoped output上でordinary attribute mutationの独立Chromium/DOM・record payload/flags/GC/lifetimeを確認しstrict controls全15を通す。最新のcombined final sourceで全harness1491/static1561のq14 old whole491/839維持・全callback診断、host/target warning0/C全文format/Acid非増加を行う。p209のq16 unclearedを過去に遡ってclearせず、新attemptでproducerを再検証し、そのoutput後にp190 actual48へ進む。full WS/target/p100は別関門。

Event ws074-p253-planned-from-q16-20261018: q16のknown unmet attributesを独立native featureへ分離。p209コードの必要outputは限定preflightとして保持、required15/global未達を隠してPhase/itemをclearしない。q16終端と新Queueでの再開をQueue/historyへ記録し、p209/p190/WSへ構造eventを配送。共有projection pending。

## q17 exact partial design（2026-10-18 14:12 JST、15:42 JSTまで）

VM vm_iter_start/nextを使いfilter iterableのstep→DOMString変換を順に行い、別のprivate snapshot arrayをrootして持つ。WebIDL sequenceはObjectのみ、undefined optionはmissing、dictionary membersはlexicographic Get順、attributes省略+old/filter指定はimplicit true、explicit falseとの矛盾はTypeError。characterData flagsのvalidationは行うが通知機能を追加したとはしない。

DOM document末尾へprivate attribute callbackを追加（既存offsetを保つ）。bind_frames_attach/releaseがopaque ownerへ接続/解除し、document.view/managed contextのtraceを使いlate detached deliveryを拒否。ordinary dom_element_set_attribute/remove_attributeだけがcommit後に通知し、setter/reflection/classList/CSSOMの共通経路を覆う。parser/namespace add_attribute_uriはpublication前にVM allocationをしない現状のまま。namespace/Attr-node APIsはこのpartialでは通知しないと明示する。

DOM commitからcallback entryまではVM allocationを入れず、callbackが旧name/value/targetをrootしてからrecordを構築する。観測targetの祖先/subtreeとfilterで選別し、oldValueフラグ別に独立recordを既存pending queueへ入れる。empty listは下記改訂どおりnative NodeList coreを使い、既存childList payloadの未達は残す。observe変換完了まで旧registrationを保持し、published filterはprimary rootまたはmanaged traceで保護、disconnect/reobserveで解放する。

new/set-same/remove/no-op、oldValue/filter/subtree/dictionary options/iterable conversion、takeRecords/disconnect、normal多observer/childList混在・own child context/GCをChrome/DOM比較。full MutationObserver（multi-target/characterData/namespaces/record IDL・reentrant delivery等）の完成はclaimしない。q16とnew attribute sourceを含む同じfinal pairでall1491/static1561を一回照合、old whole491/839維持と全遷移を記録してp209/p190各新attemptへ渡す。C全文/Guardrail/browser standard・host/target warning0/Acidを適用。

Event ws074-p253-q17-selected-20261018: actual sourceからhook ownerがwindow.cでなくframe.c、namespace addのallocation invariantを確認してscopeを確定。q16限局preflightをprerequisiteとして再利用し、属性対応前の不要なglobal再実行はしない。元q16未達は保持し、全体はcombined final sourceで判定する。

Event ws074-p253-native-list-and-owner-design-20261018: precode確認で既存environment_node_listがArrayへconstructor名を付ける形と判明。新attribute recordではこれを再利用せず、collection.cのnative NodeList prototype/brand/index/item/traceにstatic-empty kindを追加して実際の空NodeList coreを提供する。RadioNodeList固有getter/setterは別brandを保ち、HTMLCollection/既存Radioの受け入れを維持。iterable methodsとMutationRecord完全IDL等は既存未実装面として明示し、constructor名だけで型を偽装しない。既存childList helperはこのpartialでは変更せず、未達をp242へ記録する。

source候補へbind/collection.cとprivate empty-list bridgeを追加。DOM hookはactive Window-owned Documentの普通set/removeに限定し、standalone/XML Document・cross-document registrations、namespace/Attr-node APIsはこのpartialでは通知しないと明示する。callback entry前までDOM select-state更新がVM allocationしない実コードを確認し、borrowed old name/valueはcallback entryでrootしてからVM recordを構築する。URI属性appendのpublication前no-allocation invariantを守るためgeneric add_attributeへ通知を入れない。record coreのnative empty listが内部の実装選択として必要になったための設計確定であり、public browser ABI/固定分母/strict wait条件は変更しない。

2026-10-18 q17 design correction: WebIDL sequence conversion原典§3.2.21.1と独立Chromium actualを再確認。member ToStringのthrowではIteratorCloseを行わず即abrupt completion（return呼出し0）。前のclose想定を撤回し例外identity/registration保持を検証する。既存MutationObserverとPromiseのmicrotask相対順は今回のordinary attributes partialに含めず、timerから通常deliveryを確認。full observer schedulingは残課題。

Event ws074-q17-preflight-20261018: ordinary attributes・post-font native controls15/15、Chrome新fixtureのpayload/options/GC出力一致、DOM35/35を確認。q17 final frozen pairでglobal1491/static1561とstrict固定48の実測へ進む。global未実行のためterminal clearanceは未判定。

Event ws074-p253-q17-partial-cleared-20261018: [証拠](evidence/20261018-q17/README.md)。全harness1491はwhole491・callback11531 PASS/20226 FAIL/14 NOTRUNを維持し、whole/callback後退・改善・欠損・追加・順序変更0。全static1561は839 PASS/722 FAIL、全reference pixel metrics unchanged。strict48は39 PASS/9 readiness_missing。最終source f1b83fa8f/pair hash read-back一致、固定WPT tracked clean。確認済み1369/3805（35.978975%）、q14から39増。未実行292 harness/413 reftest・native crash依存9は残す。 p253 ordinary Window-owned属性出力・p209 producer再検証の各Queue partialはcleared、whole Phaseは残るAPI/standalone/cross-doc/namespace/IDL/scheduling等を根拠なくclearしない。q15/q16のunclearedは履歴保持。host/target5 warning0・C全文新logic/生成style0・DOM35/35/Chrome22/controls15・budget6/font2/Acid非増加を確認。共有projection pending。
