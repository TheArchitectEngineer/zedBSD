<!-- awesome-plan project=zedbsd record=ws074-p257 -->

# ws074-p257: synchronous generator delegation

Status: cleared（2026-10-18、own synchronous delegation criteria verified）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006
Queue: ws074-q20261018-29-i01

Objective: native synchronous generator yield*をactual iterable protocolで実行し、固定WebIDL2のcompiler prerequisiteを解消する。async generators/for awaitは範囲外。
Completion criteria: sync delegationのnext/throw/return・normal/abrupt completion・result identity・finally/GCと独立Chrome/native対照、final source full-standard conformance・host/target warning0、既存PASS後退無しを実証。WebIDL2後続API不足を実装済みと混同しない。
Design: generator-owned managed iterator recordがactual iterator/cached nextを保持。start/step opcodesで普通のcompiler loweringを行い、stepはyield/normal/return outcomeを返す。delegated done falseのresultを専用suspensionから外へ同一objectで渡す。通常yield/awaitは従来wrappingを維持。return completionは既存js_emit_returnでfinallyを通る。
Prerequisites: q28 verified VM/Window/GC、p086既存generator output（元Phase範囲を変更せずyield*未対応という歴史を保持）、p189 q24 pinned WebIDL2/IDL inputs。Guardrail/C全文/browser component全文を適用。
Verification / bounds / generation scope: [current Queue exact snapshot](../queue.md)。TC39 primary https://tc39.es/ecma262/multipage/ecmascript-language-functions-and-classes.html#sec-generator-function-definitions-runtime-semantics-evaluation を参照。max90分、2026-10-18 20:35 JST、未達はuncleared。

Event ws074-p257-q29-selected-20261018: q24/28の原WebIDL2 unsupported yield*をsourceで確認。source変更前に有限scopeを選定、独立protocol/negative/GCを必要条件とする。p189はverified delegation後に再実行するだけでrunner/API補完へ範囲を広げない。whole WS/p100未達、共有Master next/registry/Project投影はQ1所有のためpending。

## Result / acceptance

Event ws074-p257-q29-cleared-20261018: source9da3cd223/final pairでnative同期yield*を通常compiler/VMへ実装。actual iterator/cached next、next初回undefinedの1引数、throw/return動的method取得、missing throwのnormal closeと例外優先順位、done/value順、done falseのvalue非観測とoriginal result同一性、normal/return/finallyを確認。managed recordとexplicit rootsでGC中のoperand/method/iterator/result/sentを保持し、ordinary yield/awaitは従来経路を維持。
独立Chrome/native38条件が全件一致、保守的stack無効のnative GC10、DOM41/41（既存promise/asyncを含む）、VM interpreter45・factory GC56、CSSOM12+12、producer7/observer10/ErrorEvent9/ErrorReporting19を通過。host warning0・q02 exact5source target compile warning0、C全文new/changed scopeは許可forward cleanup25以外0。full target link/runtime/p100は未達。
固定1508を237.1秒、同じsettle5000/process20で実測し494 PASS/955 FAIL/13 harness_error/20 no_completion/24 no_output/2 multiple、callbacks11540 PASS/20642 FAIL/14 NOTRUN。旧whole PASSと全PASS callbackの名前/数を保持、whole gain0。差はevent-timing/storage-accessのoriginal IDL setup2件のmessageだけで、yield*拒否/WebIDL2 undefinedからProxy未定義へ進んだ。原WebIDL2 bundleをloadしparse APIが実在することはnative/Chrome両方PASS、actual parse/validator/invalid IDL対照はChrome4/4・native1/4でProxyがblock。negative after-parserはChromeが意図assertまで到達、nativeはその前のProxy errorでありnegative-after-parser成立と主張しない。
Acid3 score/inventory100・pixel0、Acid2既存差873を再確認。static844/scripted105/strict crash39はq28の原証拠を再利用し、新pairでの全pixel/capture/crash再測定や同一性を主張しない。render sourceとrunner/manifest/WPT/fontは不変、native VM scopeだけ変更したことをsource差分/hashで保存。確認済み1482/3805（38.948752%）・held0を保持、未attempt275は分母に残す。
i01 scoped partial cleared、p257 own synchronous delegation criteriaをverifiedとしてcleared。async generator/for-await/全Test262適合は本Phase範囲外で未実装、Proxy/正規WebIDL2 parse完了はp189の次候補。whole p189/p255/p256/p100/target/WS100%は未達。全raw/元source/stdio/binary/hash/checkpointsをarchive sequential read-back済み。shared Master/Queue/next/cache/GitHub投影pending、push無し。

[Full evidence](evidence/20261018-q29/README.md)。local Phase cleared、GitHub Issue公開/closureは未実施・Q1投影pending。
