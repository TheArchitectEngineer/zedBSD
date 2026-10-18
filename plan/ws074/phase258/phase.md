<!-- awesome-plan project=zedbsd record=ws074-p258 -->

# ws074-p258: native Proxy internal methods and lifetime

Status: uncleared（q30 private read foundation partial cleared、whole Proxy operations未達）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006
Queue: ws074-q20261018-30-i01
Origin: p189/q29 original WebIDL2 parseがProxy未定義で停止。

Objective / own criteria: native managed Proxyを実object internal methodsへ接続し、constructor/revocableと各internal operationのtrap/fallback/invariant/receiver/revocation/callability/GCを正しく実行する。original WebIDL2 actual parse/validatorに必要なget/set/array behaviorを通常APIで提供し、independent controls/原WPTを通す。部分read stageだけでwholeをclearしない。
Design: vm_native_operationsはscript-free heap interfaceのまま保持。Proxyにはmanaged target/handler recordを持たせ、scriptを実行可能なrealm-aware access/internal methodからtrapを呼ぶ。opaque JS constructor公開前に必要なoperationを段階的に整える。各段階は自走承認に基づく新finite Queueで正確に選定する。
Current scope: [q30 exact Queue](../queue.md)のnoncallable/nonProxy target・read-only foundation/private VM factoryだけ。未選定write/descriptor/ownkeys/prototype/IsArray/function/call/constructとglobal公開を承認と混同しない。全Phase完了基準から消さない。
Standards: Guardrail/C全文/browser component全文、簡約版無し。一次 https://tc39.es/ecma262/multipage/ordinary-and-exotic-objects-behaviours.html#sec-proxy-object-internal-methods-and-internal-slots-get-p-receiver 。既存p100がWS全source near-final conformanceを所有し、本partialはnew/changed scopeの全文適合を検証。
Verification/bounds/affected components: q30 snapshotの通り。private object kind/cell/newborn/trace/revokeとactual receiver/trap/invariant/GCをnative/Chromeで確認し、ordinary whole/callback PASSを守る。target single-source compileのみとfull target runtimeを区別。未達はunclearedとresume。

Event ws074-p258-q30-selected-20261018: q29 verified delegationの後、正規WebIDL2がProxyを要求することを原source/Chrome actual4/4・native1/4で確認。既存heap native hooksはuser script禁止なので、その契約を保つrealm-aware Get foundationを選定。partial source/scope/90分/gatesを実装前に保存。global Proxy/whole parserを先取り公開・認定しない。共有Master/next/Queue/Project/GitHubの投影pending。

## q30 partial result / resume

Event ws074-p258-q30-partial-cleared-20261018: final source d9e9aa941/frozen pairでmanaged native Proxy read foundationを実装。realm-aware vm_get_receiverがactual receiverをprototype/Proxy fallbackへ保持し、通常vm_getはbaseをreceiverとする既存動作を保つ。managed recordのtarget/handler trace、idempotent revoke、GetMethod/handler this/3引数・string/symbol key、fixed data/accessor invariantとSameValue、trap/getter中revoke後の元target保持を実装。noscriptのvm_native_operations定義はq29と全bytes同一、script trapはaccess層だけで実行する。
同じ32 control sourceがnative/Chrome genuine Proxy/Reflectで全件一致。nativeは保守的C stack保持を無効にし、real threshold collection前にcaller rootsを落としたfactory入力/managed realm保持、foreign trap後のscript-free descriptor GC中のsole returned objectとmanaged realm生存、処理後の実realm回収を確認。Proxyだけのrootでtarget/handlerを保持しrevoke後は両者回収、最後のProxy root解放後はProxy自体を回収。循環native fallbackはRangeErrorで有界停止し、全normal/abrupt終了後property_depth=0。global Proxyは未公開のまま、private factoryのcallable/Proxy target拒否も実検証。
final pairでDOM41/41、interpreter45・factory GC56、CSSOM12+12、producer7/observer10/ErrorEvent9/ErrorReporting19、generator delegation10を通過。host final warning0・q02 exact object/access/realm3source target compile warning0、C全文new/changed scopeは許可forward cleanup45以外0。初期fixtureのprivate header include漏れと誤ったAPI名vm_to_property_keyはactual vm_to_keyへ修正、元build error logsを保持し、最終warning0と区別する。
全1508 harnessを238.2秒、同じsettle5000/process20で実測し、q29に対するwhole判定・全callback object・completion objectが完全同一。494 PASS/955 FAIL/13 harness_error/20 no_completion/24 no_output/2 multiple、callbacks11540 PASS/20642 FAIL/14 NOTRUN、旧PASS後退/新whole gain/差0。原WebIDL2のProxy未定義はglobal未公開なので解消したとしない。
Acid3 score/inventory100・pixel0、Acid2既存差873を再確認。static844/scripted105/strict crash39はq28の原証拠を再利用し、新pairでの全capture/strict equalityは未測定。source deltaはVM5 C/headerだけ、render/runner/WPT/manifest/font不変とsource/pair hashesを照合。確認済1482/3805（38.948752%）・held0を維持、未attempt275を分母へ保持。
i01 selected read foundation partialだけcleared、whole p258はwrite/descriptor/ownkeys/prototype/extent/has/delete/IsArray/call/construct・global constructor/revocable/actual WebIDL2未達でuncleared。次finite Queueで必要なinternal methodを選定して継続する。whole p189/p255/p256/p100/full target link/runtime/WS100%未達。全native/oracle/build/Acid/census rawをarchive全member sequential read-back済み。共有Master/Queue/next/cache/GitHub投影pending、push無し。

[Full evidence](evidence/20261018-q30/README.md)。次scopeの開始前に最新source/required methods/原WebIDL2とexact Queueを再確認する。global公開は後続readinessの別scope、whole acceptanceをread-only出力だけでclearしない。
