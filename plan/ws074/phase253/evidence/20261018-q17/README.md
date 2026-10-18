# q17 ordinary attribute observer partial

## 実装と前提

Window-owned Documentのordinary set/removeをprivate opaque hookでbindへ接続。DOM commit→callback入口にVM allocationを挟まず、旧name/value/targetをrootしてからobserverごとの完全な属性payloadを構築しpending arrayへpublishする。new/set-same/remove/no-op、attributes/subtree、oldValue、iterable filter snapshot、dictionary getter順/implicit flags/invalid optionsを実装。再登録は既存pendingを保持、disconnectは破棄する。primary root/managed traceがfilterも保有し、frame detach/release時はhookを解除する。

新recordのaddedNodes/removedNodesには既存Array型偽装helperを使わずnative collection backingのimmutable empty NodeList coreを追加。正しいNodeList prototype/constructor/brand/item/index/length、HTMLCollection/RadioNodeList固有ブランドの拒否を確認。既存childList helperのfull NodeList IDL未達は変更していない。

## 独立検証

[fixture](mutation-attributes.html)・[Chromium期待値](mutation-attributes.expected)・[native実出力](attributes-native.out)。最終fixtureは22行が独立Chromiumとnativeでexact一致。属性payload/同値set/oldValue、className/classList/CSSOM、own/subtree/filter、7 getter順、memberごとのnext→ToString、例外identityとregistration保持、reobserve/takeRecords/disconnect、mixed childList、own子realm・frame削除後の保持・GC圧力、512 mutations×2 observersの通常deliveryを確認。WebIDL sequenceのToString throwはIteratorCloseを行わない原典どおりreturn呼出し0。fixture hashは[final proof](fixture-final-proof.json)、対応する最終DOM suiteは[35/35](dom.log)。

[公式wait対照15/15](controls.log)、CSSOM各12/12、既存mutation-recordとrealm-retirement PASS。targetは変更4 Cとproducer viewのexact single-source compile5件を実行しwarning/error0。[commands/results](target-results.json)と各target log、[tool versions](versions.json)。共有sysroot変更/target link/runtime/QEMUは実行せず後段へ残す。

C全文・Guardrail・browser component全文に従い生成logic/新branchのerror/success、declarations、条件、paragraph、GC ownershipとABI/Wayland境界を手動確認。clang-format19を変更範囲に適用しmultiline definition signatureを保持。[生成関数style候補0](generated-style.log)、[full-file既存候補は増加0](style-comparison.json)。既存関数内の未変更goto等はfull-file checkerに残る。これをWS最終full-source conformance完了とはしない。Python専用full standardは無く既存tool/Guardrailに従う。

初期buildのうちbuild-secondは、前buildと重なったlinkで一時的undefined reference。中間logを保持し、以後serializeしたbuild-third/final-preflight/final/last-commentsは成功。最終source f1b83fa8f・frozen pairは[source proof](source-proof.json)、その後のfixture強化はtestのみでengine不変。

## 既存の未対応面

namespace/Attr-node API、standalone/XML/cross-document registrations、multi-target、characterData、MutationRecord/NodeList全IDL・iterable、reentrant deliveryは未対応として保持。Promiseとの相対microtask順も既存queue frameworkの残り。[独立最小例](observer-promise-order.html)では[Chrome](observer-promise-order.chrome)がobserver,promise、[native](observer-promise-order.native)がpromise,observer。通常timerからのdelivery試験をこのpartialの対象とし、full observer準拠を主張しない。

## 終端gate

同じfinal pairの全1491/static1561比較が進行中。old whole491/839維持と全callback診断を記録してからattemptを判定する。p209 producer / p190 strict48はそれぞれの別evidenceへ。現時点のpreflightだけでPhase/WS whole clearanceや確認済み総数の更新はしない。共有projection pending。
